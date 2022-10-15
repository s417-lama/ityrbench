#pragma once

#include <optional>

#include "uth.h"
#include "pcas/pcas.hpp"

#include "ityr/util.hpp"
#include "ityr/ito_group.hpp"
#include "ityr/ito_pattern.hpp"
#include "ityr/logger/logger.hpp"

namespace ityr {

#ifndef ITYR_LOGGER_IMPL
#define ITYR_LOGGER_IMPL impl_dummy
#endif

template <typename P>
using ityr_logger_impl_t = logger::ITYR_LOGGER_IMPL<P>;

#undef ITYR_LOGGER_IMPL

#ifndef ITYR_DIST_POLICY
#define ITYR_DIST_POLICY pcas::mem_mapper::cyclic
#endif

template <typename ParentPolicy>
struct my_pcas_policy : pcas::policy_default {
  using wallclock_t = typename ParentPolicy::wallclock_t;
  template <typename P>
  using logger_impl_t = typename ParentPolicy::template logger_impl_t<P>;

#ifndef ITYR_BLOCK_SIZE
#define ITYR_BLOCK_SIZE 65536
#endif
  constexpr static uint64_t block_size = ITYR_BLOCK_SIZE;
#undef ITYR_BLOCK_SIZE

#ifndef ITYR_ENABLE_WRITE_THROUGH
#define ITYR_ENABLE_WRITE_THROUGH 0
#endif
  constexpr static bool enable_write_through = ITYR_ENABLE_WRITE_THROUGH;
#undef ITYR_ENABLE_WRITE_THROUGH
};

template <typename P>
class iro_pcas_default {
  using my_pcas = pcas::pcas_if<my_pcas_policy<P>>;

  static std::optional<my_pcas>& get_instance() {
    static std::optional<my_pcas> instance;
    return instance;
  }

  static my_pcas& pc() {
    assert(get_instance().has_value());
    return *get_instance();
  }

public:
  template <typename T>
  using global_ptr = pcas::global_ptr<T>;
  using access_mode = pcas::access_mode;
  using release_handler = pcas::release_handler;

  static void init(size_t cache_size) {
    assert(!get_instance().has_value());
    get_instance().emplace(cache_size);
  }

  static void fini() {
    assert(get_instance().has_value());
    get_instance().reset();
  }

  static void release() {
    pc().release();
  }

  static void release_lazy(release_handler* handler) {
    pc().release_lazy(handler);
  }

  static void acquire() {
    pc().acquire();
  }

  static void acquire(release_handler handler) {
    pc().acquire(handler);
  }

  static void poll() {
    pc().poll();
  }

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) {
    return pc().template malloc<T, ITYR_DIST_POLICY>(nelems);
  }

  template <typename T>
  static void free(global_ptr<T> ptr) {
    pc().free(ptr);
  }

  template <typename ConstT, typename T>
  static std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
    pc().get(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    pc().put(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  static void willread(global_ptr<T> ptr, uint64_t nelems) {
    pc().willread(ptr, nelems);
  }

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) {
    return pc().template checkout<Mode>(ptr, nelems);
  }

  template <typename T>
  static void checkin(T* raw_ptr, uint64_t nelems) {
    pc().checkin(raw_ptr, nelems);
  }

  static void logger_clear() {
    my_pcas::logger::clear();
  }

  static void logger_flush(uint64_t t_begin, uint64_t t_end) {
    my_pcas::logger::flush(t_begin, t_end);
  }

  static void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {
    my_pcas::logger::flush_and_print_stat(t_begin, t_end);
  }
};

template <typename P>
class iro_pcas_getput {
  using my_pcas = pcas::pcas_if<my_pcas_policy<P>>;

  static std::optional<my_pcas>& get_instance() {
    static std::optional<my_pcas> instance;
    return instance;
  }

  static my_pcas& pc() {
    assert(get_instance().has_value());
    return *get_instance();
  }

public:
  template <typename T>
  using global_ptr = pcas::global_ptr<T>;
  using access_mode = pcas::access_mode;
  using release_handler = pcas::release_handler;

  static void init(size_t cache_size) {
    assert(!get_instance().has_value());
    get_instance().emplace(cache_size);
  }

