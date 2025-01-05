/*
 * Copyright (c) 2023 Lee Howes
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

#include <thread>
#include <iostream>
#include <chrono>

#define STDEXEC_SYSTEM_CONTEXT_HEADER_ONLY 1

#include <stdexec/execution.hpp>

#include <exec/async_scope.hpp>
#include <exec/inline_scheduler.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/system_context.hpp>

#include <catch2/catch.hpp>
#include <test_common/receivers.hpp>


namespace ex = stdexec;
namespace scr = exec::system_context_replaceability;

TEST_CASE("system_context can return a scheduler", "[types][system_scheduler]") {
  auto sched = exec::get_system_scheduler();
  STATIC_REQUIRE(ex::scheduler<decltype(sched)>);
}

TEST_CASE("system scheduler is not default constructible", "[types][system_scheduler]") {
  auto sched = exec::get_system_scheduler();
  using sched_t = decltype(sched);
  STATIC_REQUIRE(!std::is_default_constructible_v<sched_t>);
  STATIC_REQUIRE(std::is_destructible_v<sched_t>);
}

TEST_CASE("system scheduler is copyable and movable", "[types][system_scheduler]") {
  auto sched = exec::get_system_scheduler();
  using sched_t = decltype(sched);
  STATIC_REQUIRE(std::is_copy_constructible_v<sched_t>);
  STATIC_REQUIRE(std::is_move_constructible_v<sched_t>);
}

TEST_CASE("a copied scheduler is equal to the original", "[types][system_scheduler]") {
  auto sched1 = exec::get_system_scheduler();
  auto sched2 = sched1;
  REQUIRE(sched1 == sched2);
}

TEST_CASE(
  "two schedulers obtained from get_system_scheduler() are equal",
  "[types][system_scheduler]") {
  auto sched1 = exec::get_system_scheduler();
  auto sched2 = exec::get_system_scheduler();
  REQUIRE(sched1 == sched2);
}

TEST_CASE("system scheduler can produce a sender", "[types][system_scheduler]") {
  auto snd = ex::schedule(exec::get_system_scheduler());
  using sender_t = decltype(snd);

  STATIC_REQUIRE(ex::sender<sender_t>);
  STATIC_REQUIRE(ex::sender_of<sender_t, ex::set_value_t()>);
  STATIC_REQUIRE(ex::sender_of<sender_t, ex::set_stopped_t()>);
}

TEST_CASE("trivial schedule task on system context", "[types][system_scheduler]") {
  exec::system_scheduler sched = exec::get_system_scheduler();

  ex::sync_wait(ex::schedule(sched));
}

TEST_CASE("simple schedule task on system context", "[types][system_scheduler]") {
  std::thread::id this_id = std::this_thread::get_id();
  std::thread::id pool_id{};
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto snd = ex::then(ex::schedule(sched), [&] { pool_id = std::this_thread::get_id(); });

  ex::sync_wait(std::move(snd));

  REQUIRE(pool_id != std::thread::id{});
  REQUIRE(this_id != pool_id);
  (void) snd;
}

TEST_CASE("simple schedule forward progress guarantee", "[types][system_scheduler]") {
  exec::system_scheduler sched = exec::get_system_scheduler();
  REQUIRE(ex::get_forward_progress_guarantee(sched) == ex::forward_progress_guarantee::parallel);
}

TEST_CASE("get_completion_scheduler", "[types][system_scheduler]") {
  exec::system_scheduler sched = exec::get_system_scheduler();
  REQUIRE(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(sched))) == sched);
}

TEST_CASE("simple chain task on system context", "[types][system_scheduler]") {
  std::thread::id this_id = std::this_thread::get_id();
  std::thread::id pool_id{};
  std::thread::id pool_id2{};
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto snd = ex::then(ex::schedule(sched), [&] { pool_id = std::this_thread::get_id(); });
  auto snd2 = ex::then(std::move(snd), [&] { pool_id2 = std::this_thread::get_id(); });

  ex::sync_wait(std::move(snd2));

  REQUIRE(pool_id != std::thread::id{});
  REQUIRE(this_id != pool_id);
  REQUIRE(pool_id == pool_id2);
  (void) snd;
  (void) snd2;
}

// TODO: fix this test. This also makes tsan and asan unhappy.
// TEST_CASE("checks stop_token before starting the work", "[types][system_scheduler]") {
//   exec::system_scheduler sched = exec::get_system_scheduler();

//   exec::async_scope scope;
//   scope.request_stop();

//   bool called = false;
//   auto snd = ex::then(ex::schedule(sched), [&called] { called = true; });

//   // Start the sender in a stopped scope
//   scope.spawn(std::move(snd));

//   // Wait for everything to be completed.
//   ex::sync_wait(scope.on_empty());

//   // Assert.
//   // TODO: called should be false
//   REQUIRE(called);
// }

TEST_CASE("simple bulk task on system context", "[types][system_scheduler]") {
  std::thread::id this_id = std::this_thread::get_id();
  constexpr size_t num_tasks = 16;
  std::thread::id pool_ids[num_tasks];
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto bulk_snd = ex::bulk(ex::schedule(sched), num_tasks, [&](unsigned long id) {
    pool_ids[id] = std::this_thread::get_id();
  });

  ex::sync_wait(std::move(bulk_snd));

  for (size_t i = 0; i < num_tasks; ++i) {
    REQUIRE(pool_ids[i] != std::thread::id{});
    REQUIRE(this_id != pool_ids[i]);
  }
  (void) bulk_snd;
}

TEST_CASE("simple bulk chaining on system context", "[types][system_scheduler]") {
  std::thread::id this_id = std::this_thread::get_id();
  constexpr size_t num_tasks = 16;
  std::thread::id pool_id{};
  std::thread::id propagated_pool_ids[num_tasks];
  std::thread::id pool_ids[num_tasks];
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto snd = ex::then(ex::schedule(sched), [&] {
    pool_id = std::this_thread::get_id();
    return pool_id;
  });

  auto bulk_snd =
    ex::bulk(std::move(snd), num_tasks, [&](unsigned long id, std::thread::id propagated_pool_id) {
      propagated_pool_ids[id] = propagated_pool_id;
      pool_ids[id] = std::this_thread::get_id();
    });

  std::optional<std::tuple<std::thread::id>> res = ex::sync_wait(std::move(bulk_snd));

  // Assert: first `schedule` is run on a different thread than the current thread.
  REQUIRE(pool_id != std::thread::id{});
  REQUIRE(this_id != pool_id);
  // Assert: bulk items are run and they propagate the received value.
  for (size_t i = 0; i < num_tasks; ++i) {
    REQUIRE(pool_ids[i] != std::thread::id{});
    REQUIRE(propagated_pool_ids[i] == pool_id);
    REQUIRE(this_id != pool_ids[i]);
  }
  // Assert: the result of the bulk operation is the same as the result of the first `schedule`.
  CHECK(res.has_value());
  CHECK(std::get<0>(res.value()) == pool_id);
}

struct my_system_scheduler_impl : exec::__system_context_default_impl::__system_scheduler_impl {
  using base_t = exec::__system_context_default_impl::__system_scheduler_impl;

  my_system_scheduler_impl() = default;

  int num_schedules() const {
    return count_schedules_;
  }

  void schedule(scr::storage __s, scr::receiver* __r, scr::env __e) noexcept override {
    count_schedules_++;
    base_t::schedule(__s, __r, __e);
  }


 private:
  int count_schedules_ = 0;
};

struct my_inline_scheduler_impl : scr::system_scheduler {
  void schedule(scr::storage s, scr::receiver* r, scr::env e) noexcept override {
    r->set_value();
  }

  void
    bulk_schedule(uint32_t count, scr::storage s, scr::bulk_item_receiver* r, scr::env e) noexcept
    override {
    for (uint32_t i = 0; i < count; ++i)
      r->start(i);
    r->set_value();
  }
};

TEST_CASE(
  "can change the implementation of system context at runtime",
  "[types][system_scheduler]") {
  my_system_scheduler_impl my_scheduler;
  bool r = scr::set_system_context_backend<scr::system_scheduler>(&my_scheduler);
  REQUIRE(r);

  std::thread::id this_id = std::this_thread::get_id();
  std::thread::id pool_id{};
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto snd = ex::then(ex::schedule(sched), [&] { pool_id = std::this_thread::get_id(); });

  REQUIRE(my_scheduler.num_schedules() == 0);
  ex::sync_wait(std::move(snd));
  REQUIRE(my_scheduler.num_schedules() == 1);

  REQUIRE(pool_id != std::thread::id{});
  REQUIRE(this_id != pool_id);
}

TEST_CASE(
  "can change the implementation of system context at runtime, with an inline scheduler",
  "[types][system_scheduler]") {
  my_inline_scheduler_impl my_scheduler;
  bool r = scr::set_system_context_backend<scr::system_scheduler>(&my_scheduler);
  REQUIRE(r);

  std::thread::id this_id = std::this_thread::get_id();
  std::thread::id pool_id{};
  exec::system_scheduler sched = exec::get_system_scheduler();

  auto snd = ex::then(ex::schedule(sched), [&] { pool_id = std::this_thread::get_id(); });

  ex::sync_wait(std::move(snd));

  REQUIRE(this_id == pool_id);
}

TEST_CASE("empty environment always returns nullopt for any query", "[types][system_scheduler]") {
  scr::env frontend_env{};

  scr::env env = frontend_env; // simulate a copy to the backend
  REQUIRE(env.try_query<stdexec::inplace_stop_token>() == std::nullopt);
  REQUIRE(env.try_query<int>() == std::nullopt);
  REQUIRE(env.try_query<std::allocator<int>>() == std::nullopt);
}

TEST_CASE("environment with a stop token can expose its stop token", "[types][system_scheduler]") {
  stdexec::inplace_stop_source ss;
  auto token = ss.get_token();
  scr::env frontend_env{token};

  scr::env env = frontend_env; // simulate a copy to the backend
  auto o1 = env.try_query<stdexec::inplace_stop_token>();
  REQUIRE(o1.has_value());
  REQUIRE(o1.value().stop_requested() == false);
  REQUIRE(o1.value() == token);

  ss.request_stop();
  REQUIRE(o1.value().stop_requested() == true);

  REQUIRE(env.try_query<int>() == std::nullopt);
  REQUIRE(env.try_query<std::allocator<int>>() == std::nullopt);
}

TEST_CASE("environment constructed with tuple", "[types][system_scheduler]") {
  stdexec::inplace_stop_source ss;
  std::tuple<stdexec::inplace_stop_token> data{ss.get_token()};
  scr::env frontend_env{data};
  scr::env env = frontend_env; // simulate a copy to the backend

  // Can extract the stop token
  auto o1 = env.try_query<stdexec::inplace_stop_token>();
  REQUIRE(o1.has_value());
  REQUIRE(o1.value().stop_requested() == false);
  REQUIRE(o1.value() == ss.get_token());

  // Cannot extract other types
  REQUIRE(env.try_query<int>() == std::nullopt);
  REQUIRE(env.try_query<std::allocator<int>>() == std::nullopt);
}
