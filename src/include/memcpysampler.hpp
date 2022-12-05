#pragma once
#ifndef MEMCPYSAMPLER_HPP
#define MEMCPYSAMPLER_HPP

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mallocrecursionguard.hpp"
#if !defined(_WIN32)
#include <unistd.h>  // for getpid()
#endif

#include "printf.h"
#include "pywhere.hpp"
#include "sampler.hpp"

#if !defined(__APPLE__) && !defined(_WIN32)
#include <endian.h>
#endif

void *memcpy_musl(void *dest, const void *src, size_t n) {
  unsigned char *d = reinterpret_cast<unsigned char *>(dest);
  const unsigned char *s = reinterpret_cast<const unsigned char *>(src);

#ifdef __GNUC__

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define LS >>
#define RS <<
#else
#define LS <<
#define RS >>
#endif

  typedef uint32_t __attribute__((__may_alias__)) u32;
  uint32_t w, x;

  for (; (uintptr_t)s % 4 && n; n--) *d++ = *s++;

  if ((uintptr_t)d % 4 == 0) {
    for (; n >= 16; s += 16, d += 16, n -= 16) {
      *(u32 *)(d + 0) = *(u32 *)(s + 0);
      *(u32 *)(d + 4) = *(u32 *)(s + 4);
      *(u32 *)(d + 8) = *(u32 *)(s + 8);
      *(u32 *)(d + 12) = *(u32 *)(s + 12);
    }
    if (n & 8) {
      *(u32 *)(d + 0) = *(u32 *)(s + 0);
      *(u32 *)(d + 4) = *(u32 *)(s + 4);
      d += 8;
      s += 8;
    }
    if (n & 4) {
      *(u32 *)(d + 0) = *(u32 *)(s + 0);
      d += 4;
      s += 4;
    }
    if (n & 2) {
      *d++ = *s++;
      *d++ = *s++;
    }
    if (n & 1) {
      *d = *s;
    }
    return dest;
  }

  if (n >= 32) switch ((uintptr_t)d % 4) {
      case 1:
        w = *(u32 *)s;
        *d++ = *s++;
        *d++ = *s++;
        *d++ = *s++;
        n -= 3;
        for (; n >= 17; s += 16, d += 16, n -= 16) {
          x = *(u32 *)(s + 1);
          *(u32 *)(d + 0) = (w LS 24) | (x RS 8);
          w = *(u32 *)(s + 5);
          *(u32 *)(d + 4) = (x LS 24) | (w RS 8);
          x = *(u32 *)(s + 9);
          *(u32 *)(d + 8) = (w LS 24) | (x RS 8);
          w = *(u32 *)(s + 13);
          *(u32 *)(d + 12) = (x LS 24) | (w RS 8);
        }
        break;
      case 2:
        w = *(u32 *)s;
        *d++ = *s++;
        *d++ = *s++;
        n -= 2;
        for (; n >= 18; s += 16, d += 16, n -= 16) {
          x = *(u32 *)(s + 2);
          *(u32 *)(d + 0) = (w LS 16) | (x RS 16);
          w = *(u32 *)(s + 6);
          *(u32 *)(d + 4) = (x LS 16) | (w RS 16);
          x = *(u32 *)(s + 10);
          *(u32 *)(d + 8) = (w LS 16) | (x RS 16);
          w = *(u32 *)(s + 14);
          *(u32 *)(d + 12) = (x LS 16) | (w RS 16);
        }
        break;
      case 3:
        w = *(u32 *)s;
        *d++ = *s++;
        n -= 1;
        for (; n >= 19; s += 16, d += 16, n -= 16) {
          x = *(u32 *)(s + 3);
          *(u32 *)(d + 0) = (w LS 8) | (x RS 24);
          w = *(u32 *)(s + 7);
          *(u32 *)(d + 4) = (x LS 8) | (w RS 24);
          x = *(u32 *)(s + 11);
          *(u32 *)(d + 8) = (w LS 8) | (x RS 24);
          w = *(u32 *)(s + 15);
          *(u32 *)(d + 12) = (x LS 8) | (w RS 24);
        }
        break;
    }
  if (n & 16) {
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
  }
  if (n & 8) {
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
  }
  if (n & 4) {
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
  }
  if (n & 2) {
    *d++ = *s++;
    *d++ = *s++;
  }
  if (n & 1) {
    *d = *s;
  }
  return dest;
#endif

  for (; n; n--) *d++ = *s++;
  return dest;
}



