# rpmalloc - General Purpose Memory Allocator
This library provides a cross platform lock free thread caching 16-byte aligned memory allocator implemented in C.
This is a fork of rpmalloc 1.4.5.

Platforms currently supported:

- Windows
- MacOS
- iOS
- Linux
- Android
- Haiku

The code should be easily portable to any platform with atomic operations and an mmap-style virtual memory management API. The API used to map/unmap memory pages can be configured in runtime to a custom implementation and mapping granularity/size.

This library is put in the public domain; you can redistribute it and/or modify it without any restrictions. Or, if you choose, you can use it under the MIT license.

# Performance
We believe rpmalloc is faster than most popular memory allocators like tcmalloc, hoard, ptmalloc3 and others without causing extra allocated memory overhead in the thread caches compared to these allocators. We also believe the implementation to be easier to read and modify compared to these allocators, as it is a single source file of ~3000 lines of C code. All allocations have a natural 16-byte alignment.

Contained in a parallel repository is a benchmark utility that performs interleaved unaligned allocations and deallocations (both in-thread and cross-thread) in multiple threads. It measures number of memory operations performed per CPU second, as well as memory overhead by comparing the virtual memory mapped with the number of bytes requested in allocation calls. The setup of number of thread, cross-thread deallocation rate and allocation size limits is configured by command line arguments.

https://github.com/mjansson/rpmalloc-benchmark

Below is an example performance comparison chart of rpmalloc and other popular allocator implementations, with default configurations used.

