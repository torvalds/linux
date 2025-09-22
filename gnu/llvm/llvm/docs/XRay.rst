====================
XRay Instrumentation
====================

:Version: 1 as of 2016-11-08

.. contents::
   :local:


Introduction
============

XRay is a function call tracing system which combines compiler-inserted
instrumentation points and a runtime library that can dynamically enable and
disable the instrumentation.

More high level information about XRay can be found in the `XRay whitepaper`_.

This document describes how to use XRay as implemented in LLVM.

XRay in LLVM
============

XRay consists of three main parts:

- Compiler-inserted instrumentation points.
- A runtime library for enabling/disabling tracing at runtime.
- A suite of tools for analysing the traces.

  **NOTE:** As of July 25, 2018 , XRay is only available for the following
  architectures running Linux: x86_64, arm7 (no thumb), aarch64, powerpc64le,
  mips, mipsel, mips64, mips64el, NetBSD: x86_64, FreeBSD: x86_64 and
  OpenBSD: x86_64.

The compiler-inserted instrumentation points come in the form of nop-sleds in
the final generated binary, and an ELF section named ``xray_instr_map`` which
contains entries pointing to these instrumentation points. The runtime library
relies on being able to access the entries of the ``xray_instr_map``, and
overwrite the instrumentation points at runtime.

Using XRay
==========

You can use XRay in a couple of ways:

- Instrumenting your C/C++/Objective-C/Objective-C++ application.
- Generating LLVM IR with the correct function attributes.

The rest of this section covers these main ways and later on how to customize
what XRay does in an XRay-instrumented binary.

Instrumenting your C/C++/Objective-C Application
------------------------------------------------

The easiest way of getting XRay instrumentation for your application is by
enabling the ``-fxray-instrument`` flag in your clang invocation.

For example:

::

  clang -fxray-instrument ...

By default, functions that have at least 200 instructions (or contain a loop) will
get XRay instrumentation points. You can tweak that number through the
``-fxray-instruction-threshold=`` flag:

::

  clang -fxray-instrument -fxray-instruction-threshold=1 ...

The loop detection can be disabled with ``-fxray-ignore-loops`` to use only the
instruction threshold. You can also specifically instrument functions in your
binary to either always or never be instrumented using source-level attributes.
You can do it using the GCC-style attributes or C++11-style attributes.

.. code-block:: c++

    [[clang::xray_always_instrument]] void always_instrumented();

    [[clang::xray_never_instrument]] void never_instrumented();

    void alt_always_instrumented() __attribute__((xray_always_instrument));

    void alt_never_instrumented() __attribute__((xray_never_instrument));

When linking a binary, you can either manually link in the `XRay Runtime
Library`_ or use ``clang`` to link it in automatically with the
``-fxray-instrument`` flag. Alternatively, you can statically link-in the XRay
runtime library from compiler-rt -- those archive files will take the name of
`libclang_rt.xray-{arch}` where `{arch}` is the mnemonic supported by clang
(x86_64, arm7, etc.).

LLVM Function Attribute
-----------------------

If you're using LLVM IR directly, you can add the ``function-instrument``
string attribute to your functions, to get the similar effect that the
C/C++/Objective-C source-level attributes would get:

.. code-block:: llvm

    define i32 @always_instrument() uwtable "function-instrument"="xray-always" {
      ; ...
    }

    define i32 @never_instrument() uwtable "function-instrument"="xray-never" {
      ; ...
    }

You can also set the ``xray-instruction-threshold`` attribute and provide a
numeric string value for how many instructions should be in the function before
it gets instrumented.

.. code-block:: llvm

    define i32 @maybe_instrument() uwtable "xray-instruction-threshold"="2" {
      ; ...
    }

Special Case File
-----------------

Attributes can be imbued through the use of special case files instead of
adding them to the original source files. You can use this to mark certain
functions and classes to be never, always, or instrumented with first-argument
logging from a file. The file's format is described below:

.. code-block:: bash

    # Comments are supported
    [always]
    fun:always_instrument
    fun:log_arg1=arg1 # Log the first argument for the function

    [never]
    fun:never_instrument

These files can be provided through the ``-fxray-attr-list=`` flag to clang.
You may have multiple files loaded through multiple instances of the flag.

XRay Runtime Library
--------------------

The XRay Runtime Library is part of the compiler-rt project, which implements
the runtime components that perform the patching and unpatching of inserted
instrumentation points. When you use ``clang`` to link your binaries and the
``-fxray-instrument`` flag, it will automatically link in the XRay runtime.

