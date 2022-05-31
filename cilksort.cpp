/*
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Matteo Frigo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * this program uses an algorithm that we call `cilksort'.
 * The algorithm is essentially mergesort:
 *
 *   cilksort(in[1..n]) =
 *       spawn cilksort(in[1..n/2], tmp[1..n/2])
 *       spawn cilksort(in[n/2..n], tmp[n/2..n])
 *       sync
 *       spawn cilkmerge(tmp[1..n/2], tmp[n/2..n], in[1..n])
 *
 *
 * The procedure cilkmerge does the following:
 *
 *       cilkmerge(A[1..n], B[1..m], C[1..(n+m)]) =
 *          find the median of A \union B using binary
 *          search.  The binary search gives a pair
 *          (ma, mb) such that ma + mb = (n + m)/2
 *          and all elements in A[1..ma] are smaller than
 *          B[mb..m], and all the B[1..mb] are smaller
 *          than all elements in A[ma..n].
 *
 *          spawn cilkmerge(A[1..ma], B[1..mb], C[1..(n+m)/2])
 *          spawn cilkmerge(A[ma..m], B[mb..n], C[(n+m)/2 .. (n+m)])
 *          sync
 *
 * The algorithm appears for the first time (AFAIK) in S. G. Akl and
 * N. Santoro, "Optimal Parallel Merging and Sorting Without Memory
 * Conflicts", IEEE Trans. Comp., Vol. C-36 No. 11, Nov. 1987 .  The
 * paper does not express the algorithm using recursion, but the
 * idea of finding the median is there.
 *
 * For cilksort of n elements, T_1 = O(n log n) and
 * T_\infty = O(log^3 n).  There is a way to shave a
 * log factor in the critical path (left as homework).
 */

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <random>

#include "uth.h"
#include "pcas/pcas.hpp"

#ifndef FIXED_SEED
# define FIXED_SEED 1
#endif

template <typename T>
class raw_span {
  using this_t = raw_span<T>;
  T* ptr_;
  size_t n_;
public:
  using element_type = T;

  raw_span(T* ptr, size_t n) : ptr_(ptr), n_(n) {}

  constexpr size_t size() const noexcept { return n_; }

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
  void for_each(F f) {
    for (size_t i = 0; i < n_; i++) {
      f(ptr_[i]);
    }
  }

  template <typename Acc, typename F>
  Acc reduce(Acc init, F f) {
    Acc acc = init;
    for (size_t i = 0; i < n_; i++) {
      acc = f(acc, ptr_[i]);
    }
    return acc;
  }

  template <pcas::access_mode Mode>
  raw_span<T> checkout() const { return this; }

  void checkin(raw_span<T> s) const {}

  friend void copy(this_t dest, this_t src) {
    assert(dest.n_ == src.n_);
    std::memcpy(dest.ptr_, src.ptr_, sizeof(T) * dest.n_);
  }
};

pcas::pcas* g_pc = NULL;

template <typename T, size_t BlockSize = 65536>
class gptr_span {
  using this_t = gptr_span<T, BlockSize>;
  using ptr_t = pcas::global_ptr<T>;
  ptr_t ptr_;
  size_t n_;
public:
  using element_type = T;

  gptr_span(ptr_t ptr, size_t n) : ptr_(ptr), n_(n) {}

  constexpr size_t size() const noexcept { return n_; }

  constexpr T operator[](size_t i) const {
    assert(i < n_);
    auto p = g_pc->checkout<pcas::access_mode::read>(ptr_ + i, 1);
    T ret = *p;
    g_pc->checkin(p);
    return ret;
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

  template <typename F>
  void for_each(F f) {
    for (size_t i = 0; i < n_; i += BlockSize) {
      size_t b = std::min(n_ - i, BlockSize);
      auto p = g_pc->checkout<pcas::access_mode::read_write>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(p[j]);
      }
      g_pc->checkin(p);
    }
  }

  template <typename Acc, typename F>
  Acc reduce(Acc init, F f) {
    Acc acc = init;
    for (size_t i = 0; i < n_; i += BlockSize) {
      size_t b = std::min(n_ - i, BlockSize);
      auto p = g_pc->checkout<pcas::access_mode::read>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        acc = f(acc, p[j]);
      }
      g_pc->checkin(p);
    }
    return acc;
  }

  template <pcas::access_mode Mode>
  raw_span<T> checkout() const {
    auto p = g_pc->checkout<Mode>(ptr_, n_);
    return {const_cast<T*>(p), n_}; // TODO: reconsider const type handling
  }

  void checkin(raw_span<T> s) const {
    g_pc->checkin(&s[0]);
  }

  friend void copy(this_t dest, this_t src) {
    assert(dest.n_ == src.n_);
    for (size_t i = 0; i < dest.n_; i += BlockSize) {
      size_t b = std::min(dest.n_ - i, BlockSize);
      auto d = g_pc->checkout<pcas::access_mode::write>(dest.ptr_ + i, b);
      auto s = g_pc->checkout<pcas::access_mode::read >(src.ptr_  + i, b);
      std::memcpy(d, s, sizeof(T) * b);
      g_pc->checkin(d);
      g_pc->checkin(s);
    }
  }
};

