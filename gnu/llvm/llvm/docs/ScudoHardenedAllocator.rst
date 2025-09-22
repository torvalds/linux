========================
Scudo Hardened Allocator
========================

.. contents::
   :local:
   :depth: 2

Introduction
============

The Scudo Hardened Allocator is a user-mode allocator, originally based on LLVM
Sanitizers'
`CombinedAllocator <https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_allocator_combined.h>`_.
It aims at providing additional mitigation against heap based vulnerabilities,
while maintaining good performance. Scudo is currently the default allocator in
`Fuchsia <https://fuchsia.dev/>`_, and in `Android <https://www.android.com/>`_
since Android 11.

The name "Scudo" comes from the Italian word for
`shield <https://www.collinsdictionary.com/dictionary/italian-english/scudo>`_
(and Escudo in Spanish).

Design
======

Allocator
---------
Scudo was designed with security in mind, but aims at striking a good balance
between security and performance. It was designed to be highly tunable and
configurable, and while we provide some default configurations, we encourage
consumers to come up with the parameters that will work best for their use
cases.

The allocator combines several components that serve distinct purposes:

- the Primary allocator: fast and efficient, it services smaller allocation
  sizes by carving reserved memory regions into blocks of identical size. There
  are currently two Primary allocators implemented, specific to 32 and 64 bit
  architectures. It is configurable via compile time options.

- the Secondary allocator: slower, it services larger allocation sizes via the
  memory mapping primitives of the underlying operating system. Secondary backed
  allocations are surrounded by Guard Pages. It is also configurable via compile
  time options.

- the thread specific data Registry: defines how local caches operate for each
  thread. There are currently two models implemented: the exclusive model where
  each thread holds its own caches (using the ELF TLS); or the shared model
  where threads share a fixed size pool of caches.

- the Quarantine: offers a way to delay the deallocation operations, preventing
  blocks to be immediately available for reuse. Blocks held will be recycled
  once certain size criteria are reached. This is essentially a delayed freelist
  which can help mitigate some use-after-free situations. This feature is fairly
  costly in terms of performance and memory footprint, is mostly controlled by
  runtime options and is disabled by default.

Allocations Header
------------------
Every chunk of heap memory returned to an application by the allocator will be
preceded by a header. This has two purposes:

- being to store various information about the chunk, that can be leveraged to
  ensure consistency of the heap operations;

- being able to detect potential corruption. For this purpose, the header is
  checksummed and corruption of the header will be detected when said header is
  accessed (note that if the corrupted header is not accessed, the corruption
  will remain undetected).

The following information is stored in the header:

- the class ID for that chunk, which identifies the region where the chunk
  resides for Primary backed allocations, or 0 for Secondary backed allocations;

- the state of the chunk (available, allocated or quarantined);

- the allocation type (malloc, new, new[] or memalign), to detect potential
  mismatches in the allocation APIs used;

- the size (Primary) or unused bytes amount (Secondary) for that chunk, which is
  necessary for reallocation or sized-deallocation operations;

- the offset of the chunk, which is the distance in bytes from the beginning of
  the returned chunk to the beginning of the backend allocation (the "block");

- the 16-bit checksum;

This header fits within 8 bytes on all platforms supported, and contributes to a
small overhead for each allocation.

The checksum is computed using a CRC32 (made faster with hardware support)
of the global secret, the chunk pointer itself, and the 8 bytes of header with
the checksum field zeroed out. It is not intended to be cryptographically
strong.

The header is atomically loaded and stored to prevent races. This is important
as two consecutive chunks could belong to different threads. We work on local
copies and use compare-exchange primitives to update the headers in the heap
memory, and avoid any type of double-fetching.

Randomness
----------
Randomness is a critical factor to the additional security provided by the
allocator. The allocator trusts the memory mapping primitives of the OS to
provide pages at (mostly) non-predictable locations in memory, as well as the
binaries to be compiled with ASLR. In the event one of those assumptions is
incorrect, the security will be greatly reduced. Scudo further randomizes how
blocks are allocated in the Primary, can randomize how caches are assigned to
threads.

Memory reclaiming
-----------------
Primary and Secondary allocators have different behaviors with regard to
reclaiming. While Secondary mapped allocations can be unmapped on deallocation,
it isn't the case for the Primary, which could lead to a steady growth of the
RSS of a process. To counteract this, if the underlying OS allows it, pages
that are covered by contiguous free memory blocks in the Primary can be
released: this generally means they won't count towards the RSS of a process and
be zero filled on subsequent accesses). This is done in the deallocation path,
and several options exist to tune this behavior.

Usage
=====

Platform
--------
If using Fuchsia or an Android version greater than 11, your memory allocations
are already service by Scudo (note that Android Svelte configurations still use
jemalloc).

Library
-------
The allocator static library can be built from the LLVM tree thanks to the
``scudo_standalone`` CMake rule. The associated tests can be exercised thanks to
the ``check-scudo_standalone`` CMake rule.

Linking the static library to your project can require the use of the
``whole-archive`` linker flag (or equivalent), depending on your linker.
Additional flags might also be necessary.

