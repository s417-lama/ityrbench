#include "uth.h"
#include "pcas/pcas.hpp"

template <typename P>
class iro;

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group {
  using iro = iro<P>;

  madm::uth::thread<void> tasks_[MaxTasks];
  size_t n_ = 0;

public:
  ito_group() {}

  template <typename F, typename... Args>
  void run(F f, Args... args) {
    assert(n_ < MaxTasks);
    if (SpawnLastTask || n_ < MaxTasks - 1) {
      if (!P::work_first) {
        iro::release();
      }
      new (&tasks_[n_++]) madm::uth::thread<void>{[=]() {
        if (!P::work_first) {
          iro::acquire();
        }
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

template <typename P>
class iro {
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
struct ityr_if {
  template <size_t MaxTasks, bool SpawnLastTask = false>
  using ito_group = ito_group<P, MaxTasks, SpawnLastTask>;

  using iro = iro<P>;

  static uint64_t rank() {
    return madm::uth::get_pid();
  }

  static uint64_t nprocs() {
    return madm::uth::get_n_procs();
  }

  template <typename F, typename... Args>
  static void main(F f, Args... args) {
    madm::uth::start(f, args...);
  }
};

struct ityr_policy {
  static constexpr bool work_first = false;
};
