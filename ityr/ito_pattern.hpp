#pragma once

#include <tuple>
#include <functional>

#include "uth.h"

#include "ityr/iro.hpp"

#define ITYR_CONCAT(a, b) a##b

#define ITYR_FORLOOP_0(op, arg) op(0, arg)
#define ITYR_FORLOOP_1(op, arg) ITYR_FORLOOP_0(op, arg) op(1, arg)
#define ITYR_FORLOOP_2(op, arg) ITYR_FORLOOP_1(op, arg) op(2, arg)
#define ITYR_FORLOOP_3(op, arg) ITYR_FORLOOP_2(op, arg) op(3, arg)
#define ITYR_FORLOOP_4(op, arg) ITYR_FORLOOP_3(op, arg) op(4, arg)
#define ITYR_FORLOOP_5(op, arg) ITYR_FORLOOP_4(op, arg) op(5, arg)
#define ITYR_FORLOOP_6(op, arg) ITYR_FORLOOP_5(op, arg) op(6, arg)
#define ITYR_FORLOOP_7(op, arg) ITYR_FORLOOP_6(op, arg) op(7, arg)
#define ITYR_FORLOOP_8(op, arg) ITYR_FORLOOP_7(op, arg) op(8, arg)

#define ITYR_FORLOOP_P_0(op)
#define ITYR_FORLOOP_P_1(op) ITYR_FORLOOP_P_0(op) , op(1)
#define ITYR_FORLOOP_P_2(op) ITYR_FORLOOP_P_1(op) , op(2)
#define ITYR_FORLOOP_P_3(op) ITYR_FORLOOP_P_2(op) , op(3)
#define ITYR_FORLOOP_P_4(op) ITYR_FORLOOP_P_3(op) , op(4)
#define ITYR_FORLOOP_P_5(op) ITYR_FORLOOP_P_4(op) , op(5)
#define ITYR_FORLOOP_P_6(op) ITYR_FORLOOP_P_5(op) , op(6)
#define ITYR_FORLOOP_P_7(op) ITYR_FORLOOP_P_6(op) , op(7)
#define ITYR_FORLOOP_P_8(op) ITYR_FORLOOP_P_7(op) , op(8)

#define ITYR_FORLOOP_C_0(op)
#define ITYR_FORLOOP_C_1(op) op(1)
#define ITYR_FORLOOP_C_2(op) ITYR_FORLOOP_C_1(op) , op(2)
#define ITYR_FORLOOP_C_3(op) ITYR_FORLOOP_C_2(op) , op(3)
#define ITYR_FORLOOP_C_4(op) ITYR_FORLOOP_C_3(op) , op(4)
#define ITYR_FORLOOP_C_5(op) ITYR_FORLOOP_C_4(op) , op(5)
#define ITYR_FORLOOP_C_6(op) ITYR_FORLOOP_C_5(op) , op(6)
#define ITYR_FORLOOP_C_7(op) ITYR_FORLOOP_C_6(op) , op(7)
#define ITYR_FORLOOP_C_8(op) ITYR_FORLOOP_C_7(op) , op(8)

#define ITYR_TEMPLATE_PARAM_STR(x) ITYR_CONCAT(typename Arg, x)
#define ITYR_TYPENAME_STR(x) ITYR_CONCAT(Arg, x)
#define ITYR_FUNC_PARAM_STR(x) ITYR_CONCAT(Arg, x)&& ITYR_CONCAT(arg, x)
#define ITYR_FORWARD_ARG_STR(x) std::forward<ITYR_CONCAT(Arg, x)>(ITYR_CONCAT(arg, x))

#define ITYR_PARALLEL_INVOKE_DEF_IMPL(n, impl) \
  template <typename Fn ITYR_FORLOOP_P_##n(ITYR_TEMPLATE_PARAM_STR), \
            typename... Rest, \
            std::enable_if_t<std::is_invocable_v<Fn ITYR_FORLOOP_P_##n(ITYR_TYPENAME_STR)>, std::nullptr_t> = nullptr> \
  auto parallel_invoke(Fn&& f ITYR_FORLOOP_P_##n(ITYR_FUNC_PARAM_STR), Rest&&... r) { \
    return impl<std::invoke_result_t<Fn ITYR_FORLOOP_P_##n(ITYR_TYPENAME_STR)>>( \
      f, \
      std::make_tuple(ITYR_FORLOOP_C_##n(ITYR_FORWARD_ARG_STR)), \
      std::forward<Rest>(r)... \
    ); \
  }

