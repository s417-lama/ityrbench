#pragma once

#include "pcas/pcas.hpp"

#include "ityr/iro_ref.hpp"
#include "ityr/wallclock.hpp"
#include "ityr/logger/impl_dummy.hpp"

namespace ityr {

template <typename P>
class iro_if {
  using impl_t = typename P::template iro_impl_t<P>;

  static std::optional<impl_t>& get_optional_instance() {
    static std::optional<impl_t> instance;
    return instance;
  }

  static impl_t& get_instance() {
    assert(get_optional_instance().has_value());
    return *get_optional_instance();
  }

public:
  template <typename T>
  using global_ptr = typename impl_t::template global_ptr<T>;
  using access_mode = typename impl_t::access_mode;
  using release_handler = typename impl_t::release_handler;

  static void init(size_t cache_size) {
    assert(!get_optional_instance().has_value());
    get_optional_instance().emplace(cache_size);
  }

  static void fini() {
    assert(get_optional_instance().has_value());
    get_optional_instance().reset();
  }

  static void release() {
    get_instance().release();
  }

  static void release_lazy(release_handler* handler) {
    get_instance().release_lazy(handler);
  }

  static void acquire() {
    get_instance().acquire();
  }

  static void acquire(release_handler handler) {
    get_instance().acquire(handler);
  }

  static void poll() {
    get_instance().poll();
  }

  template <typename T>
  static global_ptr<T> malloc(uint64_t nelems) {
    return get_instance().template malloc<T>(nelems);
  }

  template <typename T>
  static void free(global_ptr<T> ptr) {
    get_instance().free(ptr);
  }

  template <typename ConstT, typename T>
  static std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
    get_instance().get(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  static void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    get_instance().put(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  static void willread(global_ptr<T> ptr, uint64_t nelems) {
    get_instance().willread(ptr, nelems);
  }

  template <access_mode Mode, typename T>
  static auto checkout(global_ptr<T> ptr, uint64_t nelems) {
    return get_instance().template checkout<Mode>(ptr, nelems);
  }

  template <typename T>
  static void checkin(T* raw_ptr, uint64_t nelems) {
    get_instance().checkin(raw_ptr, nelems);
  }

  static void logger_clear() {
    get_instance().logger_clear();
  }

  static void logger_flush(uint64_t t_begin, uint64_t t_end) {
    get_instance().logger_flush(t_begin, t_end);
  }

  static void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {
    get_instance().logger_flush_and_print_stat(t_begin, t_end);
  }

};

template <typename P>
struct my_pcas_policy : public pcas::policy_default {
  template <typename GPtrT>
  using global_ref = typename P::template global_ref<GPtrT>;
  using wallclock_t = typename P::wallclock_t;
  template <typename P_>
  using logger_impl_t = typename P::template logger_impl_t<P_>;

#ifndef ITYR_DIST_POLICY
#define ITYR_DIST_POLICY cyclic
#endif
  template <uint64_t BlockSize>
  using default_mem_mapper = pcas::mem_mapper::ITYR_DIST_POLICY<BlockSize>;
#undef ITYR_DIST_POLICY

#ifndef ITYR_BLOCK_SIZE
#define ITYR_BLOCK_SIZE 65536
#endif
  constexpr static uint64_t block_size = ITYR_BLOCK_SIZE;
#undef ITYR_BLOCK_SIZE

#ifndef ITYR_ENABLE_WRITE_THROUGH
#define ITYR_ENABLE_WRITE_THROUGH 0
#endif
  constexpr static bool enable_write_through = ITYR_ENABLE_WRITE_THROUGH;
#undef ITYR_ENABLE_WRITE_THROUGH
};

template <typename P>
class iro_pcas_default : public pcas::pcas_if<my_pcas_policy<P>> {
  using base_t = pcas::pcas_if<my_pcas_policy<P>>;

public:
  template <typename T>
  using global_ptr = typename base_t::template global_ptr<T>;
  using access_mode = pcas::access_mode;
  using release_handler = pcas::release_handler;

  using base_t::base_t;

  void logger_clear() {
    base_t::logger::clear();
  }

  void logger_flush(uint64_t t_begin, uint64_t t_end) {
    base_t::logger::flush(t_begin, t_end);
  }

  void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {
    base_t::logger::flush_and_print_stat(t_begin, t_end);
  }
};

template <typename P>
class iro_pcas_nocache : public iro_pcas_default<P> {
  using base_t = iro_pcas_default<P>;

public:
  template <typename T>
  using global_ptr = typename base_t::template global_ptr<T>;
  using access_mode = typename base_t::access_mode;
  using release_handler = typename base_t::release_handler;

  using base_t::base_t;
  using base_t::get_nocache;
  using base_t::put_nocache;

  void release() {}
  void release_lazy(release_handler*) {}
  void acquire() {}
  void acquire(release_handler) {}
  void poll() {}
  template <typename T>
  void willread(global_ptr<T>, uint64_t) {}

  template <typename ConstT, typename T>
  std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
    get_nocache(from_ptr, to_ptr, nelems);
  }

  template <typename T>
  void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    put_nocache(from_ptr, to_ptr, nelems);
  }

