/*
 * Copyright (c) 2024 Rishabh Dwivedi <rishabhdwivedi17@gmail.com>
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "catch2/catch.hpp"
#include "exec/libdispatch_queue.hpp"
#include "stdexec/execution.hpp"

namespace {
  TEST_CASE("libdispatch queue should be able to process tasks") {
    exec::libdispatch_queue queue;
    auto sch = queue.get_scheduler();

    std::vector<int> data{1, 2, 3, 4, 5};
    auto add = [](auto const & data) {
      return std::accumulate(std::begin(data), std::end(data), 0);
    };
    auto sender = stdexec::transfer_just(sch, std::move(data)) | stdexec::then(add);

    auto completion_scheduler =
      stdexec::get_completion_scheduler<stdexec::set_value_t>(stdexec::get_env(sender));

    CHECK(completion_scheduler == sch);
    auto [res] = stdexec::sync_wait(sender).value();
    CHECK(res == 15);
  }

  TEST_CASE(
    "libdispatch queue bulk algorithm should call callback function with all allowed shapes") {
    exec::libdispatch_queue queue;
    auto sch = queue.get_scheduler();

    std::vector<int> data{1, 2, 3, 4, 5};
    auto size = data.size();
    auto expensive_computation = [](auto i, auto& data) {
      data[i] = 2 * data[i];
    };
    auto add = [](auto const & data) {
      return std::accumulate(std::begin(data), std::end(data), 0);
    };
    auto sender = stdexec::transfer_just(sch, std::move(data))             //
                | stdexec::bulk(stdexec::par, size, expensive_computation) //
                | stdexec::then(add);

    auto completion_scheduler =
      stdexec::get_completion_scheduler<stdexec::set_value_t>(stdexec::get_env(sender));

    CHECK(completion_scheduler == sch);
    auto [res] = stdexec::sync_wait(sender).value();
    CHECK(res == 30);
  }

  TEST_CASE("libdispatch bulk should handle exceptions gracefully") {
    exec::libdispatch_queue queue;
    auto sch = queue.get_scheduler();

    std::vector<int> data{1, 2, 3, 4, 5};
    auto size = data.size();
    auto expensive_computation = [](auto i, auto data) {
      if (i == 0)
        throw 999;
      return 2 * data[i];
    };
    auto add = [](auto const & data) {
      return std::accumulate(std::begin(data), std::end(data), 0);
    };
    auto sender = stdexec::transfer_just(sch, std::move(data))             //
                | stdexec::bulk(stdexec::par, size, expensive_computation) //
                | stdexec::then(add);


    try {
      stdexec::sync_wait(sender);
      CHECK(false);
    } catch (int e) {
      CHECK(e == 999);
    }
  }

  template <class Sched, class Policy>
  int number_of_threads_in_bulk(Sched sch, const Policy& policy, int n) {
    std::vector<std::thread::id> tids(n);
    auto fun = [&tids](std::size_t idx) {
      tids[idx] = std::this_thread::get_id();
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    };

    auto snd = stdexec::just()            //
             | stdexec::continues_on(sch) //
             | stdexec::bulk(policy, tids.size(), fun);
    stdexec::sync_wait(std::move(snd));

    std::sort(tids.begin(), tids.end());
    return static_cast<int>(std::unique(tids.begin(), tids.end()) - tids.begin());
  }

  TEST_CASE(
    "libdispatch execute bulk work in accordance with the execution policy",
    "[libdispatch]") {
    exec::libdispatch_queue queue;
    auto sch = queue.get_scheduler();

    SECTION("seq execution policy") {
      REQUIRE(number_of_threads_in_bulk(sch, stdexec::seq, 42) == 1);
    }
    SECTION("unseq execution policy") {
      REQUIRE(number_of_threads_in_bulk(sch, stdexec::unseq, 42) == 1);
    }
    SECTION("par execution policy") {
      REQUIRE(number_of_threads_in_bulk(sch, stdexec::par, 42) > 1);
    }
    SECTION("par_unseq execution policy") {
      REQUIRE(number_of_threads_in_bulk(sch, stdexec::par_unseq, 42) > 1);
    }
  }

  template <class Sched, class Policy>
  int number_of_threads_in_bulk_chunked(Sched sch, const Policy& policy, int n) {
    std::vector<std::thread::id> tids(n);
    auto fun = [&tids](std::size_t b, std::size_t e) {
      while (b < e)
        tids[b++] = std::this_thread::get_id();
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    };

    auto snd = stdexec::just()            //
             | stdexec::continues_on(sch) //
             | stdexec::bulk_chunked(policy, tids.size(), fun);
    stdexec::sync_wait(std::move(snd));

    std::sort(tids.begin(), tids.end());
    return static_cast<int>(std::unique(tids.begin(), tids.end()) - tids.begin());
  }

  TEST_CASE(
    "libdispatch execute bulk_chunked work in accordance with the execution policy",
    "[libdispatch]") {
    exec::libdispatch_queue queue;
    auto sch = queue.get_scheduler();

    SECTION("seq execution policy") {
      REQUIRE(number_of_threads_in_bulk_chunked(sch, stdexec::seq, 42) == 1);
    }
    SECTION("unseq execution policy") {
      REQUIRE(number_of_threads_in_bulk_chunked(sch, stdexec::unseq, 42) == 1);
    }
    SECTION("par execution policy") {
      REQUIRE(number_of_threads_in_bulk_chunked(sch, stdexec::par, 42) > 1);
    }
    SECTION("par_unseq execution policy") {
      REQUIRE(number_of_threads_in_bulk_chunked(sch, stdexec::par_unseq, 42) > 1);
    }
  }
} // namespace