The default implementation of the XRay runtime will enable XRay instrumentation
before ``main`` starts, which works for applications that have a short
lifetime. This implementation also records all function entry and exit events
which may result in a lot of records in the resulting trace.

Also by default the filename of the XRay trace is ``xray-log.XXXXXX`` where the
``XXXXXX`` part is randomly generated.

These options can be controlled through the ``XRAY_OPTIONS`` environment
variable, where we list down the options and their defaults below.

+-------------------+-----------------+---------------+------------------------+
| Option            | Type            | Default       | Description            |
+===================+=================+===============+========================+
| patch_premain     | ``bool``        | ``false``     | Whether to patch       |
|                   |                 |               | instrumentation points |
|                   |                 |               | before main.           |
+-------------------+-----------------+---------------+------------------------+
| xray_mode         | ``const char*`` | ``""``        | Default mode to        |
|                   |                 |               | install and initialize |
|                   |                 |               | before ``main``.       |
+-------------------+-----------------+---------------+------------------------+
| xray_logfile_base | ``const char*`` | ``xray-log.`` | Filename base for the  |
|                   |                 |               | XRay logfile.          |
+-------------------+-----------------+---------------+------------------------+
| verbosity         | ``int``         | ``0``         | Runtime verbosity      |
|                   |                 |               | level.                 |
+-------------------+-----------------+---------------+------------------------+


If you choose to not use the default logging implementation that comes with the
XRay runtime and/or control when/how the XRay instrumentation runs, you may use
the XRay APIs directly for doing so. To do this, you'll need to include the
``xray_log_interface.h`` from the compiler-rt ``xray`` directory. The important API
functions we list below:

- ``__xray_log_register_mode(...)``: Register a logging implementation against
  a string Mode identifier. The implementation is an instance of
  ``XRayLogImpl`` defined in ``xray/xray_log_interface.h``.
- ``__xray_log_select_mode(...)``: Select the mode to install, associated with
  a string Mode identifier. Only implementations registered with
  ``__xray_log_register_mode(...)`` can be chosen with this function.
- ``__xray_log_init_mode(...)``: This function allows for initializing and
  re-initializing an installed logging implementation. See
  ``xray/xray_log_interface.h`` for details, part of the XRay compiler-rt
  installation.

Once a logging implementation has been initialized, it can be "stopped" by
finalizing the implementation through the ``__xray_log_finalize()`` function.
The finalization routine is the opposite of the initialization. When finalized,
an implementation's data can be cleared out through the
``__xray_log_flushLog()`` function. For implementations that support in-memory
processing, these should register an iterator function to provide access to the
data via the ``__xray_log_set_buffer_iterator(...)`` which allows code calling
the ``__xray_log_process_buffers(...)`` function to deal with the data in
memory.

All of this is better explained in the ``xray/xray_log_interface.h`` header.

Basic Mode
----------

XRay supports a basic logging mode which will trace the application's
execution, and periodically append to a single log. This mode can be
installed/enabled by setting ``xray_mode=xray-basic`` in the ``XRAY_OPTIONS``
environment variable. Combined with ``patch_premain=true`` this can allow for
tracing applications from start to end.

Like all the other modes installed through ``__xray_log_select_mode(...)``, the
implementation can be configured through the ``__xray_log_init_mode(...)``
function, providing the mode string and the flag options. Basic-mode specific
defaults can be provided in the ``XRAY_BASIC_OPTIONS`` environment variable.

Flight Data Recorder Mode
-------------------------

XRay supports a logging mode which allows the application to only capture a
fixed amount of memory's worth of events. Flight Data Recorder (FDR) mode works
very much like a plane's "black box" which keeps recording data to memory in a
fixed-size circular queue of buffers, and have the data available
programmatically until the buffers are finalized and flushed. To use FDR mode
on your application, you may set the ``xray_mode`` variable to ``xray-fdr`` in
the ``XRAY_OPTIONS`` environment variable. Additional options to the FDR mode
implementation can be provided in the ``XRAY_FDR_OPTIONS`` environment
variable. Programmatic configuration can be done by calling
``__xray_log_init_mode("xray-fdr", <configuration string>)`` once it has been
selected/installed.

When the buffers are flushed to disk, the result is a binary trace format
described by `XRay FDR format <XRayFDRFormat.html>`_

When FDR mode is on, it will keep writing and recycling memory buffers until
the logging implementation is finalized -- at which point it can be flushed and
re-initialised later. To do this programmatically, we follow the workflow
provided below:

