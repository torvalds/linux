=================
DataFlowSanitizer
=================

.. toctree::
   :hidden:

   DataFlowSanitizerDesign

.. contents::
   :local:

Introduction
============

DataFlowSanitizer is a generalised dynamic data flow analysis.

Unlike other Sanitizer tools, this tool is not designed to detect a
specific class of bugs on its own.  Instead, it provides a generic
dynamic data flow analysis framework to be used by clients to help
detect application-specific issues within their own code.

How to build libc++ with DFSan
==============================

DFSan requires either all of your code to be instrumented or for uninstrumented
functions to be listed as ``uninstrumented`` in the `ABI list`_.

If you'd like to have instrumented libc++ functions, then you need to build it
with DFSan instrumentation from source. Here is an example of how to build
libc++ and the libc++ ABI with data flow sanitizer instrumentation.

.. code-block:: console

  mkdir libcxx-build
  cd libcxx-build

  # An example using ninja
  cmake -GNinja -S <monorepo-root>/runtimes \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_SANITIZER="DataFlow" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi"

  ninja cxx cxxabi

Note: Ensure you are building with a sufficiently new version of Clang.

Usage
=====

With no program changes, applying DataFlowSanitizer to a program
will not alter its behavior.  To use DataFlowSanitizer, the program
uses API functions to apply tags to data to cause it to be tracked, and to
check the tag of a specific data item.  DataFlowSanitizer manages
the propagation of tags through the program according to its data flow.

The APIs are defined in the header file ``sanitizer/dfsan_interface.h``.
For further information about each function, please refer to the header
file.

.. _ABI list:

ABI List
--------

DataFlowSanitizer uses a list of functions known as an ABI list to decide
whether a call to a specific function should use the operating system's native
ABI or whether it should use a variant of this ABI that also propagates labels
through function parameters and return values.  The ABI list file also controls
how labels are propagated in the former case.  DataFlowSanitizer comes with a
default ABI list which is intended to eventually cover the glibc library on
Linux but it may become necessary for users to extend the ABI list in cases
where a particular library or function cannot be instrumented (e.g. because
it is implemented in assembly or another language which DataFlowSanitizer does
not support) or a function is called from a library or function which cannot
be instrumented.

DataFlowSanitizer's ABI list file is a :doc:`SanitizerSpecialCaseList`.
The pass treats every function in the ``uninstrumented`` category in the
ABI list file as conforming to the native ABI.  Unless the ABI list contains
additional categories for those functions, a call to one of those functions
will produce a warning message, as the labelling behavior of the function
is unknown.  The other supported categories are ``discard``, ``functional``
and ``custom``.

* ``discard`` -- To the extent that this function writes to (user-accessible)
  memory, it also updates labels in shadow memory (this condition is trivially
  satisfied for functions which do not write to user-accessible memory).  Its
  return value is unlabelled.
* ``functional`` -- Like ``discard``, except that the label of its return value
  is the union of the label of its arguments.
* ``custom`` -- Instead of calling the function, a custom wrapper ``__dfsw_F``
  is called, where ``F`` is the name of the function.  This function may wrap
  the original function or provide its own implementation.  This category is
  generally used for uninstrumentable functions which write to user-accessible
  memory or which have more complex label propagation behavior.  The signature
  of ``__dfsw_F`` is based on that of ``F`` with each argument having a
  label of type ``dfsan_label`` appended to the argument list.  If ``F``
  is of non-void return type a final argument of type ``dfsan_label *``
  is appended to which the custom function can store the label for the
  return value.  For example:

.. code-block:: c++

  void f(int x);
  void __dfsw_f(int x, dfsan_label x_label);

  void *memcpy(void *dest, const void *src, size_t n);
  void *__dfsw_memcpy(void *dest, const void *src, size_t n,
                      dfsan_label dest_label, dfsan_label src_label,
                      dfsan_label n_label, dfsan_label *ret_label);

If a function defined in the translation unit being compiled belongs to the
``uninstrumented`` category, it will be compiled so as to conform to the
native ABI.  Its arguments will be assumed to be unlabelled, but it will
propagate labels in shadow memory.

For example:

.. code-block:: none

  # main is called by the C runtime using the native ABI.
  fun:main=uninstrumented
  fun:main=discard

  # malloc only writes to its internal data structures, not user-accessible memory.
  fun:malloc=uninstrumented
  fun:malloc=discard

  # tolower is a pure function.
  fun:tolower=uninstrumented
  fun:tolower=functional

  # memcpy needs to copy the shadow from the source to the destination region.
  # This is done in a custom function.
  fun:memcpy=uninstrumented
  fun:memcpy=custom

For instrumented functions, the ABI list supports a ``force_zero_labels``
category, which will make all stores and return values set zero labels.
Functions should never be labelled with both ``force_zero_labels``
and ``uninstrumented`` or any of the uninstrumented wrapper kinds.

