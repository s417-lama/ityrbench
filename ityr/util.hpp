#pragma once

#include <iostream>
#include <sstream>
#include <signal.h>
#include <dlfcn.h>

#define BACKWARD_HAS_BFD 1
#define BACKWARD_HAS_LIBUNWIND 1
#include "backward.hpp"

namespace ityr {

template <typename T> constexpr inline const char* typename_str()         { return "unknown"; }
template <>           constexpr inline const char* typename_str<char>()   { return "char";    }
template <>           constexpr inline const char* typename_str<short>()  { return "short";   }
template <>           constexpr inline const char* typename_str<int>()    { return "int";     }
template <>           constexpr inline const char* typename_str<long>()   { return "long";    }
template <>           constexpr inline const char* typename_str<float>()  { return "float";   }
template <>           constexpr inline const char* typename_str<double>() { return "double";  }

template <typename T>
inline T get_env_(const char* env_var, T default_val) {
  if (const char* val_str = std::getenv(env_var)) {
    T val;
    std::stringstream ss(val_str);
    ss >> val;
    if (ss.fail()) {
      fprintf(stderr, "Environment variable '%s' is invalid.\n", env_var);
      exit(1);
    }
    return val;
  } else {
    return default_val;
  }
}

template <typename T>
inline T get_env(const char* env_var, T default_val, int rank) {
  static bool print_env = get_env_("ITYR_PRINT_ENV", false);

  T val = get_env_(env_var, default_val);
  if (print_env && rank == 0) {
    std::cout << env_var << " = " << val << std::endl;
  }
  return val;
}

inline void print_backtrace() {
  backward::StackTrace st;
  st.load_here(32);
  backward::Printer p;
  p.object = true;
  p.color_mode = backward::ColorMode::always;
  p.address = true;
  p.print(st, stdout);
}

inline void signal_handler(int sig) {
  printf("Signal %d received.\n", sig);
  print_backtrace();
  exit(1);
}

inline void set_signal_handler(int sig) {
  struct sigaction sa;
  sa.sa_flags   = 0;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(sig, &sa, NULL) == -1) {
    perror("sigacton");
    exit(1);
  }
}

inline void set_signal_handlers() {
  set_signal_handler(SIGSEGV);
  set_signal_handler(SIGABRT);
  set_signal_handler(SIGBUS);
  set_signal_handler(SIGTERM);
}

template <typename T, typename = void>
struct is_const_iterator : public std::false_type {};

template <typename T>
struct is_const_iterator<T, std::enable_if_t<std::is_const_v<std::remove_pointer_t<typename std::iterator_traits<T>::pointer>>>> : public std::true_type {};

template <typename T>
constexpr inline bool is_const_iterator_v = is_const_iterator<T>::value;

static_assert(!is_const_iterator_v<std::vector<int>::iterator>);
static_assert(is_const_iterator_v<std::vector<int>::const_iterator>);

}