![Ubuntu 16.10, random [16, 8000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=301017877&format=image)

The benchmark producing these numbers were run on an Ubuntu 16.10 machine with 8 logical cores (4 physical, HT). The actual numbers are not to be interpreted as absolute performance figures, but rather as relative comparisons between the different allocators. For additional benchmark results, see the [BENCHMARKS](BENCHMARKS.md) file.

Configuration of the thread and global caches can be important depending on your use pattern. See [CACHE](CACHE.md) for a case study and some comments/guidelines.

# Required functions

Before calling any other function in the API, you __MUST__ call the initialization function, either __rpmalloc_initialize__ or __rpmalloc_initialize_config__, or you will get undefined behaviour when calling other rpmalloc entry point.

Before terminating your use of the allocator, you __SHOULD__ call __rpmalloc_finalize__ in order to release caches and unmap virtual memory, as well as prepare the allocator for global scope cleanup at process exit or dynamic library unload depending on your use case.

# Using
The easiest way to use the library is simply adding __rpmalloc.[h|c]__ to your project and compile them along with your sources. This contains only the rpmalloc specific entry points and does not provide internal hooks to process and/or thread creation at the moment. You are required to call these functions from your own code in order to initialize and finalize the allocator in your process and threads:

__rpmalloc_initialize__ : Call at process start to initialize the allocator

__rpmalloc_initialize_config__ : Optional entry point to call at process start to initialize the allocator with a custom memory mapping backend, memory page size and mapping granularity.

__rpmalloc_finalize__: Call at process exit to finalize the allocator

__rpmalloc_thread_initialize__: Call at each thread start to initialize the thread local data for the allocator

__rpmalloc_thread_finalize__: Call at each thread exit to finalize and release thread cache back to global cache

__rpmalloc_config__: Get the current runtime configuration of the allocator

Then simply use the __rpmalloc__/__rpfree__ and the other malloc style replacement functions. Remember all allocations are 16-byte aligned, so no need to call the explicit rpmemalign/rpaligned_alloc/rpposix_memalign functions unless you need greater alignment, they are simply wrappers to make it easier to replace in existing code.

If you wish to override the standard library malloc family of functions and have automatic initialization/finalization of process and threads, define __ENABLE_OVERRIDE__ to non-zero which will include the `malloc.c` file in compilation of __rpmalloc.c__, and then rebuild the library or your project where you added the rpmalloc source. If you compile rpmalloc as a separate library you must make the linker use the override symbols from the library by referencing at least one symbol. The easiest way is to simply include `rpmalloc.h` in at least one source file and call `rpmalloc_linker_reference` somewhere - it's a dummy empty function. On Windows platforms and C++ overrides you have to `#include <rpnew.h>` in at least one source file and also manually handle the initialize/finalize of the process and all threads. The list of libc entry points replaced may not be complete, use libc/stdc++ replacement only as a convenience for testing the library on an existing code base, not a final solution.

For explicit first class heaps, see the __rpmalloc_heap_*__ API under [first class heaps](#first-class-heaps) section, requiring __RPMALLOC_FIRST_CLASS_HEAPS__ tp be defined to 1.

# Building
To compile as a static library run the configure python script which generates a Ninja build script, then build using ninja. The ninja build produces two static libraries, one named `rpmalloc` and one named `rpmallocwrap`, where the latter includes the libc entry point overrides.

The configure + ninja build also produces two shared object/dynamic libraries. The `rpmallocwrap` shared library can be used with LD_PRELOAD/DYLD_INSERT_LIBRARIES to inject in a preexisting binary, replacing any malloc/free family of function calls. This is only implemented for Linux and macOS targets. The list of libc entry points replaced may not be complete, use preloading as a convenience for testing the library on an existing binary, not a final solution. The dynamic library also provides automatic init/fini of process and threads for all platforms.

The latest stable release is available in the master branch. For latest development code, use the develop branch.

# Cache configuration options
Free memory pages are cached both per thread and in a global cache for all threads. The size of the thread caches is determined by an adaptive scheme where each cache is limited by a percentage of the maximum allocation count of the corresponding size class. The size of the global caches is determined by a multiple of the maximum of all thread caches. The factors controlling the cache sizes can be set by editing the individual defines in the `rpmalloc.c` source file for fine tuned control.

__ENABLE_UNLIMITED_CACHE__: By default defined to 0, set to 1 to make all caches infinite, i.e never release spans to global cache unless thread finishes and never unmap memory pages back to the OS. Highest performance but largest memory overhead.

__ENABLE_UNLIMITED_GLOBAL_CACHE__: By default defined to 0, set to 1 to make global caches infinite, i.e never unmap memory pages back to the OS.

__ENABLE_UNLIMITED_THREAD_CACHE__: By default defined to 0, set to 1 to make thread caches infinite, i.e never release spans to global cache unless thread finishes.

__ENABLE_GLOBAL_CACHE__: By default defined to 1, enables the global cache shared between all threads. Set to 0 to disable the global cache and directly unmap pages evicted from the thread cache.

__ENABLE_THREAD_CACHE__: By default defined to 1, enables the per-thread cache. Set to 0 to disable the thread cache and directly unmap pages no longer in use (also disables the global cache).

__ENABLE_ADAPTIVE_THREAD_CACHE__: Introduces a simple heuristics in the thread cache size, keeping 25% of the high water mark for each span count class.

# Other configuration options
Detailed statistics are available if __ENABLE_STATISTICS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`. This will cause a slight overhead in runtime to collect statistics for each memory operation, and will also add 4 bytes overhead per allocation to track sizes.

Integer safety checks on all calls are enabled if __ENABLE_VALIDATE_ARGS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`. If enabled, size arguments to the global entry points are verified not to cause integer overflows in calculations.

Asserts are enabled if __ENABLE_ASSERTS__ is defined to 1 (default is 0, or disabled), either on compile command line or by setting the value in `rpmalloc.c`.

To include __malloc.c__ in compilation and provide overrides of standard library malloc entry points define __ENABLE_OVERRIDE__ to 1. To enable automatic initialization of finalization of process and threads in order to preload the library into executables using standard library malloc, define __ENABLE_PRELOAD__ to 1.

To enable the runtime configurable memory page and span sizes, define __RPMALLOC_CONFIGURABLE__ to 1. By default, memory page size is determined by system APIs and memory span size is set to 64KiB.

To enable support for first class heaps, define __RPMALLOC_FIRST_CLASS_HEAPS__ to 1. By default, the first class heap API is disabled.

# Huge pages
The allocator has support for huge/large pages on Windows, Linux and MacOS. To enable it, pass a non-zero value in the config value `enable_huge_pages` when initializing the allocator with `rpmalloc_initialize_config`. If the system does not support huge pages it will be automatically disabled. You can query the status by looking at `enable_huge_pages` in the config returned from a call to `rpmalloc_config` after initialization is done.

# Quick overview
The allocator is similar in spirit to tcmalloc from the [Google Performance Toolkit](https://github.com/gperftools/gperftools). It uses separate heaps for each thread and partitions memory blocks according to a preconfigured set of size classes, up to 2MiB. Larger blocks are mapped and unmapped directly. Allocations for different size classes will be served from different set of memory pages, each "span" of pages is dedicated to one size class. Spans of pages can flow between threads when the thread cache overflows and are released to a global cache, or when the thread ends. Unlike tcmalloc, single blocks do not flow between threads, only entire spans of pages.

# Implementation details
The allocator is based on a fixed but configurable page alignment (defaults to 64KiB) and 16 byte block alignment, where all runs of memory pages (spans) are mapped to this alignment boundary. On Windows this is automatically guaranteed up to 64KiB by the VirtualAlloc granularity, and on mmap systems it is achieved by oversizing the mapping and aligning the returned virtual memory address to the required boundaries. By aligning to a fixed size the free operation can locate the header of the memory span without having to do a table lookup (as tcmalloc does) by simply masking out the low bits of the address (for 64KiB this would be the low 16 bits).

Memory blocks are divided into three categories. For 64KiB span size/alignment the small blocks are [16, 1024] bytes, medium blocks (1024, 32256] bytes, and large blocks (32256, 2097120] bytes. The three categories are further divided in size classes. If the span size is changed, the small block classes remain but medium blocks go from (1024, span size] bytes.

Small blocks have a size class granularity of 16 bytes each in 64 buckets. Medium blocks have a granularity of 512 bytes, 61 buckets (default). Large blocks have the same granularity as the configured span size (default 64KiB). All allocations are fitted to these size class boundaries (an allocation of 36 bytes will allocate a block of 48 bytes). Each small and medium size class has an associated span (meaning a contiguous set of memory pages) configuration describing how many pages the size class will allocate each time the cache is empty and a new allocation is requested.

Spans for small and medium blocks are cached in four levels to avoid calls to map/unmap memory pages. The first level is a per thread single active span for each size class. The second level is a per thread list of partially free spans for each size class. The third level is a per thread list of free spans. The fourth level is a global list of free spans.

Each span for a small and medium size class keeps track of how many blocks are allocated/free, as well as a list of which blocks that are free for allocation. To avoid locks, each span is completely owned by the allocating thread, and all cross-thread deallocations will be deferred to the owner thread through a separate free list per span.

Large blocks, or super spans, are cached in two levels. The first level is a per thread list of free super spans. The second level is a global list of free super spans.

# Memory mapping
By default the allocator uses OS APIs to map virtual memory pages as needed, either `VirtualAlloc` on Windows or `mmap` on POSIX systems. If you want to use your own custom memory mapping provider you can use __rpmalloc_initialize_config__ and pass function pointers to map and unmap virtual memory. These function should reserve and free the requested number of bytes.

The returned memory address from the memory map function MUST be aligned to the memory page size and the memory span size (which ever is larger), both of which is configurable. Either provide the page and span sizes during initialization using __rpmalloc_initialize_config__, or use __rpmalloc_config__ to find the required alignment which is equal to the maximum of page and span size. The span size MUST be a power of two in [4096, 262144] range, and be a multiple or divisor of the memory page size.

Memory mapping requests are always done in multiples of the memory page size. You can specify a custom page size when initializing rpmalloc with __rpmalloc_initialize_config__, or pass 0 to let rpmalloc determine the system memory page size using OS APIs. The page size MUST be a power of two.

To reduce system call overhead, memory spans are mapped in batches controlled by the `span_map_count` configuration variable (which defaults to the `DEFAULT_SPAN_MAP_COUNT` value if 0, which in turn is sized according to the cache configuration define, defaulting to 64). If the memory page size is larger than the span size, the number of spans to map in a single call will be adjusted to guarantee a multiple of the page size, and the spans will be kept mapped until the entire span range can be unmapped in one call (to avoid trying to unmap partial pages).

On macOS and iOS mmap requests are tagged with tag 240 for easy identification with the vmmap tool.

# Span breaking
Super spans (spans a multiple > 1 of the span size) can be subdivided into smaller spans to fulfill a need to map a new span of memory. By default the allocator will greedily grab and break any larger span from the available caches before mapping new virtual memory. However, spans can currently not be glued together to form larger super spans again. Subspans can traverse the cache and be used by different threads individually.

A span that is a subspan of a larger super span can be individually decommitted to reduce physical memory pressure when the span is evicted from caches and scheduled to be unmapped. The entire original super span will keep track of the subspans it is broken up into, and when the entire range is decommitted the super span will be unmapped. This allows platforms like Windows that require the entire virtual memory range that was mapped in a call to VirtualAlloc to be unmapped in one call to VirtualFree, while still decommitting individual pages in subspans (if the page size is smaller than the span size).

If you use a custom memory map/unmap function you need to take this into account by looking at the `release` parameter given to the `memory_unmap` function. It is set to 0 for decommitting individual pages and the total super span byte size for finally releasing the entire super span memory range.

# Memory fragmentation
There is no memory fragmentation by the allocator in the sense that it will not leave unallocated and unusable "holes" in the memory pages by calls to allocate and free blocks of different sizes. This is due to the fact that the memory pages allocated for each size class is split up in perfectly aligned blocks which are not reused for a request of a different size. The block freed by a call to `rpfree` will always be immediately available for an allocation request within the same size class.

However, there is memory fragmentation in the meaning that a request for x bytes followed by a request of y bytes where x and y are at least one size class different in size will return blocks that are at least one memory page apart in virtual address space. Only blocks of the same size will potentially be within the same memory page span.

rpmalloc keeps an "active span" and free list for each size class. This leads to back-to-back allocations will most likely be served from within the same span of memory pages (unless the span runs out of free blocks). The rpmalloc implementation will also use any "holes" in memory pages in semi-filled spans before using a completely free span.

# First class heaps
rpmalloc provides a first class heap type with explicit heap control API. Heaps are maintained with calls to __rpmalloc_heap_acquire__ and __rpmalloc_heap_release__ and allocations/frees are done with __rpmalloc_heap_alloc__ and __rpmalloc_heap_free__. See the `rpmalloc.h` documentation for the full list of functions in the heap API. The main use case of explicit heap control is to scope allocations in a heap and release everything with a single call to __rpmalloc_heap_free_all__ without having to maintain ownership of memory blocks. Note that the heap API is not thread-safe, the caller must make sure that each heap is only used in a single thread at any given time.

# Producer-consumer scenario
Compared to the some other allocators, rpmalloc does not suffer as much from a producer-consumer thread scenario where one thread allocates memory blocks and another thread frees the blocks. In some allocators the free blocks need to traverse both the thread cache of the thread doing the free operations as well as the global cache before being reused in the allocating thread. In rpmalloc the freed blocks will be reused as soon as the allocating thread needs to get new spans from the thread cache. This enables faster release of completely freed memory pages as blocks in a memory page will not be aliased between different owning threads.

# Best case scenarios
Threads that keep ownership of allocated memory blocks within the thread and free the blocks from the same thread will have optimal performance.

Threads that have allocation patterns where the difference in memory usage high and low water marks fit within the thread cache thresholds in the allocator will never touch the global cache except during thread init/fini and have optimal performance. Tweaking the cache limits can be done on a per-size-class basis.

# Worst case scenarios
Since each thread cache maps spans of memory pages per size class, a thread that allocates just a few blocks of each size class (16, 32, ...) for many size classes will never fill each bucket, and thus map a lot of memory pages while only using a small fraction of the mapped memory. However, the wasted memory will always be less than 4KiB (or the configured memory page size) per size class as each span is initialized one memory page at a time. The cache for free spans will be reused by all size classes.

Threads that perform a lot of allocations and deallocations in a pattern that have a large difference in high and low water marks, and that difference is larger than the thread cache size, will put a lot of contention on the global cache. What will happen is the thread cache will overflow on each low water mark causing pages to be released to the global cache, then underflow on high water mark causing pages to be re-acquired from the global cache. This can be mitigated by changing the __MAX_SPAN_CACHE_DIVISOR__ define in the source code (at the cost of higher average memory overhead).

# Caveats
VirtualAlloc has an internal granularity of 64KiB. However, mmap lacks this granularity control, and the implementation instead oversizes the memory mapping with configured span size to be able to always return a memory area with the required alignment. Since the extra memory pages are never touched this will not result in extra committed physical memory pages, but rather only increase virtual memory address space.

All entry points assume the passed values are valid, for example passing an invalid pointer to free would most likely result in a segmentation fault. __The library does not try to guard against errors!__.

To support global scope data doing dynamic allocation/deallocation such as C++ objects with custom constructors and destructors, the call to __rpmalloc_finalize__ will not completely terminate the allocator but rather empty all caches and put the allocator in finalization mode. Once this call has been made, the allocator is no longer thread safe and expects all remaining calls to originate from global data destruction on main thread. Any spans or heaps becoming free during this phase will be immediately unmapped to allow correct teardown of the process or dynamic library without any leaks.

# Other languages

[Johan Andersson](https://github.com/repi) at Embark has created a Rust wrapper available at [rpmalloc-rs](https://github.com/EmbarkStudios/rpmalloc-rs)

[Stas Denisov](https://github.com/nxrighthere) has created a C# wrapper available at [Rpmalloc-CSharp](https://github.com/nxrighthere/Rpmalloc-CSharp)

# License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>


You can also use this software under the MIT license if public domain is
not recognized in your country


The MIT License (MIT)

Copyright (c) 2017 Mattias Jansson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