For example:

.. code-block:: none

  # e.g. void writes_data(char* out_buf, int out_buf_len) {...}
  # Applying force_zero_labels will force out_buf shadow to zero.
  fun:writes_data=force_zero_labels


Compilation Flags
-----------------

* ``-dfsan-abilist`` -- The additional ABI list files that control how shadow
  parameters are passed. File names are separated by comma.
* ``-dfsan-combine-pointer-labels-on-load`` -- Controls whether to include or
  ignore the labels of pointers in load instructions. Its default value is true.
  For example:

.. code-block:: c++

  v = *p;

If the flag is true, the label of ``v`` is the union of the label of ``p`` and
the label of ``*p``. If the flag is false, the label of ``v`` is the label of
just ``*p``.

* ``-dfsan-combine-pointer-labels-on-store`` -- Controls whether to include or
  ignore the labels of pointers in store instructions. Its default value is
  false. For example:

.. code-block:: c++

  *p = v;

If the flag is true, the label of ``*p`` is the union of the label of ``p`` and
the label of ``v``. If the flag is false, the label of ``*p`` is the label of
just ``v``.

* ``-dfsan-combine-offset-labels-on-gep`` -- Controls whether to propagate
  labels of offsets in GEP instructions. Its default value is true. For example:

.. code-block:: c++

  p += i;

If the flag is true, the label of ``p`` is the union of the label of ``p`` and
the label of ``i``. If the flag is false, the label of ``p`` is unchanged.

* ``-dfsan-track-select-control-flow`` -- Controls whether to track the control
  flow of select instructions. Its default value is true. For example:

.. code-block:: c++

  v = b? v1: v2;

If the flag is true, the label of ``v`` is the union of the labels of ``b``,
``v1`` and ``v2``.  If the flag is false, the label of ``v`` is the union of the
labels of just ``v1`` and ``v2``.

* ``-dfsan-event-callbacks`` -- An experimental feature that inserts callbacks for
  certain data events. Currently callbacks are only inserted for loads, stores,
  memory transfers (i.e. memcpy and memmove), and comparisons. Its default value
  is false. If this flag is set to true, a user must provide definitions for the
  following callback functions:

.. code-block:: c++

  void __dfsan_load_callback(dfsan_label Label, void* Addr);
  void __dfsan_store_callback(dfsan_label Label, void* Addr);
  void __dfsan_mem_transfer_callback(dfsan_label *Start, size_t Len);
  void __dfsan_cmp_callback(dfsan_label CombinedLabel);