using elem_t = float;

int my_rank = -1;
int n_procs = -1;

size_t n_input       = 1024;
int    n_repeats     = 10;
int    enable_check  = 1;
int    serial_exec   = 0;
size_t cutoff_insert = 64;
size_t cutoff_merge  = 16 * 1024;
size_t cutoff_quick  = 16 * 1024;

static inline elem_t my_random() {
#if FIXED_SEED
  static std::mt19937 engine(0);
#else
  static std::mt19937 engine(std::random_device{}());
#endif
  static std::uniform_real_distribution<elem_t> dist(0.0, 1.0);
  return dist(engine);
}

template <typename Span>
static inline elem_t select_pivot(Span s) {
  // median of three values
  if (s.size() < 3) return s[0];
  elem_t a = s[0];
  elem_t b = s[1];
  elem_t c = s[2];
  if ((a > b) != (a > c))      return a;
  else if ((b > a) != (b > c)) return b;
  else                         return c;
}

template <typename Span>
void insertion_sort(Span s) {
  for (size_t i = 1; i < s.size(); i++) {
    elem_t a = s[i];
    size_t j;
    for (j = 1; j <= i && s[i - j] > a; j++) {
      s[i - j + 1] = s[i - j];
    }
    s[i - j + 1] = a;
  }
}

template <typename Span>
std::pair<Span, Span> partition_seq(Span s, elem_t pivot) {
  size_t l = 0;
  size_t h = s.size() - 1;
  while (true) {
    while (s[l] < pivot) l++;
    while (s[h] > pivot) h--;
    if (l >= h) break;
    std::swap(s[l++], s[h--]);
  }
  return s.divide(h + 1);
}

template <typename Span>
void quicksort_seq(Span s) {
  if (s.size() <= cutoff_insert) {
    insertion_sort(s);
  } else {
    elem_t pivot = select_pivot(s);
    auto [s1, s2] = partition_seq(s, pivot);
    quicksort_seq(s1);
    quicksort_seq(s2);
  }
}

template <typename Span>
void merge_seq(const Span s1, const Span s2, Span dest) {
  assert(s1.size() + s2.size() == dest.size());

  size_t d = 0;
  size_t l1 = 0;
  size_t l2 = 0;
  elem_t a1 = s1[0];
  elem_t a2 = s2[0];
  while (true) {
    if (a1 < a2) {
      dest[d++] = a1;
      l1++;
      if (l1 >= s1.size()) break;
      a1 = s1[l1];
    } else {
      dest[d++] = a2;
      l2++;
      if (l2 >= s2.size()) break;
      a2 = s2[l2];
    }
  }
  if (l1 >= s1.size()) {
    Span dest_r = dest.subspan(d, s2.size() - l2);
    Span src_r  = s2.subspan(l2, s2.size() - l2);
    copy(dest_r, src_r);
  } else {
    Span dest_r = dest.subspan(d, s1.size() - l1);
    Span src_r  = s1.subspan(l1, s1.size() - l1);
    copy(dest_r, src_r);
  }
}

template <typename Span>
elem_t binary_search(Span s, const typename Span::element_type& v) {
  size_t l = 0;
  size_t h = s.size();
  while (l < h) {
    size_t m = l + (h - l) / 2;
    if (v <= s[m]) h = m;
    else           l = m + 1;
  }
  if (v < s[l]) return l;
  else          return l + 1;
}

template <typename Span>
void cilkmerge(Span s1, Span s2, Span dest) {
  assert(s1.size() + s2.size() == dest.size());

  if (s1.size() < s2.size()) {
    // s2 is always smaller
    std::swap(s1, s2);
  }
  if (s2.size() == 0) {
    copy(dest, s1);
    return;
  }
  if (s1.size() < cutoff_merge) {
    auto s1_ = s1.template checkout<pcas::access_mode::read>();
    auto s2_ = s2.template checkout<pcas::access_mode::read>();
    auto dest_ = dest.template checkout<pcas::access_mode::write>();

    merge_seq(s1_, s2_, dest_);

    s1.checkin(s1_);
    s2.checkin(s2_);
    dest.checkin(dest_);
    return;
  }

  size_t split1 = s1.size() / 2;
  size_t split2 = binary_search(s2, s1[split1]);

  auto [s11  , s12  ] = s1.divide(split1);
  auto [s21  , s22  ] = s2.divide(split2);
  auto [dest1, dest2] = dest.divide(split1 + split2);

  cilkmerge(s11, s21, dest1);
  cilkmerge(s12, s22, dest2);
}