  static void fini() {
    assert(get_instance().has_value());
    get_instance().reset();
  }

  static void release() {}

  static void release_lazy(release_handler*) {}

  static void acquire() {}

  static void acquire(release_handler) {}

  static void poll() {}

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) {
    return pc().template malloc<T, ITYR_DIST_POLICY>(nelems);
  }

  template <typename T>
  static void free(global_ptr<T> ptr) {
    pc().free(ptr);
  }

#ifndef ITYR_IRO_DISABLE_CACHE
#define ITYR_IRO_DISABLE_CACHE 0
#endif

  template <typename ConstT, typename T>
  static std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
#if ITYR_IRO_DISABLE_CACHE
    pc().get_nocache(from_ptr, to_ptr, nelems);
#else
    pc().get(from_ptr, to_ptr, nelems);
#endif
  }

  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
#if ITYR_IRO_DISABLE_CACHE
    pc().put_nocache(from_ptr, to_ptr, nelems);
#else
    pc().put(from_ptr, to_ptr, nelems);
#endif
  }

#undef ITYR_IRO_DISABLE_CACHE

  template <typename T>
  static void willread(global_ptr<T>, uint64_t) {}

  template <access_mode Mode, typename T>
  static std::conditional_t<Mode == access_mode::read, const T*, T*>
  checkout(global_ptr<T> ptr, uint64_t nelems) {
    // If T is const, then it cannot be checked out with write access mode
    static_assert(!std::is_const_v<T> || Mode == access_mode::read);

    uint64_t size = nelems * sizeof(T);
    auto ret = (std::remove_const_t<T>*)std::malloc(size + sizeof(global_ptr<uint8_t>));
    if (Mode != access_mode::write) {
      get(ptr, ret, nelems);
    }
    *((global_ptr<uint8_t>*)((uint8_t*)ret + size)) = global_ptr<uint8_t>(ptr);
    return ret;
  }

  template <typename T>
  static void checkin(const T* raw_ptr, uint64_t) {
    std::free(const_cast<T*>(raw_ptr));
  }

  template <typename T>
  static void checkin(T* raw_ptr, uint64_t nelems) {
    uint64_t size = nelems * sizeof(T);
    global_ptr<uint8_t> ptr = *((global_ptr<uint8_t>*)((uint8_t*)raw_ptr + size));
    put((uint8_t*)raw_ptr, ptr, size);
    std::free(raw_ptr);
  }

  static void logger_clear() {
    my_pcas::logger::clear();
  }

  static void logger_flush(uint64_t t_begin, uint64_t t_end) {
    my_pcas::logger::flush(t_begin, t_end);
  }

  static void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {
    my_pcas::logger::flush_and_print_stat(t_begin, t_end);
  }
};

template <typename P>
class iro_dummy {
public:
  template <typename T>
  using global_ptr = T*;
  using access_mode = pcas::access_mode;
  using release_handler = int;

  static void init(size_t cache_size) {}
  static void fini() {}

  static void release() {}
  static void release_lazy(release_handler*) {}
  static void acquire() {}
  static void acquire(release_handler) {}
  static void poll() {}

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) { return (T*)std::malloc(nelems * sizeof(T)); }
  template <typename T>
  static void free(global_ptr<T> ptr) { std::free(ptr); }

  template <typename ConstT, typename T>
  static std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
    std::memcpy(to_ptr, from_ptr, nelems * sizeof(T));
  }
  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    std::memcpy(to_ptr, from_ptr, nelems * sizeof(T));
  }

  template <typename T>
  static void willread(global_ptr<T> ptr, uint64_t nelems) {}
  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) { return ptr; }
  template <typename T>
  static void checkin(T* raw_ptr, uint64_t nelems) {}

  static void logger_clear() {}
  static void logger_flush(uint64_t t_begin, uint64_t t_end) {}
  static void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {}
};

// Wallclock Time
// -----------------------------------------------------------------------------

class wallclock_native {
public:
  static void init() {}
  static void sync() {}
  static uint64_t get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
  }
};

class wallclock_madm {
public:
  static void init() {
    madi::global_clock::init();
  }
  static void sync() {
    madi::global_clock::sync();
  }
  static uint64_t get_time() {
    return madi::global_clock::get_time();
  }
};