#define ITYR_PARALLEL_INVOKE_DEF(n, impl) \
  struct empty {}; \
  auto parallel_invoke() { return std::make_tuple(); } \
  ITYR_FORLOOP_##n(ITYR_PARALLEL_INVOKE_DEF_IMPL, impl)

namespace ityr {

template <typename Iterator>
using iterator_diff_t = typename std::iterator_traits<Iterator>::difference_type;

template <typename P, typename P::iro_t::access_mode Mode,
          typename ForwardIterator, typename Fn>
inline void for_each_serial(ForwardIterator first,
                            ForwardIterator last,
                            Fn              f) {
  if constexpr (pcas::is_global_ptr_v<ForwardIterator>) {
    auto d = std::distance(first, last);
    auto first_ = P::iro_t::template checkout<Mode>(first, d);
    for_each_serial<P, Mode>(first_, first_ + d, f);
    P::iro_t::checkin(first_, d);

  } else {
    for (; first != last; ++first) {
      f(*first);
    }
  }
}

template <typename P, typename P::iro_t::access_mode Mode1, typename P::iro_t::access_mode Mode2,
          typename ForwardIterator1, typename ForwardIterator2, typename Fn>
inline void for_each_serial(ForwardIterator1 first1,
                            ForwardIterator1 last1,
                            ForwardIterator2 first2,
                            Fn               f) {
  if constexpr (pcas::is_global_ptr_v<ForwardIterator1>) {
    auto d = std::distance(first1, last1);
    auto first1_ = P::iro_t::template checkout<Mode1>(first1, d);
    for_each_serial<P, Mode1, Mode2>(first1_, first1_ + d, first2, f);
    P::iro_t::checkin(first1_, d);

  } else if constexpr (pcas::is_global_ptr_v<ForwardIterator2>) {
    auto d = std::distance(first1, last1);
    auto first2_ = P::iro_t::template checkout<Mode2>(first2, d);
    for_each_serial<P, Mode1, Mode2>(first1, last1, first2_, f);
    P::iro_t::checkin(first2_, d);

  } else {
    for (; first1 != last1; ++first1, ++first2) {
      f(*first1, *first2);
    }
  }
}

template <typename P, typename P::iro_t::access_mode Mode1, typename P::iro_t::access_mode Mode2, typename P::iro_t::access_mode Mode3,
          typename ForwardIterator1, typename ForwardIterator2, typename ForwardIterator3, typename Fn>
inline void for_each_serial(ForwardIterator1 first1,
                            ForwardIterator1 last1,
                            ForwardIterator2 first2,
                            ForwardIterator3 first3,
                            Fn               f) {
  if constexpr (pcas::is_global_ptr_v<ForwardIterator1>) {
    auto d = std::distance(first1, last1);
    auto first1_ = P::iro_t::template checkout<Mode1>(first1, d);
    for_each_serial<P, Mode1, Mode2, Mode3>(first1_, first1_ + d, first2, first3, f);
    P::iro_t::checkin(first1_, d);

  } else if constexpr (pcas::is_global_ptr_v<ForwardIterator2>) {
    auto d = std::distance(first1, last1);
    auto first2_ = P::iro_t::template checkout<Mode2>(first2, d);
    for_each_serial<P, Mode1, Mode2, Mode3>(first1, last1, first2_, first3, f);
    P::iro_t::checkin(first2_, d);

  } else if constexpr (pcas::is_global_ptr_v<ForwardIterator3>) {
    auto d = std::distance(first1, last1);
    auto first3_ = P::iro_t::template checkout<Mode3>(first3, d);
    for_each_serial<P, Mode1, Mode2, Mode3>(first1, last1, first2, first3_, f);
    P::iro_t::checkin(first3_, d);

  } else {
    for (; first1 != last1; ++first1, ++first2, ++first3) {
      f(*first1, *first2, *first3);
    }
  }
}

template <typename P>
class ito_pattern_if {
  using impl = typename P::template ito_pattern_impl_t<P>;

public:
  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
    return impl::root_spawn(std::forward<Fn>(f), std::forward<Args>(args)...);
  }

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) {
    return impl::parallel_invoke(std::forward<Args>(args)...);
  };

  template <typename ForwardIterator, typename Fn>
  static void parallel_for(ForwardIterator                  first,
                           ForwardIterator                  last,
                           Fn                               f,
                           iterator_diff_t<ForwardIterator> cutoff = {1}) {
    impl::parallel_for(first, last, f, cutoff);
  }

  template <typename ForwardIterator, typename T, typename ReduceOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           iterator_diff_t<ForwardIterator> cutoff = {1}) {
    auto transform = [](typename ForwardIterator::value_type&& v) { return std::forward(v); };
    return impl::parallel_reduce(first, last, init, reduce, transform, cutoff);
  }

  template <typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           TransformOp                      transform,
                           iterator_diff_t<ForwardIterator> cutoff = {1}) {
    return impl::parallel_reduce(first, last, init, reduce, transform, cutoff);
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator                  first,
                                             ForwardIterator                  last,
                                             ForwardIteratorR                 result,
                                             UnaryOp                          unary_op,
                                             iterator_diff_t<ForwardIterator> cutoff = {1}) {
    return impl::parallel_transform(first, last, result, unary_op, cutoff);
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator1                  first1,
                                             ForwardIterator1                  last1,
                                             ForwardIterator2                  first2,
                                             ForwardIteratorR                  result,
                                             BinaryOp                          binary_op,
                                             iterator_diff_t<ForwardIterator1> cutoff = {1}) {
    return impl::parallel_transform(first1, last1, first2, result, binary_op, cutoff);
  }
};