.. code-block:: c++

  // Patch the sleds, if we haven't yet.
  auto patch_status = __xray_patch();

  // Maybe handle the patch_status errors.

  // When we want to flush the log, we need to finalize it first, to give
  // threads a chance to return buffers to the queue.
  auto finalize_status = __xray_log_finalize();
  if (finalize_status != XRAY_LOG_FINALIZED) {
    // maybe retry, or bail out.
  }

  // At this point, we are sure that the log is finalized, so we may try
  // flushing the log.
  auto flush_status = __xray_log_flushLog();
  if (flush_status != XRAY_LOG_FLUSHED) {
    // maybe retry, or bail out.
  }

The default settings for the FDR mode implementation will create logs named
similarly to the basic log implementation, but will have a different log
format. All the trace analysis tools (and the trace reading library) will
support all versions of the FDR mode format as we add more functionality and
record types in the future.

  **NOTE:** We do not promise perpetual support for when we update the log
  versions we support going forward. Deprecation of the formats will be
  announced and discussed on the developers mailing list.

Trace Analysis Tools
--------------------

We currently have the beginnings of a trace analysis tool in LLVM, which can be
found in the ``tools/llvm-xray`` directory. The ``llvm-xray`` tool currently
supports the following subcommands:

- ``extract``: Extract the instrumentation map from a binary, and return it as
  YAML.
- ``account``: Performs basic function call accounting statistics with various
  options for sorting, and output formats (supports CSV, YAML, and
  console-friendly TEXT).
- ``convert``: Converts an XRay log file from one format to another. We can
  convert from binary XRay traces (both basic and FDR mode) to YAML,
  `flame-graph <https://github.com/brendangregg/FlameGraph>`_ friendly text
  formats, as well as `Chrome Trace Viewer (catapult)
  <https://github.com/catapult-project/catapult>` formats.
- ``graph``: Generates a DOT graph of the function call relationships between
  functions found in an XRay trace.
- ``stack``: Reconstructs function call stacks from a timeline of function
  calls in an XRay trace.

These subcommands use various library components found as part of the XRay
libraries, distributed with the LLVM distribution. These are:

- ``llvm/XRay/Trace.h`` : A trace reading library for conveniently loading
  an XRay trace of supported forms, into a convenient in-memory representation.
  All the analysis tools that deal with traces use this implementation.
- ``llvm/XRay/Graph.h`` : A semi-generic graph type used by the graph
  subcommand to conveniently represent a function call graph with statistics
  associated with edges and vertices.
- ``llvm/XRay/InstrumentationMap.h``: A convenient tool for analyzing the
  instrumentation map in XRay-instrumented object files and binaries. The
  ``extract`` and ``stack`` subcommands uses this particular library.


Minimizing Binary Size
----------------------

XRay supports several different instrumentation points including ``function-entry``,
``function-exit``, ``custom``, and ``typed`` points. These can be enabled individually
using the ``-fxray-instrumentation-bundle=`` flag. For example if you only wanted to
instrument function entry and custom points you could specify:

::

  clang -fxray-instrument -fxray-instrumentation-bundle=function-entry,custom ...

This will omit the other sled types entirely, reducing the binary size. You can also
instrument just a sampled subset of functions using instrumentation groups.
For example, to instrument only a quarter of available functions invoke:

::

  clang -fxray-instrument -fxray-function-groups=4

A subset will be chosen arbitrarily based on a hash of the function name. To sample a
different subset you can specify ``-fxray-selected-function-group=`` with a group number
in the range of 0 to ``xray-function-groups`` - 1.  Together these options could be used
to produce multiple binaries with different instrumented subsets. If all you need is
runtime control over which functions are being traced at any given time it is better
to selectively patch and unpatch the individual functions you need using the XRay
Runtime Library's ``__xray_patch_function()`` method.

Future Work
===========

There are a number of ongoing efforts for expanding the toolset building around
the XRay instrumentation system.

Trace Analysis Tools
--------------------

- Work is in progress to integrate with or develop tools to visualize findings
  from an XRay trace. Particularly, the ``stack`` tool is being expanded to
  output formats that allow graphing and exploring the duration of time in each
  call stack.
- With a large instrumented binary, the size of generated XRay traces can
  quickly become unwieldy. We are working on integrating pruning techniques and
  heuristics for the analysis tools to sift through the traces and surface only
  relevant information.

More Platforms
--------------

We're looking forward to contributions to port XRay to more architectures and
operating systems.

.. References...

.. _`XRay whitepaper`: http://research.google.com/pubs/pub45287.html

