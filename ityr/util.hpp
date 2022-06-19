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
  p.print(st, stderr);
}

inline void segv_handler(int sig) {
  print_backtrace();
  exit(1);
}

inline void set_segv_handler() {
  struct sigaction sa;
  sa.sa_flags   = 0;
  sa.sa_handler = segv_handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    printf("sigacton for SIGSEGV failed.\n");
    exit(1);
  }
}

}
