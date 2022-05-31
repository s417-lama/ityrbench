#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <chrono>

#include "uth.h"

class measure_time {
  std::chrono::time_point<std::chrono::system_clock> t_start_;
public:
  measure_time() {
    t_start_ = std::chrono::system_clock::now();
  }
  ~measure_time() {
    std::chrono::nanoseconds d = std::chrono::system_clock::now() - t_start_;
    std::cout << d.count() << " ns" << std::endl;
  }
};

int fib(int n) {
  if (n < 2) {
    return n;
  } else {
    madm::uth::thread<int> th([=] { return fib(n - 1); });
    int y = fib(n - 2);
    int x = th.join();
    return x + y;
  }
}

int real_main(int argc, char** argv) {
  int my_rank = madm::uth::get_pid();
  int n_procs = madm::uth::get_n_procs();
  int n = (argc > 1 ? atoi(argv[1]) : 35);

  madm::uth::barrier();

  if (my_rank == 0) {
    measure_time mt;
    madm::uth::thread<int> th(fib, n);
    int x = th.join();
  }

  madm::uth::barrier();

  return 0;
}

int main(int argc, char** argv) {
  madm::uth::start(real_main, argc, argv);
  return 0;
}
