#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <random>
#include <sstream>

#include <mpi.h>

#include "pcg_random.hpp"

#include "ityr/ityr.hpp"

#ifndef ITYR_BENCH_USE_SEQ_STL
#define ITYR_BENCH_USE_SEQ_STL 0
#endif

enum class kind_value {
  Init = 0,
  Quicksort,
  Merge,
  BinarySearch,
  Copy,
  QuicksortKernel,
  MergeKernel,
  _NKinds,
};

class kind : public ityr::logger::kind_base<kind, kind_value> {
public:
  using ityr::logger::kind_base<kind, kind_value>::kind_base;
  constexpr const char* str() const {
    switch (val_) {
      case value::Init:               return "";
      case value::Quicksort:          return "quicksort";
      case value::Merge:              return "merge";
      case value::BinarySearch:       return "binary_search";
      case value::QuicksortKernel:    return "quicksort_kernel";
      case value::MergeKernel:        return "merge_kernel";
      case value::Copy:               return "copy";
      default:                        return "other";
    }
  }
};

struct my_ityr_policy : ityr::ityr_policy {
  using logger_kind_t = kind;
};

using my_ityr = ityr::ityr_if<my_ityr_policy>;

template <typename T>
class raw_span {
  using this_t = raw_span<T>;
  T* ptr_;
  size_t n_;

public:
  using element_type = T;

  raw_span(T* ptr, size_t n) : ptr_(ptr), n_(n) {}
  template <typename U>
  raw_span(raw_span<U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

  constexpr T* data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return n_; }

  constexpr T* begin() const noexcept { return ptr_; }
  constexpr T* end()   const noexcept { return ptr_ + n_; }

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
  void for_each(F f) const {
    for (size_t i = 0; i < n_; i++) {
      f(const_cast<const T&>(ptr_[i]));
    }
  }

  template <typename F>
  void map(F f) {
    for (size_t i = 0; i < n_; i++) {
      f(ptr_[i]);
    }
  }

  template <typename Acc, typename ReduceOp, typename TransformOp>
  Acc reduce(Acc         init,
             ReduceOp    reduce_op,
             TransformOp transform_op) {
    Acc acc = init;
    for (size_t i = 0; i < n_; i++) {
      acc = reduce_op(acc, transform_op(ptr_[i]));
    }
    return acc;
  }

  void willread() const {}
};

template <my_ityr::iro::access_mode Mode,
          typename T, typename Fn>
void with_checkout(raw_span<T> s, Fn f) {
  f(s);
}

template <my_ityr::iro::access_mode Mode1,
          my_ityr::iro::access_mode Mode2,
          typename T1, typename T2, typename Fn>
void with_checkout(raw_span<T1> s1, raw_span<T2> s2, Fn f) {
  f(s1, s2);
}

template <my_ityr::iro::access_mode Mode1,
          my_ityr::iro::access_mode Mode2,
          my_ityr::iro::access_mode Mode3,
          typename T1, typename T2, typename T3, typename Fn>
void with_checkout(raw_span<T1> s1, raw_span<T2> s2, raw_span<T3> s3, Fn f) {
  f(s1, s2, s3);
}

template <typename T>
class gptr_span {
  using this_t = gptr_span<T>;
  using ptr_t = my_ityr::iro::global_ptr<T>;
  ptr_t ptr_;
  size_t n_;

public:
  using element_type = T;

  gptr_span(ptr_t ptr, size_t n) : ptr_(ptr), n_(n) {}
  template <typename U>
  gptr_span(gptr_span<U> s) : ptr_(s.data()), n_(s.size() * sizeof(U) / sizeof(T)) {}

  constexpr ptr_t data() const noexcept { return ptr_; }
  constexpr size_t size() const noexcept { return n_; }

  constexpr ptr_t begin() const noexcept { return ptr_; }
  constexpr ptr_t end()   const noexcept { return ptr_ + n_; }

  constexpr auto operator[](size_t i) const {
    assert(i < n_);
    return ptr_[i];
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

  template <typename F, uint64_t BlockSize = 65536>
  void for_each(F f) const {
    for (size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
      size_t b = std::min(n_ - i, BlockSize / sizeof(T));
      auto p = my_ityr::iro::checkout<my_ityr::iro::access_mode::read>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(const_cast<const T&>(p[j]));
      }
      my_ityr::iro::checkin(p, b);
    }
  }