Your linked binary should now make use of the Scudo allocation and deallocation
functions.

You may also build Scudo like this:

.. code:: console

  cd $LLVM/compiler-rt/lib
  clang++ -fPIC -std=c++17 -msse4.2 -O2 -pthread -shared \
    -I scudo/standalone/include \
    scudo/standalone/*.cpp \
    -o $HOME/libscudo.so

and then use it with existing binaries as follows:

.. code:: console

  LD_PRELOAD=$HOME/libscudo.so ./a.out

Clang
-----
With a recent version of Clang (post rL317337), the "old" version of the
allocator can be linked with a binary at compilation using the
``-fsanitize=scudo`` command-line argument, if the target platform is supported.
Currently, the only other sanitizer Scudo is compatible with is UBSan
(eg: ``-fsanitize=scudo,undefined``). Compiling with Scudo will also enforce
PIE for the output binary.

We will transition this to the standalone Scudo version in the future.

Options
-------
Several aspects of the allocator can be configured on a per process basis
through the following ways:

- at compile time, by defining ``SCUDO_DEFAULT_OPTIONS`` to the options string
  you want set by default;

- by defining a ``__scudo_default_options`` function in one's program that
  returns the options string to be parsed. Said function must have the following
  prototype: ``extern "C" const char* __scudo_default_options(void)``, with a
  default visibility. This will override the compile time define;

- through the environment variable SCUDO_OPTIONS, containing the options string
  to be parsed. Options defined this way will override any definition made
  through ``__scudo_default_options``.

- via the standard ``mallopt`` `API <https://man7.org/linux/man-pages/man3/mallopt.3.html>`_,
  using parameters that are Scudo specific.

When dealing with the options string, it follows a syntax similar to ASan, where
distinct options can be assigned in the same string, separated by colons.

For example, using the environment variable:

.. code:: console

  SCUDO_OPTIONS="delete_size_mismatch=false:release_to_os_interval_ms=-1" ./a.out

Or using the function:

.. code:: cpp

  extern "C" const char *__scudo_default_options() {
    return "delete_size_mismatch=false:release_to_os_interval_ms=-1";
  }


The following "string" options are available:

+---------------------------------+----------------+-------------------------------------------------+
| Option                          | Default        | Description                                     |
+---------------------------------+----------------+-------------------------------------------------+
| quarantine_size_kb              | 0              | The size (in Kb) of quarantine used to delay    |
|                                 |                | the actual deallocation of chunks. Lower value  |
|                                 |                | may reduce memory usage but decrease the        |
|                                 |                | effectiveness of the mitigation; a negative     |
|                                 |                | value will fallback to the defaults. Setting    |
|                                 |                | *both* this and thread_local_quarantine_size_kb |
|                                 |                | to zero will disable the quarantine entirely.   |
+---------------------------------+----------------+-------------------------------------------------+
| quarantine_max_chunk_size       | 0              | Size (in bytes) up to which chunks can be       |
|                                 |                | quarantined.                                    |
+---------------------------------+----------------+-------------------------------------------------+
| thread_local_quarantine_size_kb | 0              | The size (in Kb) of per-thread cache use to     |
|                                 |                | offload the global quarantine. Lower value may  |
|                                 |                | reduce memory usage but might increase          |
|                                 |                | contention on the global quarantine. Setting    |
|                                 |                | *both* this and quarantine_size_kb to zero will |
|                                 |                | disable the quarantine entirely.                |
+---------------------------------+----------------+-------------------------------------------------+
| dealloc_type_mismatch           | false          | Whether or not we report errors on              |
|                                 |                | malloc/delete, new/free, new/delete[], etc.     |
+---------------------------------+----------------+-------------------------------------------------+
| delete_size_mismatch            | true           | Whether or not we report errors on mismatch     |
|                                 |                | between sizes of new and delete.                |
+---------------------------------+----------------+-------------------------------------------------+
| zero_contents                   | false          | Whether or not we zero chunk contents on        |
|                                 |                | allocation.                                     |
+---------------------------------+----------------+-------------------------------------------------+
| pattern_fill_contents           | false          | Whether or not we fill chunk contents with a    |
|                                 |                | byte pattern on allocation.                     |
+---------------------------------+----------------+-------------------------------------------------+
| may_return_null                 | true           | Whether or not a non-fatal failure can return a |
|                                 |                | NULL pointer (as opposed to terminating).       |
+---------------------------------+----------------+-------------------------------------------------+
| release_to_os_interval_ms       | 5000           | The minimum interval (in ms) at which a release |
|                                 |                | can be attempted (a negative value disables     |
|                                 |                | reclaiming).                                    |
+---------------------------------+----------------+-------------------------------------------------+
| allocation_ring_buffer_size     | 32768          | If stack trace collection is requested, how     |
|                                 |                | many previous allocations to keep in the        |
|                                 |                | allocation ring buffer.                         |
|                                 |                |                                                 |
|                                 |                | This buffer is used to provide allocation and   |
|                                 |                | deallocation stack traces for MTE fault         |
|                                 |                | reports. The larger the buffer, the more        |
|                                 |                | unrelated allocations can happen between        |
|                                 |                | (de)allocation and the fault.                   |
|                                 |                | If your sync-mode MTE faults do not have        |
|                                 |                | (de)allocation stack traces, try increasing the |
|                                 |                | buffer size.                                    |
|                                 |                |                                                 |
|                                 |                | Stack trace collection can be requested using   |
|                                 |                | the scudo_malloc_set_track_allocation_stacks    |
|                                 |                | function.                                       |
+---------------------------------+----------------+-------------------------------------------------+

Additional flags can be specified, for example if Scudo if compiled with
`GWP-ASan <https://llvm.org/docs/GwpAsan.html>`_ support.

The following "mallopt" options are available (options are defined in
``include/scudo/interface.h``):

+---------------------------+-------------------------------------------------------+
| Option                    | Description                                           |
+---------------------------+-------------------------------------------------------+
| M_DECAY_TIME              | Sets the release interval option to the specified     |
|                           | value (Android only allows 0 or 1 to respectively set |
|                           | the interval to the minimum and maximum value as      |
|                           | specified at compile time).                           |
+---------------------------+-------------------------------------------------------+
| M_PURGE                   | Forces immediate memory reclaiming but does not       |
|                           | reclaim everything. For smaller size classes, there   |
|                           | is still some memory that is not reclaimed due to the |
|                           | extra time it takes and the small amount of memory    |
|                           | that can be reclaimed.                                |
|                           | The value is ignored.                                 |
+---------------------------+-------------------------------------------------------+
| M_PURGE_ALL               | Same as M_PURGE but will force release all possible   |
|                           | memory regardless of how long it takes.               |
|                           | The value is ignored.                                 |
+---------------------------+-------------------------------------------------------+
| M_MEMTAG_TUNING           | Tunes the allocator's choice of memory tags to make   |
|                           | it more likely that a certain class of memory errors  |
|                           | will be detected. The value argument should be one of |
|                           | the enumerators of ``scudo_memtag_tuning``.           |
+---------------------------+-------------------------------------------------------+
| M_THREAD_DISABLE_MEM_INIT | Tunes the per-thread memory initialization, 0 being   |
|                           | the normal behavior, 1 disabling the automatic heap   |
|                           | initialization.                                       |
+---------------------------+-------------------------------------------------------+
| M_CACHE_COUNT_MAX         | Set the maximum number of entries than can be cached  |
|                           | in the Secondary cache.                               |
+---------------------------+-------------------------------------------------------+
| M_CACHE_SIZE_MAX          | Sets the maximum size of entries that can be cached   |
|                           | in the Secondary cache.                               |
+---------------------------+-------------------------------------------------------+
| M_TSDS_COUNT_MAX          | Increases the maximum number of TSDs that can be used |
|                           | up to the limit specified at compile time.            |
+---------------------------+-------------------------------------------------------+

Error Types
===========

The allocator will output an error message, and potentially terminate the
process, when an unexpected behavior is detected. The output usually starts with
``"Scudo ERROR:"`` followed by a short summary of the problem that occurred as
well as the pointer(s) involved. Once again, Scudo is meant to be a mitigation,
and might not be the most useful of tools to help you root-cause the issue,
please consider `ASan <https://github.com/google/sanitizers/wiki/AddressSanitizer>`_
for this purpose.

Here is a list of the current error messages and their potential cause:

- ``"corrupted chunk header"``: the checksum verification of the chunk header
  has failed. This is likely due to one of two things: the header was
  overwritten (partially or totally), or the pointer passed to the function is
  not a chunk at all;

- ``"race on chunk header"``: two different threads are attempting to manipulate
  the same header at the same time. This is usually symptomatic of a
  race-condition or general lack of locking when performing operations on that
  chunk;

- ``"invalid chunk state"``: the chunk is not in the expected state for a given
  operation, eg: it is not allocated when trying to free it, or it's not
  quarantined when trying to recycle it, etc. A double-free is the typical
  reason this error would occur;

- ``"misaligned pointer"``: we strongly enforce basic alignment requirements, 8
  bytes on 32-bit platforms, 16 bytes on 64-bit platforms. If a pointer passed
  to our functions does not fit those, something is definitely wrong.

- ``"allocation type mismatch"``: when the optional deallocation type mismatch
  check is enabled, a deallocation function called on a chunk has to match the
  type of function that was called to allocate it. Security implications of such
  a mismatch are not necessarily obvious but situational at best;

- ``"invalid sized delete"``: when the C++14 sized delete operator is used, and
  the optional check enabled, this indicates that the size passed when
  deallocating a chunk is not congruent with the one requested when allocating
  it. This is likely to be a `compiler issue <https://software.intel.com/en-us/forums/intel-c-compiler/topic/783942>`_,
  as was the case with Intel C++ Compiler, or some type confusion on the object
  being deallocated;

- ``"RSS limit exhausted"``: the maximum RSS optionally specified has been
  exceeded;

Several other error messages relate to parameter checking on the libc allocation
APIs and are fairly straightforward to understand.