template <typename P>
class ito_pattern_serial {
  using iro = typename P::iro_t;

  struct parallel_invoke_inner_state {
    template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
      if constexpr (std::is_void_v<RetVal>) {
        std::apply(f, args);
        return std::tuple_cat(std::make_tuple(empty{}), parallel_invoke(std::forward<Rest>(r)...));
      } else {
        auto&& ret = std::apply(f, args);
        return std::tuple_cat(std::make_tuple(ret), parallel_invoke(std::forward<Rest>(r)...));
      }
    }

    ITYR_PARALLEL_INVOKE_DEF(8, parallel_invoke_impl)
  };

public:
  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
    return std::forward<Fn>(f)(std::forward<Args>(args)...);
  }

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) {
    parallel_invoke_inner_state s;
    return s.parallel_invoke(std::forward<Args>(args)...);
  }

  template <typename ForwardIterator, typename Fn>
  static void parallel_for(ForwardIterator                  first,
                           ForwardIterator                  last,
                           Fn                               f,
                           iterator_diff_t<ForwardIterator> cutoff [[maybe_unused]]) {
    for_each_serial<P, iro::access_mode::read_write>(first, last, f);
  }

  template <typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           TransformOp                      transform,
                           iterator_diff_t<ForwardIterator> cutoff [[maybe_unused]]) {
    T acc = init;
    for_each_serial<P, iro::access_mode::read>(first, last, [&](const auto& v) {
      acc = reduce(acc, transform(v));
    });
    return acc;
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator                  first,
                                             ForwardIterator                  last,
                                             ForwardIteratorR                 result,
                                             UnaryOp                          unary_op,
                                             iterator_diff_t<ForwardIterator> cutoff) {
    for_each_serial<P, iro::access_mode::read, iro::access_mode::write>(
        first, last, result, [&](const auto& v, auto& r) {
      r = unary_op(v);
    });
    auto d = std::distance(first, last);
    return std::next(result, d);
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator1                  first1,
                                             ForwardIterator1                  last1,
                                             ForwardIterator2                  first2,
                                             ForwardIteratorR                  result,
                                             BinaryOp                          binary_op,
                                             iterator_diff_t<ForwardIterator1> cutoff) {
    for_each_serial<P, iro::access_mode::read, iro::access_mode::read, iro::access_mode::write>(
        first1, last1, first2, result, [&](const auto& v1, const auto& v2, auto& r) {
      r = binary_op(v1, v2);
    });
    auto d = std::distance(first1, last1);
    return std::next(result, d);
  }

};