  template <typename F, uint64_t BlockSize = 65536>
  void map(F f) {
    for (size_t i = 0; i < n_; i += BlockSize / sizeof(T)) {
      size_t b = std::min(n_ - i, BlockSize / sizeof(T));
      auto p = my_ityr::iro::checkout<my_ityr::iro::access_mode::read_write>(ptr_ + i, b);
      for (size_t j = 0; j < b; j++) {
        f(p[j]);
      }
      my_ityr::iro::checkin(p, b);
    }
  }

  template <typename Acc, typename ReduceOp, typename TransformOp, uint64_t BlockSize = 65536>
  Acc reduce(Acc         init,
             ReduceOp    reduce_op,
             TransformOp transform_op) const {
    return my_ityr::parallel_reduce(ptr_, ptr_ + n_, init, reduce_op, transform_op, BlockSize);
  }

  void willread() const {
    my_ityr::iro::willread(ptr_, n_);
  }
};

template <my_ityr::iro::access_mode Mode,
          typename T, typename Fn>
void with_checkout(gptr_span<T> s, Fn f) {
  auto p = my_ityr::iro::checkout<Mode>(s.data(), s.size());
  f(raw_span<T>{p, s.size()});
  my_ityr::iro::checkin<Mode>(p, s.size());
}

template <my_ityr::iro::access_mode Mode1,
          my_ityr::iro::access_mode Mode2,
          typename T1, typename T2, typename Fn>
void with_checkout(gptr_span<T1> s1, gptr_span<T2> s2, Fn f) {
  auto p1 = my_ityr::iro::checkout<Mode1>(s1.data(), s1.size());
  auto p2 = my_ityr::iro::checkout<Mode2>(s2.data(), s2.size());
  f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()});
  my_ityr::iro::checkin<Mode1>(p1, s1.size());
  my_ityr::iro::checkin<Mode2>(p2, s2.size());
}

template <my_ityr::iro::access_mode Mode1,
          my_ityr::iro::access_mode Mode2,
          my_ityr::iro::access_mode Mode3,
          typename T1, typename T2, typename T3, typename Fn>
void with_checkout(gptr_span<T1> s1, gptr_span<T2> s2, gptr_span<T3> s3, Fn f) {
  auto p1 = my_ityr::iro::checkout<Mode1>(s1.data(), s1.size());
  auto p2 = my_ityr::iro::checkout<Mode2>(s2.data(), s2.size());
  auto p3 = my_ityr::iro::checkout<Mode3>(s3.data(), s3.size());
  f(raw_span<T1>{p1, s1.size()}, raw_span<T2>{p2, s2.size()}, raw_span<T3>{p3, s3.size()});
  my_ityr::iro::checkin<Mode1>(p1, s1.size());
  my_ityr::iro::checkin<Mode2>(p2, s2.size());
  my_ityr::iro::checkin<Mode3>(p3, s3.size());
}

enum class exec_t {
  Serial = 0,
  StdSort = 1,
  Parallel = 2,
};

std::ostream& operator<<(std::ostream& o, const exec_t& e) {
  switch (e) {
    case exec_t::Serial:   o << "serial"  ; break;
    case exec_t::StdSort:  o << "std_sort"; break;
    case exec_t::Parallel: o << "parallel"; break;
  }
  return o;
}

template <typename T>
std::string to_str(T x) {
  std::stringstream ss;
  ss << x;
  return ss.str();
}

#ifndef ITYR_BENCH_ELEM_TYPE
#define ITYR_BENCH_ELEM_TYPE int
#endif

using elem_t = ITYR_BENCH_ELEM_TYPE;

#undef ITYR_BENCH_ELEM_TYPE

int my_rank = -1;
int n_ranks = -1;

size_t n_input       = 1024;
int    n_repeats     = 10;
exec_t exec_type     = exec_t::Parallel;
size_t cache_size    = 16;
int    verify_result = 1;
size_t cutoff_insert = 64;
size_t cutoff_merge  = 16 * 1024;
size_t cutoff_quick  = 16 * 1024;

#if !ITYR_BENCH_USE_SEQ_STL
template <template<typename> typename Span, typename T>
static inline T select_pivot(Span<const T> s) {
  // median of three values
  if (s.size() < 3) return s[0];
  auto a = s[0];
  auto b = s[1];
  auto c = s[2];
  if ((a > b) != (a > c))      return a;
  else if ((b > a) != (b > c)) return b;
  else                         return c;
}

