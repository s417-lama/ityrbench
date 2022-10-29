#pragma once

#include "pcas/pcas.hpp"

namespace ityr {

template <typename P, typename GPtrT>
class iro_ref : public pcas::global_ref_base<GPtrT> {
  using base_t = pcas::global_ref_base<GPtrT>;
  using this_t = iro_ref;
  using value_t = typename GPtrT::value_type;
  using iro = typename P::iro_t;

  using base_t::ptr_;

  template <typename Fn>
  void with_read_write(Fn&& f) {
    value_t* vp = iro::checkout<iro::access_mode::read_write>(ptr_, 1);
    std::forward<Fn>(f)(*vp);
    iro::checkin(vp, 1);
  }

public:
  using base_t::base_t;

  operator value_t() {
    std::remove_const_t<value_t> ret;
    iro::get(ptr_, &ret, 1);
    return ret;
  }

  this_t& operator=(const value_t& v) {
    iro::put(&v, ptr_, 1);
    return *this;
  }

  this_t& operator+=(const value_t& v) {
    with_read_write([&](value_t& this_v) { this_v += v; });
    return *this;
  }

  this_t& operator-=(const value_t& v) {
    with_read_write([&](value_t& this_v) { this_v -= v; });
    return *this;
  }

  this_t& operator++() {
    with_read_write([&](value_t& this_v) { ++this_v; });
    return *this;
  }

  this_t& operator--() {
    with_read_write([&](value_t& this_v) { --this_v; });
    return *this;
  }
};

}
