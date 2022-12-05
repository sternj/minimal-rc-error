#pragma once

#ifndef SAMPLEHEAP_H
#define SAMPLEHEAP_H

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>  // for getpid()

#include <atomic>
#include <random>

// We're unable to use the limited API because, for example,
// there doesn't seem to be a function returning co_filename
//#define Py_LIMITED_API 0x03070000
#include <execinfo.h>

#include "common.hpp"
#include "mallocrecursionguard.hpp"

#include "printf.h"
#include "pywhere.hpp"
#include "scaleneheader.hpp"
#include "thresholdsampler.hpp"

#define USE_ATOMICS 0
#if USE_ATOMICS
typedef std::atomic<uint64_t> counterType;
#else
typedef uint64_t counterType;
#endif

template <uint64_t DefaultAllocationSamplingRateBytes, class SuperHeap>
class SampleHeap : public SuperHeap {
  constexpr static auto sampling_window_envname =
      "SCALENE_ALLOCATION_SAMPLING_WINDOW";

 public:
  enum { Alignment = SuperHeap::Alignment };
  enum AllocSignal { MallocSignal = SIGXCPU, FreeSignal = SIGXFSZ };
  static constexpr uint64_t NEWLINE =
      98821;  // Sentinel value denoting a new line has executed

  SampleHeap()
      : _lastMallocTrigger(nullptr),
        _freedLastMallocTrigger(false) {
                                 // invoked here so the file gets initialized before python
                      // attempts to read from it

    get_signal_init_lock().lock();
    auto old_malloc = signal(MallocSignal, SIG_IGN);
    if (old_malloc != SIG_DFL) {
      signal(MallocSignal, old_malloc);
    }
    auto old_free = signal(FreeSignal, SIG_IGN);
    if (old_free != SIG_DFL) {
      signal(FreeSignal, old_free);
    }
    get_signal_init_lock().unlock();
  }

  ATTRIBUTE_ALWAYS_INLINE inline void* malloc(size_t sz) {
    MallocRecursionGuard g;
    auto ptr = SuperHeap::malloc(sz);
    if (unlikely(ptr == nullptr)) {
      return nullptr;
    }
    if (pythonDetected() && !g.wasInMalloc()) {
      auto realSize = SuperHeap::getSize(ptr);
      if (realSize > 0) {
        if (sz == NEWLINE + sizeof(ScaleneHeader)) {
          // Don't count these allocations
          return ptr;
        }
        register_malloc(realSize, ptr, false);  // false -> invoked from C/C++
      }
      
    }
    return ptr;
  }
  ATTRIBUTE_ALWAYS_INLINE inline void* realloc(void* ptr, size_t sz) {
    MallocRecursionGuard g;
    if (!ptr) {
      ptr = SuperHeap::malloc(sz);
      return ptr;
    }
    if (sz == 0) {
      SuperHeap::free(ptr);
#if defined(__APPLE__)
      // 0 size = free. We return a small object.  This behavior is
      // apparently required under Mac OS X and optional under POSIX.
      return SuperHeap::malloc(1);
#else
      // For POSIX, don't return anything.
      return nullptr;
#endif
    }
    size_t objSize = SuperHeap::getSize(ptr);

    void* buf = SuperHeap::malloc(sz);
    size_t buf_size = buf ? SuperHeap::getSize(buf) : 0;
    if (buf) {
      if (objSize == buf_size) {
        // The objects are the same actual size.
        // Free the new object and return the original.
        SuperHeap::free(buf);
        return ptr;
      }
      // Copy the contents of the original object
      // up to the size of the new block.
      size_t minSize = (objSize < sz) ? objSize : sz;
      memcpy(buf, ptr, minSize);
    }

    // Free the old block.
    SuperHeap::free(ptr);
    if (buf) {
      if (sz < buf_size) {
        register_malloc(buf_size - sz, buf,
                        false);  // false -> invoked from C/C++
      } else if (sz > buf_size) {
        register_free(sz - buf_size, ptr);
      }
    }
    // Return a pointer to the new one.
    return buf;
  }
  inline void register_malloc(size_t realSize, void* ptr,
                              bool inPythonAllocator = true) {
    assert(realSize);
        std::string filename;
    int lineno = 1;
    int bytei = 0;

#if 1
    decltype(whereInPython)* where = p_whereInPython;
    if (where != nullptr && where(filename, lineno, bytei)) {
      ;
    }
    raise(MallocSignal);
#endif
  }

 public:
  ATTRIBUTE_ALWAYS_INLINE inline void free(void* ptr) {
    MallocRecursionGuard g;
    if (unlikely(ptr == nullptr)) {
      return;
    }
    auto realSize = SuperHeap::getSize(ptr);
    SuperHeap::free(ptr);
    if (pythonDetected() && !g.wasInMalloc()) {
      register_free(realSize, ptr);
    }
  }

  inline void register_free(size_t realSize, void* ptr) {
    
    size_t sampleFreeSize;
    if (unlikely(ptr && (ptr == _lastMallocTrigger))) {
      _freedLastMallocTrigger = true;
    }
        std::string filename;
    int lineno = 1;
    int bytei = 0;

#if 1
    decltype(whereInPython)* where = p_whereInPython;
    if (where != nullptr && where(filename, lineno, bytei)) {
      ;
    }
#endif
  }

 private:
  void process_free(size_t sampleFree) {
    std::string filename;
    int lineno = 1;
    int bytei = 0;

#if 1
    decltype(whereInPython)* where = p_whereInPython;
    if (where != nullptr && where(filename, lineno, bytei)) {
      ;
    }
#endif

    writeCount(FreeSignal, sampleFree, nullptr, filename, lineno, bytei);
#if !SCALENE_DISABLE_SIGNALS
    raise(FreeSignal);
#endif
    freeTriggered()++;
  }

 public:
  void* memalign(size_t alignment, size_t sz) {
    MallocRecursionGuard g;
    auto ptr = SuperHeap::memalign(alignment, sz);
    if (unlikely(ptr == nullptr)) {
      return nullptr;
    }
    if (pythonDetected() && !g.wasInMalloc()) {
      auto realSize = SuperHeap::getSize(ptr);
      assert(realSize >= sz);
      assert((sz < 16) || (realSize <= 2 * sz));
      register_malloc(realSize, ptr, false);  // false -> invoked from C/C++
    }
    return ptr;
  }

 private:
  // Prevent copying and assignment.
  SampleHeap(const SampleHeap&) = delete;
  SampleHeap& operator=(const SampleHeap&) = delete;

  static auto& mallocTriggered() {
    static std::atomic<uint64_t> _mallocTriggered{0};
    return _mallocTriggered;
  }
  static auto& freeTriggered() {
    static std::atomic<uint64_t> _freeTriggered{0};
    return _freeTriggered;
  }

  counterType _pythonCount{0};
  counterType _cCount{0};

  void* _lastMallocTrigger;
  bool _freedLastMallocTrigger;
#if 0
  typedef PoissonSampler Sampler;
#warning "Experimental use only: Poisson sampler"
#else
  typedef ThresholdSampler Sampler;
#endif


  static HL::PosixLock& get_signal_init_lock() {
    static HL::PosixLock signal_init_lock;
    return signal_init_lock;
  }
};

#endif