// ityr interface
// -----------------------------------------------------------------------------

template <typename P>
class ityr_if {
public:
  template <size_t MaxTasks, bool SpawnLastTask = false>
  using ito_group = typename P::template ito_group_t<P, MaxTasks, SpawnLastTask>;

  using ito_pattern = typename P::template ito_pattern_t<P>;

  using iro = typename P::template iro_t<P>;

  using wallclock = typename P::wallclock_t;

  using logger_kind = typename P::logger_kind_t::value;

  using logger = typename logger::template logger_if<logger::policy<P>>;

  static uint64_t rank() { return P::rank(); }

  static uint64_t n_ranks() { return P::n_ranks(); }

  template <typename F, typename... Args>
  static void main(F f, Args... args) {
    set_signal_handlers();
    P::main(f, args...);
  }

  static void barrier() { return P::barrier(); }

  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) { return P::template root_spawn<P>(std::forward<Fn>(f), std::forward<Args>(args)...); }

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) { return ito_pattern::parallel_invoke(std::forward<Args>(args)...); }
};

// Serial
// -----------------------------------------------------------------------------

struct ityr_policy_serial {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_serial<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using ito_pattern_t = ito_pattern_serial<P>;

  template <typename P>
  using iro_t = iro_dummy<P>;

  using wallclock_t = wallclock_native;

  using logger_kind_t = logger::kind_dummy;

  template <typename P>
  using logger_impl_t = logger::impl_dummy<P>;

  static uint64_t rank() { return 0; }

  static uint64_t n_ranks() { return 1; }

  template <typename Fn, typename... Args>
  static void main(Fn&& f, Args&&... args) { f(std::forward<Args>(args)...); }

  template <typename P, typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) { return f(std::forward<Args>(args)...); }

  static void barrier() {};
};

// Naive
// -----------------------------------------------------------------------------

struct ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_naive<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using ito_pattern_t = ito_pattern_naive<P>;

#ifndef ITYR_IRO_GETPUT
#define ITYR_IRO_GETPUT 0
#endif

#if ITYR_IRO_GETPUT
  template <typename P>
  using iro_t = iro_pcas_getput<P>;
#else
  template <typename P>
  using iro_t = iro_pcas_default<P>;
#endif

#undef ITYR_IRO_GETPUT

  using wallclock_t = wallclock_madm;

  using logger_kind_t = logger::kind_dummy;

  template <typename P>
  using logger_impl_t = ityr_logger_impl_t<P>;

  static uint64_t rank() {
    return madm::uth::get_pid();
  }

  static uint64_t n_ranks() {
    return madm::uth::get_n_procs();
  }

  template <typename Fn, typename... Args>
  static void main(Fn&& f, Args&&... args) {
    madm::uth::start(std::forward<Fn>(f), std::forward<Args>(args)...);
  }

  template <typename P, typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
    using iro = typename P::template iro_t<P>;
    using ret_t = std::invoke_result_t<Fn, Args...>;
    iro::release();
    madm::uth::thread<ret_t> th(std::forward<Fn>(f), std::forward<Args>(args)...);
    if constexpr (std::is_void_v<ret_t>) {
      th.join();
      iro::acquire();
    } else {
      auto&& ret = th.join();
      iro::acquire();
      return ret;
    }
  }

  static void barrier() {
    madm::uth::barrier();
  };
};

// Work-first fence elimination
// -----------------------------------------------------------------------------

struct ityr_policy_workfirst : ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_workfirst<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using ito_pattern_t = ito_pattern_workfirst<P>;
};

// Work-first fence elimination + lazy release
// -----------------------------------------------------------------------------

struct ityr_policy_workfirst_lazy : ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_workfirst_lazy<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using ito_pattern_t = ito_pattern_workfirst_lazy<P>;
};

// Policy selection
// -----------------------------------------------------------------------------

#ifndef ITYR_POLICY
#define ITYR_POLICY ityr_policy_naive
#endif

using ityr_policy = ITYR_POLICY;

#undef ITYR_POLICY

#undef ITYR_DIST_POLICY

}