template <template<typename> typename Span, typename T>
void insertion_sort(Span<T> s) {
  for (size_t i = 1; i < s.size(); i++) {
    auto a = s[i];
    size_t j;
    for (j = 1; j <= i && s[i - j] > a; j++) {
      s[i - j + 1] = s[i - j];
    }
    s[i - j + 1] = a;
  }
}

template <template<typename> typename Span, typename T>
std::pair<Span<T>, Span<T>> partition_seq(Span<T> s, T pivot) {
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
#endif

template <template<typename> typename Span, typename T>
void quicksort_seq(Span<T> s) {
#if ITYR_BENCH_USE_SEQ_STL
  std::sort(s.begin(), s.end());
#else
  if (s.size() <= cutoff_insert) {
    insertion_sort(s);
  } else {
    auto pivot = select_pivot(Span<const T>(s));
    auto [s1, s2] = partition_seq(s, pivot);
    quicksort_seq(s1);
    quicksort_seq(s2);
  }
#endif
}

template <template<typename> typename Span, typename T>
void merge_seq(Span<const T> s1, Span<const T> s2, Span<T> dest) {
  assert(s1.size() + s2.size() == dest.size());

#if ITYR_BENCH_USE_SEQ_STL
  std::merge(s1.begin(), s1.end(), s2.begin(), s2.end(), dest.begin());
#else
  size_t d = 0;
  size_t l1 = 0;
  size_t l2 = 0;
  auto a1 = s1[0];
  auto a2 = s2[0];
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
    std::copy(src_r.begin(), src_r.end(), dest_r.begin());
  } else {
    Span dest_r = dest.subspan(d, s1.size() - l1);
    Span src_r  = s1.subspan(l1, s1.size() - l1);
    std::copy(src_r.begin(), src_r.end(), dest_r.begin());
  }
#endif
}

template <template<typename> typename Span, typename T>
size_t binary_search(Span<const T> s, const T& v) {
#if ITYR_BENCH_USE_SEQ_STL
  auto it = std::lower_bound(s.begin(), s.end(), v);
  return it - s.begin();
#else
  size_t l = 0;
  size_t h = s.size();
  while (l < h) {
    size_t m = l + (h - l) / 2;
    if (v <= s[m]) h = m;
    else           l = m + 1;
  }
  return h;
#endif
}

template <template<typename> typename Span, typename T>
void cilkmerge(Span<const T> s1, Span<const T> s2, Span<T> dest) {
  assert(s1.size() + s2.size() == dest.size());

  if (s1.size() < s2.size()) {
    // s2 is always smaller
    std::swap(s1, s2);
  }

  if (s2.size() == 0) {
    auto ev = my_ityr::logger::record<my_ityr::logger_kind::Copy>();

    with_checkout<my_ityr::iro::access_mode::read,
                  my_ityr::iro::access_mode::write>(
        s1, dest, [&](auto s1_, auto dest_) {
      std::copy(s1_.begin(), s1_.end(), dest_.begin());
    });

    return;
  }

  /* if (s1.size() * sizeof(elem_t) < 4 * 65536) { */
  /*   s1.willread(); */
  /*   s2.willread(); */
  /* } */

  if (dest.size() <= cutoff_merge) {
    auto ev = my_ityr::logger::record<my_ityr::logger_kind::Merge>();

    with_checkout<my_ityr::iro::access_mode::read,
                  my_ityr::iro::access_mode::read,
                  my_ityr::iro::access_mode::write>(
        s1, s2, dest, [&](auto s1_, auto s2_, auto dest_) {
      auto ev2 = my_ityr::logger::record<my_ityr::logger_kind::MergeKernel>();
      merge_seq(s1_, s2_, dest_);
    });

    return;
  }

  size_t split1, split2;
  {
    auto ev = my_ityr::logger::record<my_ityr::logger_kind::BinarySearch>();
    split1 = (s1.size() + 1) / 2;
    split2 = binary_search(s2, T(s1[split1 - 1]));
  }

  auto [s11  , s12  ] = s1.divide(split1);
  auto [s21  , s22  ] = s2.divide(split2);
  auto [dest1, dest2] = dest.divide(split1 + split2);

  my_ityr::parallel_invoke(
    cilkmerge<Span, T>, s11, s21, dest1,
    cilkmerge<Span, T>, s12, s22, dest2
  );
}

