#pragma once

#include "uth.h"
#include "pcas/pcas.hpp"

#include "ityr/util.hpp"
#include "ityr/ito_group.hpp"
#include "ityr/logger/logger.hpp"

namespace ityr {

#ifndef ITYR_LOGGER_IMPL
#define ITYR_LOGGER_IMPL impl_dummy
#endif

template <typename P>
using ityr_logger_impl_t = logger::ITYR_LOGGER_IMPL<P>;

#undef ITYR_LOGGER_IMPL

template <typename ParentPolicy>
struct my_pcas_policy : pcas::policy_default {
  using wallclock_t = typename ParentPolicy::wallclock_t;
  template <typename P>
  using logger_impl_t = typename ParentPolicy::template logger_impl_t<P>;
};

template <typename P>
class iro_pcas_default {
  using my_pcas = pcas::pcas_if<my_pcas_policy<P>>;

public:
  template <typename T>
  using global_ptr = pcas::global_ptr<T>;
  using access_mode = pcas::access_mode;

  static void init(size_t cache_size) {
    pc(cache_size);
  }

  static void release() {
    pc().release();
  }

  static void acquire() {
    pc().acquire();
  }

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) {
    return pc().template malloc<T>(nelems);
  }

  template <typename T>
  static void free(global_ptr<T> ptr) {
    pc().free(ptr);
  }

  template <typename T>
  static void get(global_ptr<T> from_ptr, T* to_ptr, uint64_t nelems) {
    return pc().get(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    return pc().put(from_ptr, to_ptr, nelems);
  }

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) {
    return pc().template checkout<Mode>(ptr, nelems);
  }

  template <typename T>
  static void checkin(T* raw_ptr) {
    pc().checkin(raw_ptr);
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

private:
  static my_pcas& pc(size_t cache_size = 0) {
    static my_pcas g_pc(cache_size);
    return g_pc;
  }
};

template <typename P>
class iro_dummy {
public:
  template <typename T>
  using global_ptr = T*;
  using access_mode = pcas::access_mode;

  static void init(size_t cache_size) {}

  static void release() {}
  static void acquire() {}

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) { return (T*)std::malloc(nelems * sizeof(T)); }
  template <typename T>
  static void free(global_ptr<T> ptr) { std::free(ptr); }

  template <typename T>
  static void get(global_ptr<T> from_ptr, T* to_ptr, uint64_t nelems) {}
  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {}

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) { return ptr; }
  template <typename T>
  static void checkin(T* raw_ptr) {}

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

  using iro = typename P::template iro_t<P>;

  using wallclock = typename P::wallclock_t;

  using logger_kind = typename P::logger_kind_t::value;

  using logger = typename logger::template logger_if<logger::policy<P>>;

  static uint64_t rank() { return P::rank(); }

  static uint64_t n_ranks() { return P::n_ranks(); }

  template <typename F, typename... Args>
  static void main(F f, Args... args) {
    set_segv_handler();
    P::main(f, args...);
  }

  static void barrier() { return P::barrier(); }
};

// Serial
// -----------------------------------------------------------------------------

struct ityr_policy_serial {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_serial<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using iro_t = iro_dummy<P>;

  using wallclock_t = wallclock_native;

  using logger_kind_t = logger::kind_dummy;

  template <typename P>
  using logger_impl_t = logger::impl_dummy<P>;

  static uint64_t rank() { return 0; }

  static uint64_t n_ranks() { return 1; }

  template <typename F, typename... Args>
  static void main(F f, Args... args) { f(args...); }

  static void barrier() {};
};

// Naive
// -----------------------------------------------------------------------------

struct ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_naive<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using iro_t = iro_pcas_default<P>;

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

  template <typename F, typename... Args>
  static void main(F f, Args... args) {
    madm::uth::start(f, args...);
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
};

// Policy selection
// -----------------------------------------------------------------------------

#ifndef ITYR_POLICY
#define ITYR_POLICY ityr_policy_naive
#endif

using ityr_policy = ITYR_POLICY;

#undef ITYR_POLICY

}