template <typename P>
class ito_pattern_naive {
  using iro = typename P::iro_t;

  struct parallel_invoke_inner_state {
    template <typename RetVal, typename Fn, typename ArgsTuple>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args) {
      if constexpr (std::is_void_v<RetVal>) {
        iro::acquire();
        std::apply(f, args);
        iro::release();
        return std::make_tuple(empty{});
      } else {
        iro::acquire();
        auto&& r = std::apply(f, args);
        iro::release();
        return std::make_tuple(r);
      }
    };

    template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
      if constexpr (std::is_void_v<RetVal>) {
        madm::uth::thread<void> th{[=] {
          iro::acquire();
          std::apply(f, args);
          iro::release();
        }};
        auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);
        th.join();
        return std::tuple_cat(std::make_tuple(empty{}), ret_rest);
      } else {
        madm::uth::thread<RetVal> th{[=] {
          iro::acquire();
          auto&& r = std::apply(f, args);
          iro::release();
          return r;
        }};
        auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);
        auto&& ret = th.join();
        return std::tuple_cat(std::make_tuple(ret), ret_rest);
      }
    };

    ITYR_PARALLEL_INVOKE_DEF(8, parallel_invoke_impl)
  };

public:
  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
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

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) {
    iro::release();
    parallel_invoke_inner_state s;
    auto ret = s.parallel_invoke(std::forward<Args>(args)...);
    iro::acquire();
    return ret;
  }

  template <typename ForwardIterator, typename Fn>
  static void parallel_for(ForwardIterator                  first,
                           ForwardIterator                  last,
                           Fn                               f,
                           iterator_diff_t<ForwardIterator> cutoff) {
    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read_write>(first, last, f);
    } else {
      auto mid = std::next(first, d / 2);

      iro::release();
      auto th = madm::uth::thread<void>{[=] {
        iro::acquire();
        parallel_for(first, mid, f, cutoff);
        iro::release();
      }};
      iro::acquire();

      parallel_for(mid, last, std::forward<Fn>(f), cutoff);

      iro::release();
      th.join();
      iro::acquire();
    }
  }

  template <typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           TransformOp                      transform,
                           iterator_diff_t<ForwardIterator> cutoff) {
    auto d = std::distance(first, last);
    if (d <= cutoff) {
      T acc = init;
      for_each_serial<P, iro::access_mode::read>(first, last, [&](const auto& v) {
        acc = reduce(acc, transform(v));
      });
      return acc;
    } else {
      auto mid = std::next(first, d / 2);

      iro::release();
      auto th = madm::uth::thread<T>{[=] {
        iro::acquire();
        T ret = parallel_for(first, mid, init, reduce, transform, cutoff);
        iro::release();
        return ret;
      }};
      iro::acquire();

      T acc2 = parallel_for(mid, last, init, reduce, transform, cutoff);

      iro::release();
      T acc1 = th.join();
      iro::acquire();

      return reduce(acc1, acc2);
    }
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator                  first,
                                             ForwardIterator                  last,
                                             ForwardIteratorR                 result,
                                             UnaryOp                          unary_op,
                                             iterator_diff_t<ForwardIterator> cutoff) {
    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::write>(
          first, last, result, [&](const auto& v, auto& r) {
        r = unary_op(v);
      });
    } else {
      auto mid = std::next(first, d / 2);

      iro::release();
      auto th = madm::uth::thread<void>{[=] {
        iro::acquire();
        parallel_transform(first, mid, result, unary_op, cutoff);
        iro::release();
      }};
      iro::acquire();

      auto result_mid = std::next(result, d / 2);
      parallel_transform(mid, last, result_mid, unary_op, cutoff);

      iro::release();
      th.join();
      iro::acquire();
    }
    return std::next(result, d);
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator1                  first1,
                                             ForwardIterator1                  last1,
                                             ForwardIterator2                  first2,
                                             ForwardIteratorR                  result,
                                             BinaryOp                          binary_op,
                                             iterator_diff_t<ForwardIterator1> cutoff) {
    auto d = std::distance(first1, last1);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::read, iro::access_mode::write>(
          first1, last1, first2, result, [&](const auto& v1, const auto& v2, auto& r) {
        r = binary_op(v1, v2);
      });
    } else {
      auto mid1 = std::next(first1, d / 2);

      iro::release();
      auto th = madm::uth::thread<void>{[=] {
        iro::acquire();
        parallel_transform(first1, mid1, first2, result, binary_op, cutoff);
        iro::release();
      }};
      iro::acquire();

      auto mid2 = std::next(first2, d / 2);
      auto result_mid = std::next(result, d / 2);
      parallel_transform(mid1, last1, mid2, result_mid, binary_op, cutoff);

      iro::release();
      th.join();
      iro::acquire();
    }
    return std::next(result, d);
  }

};