template <template<typename> typename Span, typename T>
void cilksort(Span<T> a, Span<T> b) {
  assert(a.size() == b.size());

  if (a.size() <= cutoff_quick) {
    auto ev = my_ityr::logger::record<my_ityr::logger_kind::Quicksort>();

    with_checkout<my_ityr::iro::access_mode::read_write>(a, [&](auto a_) {
      auto ev2 = my_ityr::logger::record<my_ityr::logger_kind::QuicksortKernel>();
      quicksort_seq(a_);
    });

    return;
  }

  /* if (a.size() * sizeof(elem_t) < 4 * 65536) { */
  /*   a.willread(); */
  /*   b.willread(); */
  /* } */

  auto [a12, a34] = a.divide_two();
  auto [b12, b34] = b.divide_two();

  auto [a1, a2] = a12.divide_two();
  auto [a3, a4] = a34.divide_two();
  auto [b1, b2] = b12.divide_two();
  auto [b3, b4] = b34.divide_two();

  my_ityr::parallel_invoke(
    cilksort<Span, T>, a1, b1,
    cilksort<Span, T>, a2, b2,
    cilksort<Span, T>, a3, b3,
    cilksort<Span, T>, a4, b4
  );

  my_ityr::parallel_invoke(
    cilkmerge<Span, T>, Span<const T>(a1), Span<const T>(a2), b12,
    cilkmerge<Span, T>, Span<const T>(a3), Span<const T>(a4), b34
  );

  cilkmerge(Span<const T>(b12), Span<const T>(b34), a);
}

template <typename T, typename Rng>
std::enable_if_t<std::is_integral_v<T>, T>
gen_random_elem(Rng& r) {
  static std::uniform_int_distribution<T> dist(0, std::numeric_limits<T>::max());
  return dist(r);
}

template <typename T, typename Rng>
std::enable_if_t<std::is_floating_point_v<T>, T>
gen_random_elem(Rng& r) {
  static std::uniform_real_distribution<T> dist(0, 1.0);
  return dist(r);
}

template <template<typename> typename Span, typename T>
void init_array(Span<T> s) {
  static int counter = 0;
  auto seed = counter++;

  my_ityr::root_spawn([=] {
    my_ityr::parallel_transform(ityr::count_iterator<std::size_t>(0),
                                ityr::count_iterator<std::size_t>(s.size()),
                                s.begin(),
                                [=](std::size_t i) {
      pcg32 rng(seed, i);
      return gen_random_elem<T>(rng);
    }, my_ityr::iro::block_size / sizeof(T));
  });

  /* std::copy(s.begin(), s.end(), std::ostream_iterator<T>(std::cout, ",")); */
  /* std::cout << std::endl; */
}

template <template<typename> typename Span, typename T>
bool check_sorted(Span<const T> s) {
  struct acc_type {
    bool is_init;
    bool success;
    T first;
    T last;
  };
  acc_type init{true, true, T{}, T{}};
  auto ret = s.reduce(init, [](auto l, auto r) {
    if (l.is_init) return r;
    if (r.is_init) return l;
    if (!l.success || !r.success) return acc_type{false, false, l.first, r.last};
    else if (l.last > r.first) return acc_type{false, false, l.first, r.last};
    else return acc_type{false, true, l.first, r.last};
  }, [](const T& e) { return acc_type{false, true, e, e}; });
  return ret.success;
}