template <uint64_t MemcpySamplingRateBytes>
class MemcpySampler {
  enum { MemcpySignal = SIGPROF };
  static constexpr auto flags =
      O_WRONLY | O_CREAT | O_SYNC | O_APPEND;  // O_TRUNC;
  static constexpr auto perms = S_IRUSR | S_IWUSR;
  static constexpr auto fname = "/tmp/scalene-memcpy-signal%d";

 public:
  MemcpySampler()
      :
        _interval(MemcpySamplingRateBytes),
        _memcpyOps(0),
        _memcpyTriggered(0) {
    static HL::PosixLock init_lock;
    init_lock.lock();
    auto old_sig = signal(MemcpySignal, SIG_IGN);
    if (old_sig != SIG_DFL) signal(MemcpySignal, old_sig);
    init_lock.unlock();
    auto pid = getpid();
    snprintf_((char *)scalene_memcpy_signal_filename,
              sizeof(scalene_memcpy_signal_filename), fname, pid);
    // printf("initialized (%s)\n", scalene_memcpy_signal_filename);
  }

  int local_strlen(const char *str) {
    int len = 0;
    while (*str != '\0') {
      len++;
      str++;
    }
    return len;
  }

  ~MemcpySampler() { unlink(scalene_memcpy_signal_filename); }

  ATTRIBUTE_ALWAYS_INLINE inline void *memcpy(void *dst, const void *src,
                                              size_t n) {
    MallocRecursionGuard g;
    auto result = local_memcpy(dst, src, n);
    if (pythonDetected() && !g.wasInMalloc()) incrementMemoryOps(n);
    return result;  // always dst
  }

  ATTRIBUTE_ALWAYS_INLINE inline void *memmove(void *dst, const void *src,
                                               size_t n) {
    MallocRecursionGuard g;
    auto result = local_memmove(dst, src, n);
    if (pythonDetected() && !g.wasInMalloc()) incrementMemoryOps(n);
    return result;  // always dst
  }

  ATTRIBUTE_ALWAYS_INLINE inline char *strcpy(char *dst, const char *src) {
    MallocRecursionGuard g;
    auto n = ::strlen(src);
    auto result = local_strcpy(dst, src);
    if (pythonDetected() && !g.wasInMalloc()) incrementMemoryOps(n);
    return result;  // always dst
  }

 private:
  //// local implementations of memcpy and friends.
  Sampler<MemcpySamplingRateBytes> _memcpySampler;
  // SampleFile _samplefile;
  ATTRIBUTE_ALWAYS_INLINE inline void *local_memcpy(void *dst, const void *src,
                                                    size_t n) {
#if defined(__APPLE__)
    return ::memcpy(dst, src, n);
#else
    return memcpy_musl(dst, src, n);
#endif
  }

  void *local_memmove(void *dst, const void *src, size_t n) {
#if defined(__APPLE__)
    return ::memmove(dst, src, n);
#else
    // TODO: optimize if these areas don't overlap.
    void *buf = malloc(n);
    local_memcpy(buf, src, n);
    local_memcpy(dst, buf, n);
    free(buf);
    return dst;
#endif
  }

  char *local_strcpy(char *dst, const char *src) {
    char *orig_dst = dst;
    while (*src != '\0') {
      *dst++ = *src++;
    }
    *dst = '\0';
    return orig_dst;
  }

  void incrementMemoryOps(int n) {
    if (pythonDetected()) {
      _memcpyOps += n;
      auto sampleMemop = _memcpySampler.sample(n);
      if (unlikely(sampleMemop)) {
        writeCount();
#if !SCALENE_DISABLE_SIGNALS
        raise(MemcpySignal);
#endif
      }
    }
  }

  uint64_t _interval;
  uint64_t _memcpyOps;
  unsigned long long _memcpyTriggered;
  static constexpr int BUFFER_LENGTH = 1024;
  char scalene_memcpy_signal_filename[BUFFER_LENGTH];

  void writeCount() {
#if 1
    std::string filename;
    int lineno = 1;
    int bytei = 0;
    decltype(whereInPython) *where = p_whereInPython;
    if (where != nullptr && where(filename, lineno, bytei)) {
      ;
    }
#endif
    char buf[BUFFER_LENGTH];
    snprintf_(buf, sizeof(buf), "%d,%d,%d,%s,%d,%d\n\n", _memcpyTriggered,
              _memcpyOps, getpid(), filename.c_str(), lineno, bytei);
    // _samplefile.writeToFile(buf);
  }
};

#endif
