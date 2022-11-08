#pragma once

#include <cstdlib>
#include <cassert>

#include "pcas/pcas.hpp"

#include "ityr/iro.hpp"
#include "ityr/ito_pattern.hpp"

namespace ityr {

template <typename T>
class raw_span {
  using this_t = raw_span<T>;
  T* ptr_;
  size_t n_;

public:
  using element_type = T;

  raw_span(T* ptr, size_t n) : ptr_(ptr), n_(n) {}
  template <typename U>
  raw_span(raw_span<U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

  constexpr T* data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return n_; }

  constexpr T* begin() const noexcept { return ptr_; }
  constexpr T* end()   const noexcept { return ptr_ + n_; }

  constexpr T& operator[](size_t i) const { assert(i < n_); return ptr_[i]; }

  constexpr this_t subspan(size_t offset, size_t count) const {
    assert(offset + count <= n_);
    return {ptr_ + offset, count};
  }

  constexpr std::pair<this_t, this_t> divide(size_t at) const {
    return {subspan(0, at), subspan(at, n_ - at)};
  }

  constexpr std::pair<this_t, this_t> divide_two() const {
    return divide(n_ / 2);
  }

  template <typename F>
  void for_each(F f) const {
    for (size_t i = 0; i < n_; i++) {
      f(const_cast<const T&>(ptr_[i]));
    }
  }

  template <typename F>
  void map(F f) {
    for (size_t i = 0; i < n_; i++) {
      f(ptr_[i]);
    }
  }

  template <typename Acc, typename ReduceOp, typename TransformOp>
  Acc reduce(Acc         init,
             ReduceOp    reduce_op,
             TransformOp transform_op) {
    Acc acc = init;
    for (size_t i = 0; i < n_; i++) {
      acc = reduce_op(acc, transform_op(ptr_[i]));
    }
    return acc;
  }

  void willread() const {}
};

template <pcas::access_mode Mode,
          typename T, typename Fn>
void with_checkout(raw_span<T> s, Fn f) {
  f(s);
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          typename T1, typename T2, typename Fn>
void with_checkout(raw_span<T1> s1, raw_span<T2> s2, Fn f) {
  f(s1, s2);
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          pcas::access_mode Mode3,
          typename T1, typename T2, typename T3, typename Fn>
void with_checkout(raw_span<T1> s1, raw_span<T2> s2, raw_span<T3> s3, Fn f) {
  f(s1, s2, s3);
}

template <typename P, typename T>
class global_span_if {
  using this_t = global_span_if<P, T>;
  using ptr_t = typename P::iro::template global_ptr<T>;
  ptr_t ptr_;
  size_t n_;

public:
  using element_type = T;

  global_span_if(ptr_t ptr, size_t n) : ptr_(ptr), n_(n) {}
  template <typename U>
  global_span_if(global_span_if<P, U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

  constexpr ptr_t data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return n_; }

  constexpr ptr_t begin() const noexcept { return ptr_; }
  constexpr ptr_t end()   const noexcept { return ptr_ + n_; }

  constexpr auto operator[](size_t i) const {
    assert(i < n_);
    return ptr_[i];
  }

  constexpr this_t subspan(size_t offset, size_t count) const {
    assert(offset + count <= n_);
    return {ptr_ + offset, count};
  }

  constexpr std::pair<this_t, this_t> divide(size_t at) const {
    return {subspan(0, at), subspan(at, n_ - at)};
  }

  constexpr std::pair<this_t, this_t> divide_two() const {
    return divide(n_ / 2);
  }

  template <typename F, std::size_t BlockSize = 65536>
  void for_each(F f) const {
    for (size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
      size_t b = std::min(n_ - i, BlockSize / sizeof(T));
      auto p = P::iro::template checkout<pcas::access_mode::read>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(const_cast<const T&>(p[j]));
      }
      P::iro::template checkin(p, b);
    }
  }

  template <typename F, std::size_t BlockSize = 65536>
  void map(F f) {
    for (size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
      size_t b = std::min(n_ - i, BlockSize / sizeof(T));
      auto p = P::iro::template checkout<pcas::access_mode::read_write>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(p[j]);
      }
      P::iro::template checkin(p, b);
    }
  }

  template <typename Acc, typename ReduceOp, typename TransformOp, std::size_t BlockSize = 65536>
  Acc reduce(Acc         init,
             ReduceOp    reduce_op,
             TransformOp transform_op) const {
    return P::ito_pattern::parallel_reduce(ptr_, ptr_ + n_, init, reduce_op, transform_op, BlockSize);
  }

  void willread() const {
    P::iro::willread(ptr_, n_);
  }
};

template <pcas::access_mode Mode,
          typename P, typename T, typename Fn>
void with_checkout(global_span_if<P, T> s, Fn f) {
  auto p = P::iro::template checkout<Mode>(s.data(), s.size());
  f(raw_span<T>{p, s.size()});
  P::iro::template checkin<Mode>(p, s.size());
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          typename P, typename T1, typename T2, typename Fn>
void with_checkout(global_span_if<P, T1> s1, global_span_if<P, T2> s2, Fn f) {
  auto p1 = P::iro::template checkout<Mode1>(s1.data(), s1.size());
  auto p2 = P::iro::template checkout<Mode2>(s2.data(), s2.size());
  f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()});
  P::iro::template checkin<Mode1>(p1, s1.size());
  P::iro::template checkin<Mode2>(p2, s2.size());
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          pcas::access_mode Mode3,
          typename P, typename T1, typename T2, typename T3, typename Fn>
void with_checkout(global_span_if<P, T1> s1, global_span_if<P, T2> s2, global_span_if<P, T3> s3, Fn f) {
  auto p1 = P::iro::template checkout<Mode1>(s1.data(), s1.size());
  auto p2 = P::iro::template checkout<Mode2>(s2.data(), s2.size());
  auto p3 = P::iro::template checkout<Mode3>(s3.data(), s3.size());
  f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()}, raw_span<T3>{p3, s3.size()});
  P::iro::template checkin<Mode1>(p1, s1.size());
  P::iro::template checkin<Mode2>(p2, s2.size());
  P::iro::template checkin<Mode3>(p3, s3.size());
}

struct global_span_policy_default {
  using iro = iro_if<iro_policy_default>;
  using ito_pattern = ito_pattern_if<ito_pattern_policy_default>;
};

}
