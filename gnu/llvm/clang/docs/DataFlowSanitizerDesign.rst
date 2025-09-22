DataFlowSanitizer Design Document
=================================

This document sets out the design for DataFlowSanitizer, a general
dynamic data flow analysis.  Unlike other Sanitizer tools, this tool is
not designed to detect a specific class of bugs on its own. Instead,
it provides a generic dynamic data flow analysis framework to be used
by clients to help detect application-specific issues within their
own code.

DataFlowSanitizer is a program instrumentation which can associate
a number of taint labels with any data stored in any memory region
accessible by the program. The analysis is dynamic, which means that
it operates on a running program, and tracks how the labels propagate
through that program.

Use Cases
---------

This instrumentation can be used as a tool to help monitor how data
flows from a program's inputs (sources) to its outputs (sinks).
This has applications from a privacy/security perspective in that
one can audit how a sensitive data item is used within a program and
ensure it isn't exiting the program anywhere it shouldn't be.

Interface
---------

A number of functions are provided which will attach taint labels to
memory regions and extract the set of labels associated with a
specific memory region. These functions are declared in the header
file ``sanitizer/dfsan_interface.h``.

.. code-block:: c

  /// Sets the label for each address in [addr,addr+size) to \c label.
  void dfsan_set_label(dfsan_label label, void *addr, size_t size);

  /// Sets the label for each address in [addr,addr+size) to the union of the
  /// current label for that address and \c label.
  void dfsan_add_label(dfsan_label label, void *addr, size_t size);

  /// Retrieves the label associated with the given data.
  ///
  /// The type of 'data' is arbitrary.  The function accepts a value of any type,
  /// which can be truncated or extended (implicitly or explicitly) as necessary.
  /// The truncation/extension operations will preserve the label of the original
  /// value.
  dfsan_label dfsan_get_label(long data);

  /// Retrieves the label associated with the data at the given address.
  dfsan_label dfsan_read_label(const void *addr, size_t size);

  /// Returns whether the given label contains the label elem.
  int dfsan_has_label(dfsan_label label, dfsan_label elem);

  /// Computes the union of \c l1 and \c l2, resulting in a union label.
  dfsan_label dfsan_union(dfsan_label l1, dfsan_label l2);

  /// Flushes the DFSan shadow, i.e. forgets about all labels currently associated
  /// with the application memory.  Use this call to start over the taint tracking
  /// within the same process.
  ///
  /// Note: If another thread is working with tainted data during the flush, that
  /// taint could still be written to shadow after the flush.
  void dfsan_flush(void);

The following functions are provided to check origin tracking status and results.

.. code-block:: c

  /// Retrieves the immediate origin associated with the given data. The returned
  /// origin may point to another origin.
  ///
  /// The type of 'data' is arbitrary. The function accepts a value of any type,
  /// which can be truncated or extended (implicitly or explicitly) as necessary.
  /// The truncation/extension operations will preserve the label of the original
  /// value.
  dfsan_origin dfsan_get_origin(long data);

  /// Retrieves the very first origin associated with the data at the given
  /// address.
  dfsan_origin dfsan_get_init_origin(const void *addr);

  /// Prints the origin trace of the label at the address `addr` to stderr. It also
  /// prints description at the beginning of the trace. If origin tracking is not
  /// on, or the address is not labeled, it prints nothing.
  void dfsan_print_origin_trace(const void *addr, const char *description);

  /// Prints the origin trace of the label at the address `addr` to a pre-allocated
  /// output buffer. If origin tracking is not on, or the address is`
  /// not labeled, it prints nothing.
  ///
  /// `addr` is the tainted memory address whose origin we are printing.
  /// `description` is a description printed at the beginning of the trace.
  /// `out_buf` is the output buffer to write the results to. `out_buf_size` is
  /// the size of `out_buf`. The function returns the number of symbols that
  /// should have been written to `out_buf` (not including trailing null byte '\0').
  /// Thus, the string is truncated iff return value is not less than `out_buf_size`.
  size_t dfsan_sprint_origin_trace(const void *addr, const char *description,
                                   char *out_buf, size_t out_buf_size);

  /// Returns the value of `-dfsan-track-origins`.
  int dfsan_get_track_origins(void);