* ``-dfsan-conditional-callbacks`` -- An experimental feature that inserts
  callbacks for control flow conditional expressions.
  This can be used to find where tainted values can control execution.

  In addition to this compilation flag, a callback handler must be registered
  using ``dfsan_set_conditional_callback(my_callback);``, where my_callback is
  a function with a signature matching
  ``void my_callback(dfsan_label l, dfsan_origin o);``.
  This signature is the same when origin tracking is disabled - in this case
  the dfsan_origin passed in it will always be 0.

  The callback will only be called when a tainted value reaches a conditional
  expression for control flow (such as an if's condition).
  The callback will be skipped for conditional expressions inside signal
  handlers, as this is prone to deadlock. Tainted values used in conditional
  expressions inside signal handlers will instead be aggregated via bitwise
  or, and can be accessed using
  ``dfsan_label dfsan_get_labels_in_signal_conditional();``.

* ``-dfsan-reaches-function-callbacks`` -- An experimental feature that inserts
  callbacks for data entering a function.

  In addition to this compilation flag, a callback handler must be registered
  using ``dfsan_set_reaches_function_callback(my_callback);``, where my_callback is
  a function with a signature matching
  ``void my_callback(dfsan_label label, dfsan_origin origin, const char *file, unsigned int line, const char *function);``
  This signature is the same when origin tracking is disabled - in this case
  the dfsan_origin passed in it will always be 0.

  The callback will be called when a tained value reach stack/registers
  in the context of a function. Tainted values can reach a function:
  * via the arguments of the function
  * via the return value of a call that occurs in the function
  * via the loaded value of a load that occurs in the function

  The callback will be skipped for conditional expressions inside signal
  handlers, as this is prone to deadlock. Tainted values reaching functions
  inside signal handlers will instead be aggregated via bitwise or, and can
  be accessed using
  ``dfsan_label dfsan_get_labels_in_signal_reaches_function()``.

* ``-dfsan-track-origins`` -- Controls how to track origins. When its value is
  0, the runtime does not track origins. When its value is 1, the runtime tracks
  origins at memory store operations. When its value is 2, the runtime tracks
  origins at memory load and store operations. Its default value is 0.

* ``-dfsan-instrument-with-call-threshold`` -- If a function being instrumented
  requires more than this number of origin stores, use callbacks instead of
  inline checks (-1 means never use callbacks). Its default value is 3500.

Environment Variables
---------------------

* ``warn_unimplemented`` -- Whether to warn on unimplemented functions. Its
  default value is false.
* ``strict_data_dependencies`` -- Whether to propagate labels only when there is
  explicit obvious data dependency (e.g., when comparing strings, ignore the fact
  that the output of the comparison might be implicit data-dependent on the
  content of the strings). This applies only to functions with ``custom`` category
  in ABI list. Its default value is true.
* ``origin_history_size`` -- The limit of origin chain length. Non-positive values
  mean unlimited. Its default value is 16.
* ``origin_history_per_stack_limit`` -- The limit of origin node's references count.
  Non-positive values mean unlimited. Its default value is 20000.
* ``store_context_size`` -- The depth limit of origin tracking stack traces. Its
  default value is 20.
* ``zero_in_malloc`` -- Whether to zero shadow space of new allocated memory. Its
  default value is true.
* ``zero_in_free`` --- Whether to zero shadow space of deallocated memory. Its
  default value is true.

Example
=======

DataFlowSanitizer supports up to 8 labels, to achieve low CPU and code
size overhead. Base labels are simply 8-bit unsigned integers that are
powers of 2 (i.e. 1, 2, 4, 8, ..., 128), and union labels are created
by ORing base labels.

The following program demonstrates label propagation by checking that
the correct labels are propagated.

.. code-block:: c++

  #include <sanitizer/dfsan_interface.h>
  #include <assert.h>

  int main(void) {
    int i = 100;
    int j = 200;
    int k = 300;
    dfsan_label i_label = 1;
    dfsan_label j_label = 2;
    dfsan_label k_label = 4;
    dfsan_set_label(i_label, &i, sizeof(i));
    dfsan_set_label(j_label, &j, sizeof(j));
    dfsan_set_label(k_label, &k, sizeof(k));

    dfsan_label ij_label = dfsan_get_label(i + j);

    assert(ij_label & i_label);  // ij_label has i_label
    assert(ij_label & j_label);  // ij_label has j_label
    assert(!(ij_label & k_label));  // ij_label doesn't have k_label
    assert(ij_label == 3);  // Verifies all of the above

    // Or, equivalently:
    assert(dfsan_has_label(ij_label, i_label));
    assert(dfsan_has_label(ij_label, j_label));
    assert(!dfsan_has_label(ij_label, k_label));

    dfsan_label ijk_label = dfsan_get_label(i + j + k);

    assert(ijk_label & i_label);  // ijk_label has i_label
    assert(ijk_label & j_label);  // ijk_label has j_label
    assert(ijk_label & k_label);  // ijk_label has k_label
    assert(ijk_label == 7);  // Verifies all of the above

    // Or, equivalently:
    assert(dfsan_has_label(ijk_label, i_label));
    assert(dfsan_has_label(ijk_label, j_label));
    assert(dfsan_has_label(ijk_label, k_label));

    return 0;
  }

Origin Tracking
===============

DataFlowSanitizer can track origins of labeled values. This feature is enabled by
``-mllvm -dfsan-track-origins=1``. For example,

.. code-block:: console

    % cat test.cc
    #include <sanitizer/dfsan_interface.h>
    #include <stdio.h>

    int main(int argc, char** argv) {
      int i = 0;
      dfsan_set_label(i_label, &i, sizeof(i));
      int j = i + 1;
      dfsan_print_origin_trace(&j, "A flow from i to j");
      return 0;
    }

    % clang++ -fsanitize=dataflow -mllvm -dfsan-track-origins=1 -fno-omit-frame-pointer -g -O2 test.cc
    % ./a.out
    Taint value 0x1 (at 0x7ffd42bf415c) origin tracking (A flow from i to j)
    Origin value: 0x13900001, Taint value was stored to memory at
      #0 0x55676db85a62 in main test.cc:7:7
      #1 0x7f0083611bbc in __libc_start_main libc-start.c:285

    Origin value: 0x9e00001, Taint value was created at
      #0 0x55676db85a08 in main test.cc:6:3
      #1 0x7f0083611bbc in __libc_start_main libc-start.c:285

By ``-mllvm -dfsan-track-origins=1`` DataFlowSanitizer collects only
intermediate stores a labeled value went through. Origin tracking slows down
program execution by a factor of 2x on top of the usual DataFlowSanitizer
slowdown and increases memory overhead by 1x. By ``-mllvm -dfsan-track-origins=2``
DataFlowSanitizer also collects intermediate loads a labeled value went through.
This mode slows down program execution by a factor of 4x.

Current status
==============

DataFlowSanitizer is a work in progress, currently under development for
x86\_64 Linux.

Design
======

Please refer to the :doc:`design document<DataFlowSanitizerDesign>`.
