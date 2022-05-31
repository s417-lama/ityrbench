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

using elem_t = float;
using gptr_t = pcas::global_ptr<elem_t>;

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

static inline elem_t select_pivot(elem_t* array, size_t n) {
  // median of three values
  if (n < 3) return array[0];
  elem_t a = array[0];
  elem_t b = array[1];
  elem_t c = array[2];
  if ((a > b) != (a > c))      return a;
  else if ((b > a) != (b > c)) return b;
  else                         return c;
}

void insertion_sort(elem_t* array, size_t n) {
  for (size_t i = 1; i < n; i++) {
    elem_t a = array[i];
    size_t j;
    for (j = 1; j <= i && array[i - j] > a; j++) {
      array[i - j + 1] = array[i - j];
    }
    array[i - j + 1] = a;
  }
}

size_t partition_seq(elem_t* array, size_t n, elem_t pivot) {
  size_t l = 0;
  size_t h = n - 1;
  while (true) {
    while (array[l] < pivot) l++;
    while (array[h] > pivot) h--;
    if (l >= h) break;
    std::swap(array[l++], array[h--]);
  }
  return h + 1;
}

void quicksort_seq(elem_t* array, size_t n) {
  if (n <= cutoff_insert) {
    insertion_sort(array, n);
  } else {
    elem_t pivot = select_pivot(array, n);
    size_t l_size = partition_seq(array, n, pivot);
    quicksort_seq(array         , l_size    );
    quicksort_seq(array + l_size, n - l_size);
  }
}

void merge_seq(elem_t* array1, size_t n1,
               elem_t* array2, size_t n2,
               elem_t* array_dest) {
  size_t d = 0;
  size_t l1 = 0;
  size_t l2 = 0;
  elem_t a1 = array1[0];
  elem_t a2 = array2[0];
  while (true) {
    if (a1 < a2) {
      array_dest[d++] = a1;
      l1++;
      if (l1 >= n1) break;
      a1 = array1[l1];
    } else {
      array_dest[d++] = a2;
      l2++;
      if (l2 >= n2) break;
      a2 = array2[l2];
    }
  }
  if (l1 >= n1) {
    std::memcpy(&array_dest[d], &array2[l2], sizeof(elem_t) * (n2 - l2));
  } else {
    std::memcpy(&array_dest[d], &array1[l1], sizeof(elem_t) * (n1 - l1));
  }
}

elem_t binary_search(elem_t* array, size_t n, elem_t value) {
  size_t l = 0;
  size_t h = n;
  while (l < h) {
    size_t m = l + (h - l) / 2;
    if (value <= array[m]) h = m;
    else                   l = m + 1;
  }
  if (value < array[l]) return l;
  else                  return l + 1;
}

void cilkmerge(elem_t* array1, size_t n1,
               elem_t* array2, size_t n2,
               elem_t* array_dest) {
  if (n1 < n2) {
    // array2 is always smaller
    std::swap(array1, array2);
    std::swap(n1, n2);
  }
  if (n2 == 0) {
    std::memcpy(array_dest, array1, sizeof(elem_t) * n1);
    return;
  }
  if (n1 < cutoff_merge) {
    merge_seq(array1, n1, array2, n2, array_dest);
    return;
  }
  size_t split1 = n1 / 2;
  size_t split2 = binary_search(array2, n2, array1[split1]);
  cilkmerge(array1         , split1     , array2         , split2     , array_dest);
  cilkmerge(array1 + split1, n1 - split1, array2 + split2, n2 - split2, array_dest + split1 + split2);
}

void cilksort(elem_t* array, elem_t* buf, size_t n) {
  if (n < cutoff_quick) {
    quicksort_seq(array, n);
    return;
  }

  size_t m = n / 4;
  elem_t* a1 = array;
  elem_t* a2 = array + m;
  elem_t* a3 = array + 2 * m;
  elem_t* a4 = array + 3 * m;
  elem_t* b1 = buf;
  elem_t* b2 = buf + m;
  elem_t* b3 = buf + 2 * m;
  elem_t* b4 = buf + 3 * m;

  cilksort(a1, b1, m        );
  cilksort(a2, b2, m        );
  cilksort(a3, b3, m        );
  cilksort(a4, b4, n - 3 * m);

  cilkmerge(a1, m, a2, m        , b1);
  cilkmerge(a3, m, a4, n - 3 * m, b3);

  cilkmerge(b1, 2 * m, b3, n - 2 * m, a1);
}

void init_array(elem_t* array, size_t n) {
  for (size_t i = 0; i < n; i++) {
    array[i] = my_random();
  }
}

bool check_sorted(elem_t* array, size_t n) {
  for (size_t i = 0; i < n - 1; i++) {
    if (array[i] > array[i + 1]) {
      return false;
    }
  }
  return true;
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
           "    -m : cutoff for mergesort (size_t)\n"
           "    -q : cutoff for quicksort (size_t)\n", argv[0]);
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
           "Cutoff (Insertion sort):       %ld\n"
           "Cutoff (Mergesort):            %ld\n"
           "Cutoff (Quicksort):            %ld\n"
           "-------------------------------------------------------------\n",
           n_procs, n_input, n_repeats, enable_check, serial_exec,
           cutoff_insert, cutoff_merge, cutoff_quick);
    printf("uth options:\n");
    madm::uth::print_options(stdout);
    printf("=============================================================\n\n");
    fflush(stdout);
  }

  /* constexpr uint64_t cache_size = 16 * 1024 * 1024; */
  /* pcas::pcas pc(cache_size); */

  /* auto array = pc.malloc<elem_t>(n_input); */
  /* auto buf   = pc.malloc<elem_t>(n_input); */
  elem_t* array = (elem_t*)malloc(sizeof(elem_t) * n_input);
  elem_t* buf   = (elem_t*)malloc(sizeof(elem_t) * n_input);

  for (int r = 0; r < n_repeats; r++) {
    init_array(array, n_input);

    uint64_t t0 = madi::global_clock::get_time();

    madm::uth::thread<void> th(cilksort, array, buf, n_input);
    th.join();
    /* std::sort(array, array + n_input); */

    uint64_t t1 = madi::global_clock::get_time();

    if (my_rank == 0) {
      printf("[%d] %ld ns\n", r, t1 - t0);
      if (enable_check) {
        if (!check_sorted(array, n_input)) {
          printf("check failed.\n");
        }
      }
    }
  }

  /* pc.free(array); */
  /* pc.free(buf); */
  free(array);
  free(buf);
  return 0;
}

int main(int argc, char** argv) {
  madm::uth::start(real_main, argc, argv);
  return 0;
}
