#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "uth.h"

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
    uint64_t t0 = madi::global_clock::get_time();
    madm::uth::thread<int> th(fib, n);
    int x = th.join();
    uint64_t t1 = madi::global_clock::get_time();
    std::cout << t1 - t0 << " ns" << std::endl;
  }

  madm::uth::barrier();

  return 0;
}

int main(int argc, char** argv) {
  madm::uth::start(real_main, argc, argv);
  return 0;
}