The following functions are provided to register hooks called by custom wrappers.

.. code-block:: c

  /// Sets a callback to be invoked on calls to `write`.  The callback is invoked
  /// before the write is done. The write is not guaranteed to succeed when the
  /// callback executes. Pass in NULL to remove any callback.
  typedef void (*dfsan_write_callback_t)(int fd, const void *buf, size_t count);
  void dfsan_set_write_callback(dfsan_write_callback_t labeled_write_callback);

  /// Callbacks to be invoked on calls to `memcmp` or `strncmp`.
  void dfsan_weak_hook_memcmp(void *caller_pc, const void *s1, const void *s2,
                              size_t n, dfsan_label s1_label,
                              dfsan_label s2_label, dfsan_label n_label);
  void dfsan_weak_hook_strncmp(void *caller_pc, const char *s1, const char *s2,
                              size_t n, dfsan_label s1_label,
                              dfsan_label s2_label, dfsan_label n_label);

Taint label representation
--------------------------

We use an 8-bit unsigned integer for the representation of a
label. The label identifier 0 is special, and means that the data item
is unlabelled. This is optimizing for low CPU and code size overhead
of the instrumentation. When a label union operation is requested at a
join point (any arithmetic or logical operation with two or more
operands, such as addition), we can simply OR the two labels in O(1).

Users are responsible for managing the 8 integer labels (i.e., keeping
track of what labels they have used so far, picking one that is yet
unused, etc).

Origin tracking trace representation
------------------------------------

An origin tracking trace is a list of chains. Each chain has a stack trace
where the DFSan runtime records a label propagation, and a pointer to its
previous chain. The very first chain does not point to any chain.

Every four 4-bytes aligned application bytes share a 4-byte origin trace ID. A
4-byte origin trace ID contains a 4-bit depth and a 28-bit hash ID of a chain.

A chain ID is calculated as a hash from a chain structure. A chain structure
contains a stack ID and the previous chain ID. The chain head has a zero
previous chain ID. A stack ID is a hash from a stack trace. The 4-bit depth
limits the maximal length of a path. The environment variable ``origin_history_size``
can set the depth limit. Non-positive values mean unlimited. Its default value
is 16. When reaching the limit, origin tracking ignores following propagation
chains.

The first chain of a trace starts by `dfsan_set_label` with non-zero labels. A
new chain is appended at the end of a trace at stores or memory transfers when
``-dfsan-track-origins`` is 1. Memory transfers include LLVM memory transfer
instructions, glibc memcpy and memmove. When ``-dfsan-track-origins`` is 2, a
new chain is also appended at loads.

Other instructions do not create new chains, but simply propagate origin trace
IDs. If an instruction has more than one operands with non-zero labels, the origin
treace ID of the last operand with non-zero label is propagated to the result of
the instruction.

Memory layout and label management
----------------------------------

The following is the memory layout for Linux/x86\_64:

+---------------+---------------+--------------------+
|    Start      |    End        |        Use         |
+===============+===============+====================+
| 0x700000000000|0x800000000000 |    application 3   |
+---------------+---------------+--------------------+
| 0x610000000000|0x700000000000 |       unused       |
+---------------+---------------+--------------------+
| 0x600000000000|0x610000000000 |      origin 1      |
+---------------+---------------+--------------------+
| 0x510000000000|0x600000000000 |    application 2   |
+---------------+---------------+--------------------+
| 0x500000000000|0x510000000000 |      shadow 1      |
+---------------+---------------+--------------------+
| 0x400000000000|0x500000000000 |       unused       |
+---------------+---------------+--------------------+
| 0x300000000000|0x400000000000 |      origin 3      |
+---------------+---------------+--------------------+
| 0x200000000000|0x300000000000 |      shadow 3      |
+---------------+---------------+--------------------+
| 0x110000000000|0x200000000000 |      origin 2      |
+---------------+---------------+--------------------+
| 0x100000000000|0x110000000000 |       unused       |
+---------------+---------------+--------------------+
| 0x010000000000|0x100000000000 |      shadow 2      |
+---------------+---------------+--------------------+
| 0x000000000000|0x010000000000 |    application 1   |
+---------------+---------------+--------------------+

