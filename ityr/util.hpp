#pragma once

#include <iostream>
#include <sstream>
#include <signal.h>
#include <dlfcn.h>

#define UNW_LOCAL_ONLY
#include "libunwind.h"

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
  unw_cursor_t cursor;
  unw_context_t context;
  unw_word_t offset;
  unw_proc_info_t pinfo;
  Dl_info dli;
  char sname[256];
  void *addr;
  int count = 0;
  char *buf;
  size_t buf_size;
  FILE* mstream = open_memstream(&buf, &buf_size);

  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  fprintf(mstream, "Backtrace:\n");
  while (unw_step(&cursor) > 0) {
    unw_get_proc_info(&cursor, &pinfo);
    unw_get_proc_name(&cursor, sname, sizeof(sname), &offset);
    addr = (char *)pinfo.start_ip + offset;
    dladdr(addr, &dli);
    fprintf(mstream, "  #%d %p in %s + 0x%lx from %s\n",
            count, addr, sname, offset, dli.dli_fname);
    count++;
  }
  fprintf(mstream, "\n");
  fflush(mstream);

  fwrite(buf, sizeof(char), buf_size, stdout);

  fclose(mstream);
  free(buf);
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