template <template<typename> typename Span, typename T>
void run(Span<T> a, Span<T> b) {
  for (int r = 0; r < n_repeats; r++) {
    if (my_rank == 0) {
      uint64_t t0 = my_ityr::wallclock::get_time();
      init_array(a);
      uint64_t t1 = my_ityr::wallclock::get_time();
      printf("Array initialized. (%ld ns)\n", t1 - t0);
    }

    my_ityr::barrier();
    my_ityr::logger::clear();
    my_ityr::barrier();

    uint64_t t0 = my_ityr::wallclock::get_time();

    if (my_rank == 0) {
      switch (exec_type) {
        case exec_t::Serial: {
          cilksort(a, b);
          break;
        }
        case exec_t::StdSort: {
          std::sort(a.begin(), a.end());
          break;
        }
        case exec_t::Parallel: {
          my_ityr::root_spawn([=]() { cilksort(a, b); });
          break;
        }
      }
    }

    uint64_t t1 = my_ityr::wallclock::get_time();

    my_ityr::barrier();

    if (my_rank == 0) {
      printf("[%d] %ld ns\n", r, t1 - t0);
      fflush(stdout);
    }

    if (n_ranks > 1) {
      // FIXME
      MPI_Bcast(&t0, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
      MPI_Bcast(&t1, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    }

    my_ityr::logger::flush_and_print_stat(t0, t1);

    if (my_rank == 0) {
      if (verify_result) {
        // FIXME: special treatment for root task
        uint64_t t0 = my_ityr::wallclock::get_time();
        /* bool success = std::is_sorted(a.begin(), a.end()); */
        bool success = my_ityr::root_spawn([=]() {
          return check_sorted(Span<const T>(a));
        });
        uint64_t t1 = my_ityr::wallclock::get_time();
        if (success) {
          printf("Check succeeded. (%ld ns)\n", t1 - t0);
        } else {
          printf("\x1b[31mCheck FAILED.\x1b[39m\n");
        }
        fflush(stdout);
      }
    }

    my_ityr::barrier();
  }
}

void show_help_and_exit(int argc, char** argv) {
  if (my_rank == 0) {
    printf("Usage: %s [options]\n"
           "  options:\n"
           "    -n : Input size (size_t)\n"
           "    -r : # of repeats (int)\n"
           "    -c : check the result (int)\n"
           "    -e : execution type (0: serial, 1: std::sort(), 2: parallel (default))\n"
           "    -s : serial execution (int)\n"
           "    -i : cutoff for insertion sort (size_t)\n"
           "    -m : cutoff for serial merge (size_t)\n"
           "    -q : cutoff for serial quicksort (size_t)\n", argv[0]);
  }
  exit(1);
}

int real_main(int argc, char **argv) {
  my_rank = my_ityr::rank();
  n_ranks = my_ityr::n_ranks();

  my_ityr::logger::init(my_rank, n_ranks);

  int opt;
  while ((opt = getopt(argc, argv, "n:r:e:c:v:s:i:m:q:h")) != EOF) {
    switch (opt) {
      case 'n':
        n_input = atoll(optarg);
        break;
      case 'r':
        n_repeats = atoi(optarg);
        break;
      case 'e':
        exec_type = exec_t(atoi(optarg));
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
           "[Cilksort]\n"
           "Element type:                  %s (%ld bytes)\n"
           "# of processes:                %d\n"
           "N (Input size):                %ld\n"
           "# of repeats:                  %d\n"
           "Execution type:                %s\n"
           "PCAS cache size:               %ld MB\n"
           "Verify result:                 %d\n"
           "Cutoff (insertion sort):       %ld\n"
           "Cutoff (merge):                %ld\n"
           "Cutoff (quicksort):            %ld\n"
           "-------------------------------------------------------------\n",
           ityr::typename_str<elem_t>(), sizeof(elem_t),
           n_ranks, n_input, n_repeats, to_str(exec_type).c_str(), cache_size, verify_result,
           cutoff_insert, cutoff_merge, cutoff_quick);
    printf("uth options:\n");
    madm::uth::print_options(stdout);
    printf("=============================================================\n\n");
    printf("PID of the main worker: %d\n", getpid());
    fflush(stdout);
  }

  my_ityr::iro::init(cache_size * 1024 * 1024);

  if (exec_type == exec_t::Parallel) {
    auto array = my_ityr::iro::malloc<elem_t>(n_input);
    auto buf   = my_ityr::iro::malloc<elem_t>(n_input);

    gptr_span<elem_t> a(array, n_input);
    gptr_span<elem_t> b(buf  , n_input);

    run(a, b);

    my_ityr::iro::free(array);
    my_ityr::iro::free(buf);
  } else {
    elem_t* array = (elem_t*)malloc(sizeof(elem_t) * n_input);
    elem_t* buf   = (elem_t*)malloc(sizeof(elem_t) * n_input);

    raw_span<elem_t> a(array, n_input);
    raw_span<elem_t> b(buf  , n_input);

    run(a, b);

    free(array);
    free(buf);
  }

  my_ityr::iro::fini();

  return 0;
}

int main(int argc, char** argv) {
  my_ityr::main(real_main, argc, argv);
  return 0;
}
