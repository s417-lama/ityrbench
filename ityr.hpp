#include "uth.h"
#include "pcas/pcas.hpp"

template <typename P>
class ityr_if;

template <typename P>
class iro_if;

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group {
  using iro = iro_if<P>;

  madm::uth::thread<void> tasks_[MaxTasks];
  bool synched_[MaxTasks];
  uint64_t initial_rank;
  size_t n_ = 0;

public:
  ito_group() { initial_rank = ityr_if<P>::rank(); }

  template <typename F, typename... Args>
  void run(F f, Args... args) {
    assert(n_ < MaxTasks);
    if (SpawnLastTask || n_ < MaxTasks - 1) {
      auto p_th = &tasks_[n_];
      if (!P::work_first) {

        iro::release();
        new (p_th) madm::uth::thread<void>{[=] {
          iro::acquire();
          f(args...);
          iro::release();
        }};
        iro::acquire();

      } else {

        iro::release();
        new (p_th) madm::uth::thread<void>{};
        synched_[n_] = p_th->spawn_aux([=]() {
          if (!P::work_first) {
            iro::acquire();
          }
          f(args...);
        }, std::make_tuple(), [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        });
        if (!synched_[n_]) {
          iro::acquire();
        }

      }
      n_++;
    } else {
      f(args...);
    }
  }

  void wait() {
    if (!P::work_first) {

      iro::release();
      for (size_t i = 0; i < n_; i++) {
        tasks_[i].join();
      }
      iro::acquire();

    } else {

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

    }
    n_ = 0;
  }
};

template <typename P>
class iro_if {
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
class ityr_if {
public:
  template <size_t MaxTasks, bool SpawnLastTask = false>
  using ito_group = ito_group<P, MaxTasks, SpawnLastTask>;

  using iro = iro_if<P>;

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

struct ityr_policy_workfirst : ityr_policy {
  static constexpr bool work_first = true;
};

#ifndef ITYR_POLICY
#define ITYR_POLICY ityr_policy
#endif

using ityr = ityr_if<ITYR_POLICY>;

#undef ITYR_POLICY
