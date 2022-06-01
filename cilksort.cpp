#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <random>

#include "uth.h"
#include "pcas/pcas.hpp"

pcas::pcas& pc(size_t cache_size = 0) {
  static pcas::pcas g_pc(cache_size);
  return g_pc;
}

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
    auto p = pc().checkout<pcas::access_mode::read>(ptr_ + i, 1);
    T ret = *p;
    pc().checkin(p);
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
      auto p = pc().checkout<pcas::access_mode::read_write>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(p[j]);
      }
      pc().checkin(p);
    }
  }

  template <typename Acc, typename F>
  Acc reduce(Acc init, F f) {
    Acc acc = init;
    for (size_t i = 0; i < n_; i += BlockSize) {
      size_t b = std::min(n_ - i, BlockSize);
      auto p = pc().checkout<pcas::access_mode::read>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        acc = f(acc, p[j]);
      }
      pc().checkin(p);
    }
    return acc;
  }

  template <pcas::access_mode Mode>
  raw_span<T> checkout() const {
    auto p = pc().checkout<Mode>(ptr_, n_);
    return {const_cast<T*>(p), n_}; // TODO: reconsider const type handling
  }

  void checkin(raw_span<T> s) const {
    pc().checkin(&s[0]);
  }

  friend void copy(this_t dest, this_t src) {
    assert(dest.n_ == src.n_);
    for (size_t i = 0; i < dest.n_; i += BlockSize) {
      size_t b = std::min(dest.n_ - i, BlockSize);
      auto d = pc().checkout<pcas::access_mode::write>(dest.ptr_ + i, b);
      auto s = pc().checkout<pcas::access_mode::read >(src.ptr_  + i, b);
      std::memcpy(d, s, sizeof(T) * b);
      pc().checkin(d);
      pc().checkin(s);
    }
  }
};

template <size_t MaxTasks, bool SpawnLastTask = false>
class task_group {
  madm::uth::thread<void> tasks_[MaxTasks];
  size_t n_ = 0;

public:
  task_group() {}

  template <typename F, typename... Args>
  void run(F f, Args... args) {
    assert(n_ < MaxTasks);
    if (SpawnLastTask || n_ < MaxTasks - 1) {
      pc().release();
      new (&tasks_[n_++]) madm::uth::thread<void>{[=]() {
        pc().acquire();
        f(args...);
        pc().release();
      }};
    } else {
      pc().acquire();
      f(args...);
      pc().release();
    }
  }

  void wait() {
    for (size_t i = 0; i < n_; i++) {
      tasks_[i].join();
    }
    pc().acquire();
    n_ = 0;
  }
};

using elem_t = float;

int my_rank = -1;
int n_procs = -1;

size_t n_input       = 1024;
int    n_repeats     = 10;
size_t cache_size    = 16;
int    verify_result = 1;
size_t cutoff_insert = 64;
size_t cutoff_merge  = 16 * 1024;
size_t cutoff_quick  = 16 * 1024;

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
size_t binary_search(Span s, const typename Span::element_type& v) {
  size_t l = 0;
  size_t h = s.size();
  while (l < h) {
    size_t m = l + (h - l) / 2;
    if (v <= s[m]) h = m;
    else           l = m + 1;
  }
  return h;
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

  size_t split1 = (s1.size() + 1) / 2;
  size_t split2 = binary_search(s2, s1[split1 - 1]);

  auto [s11  , s12  ] = s1.divide(split1);
  auto [s21  , s22  ] = s2.divide(split2);
  auto [dest1, dest2] = dest.divide(split1 + split2);

  /* cilkmerge(s11, s21, dest1); */
  /* cilkmerge(s12, s22, dest2); */
  task_group<2> tg;
  tg.run(cilkmerge<Span>, s11, s21, dest1);
  tg.run(cilkmerge<Span>, s12, s22, dest2);
  tg.wait();
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

  /* cilksort<Span>(a1, b1); */
  /* cilksort<Span>(a2, b2); */
  /* cilksort<Span>(a3, b3); */
  /* cilksort<Span>(a4, b4); */
  {
    task_group<4> tg;
    tg.run(cilksort<Span>, a1, b1);
    tg.run(cilksort<Span>, a2, b2);
    tg.run(cilksort<Span>, a3, b3);
    tg.run(cilksort<Span>, a4, b4);
    tg.wait();
  }

  /* cilkmerge(a1, a2, b12); */
  /* cilkmerge(a3, a4, b34); */
  {
    task_group<2> tg;
    tg.run(cilkmerge<Span>, a1, a2, b12);
    tg.run(cilkmerge<Span>, a3, a4, b34);
    tg.wait();
  }

  cilkmerge(b12, b34, a);
}

template <typename Span, typename Rng>
void init_array_aux(Span s, Rng r) {
  static std::uniform_real_distribution<elem_t> dist(0.0, 1.0);
  if (s.size() < cutoff_quick) {
    s.for_each([&](typename Span::element_type& e) {
      e = dist(r);
    });
  } else {
    auto [s1, s2] = s.divide_two();
    task_group<2> tg;
    tg.run(init_array_aux<Span, Rng>, s1, r);
    r.discard(s1.size());
    tg.run(init_array_aux<Span, Rng>, s2, r);
    tg.wait();
  }
}

template <typename Span>
void init_array(Span s) {
  static int counter = 0;
  std::mt19937 r(counter++);
  init_array_aux(s, r);
  /* s.for_each([&](typename Span::element_type& e) { */
  /*   printf("%f\n", e); */
  /* }); */
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
  while ((opt = getopt(argc, argv, "n:r:c:v:s:i:m:q:h")) != EOF) {
    switch (opt) {
      case 'n':
        n_input = atoll(optarg);
        break;
      case 'r':
        n_repeats = atoi(optarg);
        break;
      case 'c':
        cache_size = atoll(optarg);
        break;
      case 'v':
        verify_result = atoi(optarg);
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
           "PCAS cache size:               %ld MB\n"
           "Verify result:                 %d\n"
           "Cutoff (insertion sort):       %ld\n"
           "Cutoff (merge):                %ld\n"
           "Cutoff (quicksort):            %ld\n"
           "-------------------------------------------------------------\n",
           n_procs, n_input, n_repeats, cache_size, verify_result,
           cutoff_insert, cutoff_merge, cutoff_quick);
    printf("uth options:\n");
    madm::uth::print_options(stdout);
    printf("=============================================================\n\n");
    fflush(stdout);
  }

  pc(cache_size * 1024 * 1024);

  auto array = pc().malloc<elem_t>(n_input);
  auto buf   = pc().malloc<elem_t>(n_input);

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
      task_group<1, true> tg;
      tg.run([=]() { cilksort(a, b); });
      tg.wait();
      /* std::sort(array, array + n_input); */
    }

    uint64_t t1 = madi::global_clock::get_time();

    if (my_rank == 0) {
      printf("[%d] %ld ns\n", r, t1 - t0);
      fflush(stdout);
      if (verify_result) {
        if (!check_sorted(a)) {
          printf("check failed.\n");
        }
      }
    }

    madm::uth::barrier();
  }

  pc().free(array);
  pc().free(buf);

  /* free(array); */
  /* free(buf); */

  return 0;
}

int main(int argc, char** argv) {
  madm::uth::start(real_main, argc, argv);
  return 0;
}
