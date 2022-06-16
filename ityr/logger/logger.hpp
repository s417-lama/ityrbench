#pragma once

#include <cstdio>
#include <cstdint>

#include "ityr/logger/kind.hpp"
#include "ityr/logger/impl_dummy.hpp"
#include "ityr/logger/impl_trace.hpp"
#include "ityr/logger/impl_stats.hpp"

namespace ityr {
namespace logger {

template <typename ParentPolicy>
struct policy {
  static const char* outfile_prefix() { return "ityr"; }
  using iro_t = typename ParentPolicy::template iro_t<ParentPolicy>;
  using wallclock_t = typename ParentPolicy::wallclock_t;
  using logger_kind_t = typename ParentPolicy::logger_kind_t;
  template <typename P>
  using impl_t = typename ParentPolicy::template logger_impl_t<P>;
};

template <typename P>
class logger_if {
  using iro = typename P::iro_t;
  using kind = typename P::logger_kind_t;
  using impl = typename P::template impl_t<P>;

public:
  using begin_data_t = typename impl::begin_data_t;

  static void init(int rank, int n_ranks) {
    impl::init(rank, n_ranks);
  }

  static void flush(uint64_t t_begin, uint64_t t_end) {
    iro::logger_flush(t_begin, t_end);
    impl::flush(t_begin, t_end);
  }

  static void flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {
    iro::logger_flush_and_print_stat(t_begin, t_end);
    impl::flush_and_print_stat(t_begin, t_end);
  }

  static void warmup() {
    impl::warmup();
  }

  static void clear() {
    impl::clear();
  }

  template <typename kind::value K>
  static begin_data_t begin_event() {
    return impl::template begin_event<K>();
  }

  template <typename kind::value K>
  static void end_event(begin_data_t bd) {
    impl::template end_event<K>(bd);
  }

  template <typename kind::value K, typename Misc>
  static void end_event(begin_data_t bd, Misc m) {
    impl::template end_event<K, Misc>(bd, m);
  }

  template <typename kind::value K>
  class scope_event {
    begin_data_t bd_;
  public:
    scope_event() {
      bd_ = begin_event<K>();
    };
    ~scope_event() {
      end_event<K>(bd_);
    }
  };

  template <typename kind::value K, typename Misc>
  class scope_event_m {
    begin_data_t bd_;
    Misc m_;
  public:
    scope_event_m(Misc m) {
      bd_ = begin_event<K>();
      m_ = m;
    };
    ~scope_event_m() {
      end_event<K>(bd_, m_);
    }
  };

  template <typename kind::value K>
  static scope_event<K> record() {
    return scope_event<K>();
  }

  template <typename kind::value K, typename Misc>
  static scope_event_m<K, Misc> record(Misc m) {
    return scope_event_m<K, Misc>(m);
  }

};

}
}