Each byte of application memory corresponds to a single byte of shadow
memory, which is used to store its taint label. We map memory, shadow, and
origin regions to each other with these masks and offsets:

* shadow_addr = memory_addr ^ 0x500000000000

* origin_addr = shadow_addr + 0x100000000000

As for LLVM SSA registers, we have not found it necessary to associate a label
with each byte or bit of data, as some other tools do. Instead, labels are
associated directly with registers.  Loads will result in a union of
all shadow labels corresponding to bytes loaded, and stores will
result in a copy of the label of the stored value to the shadow of all
bytes stored to.

Propagating labels through arguments
------------------------------------

In order to propagate labels through function arguments and return values,
DataFlowSanitizer changes the ABI of each function in the translation unit.
There are currently two supported ABIs:

* Args -- Argument and return value labels are passed through additional
  arguments and by modifying the return type.

* TLS -- Argument and return value labels are passed through TLS variables
  ``__dfsan_arg_tls`` and ``__dfsan_retval_tls``.

The main advantage of the TLS ABI is that it is more tolerant of ABI mismatches
(TLS storage is not shared with any other form of storage, whereas extra
arguments may be stored in registers which under the native ABI are not used
for parameter passing and thus could contain arbitrary values).  On the other
hand the args ABI is more efficient and allows ABI mismatches to be more easily
identified by checking for nonzero labels in nominally unlabelled programs.

Implementing the ABI list
-------------------------

The `ABI list <DataFlowSanitizer.html#abi-list>`_ provides a list of functions
which conform to the native ABI, each of which is callable from an instrumented
program.  This is implemented by replacing each reference to a native ABI
function with a reference to a function which uses the instrumented ABI.
Such functions are automatically-generated wrappers for the native functions.
For example, given the ABI list example provided in the user manual, the
following wrappers will be generated under the args ABI:

.. code-block:: llvm

    define linkonce_odr { i8*, i16 } @"dfsw$malloc"(i64 %0, i16 %1) {
    entry:
      %2 = call i8* @malloc(i64 %0)
      %3 = insertvalue { i8*, i16 } undef, i8* %2, 0
      %4 = insertvalue { i8*, i16 } %3, i16 0, 1
      ret { i8*, i16 } %4
    }

    define linkonce_odr { i32, i16 } @"dfsw$tolower"(i32 %0, i16 %1) {
    entry:
      %2 = call i32 @tolower(i32 %0)
      %3 = insertvalue { i32, i16 } undef, i32 %2, 0
      %4 = insertvalue { i32, i16 } %3, i16 %1, 1
      ret { i32, i16 } %4
    }

    define linkonce_odr { i8*, i16 } @"dfsw$memcpy"(i8* %0, i8* %1, i64 %2, i16 %3, i16 %4, i16 %5) {
    entry:
      %labelreturn = alloca i16
      %6 = call i8* @__dfsw_memcpy(i8* %0, i8* %1, i64 %2, i16 %3, i16 %4, i16 %5, i16* %labelreturn)
      %7 = load i16* %labelreturn
      %8 = insertvalue { i8*, i16 } undef, i8* %6, 0
      %9 = insertvalue { i8*, i16 } %8, i16 %7, 1
      ret { i8*, i16 } %9
    }

As an optimization, direct calls to native ABI functions will call the
native ABI function directly and the pass will compute the appropriate label
internally.  This has the advantage of reducing the number of union operations
required when the return value label is known to be zero (i.e. ``discard``
functions, or ``functional`` functions with known unlabelled arguments).

Checking ABI Consistency
------------------------

DFSan changes the ABI of each function in the module.  This makes it possible
for a function with the native ABI to be called with the instrumented ABI,
or vice versa, thus possibly invoking undefined behavior.  A simple way
of statically detecting instances of this problem is to append the suffix
".dfsan" to the name of each instrumented-ABI function.

This will not catch every such problem; in particular function pointers passed
across the instrumented-native barrier cannot be used on the other side.
These problems could potentially be caught dynamically.
