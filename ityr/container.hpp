#pragma once

#include <cstdlib>
#include <cassert>

#include "pcas/pcas.hpp"

#include "ityr/util.hpp"
#include "ityr/iro.hpp"
#include "ityr/ito_pattern.hpp"

namespace ityr {

template <typename T>
class raw_span {
  using this_t = raw_span<T>;

  T* ptr_;
  std::size_t n_;

public:
  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using iterator = T*;
  using pointer = T*;
  using reference = T&;

  raw_span() : ptr_(nullptr), n_(0) {}
  raw_span(T* ptr, std::size_t n) : ptr_(ptr), n_(n) {}
  template <typename U>
  raw_span(raw_span<U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

  constexpr pointer data() const noexcept { return ptr_; }
  constexpr std::size_t size() const noexcept { return n_; }

  constexpr iterator begin() const noexcept { return ptr_; }
  constexpr iterator end()   const noexcept { return ptr_ + n_; }

  constexpr reference operator[](std::size_t i) const { assert(i < n_); return ptr_[i]; }

  constexpr reference front() const { return *ptr_; }
  constexpr reference back() const { return *(ptr_ + n_ - 1); }

  constexpr bool empty() const noexcept { return n_ == 0; }

  constexpr this_t subspan(std::size_t offset, std::size_t count) const {
    assert(offset + count <= n_);
    return {ptr_ + offset, count};
  }

  constexpr std::pair<this_t, this_t> divide(std::size_t at) const {
    return {subspan(0, at), subspan(at, n_ - at)};
  }

  constexpr std::pair<this_t, this_t> divide_two() const {
    return divide(n_ / 2);
  }

  template <typename F>
  void for_each(F f) const {
    for (std::size_t i = 0; i < n_; i++) {
      f(const_cast<const T&>(ptr_[i]));
    }
  }

  template <typename F>
  void map(F f) {
    for (std::size_t i = 0; i < n_; i++) {
      f(ptr_[i]);
    }
  }

  template <typename Acc, typename ReduceOp, typename TransformOp>
  Acc reduce(Acc         init,
             ReduceOp    reduce_op,
             TransformOp transform_op) {
    Acc acc = init;
    for (std::size_t i = 0; i < n_; i++) {
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

struct global_vector_options {
  bool collective = false;
  bool parallel_construct = true;
  bool parallel_destruct = true;
  std::size_t cutoff = 1024;
};

template <typename P>
struct global_container_if {

  template <typename T>
  class global_span {
    using this_t = global_span<T>;
    using ptr_t = typename P::iro::template global_ptr<T>;

    ptr_t ptr_;
    std::size_t n_;

  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using iterator = ptr_t;
    using pointer = ptr_t;
    using reference = typename std::iterator_traits<ptr_t>::reference;

    using policy = P;

    global_span() : ptr_(nullptr), n_(0) {}
    global_span(ptr_t ptr, std::size_t n) : ptr_(ptr), n_(n) {}
    template <typename U>
    global_span(global_span<U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

    constexpr pointer data() const noexcept { return ptr_; }
    constexpr std::size_t size() const noexcept { return n_; }

    constexpr pointer begin() const noexcept { return ptr_; }
    constexpr pointer end()   const noexcept { return ptr_ + n_; }

    constexpr reference operator[](std::size_t i) const {
      assert(i < n_);
      return ptr_[i];
    }

    constexpr reference front() const { return *ptr_; }
    constexpr reference back() const { return *(ptr_ + n_ - 1); }

    constexpr bool empty() const noexcept { return n_ == 0; }

    constexpr this_t subspan(std::size_t offset, std::size_t count) const {
      assert(offset + count <= n_);
      return {ptr_ + offset, count};
    }

    constexpr std::pair<this_t, this_t> divide(std::size_t at) const {
      return {subspan(0, at), subspan(at, n_ - at)};
    }

    constexpr std::pair<this_t, this_t> divide_two() const {
      return divide(n_ / 2);
    }

    template <typename F, std::size_t BlockSize = 65536>
    void for_each(F f) const {
      for (std::size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
        std::size_t b = std::min(n_ - i, BlockSize / sizeof(T));
        auto p = P::iro::template checkout<pcas::access_mode::read>(ptr_ + i, b);
        for (std::size_t j = 0; j < b; j++) {
          f(const_cast<const T&>(p[j]));
        }
        P::iro::template checkin(p, b);
      }
    }

    template <typename F, std::size_t BlockSize = 65536>
    void map(F f) {
      for (std::size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
        std::size_t b = std::min(n_ - i, BlockSize / sizeof(T));
        auto p = P::iro::template checkout<pcas::access_mode::read_write>(ptr_ + i, b);
        for (std::size_t j = 0; j < b; j++) {
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

  template <typename T>
  class global_vector {
    using this_t = global_vector<T>;

  public:
    using value_type      = T;
    using size_type       = std::size_t;
    using pointer         = typename P::iro::template global_ptr<T>;
    using const_pointer   = typename P::iro::template global_ptr<std::add_const_t<T>>;
    using iterator        = pointer;
    using const_iterator  = const_pointer;
    using difference_type = typename pointer::difference_type;
    using reference       = typename pointer::reference;
    using const_reference = typename const_pointer::reference;

    using policy = P;

  private:
    pointer begin_        = nullptr;
    pointer end_          = nullptr;
    pointer reserved_end_ = nullptr;

    global_vector_options opts_;

    size_type next_size(size_type least) const {
      return std::max(least, size() * 2);
    }

    pointer allocate_mem(size_type count) const {
      if (opts_.collective) {
        return P::iro::template malloc<T>(count);
      } else {
        return P::iro::template malloc_local<T>(count);
      }
    }

    void free_mem(pointer p, size_type count) const {
      P::iro::template free<T>(p, count);
    }

    template <typename Fn, typename... Args>
    auto master_do_if_coll(Fn&& f, Args&&... args) const {
      if (opts_.collective) {
        return P::ito_pattern::master_do(std::forward<Fn>(f), std::forward<Args>(args)...);
      } else {
        return std::forward<Fn>(f)(std::forward<Args>(args)...);
      }
    }

    template <typename... Args>
    void initialize_uniform(size_type count, Args&&... args) {
      begin_ = allocate_mem(count);
      end_ = begin_ + count;
      reserved_end_ = begin_ + count;

      master_do_if_coll([&]() {
        construct_elems(begin(), end(), std::forward<Args>(args)...);
      });
    }

    template <typename InputIterator>
    void initialize_from_iter(InputIterator first, InputIterator last, std::input_iterator_tag) {
      assert(!opts_.collective);
      assert(!opts_.parallel_construct);

      for (; first != last; ++first) {
        emplace_back(*first);
      }
    }

    template <typename ForwardIterator>
    void initialize_from_iter(ForwardIterator first, ForwardIterator last, std::forward_iterator_tag) {
      auto d = std::distance(first, last);

      begin_ = allocate_mem(d);
      end_ = begin_ + d;
      reserved_end_ = begin_ + d;

      master_do_if_coll([&]() {
        construct_elems_from_iter(first, last, begin());
      });
    }

    template <typename... Args>
    void construct_elems(pointer b, pointer e, Args&&... args) {
      if (opts_.parallel_construct) {
        P::ito_pattern::parallel_for<P::iro::access_mode::write>(
            b, e, [=](auto&& x) { new (&x) T(args...); }, opts_.cutoff);
      } else {
        P::ito_pattern::serial_for<P::iro::access_mode::write>(
            b, e, [&](auto&& x) { new (&x) T(std::forward<Args>(args)...); }, opts_.cutoff);
      }
    }

    template <typename ForwardIterator>
    void construct_elems_from_iter(ForwardIterator first, ForwardIterator last, pointer b) {
      if constexpr (is_const_iterator_v<ForwardIterator>) {
        if (opts_.parallel_construct) {
          P::ito_pattern::parallel_for<P::iro::access_mode::read, P::iro::access_mode::write>(
              first, last, b, [](const auto& src, auto&& x) { new (&x) T(src); }, opts_.cutoff);
        } else {
          P::ito_pattern::serial_for<P::iro::access_mode::read, P::iro::access_mode::write>(
              first, last, b, [](const auto& src, auto&& x) { new (&x) T(src); }, opts_.cutoff);
        }
      } else {
        if (opts_.parallel_construct) {
          P::ito_pattern::parallel_for<P::iro::access_mode::read_write, P::iro::access_mode::write>(
              first, last, b, [](auto&& src, auto&& x) { new (&x) T(std::forward<decltype(src)>(src)); }, opts_.cutoff);
        } else {
          P::ito_pattern::serial_for<P::iro::access_mode::read_write, P::iro::access_mode::write>(
              first, last, b, [](auto&& src, auto&& x) { new (&x) T(std::forward<decltype(src)>(src)); }, opts_.cutoff);
        }
      }
    }

    void destruct_elems(pointer b, pointer e) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        if (opts_.parallel_destruct) {
          P::ito_pattern::parallel_for<P::iro::access_mode::read_write>(
              b, e, [](auto&& x) { std::destroy_at(&x); }, opts_.cutoff);
        } else {
          P::ito_pattern::serial_for<P::iro::access_mode::read_write>(
              b, e, [](auto&& x) { std::destroy_at(&x); }, opts_.cutoff);
        }
      }
    }

    void realloc_mem(size_type count) {
      pointer old_begin = begin_;
      pointer old_end = end_;
      size_type old_capacity = capacity();

      begin_ = allocate_mem(count);
      end_ = begin_ + (old_end - old_begin);
      reserved_end_ = begin_ + count;

      construct_elems_from_iter(make_move_iterator(old_begin),
                                make_move_iterator(old_end),
                                begin());

      destruct_elems(old_begin, old_end);
      free_mem(old_begin, old_capacity);
    }

    template <typename... Args>
    void resize_impl(size_type count, Args&&... args) {
      if (count > size()) {
        if (count > capacity()) {
          size_type new_cap = next_size(count);
          realloc_mem(new_cap);
        }
        master_do_if_coll([&]() {
          construct_elems(end(), begin() + count, std::forward<Args>(args)...);
        });
        end_ = begin() + count;

      } else if (count < size()) {
        master_do_if_coll([&]() {
          destruct_elems(begin() + count, end());
        });
        end_ = begin() + count;
      }
    }

    template <typename... Args>
    void push_back_impl(Args&&... args) {
      assert(!opts_.collective);
      if (size() == capacity()) {
        size_type new_cap = next_size(size() + 1);
        realloc_mem(new_cap);
      }
      P::iro::template with_checkout<pcas::access_mode::write>(end(), 1,
          [&](auto&& x) { new (&x) T(std::forward<Args>(args)...); });
      ++end_;
    }

  public:
    global_vector() : global_vector(global_vector_options()) {}
    explicit global_vector(size_type count) : global_vector(global_vector_options(), count) {}
    explicit global_vector(size_type count, const T& value) : global_vector(global_vector_options(), count, value) {}
    template <typename InputIterator>
    global_vector(InputIterator first, InputIterator last) : global_vector(global_vector_options(), first, last) {}

    explicit global_vector(const global_vector_options& opts) : opts_(opts) {}

    explicit global_vector(const global_vector_options& opts, size_type count) : opts_(opts) {
      initialize_uniform(count);
    }

    explicit global_vector(const global_vector_options& opts, size_type count, const T& value) : opts_(opts) {
      initialize_uniform(count, value);
    }

    template <typename InputIterator>
    global_vector(const global_vector_options& opts, InputIterator first, InputIterator last) : opts_(opts) {
      initialize_from_iter(first, last,
                           typename std::iterator_traits<InputIterator>::iterator_category());
    }

    ~global_vector() {
      if (begin() != nullptr) {
        destruct_elems(begin(), end());
        free_mem(begin(), capacity());
      }
    }

    global_vector(const this_t& other) : opts_(other.options()) {
      initialize_from_iter(other.cbegin(), other.cend(), std::random_access_iterator_tag{});
    }
    this_t& operator=(const this_t& other) {
      // TODO: skip freeing memory and reuse it when it has enough amount of memory
      this->~global_vector();
      // should we copy options?
      opts_ = other.options();
      initialize_from_iter(other.cbegin(), other.cend(), std::random_access_iterator_tag{});
      return *this;
    }

    global_vector(this_t&& other) :
        begin_(other.begin_),
        end_(other.end_),
        reserved_end_(other.reserved_end_),
        opts_(other.opts_) {
      other.begin_ = nullptr;
    }
    this_t& operator=(this_t&& other) {
      this->~global_vector();
      begin_ = other.begin_;
      end_ = other.end_;
      reserved_end_ = other.reserved_end_;
      opts_ = other.opts_;
      other.begin_ = nullptr;
      return *this;
    }

    pointer data() const noexcept { return begin_; }
    size_type size() const noexcept { return end_ - begin_; }
    size_type capacity() const noexcept { return reserved_end_ - begin_; }

    global_vector_options options() const noexcept { return opts_; }

    iterator begin() const noexcept { return begin_; }
    iterator end() const noexcept { return end_; }

    const_iterator cbegin() const noexcept { return begin_; }
    const_iterator cend() const noexcept { return end_; }

    reference operator[](size_type i) const {
      assert(i <= size());
      return *(begin() + i);
    }
    reference at(size_type i) const {
      if (i >= size()) {
        std::stringstream ss;
        ss << "Global vector: Index " << i << " is out of range [0, " << size() << ").";
        throw std::out_of_range(ss.str());
      }
      return (*this)[i];
    }

    reference front() const { return *begin(); }
    reference back() const { return *(end() - 1); }

    bool empty() const noexcept { return size() == 0; }

    void swap(this_t& other) noexcept {
      using std::swap;
      swap(begin_, other.begin_);
      swap(end_, other.end_);
      swap(reserved_end_, other.reserved_end_);
      swap(opts_, other.opts_);
    }

    void clear() {
      destruct_elems();
      end_ = begin();
    }

    void reserve(size_type new_cap) {
      if (capacity() == 0 && new_cap > 0) {
        begin_ = allocate_mem(new_cap);
        end_ = begin_;
        reserved_end_ = begin_ + new_cap;

      } else if (new_cap > capacity()) {
        realloc_mem(new_cap);
      }
    }

    void resize(size_type count) {
      resize_impl(count);
    }

    void resize(size_type count, const value_type& value) {
      resize_impl(count, value);
    }

    void push_back(const value_type& value) {
      push_back_impl(value);
    }

    void push_back(value_type&& value) {
      push_back_impl(std::move(value));
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
      push_back_impl(std::forward<Args>(args)...);
      return back();
    }

    void pop_back() {
      assert(!opts_.collective);
      assert(size() > 0);
      P::iro::template with_checkout<pcas::access_mode::read_write>(end() - 1, 1,
          [&](auto&& x) { std::destroy_at(&x); });
      --end_;
    }

    friend void swap(this_t& v1, this_t& v2) noexcept {
      v1.swap(v2);
    }

  };

};

template <pcas::access_mode Mode,
          typename GlobalSpan, typename Fn>
void with_checkout(GlobalSpan s, Fn f) {
  using T = typename GlobalSpan::element_type;
  using iro = typename GlobalSpan::policy::iro;
  iro::template with_checkout<Mode>(s.data(), s.size(),
                                    [&](auto&& p) {
    f(raw_span<T>{p, s.size()});
  });
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          typename GlobalSpan1, typename GlobalSpan2, typename Fn>
void with_checkout(GlobalSpan1 s1, GlobalSpan2 s2, Fn f) {
  using T1 = typename GlobalSpan1::element_type;
  using T2 = typename GlobalSpan2::element_type;
  using iro = typename GlobalSpan1::policy::iro;
  iro::template with_checkout<Mode1, Mode2>(s1.data(), s1.size(),
                                            s2.data(), s2.size(),
                                            [&](auto&& p1, auto&& p2) {
    f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()});
  });
}

template <pcas::access_mode Mode1,
          pcas::access_mode Mode2,
          pcas::access_mode Mode3,
          typename GlobalSpan1, typename GlobalSpan2, typename GlobalSpan3, typename Fn>
void with_checkout(GlobalSpan1 s1, GlobalSpan2 s2, GlobalSpan3 s3, Fn f) {
  using T1 = typename GlobalSpan1::element_type;
  using T2 = typename GlobalSpan2::element_type;
  using T3 = typename GlobalSpan3::element_type;
  using iro = typename GlobalSpan1::policy::iro;
  iro::template with_checkout<Mode1, Mode2, Mode3>(s1.data(), s1.size(),
                                                   s2.data(), s2.size(),
                                                   s3.data(), s3.size(),
                                                   [&](auto&& p1, auto&& p2, auto&& p3) {
    f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()}, raw_span<T3>{p3, s3.size()});
  });
}

struct global_container_policy_default {
  using iro = iro_if<iro_policy_default>;
  using ito_pattern = ito_pattern_if<ito_pattern_policy_default>;
};

}