template <typename P>
class ito_pattern_workfirst {
  using iro = typename P::iro_t;

  struct parallel_invoke_inner_state {
    bool all_synched = true;
    bool blocked = false;

    template <typename RetVal, typename Fn, typename ArgsTuple>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args) {
      iro::poll();
      if constexpr (std::is_void_v<RetVal>) {
        std::apply(f, args);
        return std::make_tuple(empty{});
      } else {
        auto&& r = std::apply(f, args);
        return std::make_tuple(r);
      }
    };

    template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
      iro::poll();

      auto th = madm::uth::thread<RetVal>{};
      bool synched = th.spawn_aux(f, args,
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire();
      }
      all_synched &= synched;

      auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);

      iro::poll();

      if constexpr (std::is_void_v<RetVal>) {
        th.join_aux(0, [&] {
          // on-block callback
          if (!blocked) {
            iro::release();
            blocked = true;
          }
        });
        return std::tuple_cat(std::make_tuple(empty{}), ret_rest);
      } else {
        auto&& ret = th.join_aux(0, [&] {
          // on-block callback
          if (!blocked) {
            iro::release();
            blocked = true;
          }
        });
        return std::tuple_cat(std::make_tuple(ret), ret_rest);
      }
    };

    ITYR_PARALLEL_INVOKE_DEF(8, parallel_invoke_impl)
  };

  template <typename ForwardIterator, typename Fn>
  static bool parallel_for_impl(ForwardIterator                  first,
                                ForwardIterator                  last,
                                Fn                               f,
                                iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read_write>(first, last, f);
      return true;
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_for_impl<ForwardIterator, Fn>,
        std::make_tuple(first, mid, std::forward<Fn>(f), cutoff),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire();
      }

      synched &= parallel_for_impl(mid, last, std::forward<Fn>(f), cutoff);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

  template <bool TopLevel, typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static std::conditional_t<TopLevel, std::tuple<T, bool>, T>
  parallel_reduce_impl(ForwardIterator                  first,
                       ForwardIterator                  last,
                       T                                init,
                       ReduceOp                         reduce,
                       TransformOp                      transform,
                       iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      T acc = init;
      for_each_serial<P, iro::access_mode::read>(first, last, [&](const auto& v) {
        acc = reduce(acc, transform(v));
      });
      if constexpr (TopLevel) {
        return {acc, true};
      } else {
        return acc;
      }
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<T>{};
      bool synched = th.spawn_aux(
        parallel_reduce_impl<false, ForwardIterator, T, ReduceOp, TransformOp>,
        std::make_tuple(first, mid, init, reduce, transform, cutoff),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire();
      }

      auto ret2 = parallel_reduce_impl<TopLevel>(mid, last, init, reduce, transform, cutoff);

      auto acc1 = th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      if constexpr (TopLevel) {
        auto [acc2, synched2] = ret2;
        return {reduce(acc1, acc2), synched & synched2};
      } else {
        T acc2 = ret2;
        return reduce(acc1, acc2);
      }
    }
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static bool parallel_transform_impl(ForwardIterator                  first,
                                      ForwardIterator                  last,
                                      ForwardIteratorR                 result,
                                      UnaryOp                          unary_op,
                                      iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::write>(
          first, last, result, [&](const auto& v, auto& r) {
        r = unary_op(v);
      });
      return true;
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_transform_impl<ForwardIterator, ForwardIteratorR, UnaryOp>,
        std::make_tuple(first, mid, result, unary_op, cutoff),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire();
      }

      auto result_mid = std::next(result, d / 2);
      synched &= parallel_transform_impl(mid, last, result_mid, unary_op, cutoff);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static bool parallel_transform_impl(ForwardIterator1                  first1,
                                      ForwardIterator1                  last1,
                                      ForwardIterator2                  first2,
                                      ForwardIteratorR                  result,
                                      BinaryOp                          binary_op,
                                      iterator_diff_t<ForwardIterator1> cutoff) {
    iro::poll();

    auto d = std::distance(first1, last1);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::read, iro::access_mode::write>(
          first1, last1, first2, result, [&](const auto& v1, const auto& v2, auto& r) {
        r = binary_op(v1, v2);
      });
      return true;
    } else {
      auto mid1 = std::next(first1, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_transform_impl<ForwardIterator1, ForwardIterator2, ForwardIteratorR, BinaryOp>,
        std::make_tuple(first1, mid1, first2, result, binary_op, cutoff),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire();
      }

      auto mid2 = std::next(first2, d / 2);
      auto result_mid = std::next(result, d / 2);
      synched &= parallel_transform_impl(mid1, last1, mid2, result_mid, binary_op, cutoff);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

public:
  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
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

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) {
    iro::poll();

    auto initial_rank = P::rank();
    iro::release();
    parallel_invoke_inner_state s;
    auto ret = s.parallel_invoke(std::forward<Args>(args)...);
    if (initial_rank != P::rank() || !s.all_synched) {
      iro::acquire();
    }

    iro::poll();
    return ret;
  }

  template <typename ForwardIterator, typename Fn>
  static void parallel_for(ForwardIterator                  first,
                           ForwardIterator                  last,
                           Fn                               f,
                           iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    iro::release();
    bool synched = parallel_for_impl(first, last, std::forward<Fn>(f), cutoff);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();
  }

  template <typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           TransformOp                      transform,
                           iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    iro::release();
    auto [ret, synched] = parallel_reduce_impl<true>(first, last, init, reduce, transform, cutoff);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    return ret;
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator                  first,
                                             ForwardIterator                  last,
                                             ForwardIteratorR                 result,
                                             UnaryOp                          unary_op,
                                             iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    iro::release();
    bool synched = parallel_transform_impl(first, last, result, unary_op, cutoff);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    auto d = std::distance(first, last);
    return std::next(result, d);
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator1                  first1,
                                             ForwardIterator1                  last1,
                                             ForwardIterator2                  first2,
                                             ForwardIteratorR                  result,
                                             BinaryOp                          binary_op,
                                             iterator_diff_t<ForwardIterator1> cutoff) {
    iro::poll();

    iro::release();
    bool synched = parallel_transform_impl(first1, last1, first2, result, binary_op, cutoff);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    auto d = std::distance(first1, last1);
    return std::next(result, d);
  }

};