  template <access_mode Mode, typename T>
  std::conditional_t<Mode == access_mode::read, const T*, T*>
  checkout(global_ptr<T> ptr, uint64_t nelems) {
    // If T is const, then it cannot be checked out with write access mode
    static_assert(!std::is_const_v<T> || Mode == access_mode::read);

    uint64_t size = nelems * sizeof(T);
    auto ret = (std::remove_const_t<T>*)std::malloc(size + sizeof(global_ptr<uint8_t>));
    if (Mode != access_mode::write) {
      get(ptr, ret, nelems);
    }
    *((global_ptr<uint8_t>*)((uint8_t*)ret + size)) = global_ptr<uint8_t>(ptr);
    return ret;
  }

  template <typename T>
  void checkin(const T* raw_ptr, uint64_t) {
    std::free(const_cast<T*>(raw_ptr));
  }

  template <typename T>
  void checkin(T* raw_ptr, uint64_t nelems) {
    uint64_t size = nelems * sizeof(T);
    global_ptr<uint8_t> ptr = *((global_ptr<uint8_t>*)((uint8_t*)raw_ptr + size));
    put((uint8_t*)raw_ptr, ptr, size);
    std::free(raw_ptr);
  }
};

template <typename P>
class iro_pcas_getput : public iro_pcas_default<P> {
  using base_t = iro_pcas_default<P>;

public:
  template <typename T>
  using global_ptr = typename base_t::template global_ptr<T>;
  using access_mode = typename base_t::access_mode;
  using release_handler = typename base_t::release_handler;

  using base_t::base_t;
  using base_t::get;
  using base_t::put;

  template <access_mode Mode, typename T>
  std::conditional_t<Mode == access_mode::read, const T*, T*>
  checkout(global_ptr<T> ptr, uint64_t nelems) {
    // If T is const, then it cannot be checked out with write access mode
    static_assert(!std::is_const_v<T> || Mode == access_mode::read);

    uint64_t size = nelems * sizeof(T);
    auto ret = (std::remove_const_t<T>*)std::malloc(size + sizeof(global_ptr<uint8_t>));
    if (Mode != access_mode::write) {
      get(ptr, ret, nelems);
    }
    *((global_ptr<uint8_t>*)((uint8_t*)ret + size)) = global_ptr<uint8_t>(ptr);
    return ret;
  }

  template <typename T>
  void checkin(const T* raw_ptr, uint64_t) {
    std::free(const_cast<T*>(raw_ptr));
  }

  template <typename T>
  void checkin(T* raw_ptr, uint64_t nelems) {
    uint64_t size = nelems * sizeof(T);
    global_ptr<uint8_t> ptr = *((global_ptr<uint8_t>*)((uint8_t*)raw_ptr + size));
    put((uint8_t*)raw_ptr, ptr, size);
    std::free(raw_ptr);
  }
};

template <typename P>
class iro_dummy {
public:
  template <typename T>
  using global_ptr = T*;
  using access_mode = pcas::access_mode;
  using release_handler = int;

  iro_dummy(size_t) {}

  void release() {}
  void release_lazy(release_handler*) {}
  void acquire() {}
  void acquire(release_handler) {}
  void poll() {}

  template <typename T>
  global_ptr<T> malloc(uint64_t nelems) { return (T*)std::malloc(nelems * sizeof(T)); }
  template <typename T>
  void free(global_ptr<T> ptr) { std::free(ptr); }

  template <typename ConstT, typename T>
  std::enable_if_t<std::is_same_v<std::remove_const_t<ConstT>, T>>
  get(global_ptr<ConstT> from_ptr, T* to_ptr, uint64_t nelems) {
    std::memcpy(to_ptr, from_ptr, nelems * sizeof(T));
  }
  template <typename T>
  void put(const T* from_ptr, global_ptr<T> to_ptr, uint64_t nelems) {
    std::memcpy(to_ptr, from_ptr, nelems * sizeof(T));
  }

  template <typename T>
  void willread(global_ptr<T> ptr, uint64_t nelems) {}
  template <access_mode Mode, typename T>
  auto checkout(global_ptr<T> ptr, uint64_t nelems) { return ptr; }
  template <typename T>
  void checkin(T* raw_ptr, uint64_t nelems) {}

  void logger_clear() {}
  void logger_flush(uint64_t t_begin, uint64_t t_end) {}
  void logger_flush_and_print_stat(uint64_t t_begin, uint64_t t_end) {}
};

struct iro_policy_default {
  template <typename P>
  using iro_impl_t = iro_dummy<P>;
  using wallclock_t = wallclock_native;
  template <typename P>
  using logger_impl_t = logger::impl_dummy<P>;
};

}
