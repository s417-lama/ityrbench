#pragma once

#include "uth.h"

namespace ityr {

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group_serial {
public:
  ito_group_serial() {}

  template <typename F, typename... Args>
  void run(F f, Args... args) { f(args...); }

  void wait() {}
};

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

template <typename P, size_t MaxTasks, bool SpawnLastTask>
class ito_group_workfirst {
  using iro = typename P::template iro_t<P>;

  madm::uth::thread<void> tasks_[MaxTasks];
  bool synched_[MaxTasks];
  uint64_t initial_rank;
  size_t n_ = 0;

public:
  ito_group_workfirst() { initial_rank = P::rank(); }

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
    if (initial_rank != P::rank() ||
        (!all_synched || blocked)) {
      // FIXME: (all_synched && blocked) is true only for root tasks
      iro::acquire();
    }
    n_ = 0;
  }
};

}