template <typename P>
class ito_pattern_workfirst_lazy {
  using iro = typename P::iro_t;

  struct parallel_invoke_inner_state {
    typename iro::release_handler rh;
    bool all_synched = true;
    bool blocked = false;

    template <typename RetVal, typename Fn, typename ArgsTuple>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args) {
      iro::poll();
      if constexpr (std::is_void_v<RetVal>) {
        std::apply(f, args);
        return std::make_tuple(empty{});
      } else {
        auto&& r = std::apply(f, args);
        return std::make_tuple(r);
      }
    };

    template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
    auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
      iro::poll();

      auto th = madm::uth::thread<RetVal>{};
      bool synched = th.spawn_aux(f, args,
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire(rh);
      }
      all_synched &= synched;

      auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);

      iro::poll();

      if constexpr (std::is_void_v<RetVal>) {
        th.join_aux(0, [&] {
          // on-block callback
          if (!blocked) {
            iro::release();
            blocked = true;
          }
        });
        return std::tuple_cat(std::make_tuple(empty{}), ret_rest);
      } else {
        auto&& ret = th.join_aux(0, [&] {
          // on-block callback
          if (!blocked) {
            iro::release();
            blocked = true;
          }
        });
        return std::tuple_cat(std::make_tuple(ret), ret_rest);
      }
    };

    ITYR_PARALLEL_INVOKE_DEF(8, parallel_invoke_impl)
  };

  template <typename ForwardIterator, typename Fn>
  static bool parallel_for_impl(ForwardIterator                  first,
                                ForwardIterator                  last,
                                Fn                               f,
                                iterator_diff_t<ForwardIterator> cutoff,
                                typename iro::release_handler    rh) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read_write>(first, last, f);
      return true;
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_for_impl<ForwardIterator, Fn>,
        std::make_tuple(first, mid, f, cutoff, rh),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire(rh);
      }

      synched &= parallel_for_impl(mid, last, f, cutoff, rh);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

  template <bool TopLevel, typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static std::conditional_t<TopLevel, std::tuple<T, bool>, T>
  parallel_reduce_impl(ForwardIterator                  first,
                       ForwardIterator                  last,
                       T                                init,
                       ReduceOp                         reduce,
                       TransformOp                      transform,
                       iterator_diff_t<ForwardIterator> cutoff,
                       typename iro::release_handler    rh) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      T acc = init;
      for_each_serial<P, iro::access_mode::read>(first, last, [&](const auto& v) {
        acc = reduce(acc, transform(v));
      });
      if constexpr (TopLevel) {
        return {acc, true};
      } else {
        return acc;
      }
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<T>{};
      bool synched = th.spawn_aux(
        parallel_reduce_impl<false, ForwardIterator, T, ReduceOp, TransformOp>,
        std::make_tuple(first, mid, init, reduce, transform, cutoff, rh),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire(rh);
      }

      auto ret2 = parallel_reduce_impl<TopLevel>(mid, last, init, reduce, transform, cutoff, rh);

      auto acc1 = th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      if constexpr (TopLevel) {
        auto [acc2, synched2] = ret2;
        return {reduce(acc1, acc2), synched & synched2};
      } else {
        T acc2 = ret2;
        return reduce(acc1, acc2);
      }
    }
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static bool parallel_transform_impl(ForwardIterator                  first,
                                      ForwardIterator                  last,
                                      ForwardIteratorR                 result,
                                      UnaryOp                          unary_op,
                                      iterator_diff_t<ForwardIterator> cutoff,
                                      typename iro::release_handler    rh) {
    iro::poll();

    auto d = std::distance(first, last);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::write>(
          first, last, result, [&](const auto& v, auto& r) {
        r = unary_op(v);
      });
      return true;
    } else {
      auto mid = std::next(first, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_transform_impl<ForwardIterator, ForwardIteratorR, UnaryOp>,
        std::make_tuple(first, mid, result, unary_op, cutoff, rh),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire(rh);
      }

      auto result_mid = std::next(result, d / 2);
      synched &= parallel_transform_impl(mid, last, result_mid, unary_op, cutoff, rh);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static bool parallel_transform_impl(ForwardIterator1                  first1,
                                      ForwardIterator1                  last1,
                                      ForwardIterator2                  first2,
                                      ForwardIteratorR                  result,
                                      BinaryOp                          binary_op,
                                      iterator_diff_t<ForwardIterator1> cutoff,
                                      typename iro::release_handler     rh) {
    iro::poll();

    auto d = std::distance(first1, last1);
    if (d <= cutoff) {
      for_each_serial<P, iro::access_mode::read, iro::access_mode::read, iro::access_mode::write>(
          first1, last1, first2, result, [&](const auto& v1, const auto& v2, auto& r) {
        r = binary_op(v1, v2);
      });
      return true;
    } else {
      auto mid1 = std::next(first1, d / 2);

      auto th = madm::uth::thread<void>{};
      bool synched = th.spawn_aux(
        parallel_transform_impl<ForwardIterator1, ForwardIterator2, ForwardIteratorR, BinaryOp>,
        std::make_tuple(first1, mid1, first2, result, binary_op, cutoff, rh),
        [=] (bool parent_popped) {
          // on-die callback
          if (!parent_popped) {
            iro::release();
          }
        }
      );
      if (!synched) {
        iro::acquire(rh);
      }

      auto mid2 = std::next(first2, d / 2);
      auto result_mid = std::next(result, d / 2);
      synched &= parallel_transform_impl(mid1, last1, mid2, result_mid, binary_op, cutoff, rh);

      th.join_aux(0, [&] {
        // on-block callback
        iro::release();
      });

      return synched;
    }
  }

public:
  template <typename Fn, typename... Args>
  static auto root_spawn(Fn&& f, Args&&... args) {
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

  template <typename... Args>
  static auto parallel_invoke(Args&&... args) {
    iro::poll();

    auto initial_rank = P::rank();
    parallel_invoke_inner_state s;
    iro::release_lazy(&s.rh);
    auto ret = s.parallel_invoke(std::forward<Args>(args)...);
    if (initial_rank != P::rank() || !s.all_synched) {
      iro::acquire();
    }

    iro::poll();
    return ret;
  }

  template <typename ForwardIterator, typename Fn>
  static void parallel_for(ForwardIterator                  first,
                           ForwardIterator                  last,
                           Fn                               f,
                           iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    typename iro::release_handler rh;
    iro::release_lazy(&rh);
    bool synched = parallel_for_impl(first, last, std::forward<Fn>(f), cutoff, rh);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();
  }

  template <typename ForwardIterator, typename T, typename ReduceOp, typename TransformOp>
  static T parallel_reduce(ForwardIterator                  first,
                           ForwardIterator                  last,
                           T                                init,
                           ReduceOp                         reduce,
                           TransformOp                      transform,
                           iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    typename iro::release_handler rh;
    iro::release_lazy(&rh);
    auto [ret, synched] = parallel_reduce_impl<true>(first, last, init, reduce, transform, cutoff, rh);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    return ret;
  }

  template <typename ForwardIterator, typename ForwardIteratorR, class UnaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator                  first,
                                             ForwardIterator                  last,
                                             ForwardIteratorR                 result,
                                             UnaryOp                          unary_op,
                                             iterator_diff_t<ForwardIterator> cutoff) {
    iro::poll();

    typename iro::release_handler rh;
    iro::release_lazy(&rh);
    bool synched = parallel_transform_impl(first, last, result, unary_op, cutoff, rh);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    auto d = std::distance(first, last);
    return std::next(result, d);
  }

  template <typename ForwardIterator1, typename ForwardIterator2, typename ForwardIteratorR, class BinaryOp>
  static ForwardIteratorR parallel_transform(ForwardIterator1                  first1,
                                             ForwardIterator1                  last1,
                                             ForwardIterator2                  first2,
                                             ForwardIteratorR                  result,
                                             BinaryOp                          binary_op,
                                             iterator_diff_t<ForwardIterator1> cutoff) {
    iro::poll();

    typename iro::release_handler rh;
    iro::release_lazy(&rh);
    bool synched = parallel_transform_impl(first1, last1, first2, result, binary_op, cutoff, rh);
    if (!synched) {
      iro::acquire();
    }

    iro::poll();

    auto d = std::distance(first1, last1);
    return std::next(result, d);
  }

};

struct ito_pattern_policy_default {
  template <typename P_>
  using ito_pattern_impl_t = ito_pattern_serial<P_>;
  using iro_t = iro_if<iro_policy_default>;
  static uint64_t rank() { return 0; }
  static uint64_t n_ranks() { return 1; }
};

}
