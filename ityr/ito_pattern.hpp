#pragma once

#include <tuple>
#include <functional>

#include "uth.h"

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
  static inline auto parallel_invoke(Fn&& f ITYR_FORLOOP_P_##n(ITYR_FUNC_PARAM_STR), Rest&&... r) { \
    return impl<std::invoke_result_t<Fn ITYR_FORLOOP_P_##n(ITYR_TYPENAME_STR)>>( \
      f, \
      std::make_tuple(ITYR_FORLOOP_C_##n(ITYR_FORWARD_ARG_STR)), \
      std::forward<Rest>(r)... \
    ); \
  }

#define ITYR_PARALLEL_INVOKE_DEF(n, impl) \
  struct empty {}; \
  static inline auto parallel_invoke() { return std::make_tuple(); } \
  ITYR_FORLOOP_##n(ITYR_PARALLEL_INVOKE_DEF_IMPL, impl)

namespace ityr {

template <typename P>
struct ito_pattern_serial {

  template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
  static inline auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
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

template <typename P>
struct ito_pattern_naive {
  using iro = typename P::template iro_t<P>;

  template <typename RetVal, typename Fn, typename ArgsTuple>
  static inline auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args) {
    if constexpr (std::is_void_v<RetVal>) {
      std::apply(f, args);
      return std::make_tuple(empty{});
    } else {
      auto&& r = std::apply(f, args);
      return std::make_tuple(r);
    }
  };

  template <typename RetVal, typename Fn, typename ArgsTuple, typename... Rest>
  static inline auto parallel_invoke_impl(Fn&& f, ArgsTuple&& args, Rest... r) {
    if constexpr (std::is_void_v<RetVal>) {
      iro::release();
      madm::uth::thread<void> th{[=] {
        iro::acquire();
        std::apply(f, args);
        iro::release();
      }};
      iro::acquire();
      auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);
      iro::release();
      th.join();
      iro::acquire();
      return std::tuple_cat(std::make_tuple(empty{}), ret_rest);
    } else {
      iro::release();
      madm::uth::thread<RetVal> th{[=] {
        iro::acquire();
        auto&& r = std::apply(f, args);
        iro::release();
        return r;
      }};
      iro::acquire();
      auto&& ret_rest = parallel_invoke(std::forward<Rest>(r)...);
      iro::release();
      auto&& ret = th.join();
      iro::acquire();
      return std::tuple_cat(std::make_tuple(ret), ret_rest);
    }
  };

  ITYR_PARALLEL_INVOKE_DEF(8, parallel_invoke_impl)

};

}
