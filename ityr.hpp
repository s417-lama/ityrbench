#include "uth.h"
#include "pcas/pcas.hpp"

template <typename P>
class iro_pcas {
  static pcas::pcas& pc(size_t cache_size = 0) {
    static pcas::pcas g_pc(cache_size);
    return g_pc;
  }

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
    return pc().malloc<T>(nelems);
  }

  template <typename T>
  static void free(global_ptr<T> ptr) {
    pc().free(ptr);
  }

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) {
    return pc().checkout<Mode>(ptr, nelems);
  }

  template <typename T>
  static void checkin(T* raw_ptr) {
    pc().checkin(raw_ptr);
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

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) { return ptr; }

  template <typename T>
  static void checkin(T* raw_ptr) {}
};

// Wallclock Time
// -----------------------------------------------------------------------------

template <typename P>
class wallclock_native {
public:
  static uint64_t gettime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
  }
};

template <typename P>
class wallclock_madm {
public:
  static uint64_t gettime() {
    return madi::global_clock::get_time();
  }
};

template <typename P>
class wallclock_pcas {
public:
  static uint64_t gettime() {
    return pcas::global_clock::get_time();
  }
};

// Logger
// -----------------------------------------------------------------------------

template <typename P>
class logger_dummy {
public:
  static void flush(uint64_t t0, uint64_t t1) {}
};

template <typename P>
class logger_pcas {
public:
  static void flush(uint64_t t0, uint64_t t1) {
    pcas::logger::flush_and_print_stat(t0, t1);
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

  using wallclock = typename P::template wallclock_t<P>;

  using logger = typename P::template logger_t<P>;

  static uint64_t rank() { return P::rank(); }

  static uint64_t n_ranks() { return P::n_ranks(); }

  template <typename F, typename... Args>
  static void main(F f, Args... args) {
    P::main(f, args...);
  }

  static void barrier() { return P::barrier(); }
};

// Serial
// -----------------------------------------------------------------------------

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group_serial {
public:
  ito_group_serial() {}

  template <typename F, typename... Args>
  void run(F f, Args... args) { f(args...); }

  void wait() {}
};

struct ityr_policy_serial {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_serial<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using iro_t = iro_dummy<P>;

  template <typename P>
  using wallclock_t = wallclock_native<P>;

  template <typename P>
  using logger_t = logger_dummy<P>;

  static uint64_t rank() { return 0; }

  static uint64_t n_ranks() { return 1; }

  template <typename F, typename... Args>
  static void main(F f, Args... args) { f(args...); }

  static void barrier() {};
};

// Naive
// -----------------------------------------------------------------------------

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group_naive {
  using iro = typename P::template iro_t<P>;

  madm::uth::thread<void> tasks_[MaxTasks];
  size_t n_ = 0;

public:
  ito_group_naive() {}

  template <typename F, typename... Args>
  void run(F f, Args... args) {
    assert(n_ < MaxTasks);
    if (SpawnLastTask || n_ < MaxTasks - 1) {
      iro::release();
      new (&tasks_[n_++]) madm::uth::thread<void>{[=] {
        iro::acquire();
        f(args...);
        iro::release();
      }};
      iro::acquire();
    } else {
      f(args...);
    }
  }

  void wait() {
    iro::release();
    for (size_t i = 0; i < n_; i++) {
      tasks_[i].join();
    }
    iro::acquire();
    n_ = 0;
  }
};

struct ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_naive<P, MaxTasks, SpawnLastTask>;

  template <typename P>
  using iro_t = iro_pcas<P>;

  template <typename P>
  using wallclock_t = wallclock_pcas<P>;

  template <typename P>
  using logger_t = logger_pcas<P>;

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

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group_workfirst {
  using iro = typename P::template iro_t<P>;

  madm::uth::thread<void> tasks_[MaxTasks];
  bool synched_[MaxTasks];
  uint64_t initial_rank;
  size_t n_ = 0;

public:
  ito_group_workfirst() { initial_rank = ityr_if<P>::rank(); }

  template <typename F, typename... Args>
  void run(F f, Args... args) {
    assert(n_ < MaxTasks);
    if (SpawnLastTask || n_ < MaxTasks - 1) {
      auto p_th = &tasks_[n_];
      iro::release();
      new (p_th) madm::uth::thread<void>{};
      synched_[n_] = p_th->spawn_aux(f, std::make_tuple(args...),
      [=] (bool parent_popped) {
        // on-die callback
        if (!parent_popped) {
          iro::release();
        }
      });
      if (!synched_[n_]) {
        iro::acquire();
      }
      n_++;
    } else {
      f(args...);
    }
  }

  void wait() {
    bool blocked = false;
    for (size_t i = 0; i < n_; i++) {
      tasks_[i].join_aux(0, [&blocked] {
        // on-block callback
        if (!blocked) {
          iro::release();
          blocked = true;
        }
      });
    }
    bool all_synched = true;
    for (size_t i = 0; i < n_; i++) {
      if (!synched_[i]) {
        all_synched = false;
        break;
      }
    }
    if (initial_rank != ityr_if<P>::rank() ||
        (!all_synched || blocked)) {
      // FIXME: (all_synched && blocked) is true only for root tasks
      iro::acquire();
    }
    n_ = 0;
  }
};

struct ityr_policy_workfirst : ityr_policy_naive {
  template <typename P, size_t MaxTasks, bool SpawnLastTask>
  using ito_group_t = ito_group_workfirst<P, MaxTasks, SpawnLastTask>;
};

// Policy selection
// -----------------------------------------------------------------------------

#ifndef ITYR_POLICY
#define ITYR_POLICY ityr_policy_naive
#endif

using ityr = ityr_if<ITYR_POLICY>;

#undef ITYR_POLICY