template <typename Span>
void cilksort(Span a, Span b) {
  assert(a.size() == b.size());

  if (a.size() < cutoff_quick) {
    auto a_ = a.template checkout<pcas::access_mode::read_write>();

    quicksort_seq(a_);

    a.checkin(a_);
    return;
  }

  auto [a12, a34] = a.divide_two();
  auto [b12, b34] = b.divide_two();

  auto [a1, a2] = a12.divide_two();
  auto [a3, a4] = a34.divide_two();
  auto [b1, b2] = b12.divide_two();
  auto [b3, b4] = b34.divide_two();

  cilksort(a1, b1);
  cilksort(a2, b2);
  cilksort(a3, b3);
  cilksort(a4, b4);

  cilkmerge(a1, a2, b12);
  cilkmerge(a3, a4, b34);

  cilkmerge(b12, b34, a);
}

template <typename Span>
void init_array(Span s) {
  s.for_each([=](typename Span::element_type& e) {
    e = my_random();
  });
}

template <typename Span>
bool check_sorted(Span s) {
  using acc_type = std::pair<bool, typename Span::element_type>;
  acc_type init{true, s[0]};
  auto ret = s.reduce(init, [=](auto acc, const typename Span::element_type& e) {
    auto prev_e = acc.second;
    if (prev_e > e) return acc_type{false    , e};
    else            return acc_type{acc.first, e};
  });
  return ret.first;
}

void show_help_and_exit(int argc, char** argv) {
  if (my_rank == 0) {
    printf("Usage: %s [options]\n"
           "  options:\n"
           "    -n : Input size (size_t)\n"
           "    -r : # of repeats (int)\n"
           "    -c : check the result (int)\n"
           "    -s : serial execution (int)\n"
           "    -i : cutoff for insertion sort (size_t)\n"
           "    -m : cutoff for serial merge (size_t)\n"
           "    -q : cutoff for serial quicksort (size_t)\n", argv[0]);
  }
  exit(1);
}

int real_main(int argc, char **argv) {
  my_rank = madm::uth::get_pid();
  n_procs = madm::uth::get_n_procs();

  int opt;
  while ((opt = getopt(argc, argv, "n:r:c:s:i:m:q:h")) != EOF) {
    switch (opt) {
      case 'n':
        n_input = atoll(optarg);
        break;
      case 'r':
        n_repeats = atoi(optarg);
        break;
      case 'c':
        enable_check = atoi(optarg);
        break;
      case 's':
        serial_exec = atoi(optarg);
        break;
      case 'i':
        cutoff_insert = atoll(optarg);
        break;
      case 'm':
        cutoff_merge = atoll(optarg);
        break;
      case 'q':
        cutoff_quick = atoll(optarg);
        break;
      case 'h':
      default:
        show_help_and_exit(argc, argv);
    }
  }

  if (my_rank == 0) {
    setlocale(LC_NUMERIC, "en_US.UTF-8");
    printf("=============================================================\n"
           "[Cliksort]\n"
           "# of processes:                %d\n"
           "N (Input size):                %ld\n"
           "# of repeats:                  %d\n"
           "Check enabled:                 %d\n"
           "Serial execution:              %d\n"
           "Cutoff (insertion sort):       %ld\n"
           "Cutoff (merge):                %ld\n"
           "Cutoff (quicksort):            %ld\n"
           "-------------------------------------------------------------\n",
           n_procs, n_input, n_repeats, enable_check, serial_exec,
           cutoff_insert, cutoff_merge, cutoff_quick);
    printf("uth options:\n");
    madm::uth::print_options(stdout);
    printf("=============================================================\n\n");
    fflush(stdout);
  }

  constexpr uint64_t cache_size = 16 * 1024 * 1024;
  g_pc = new pcas::pcas(cache_size);

  auto array = g_pc->malloc<elem_t>(n_input);
  auto buf   = g_pc->malloc<elem_t>(n_input);

  gptr_span<elem_t> a(array, n_input);
  gptr_span<elem_t> b(buf  , n_input);

  /* elem_t* array = (elem_t*)malloc(sizeof(elem_t) * n_input); */
  /* elem_t* buf   = (elem_t*)malloc(sizeof(elem_t) * n_input); */
  /* raw_span<elem_t> a(array, n_input); */
  /* raw_span<elem_t> b(buf  , n_input); */

  for (int r = 0; r < n_repeats; r++) {
    if (my_rank == 0) {
      init_array(a);
    }

    uint64_t t0 = madi::global_clock::get_time();

    if (my_rank == 0) {
      madm::uth::thread<void> th([=]() { cilksort(a, b); });
      th.join();
      /* std::sort(array, array + n_input); */
    }

    uint64_t t1 = madi::global_clock::get_time();

    if (my_rank == 0) {
      printf("[%d] %ld ns\n", r, t1 - t0);
      if (enable_check) {
        if (!check_sorted(a)) {
          printf("check failed.\n");
        }
      }
    }

    madm::uth::barrier();
  }

  g_pc->free(array);
  g_pc->free(buf);

  delete g_pc;

  /* free(array); */
  /* free(buf); */

  return 0;
}

int main(int argc, char** argv) {
  madm::uth::start(real_main, argc, argv);
  return 0;
}
