==============================================
JSON Compilation Database Format Specification
==============================================

This document describes a format for specifying how to replay single
compilations independently of the build system.

Background
==========

Tools based on the C++ Abstract Syntax Tree need full information how to
parse a translation unit. Usually this information is implicitly
available in the build system, but running tools as part of the build
system is not necessarily the best solution:

-  Build systems are inherently change driven, so running multiple tools
   over the same code base without changing the code does not fit into
   the architecture of many build systems.
-  Figuring out whether things have changed is often an IO bound
   process; this makes it hard to build low latency end user tools based
   on the build system.
-  Build systems are inherently sequential in the build graph, for
   example due to generated source code. While tools that run
   independently of the build still need the generated source code to
   exist, running tools multiple times over unchanging source does not
   require serialization of the runs according to the build dependency
   graph.

Supported Systems
=================

Clang has the ability to generate compilation database fragments via
``-MJ argument <clang -MJ\<arg>>``. You can concatenate those
fragments together between ``[`` and ``]`` to create a compilation database.

Currently `CMake <https://cmake.org>`_ (since 2.8.5) supports generation
of compilation databases for Unix Makefile builds (Ninja builds in the
works) with the option ``CMAKE_EXPORT_COMPILE_COMMANDS``.

For projects on Linux, there is an alternative to intercept compiler
calls with a tool called `Bear <https://github.com/rizsotto/Bear>`_.

`Bazel <https://bazel.build>`_ can export a compilation database via
`this extractor extension
<https://github.com/hedronvision/bazel-compile-commands-extractor>`_.
Bazel is otherwise resistant to Bear and other compiler-intercept
techniques.

Clang's tooling interface supports reading compilation databases; see
the :doc:`LibTooling documentation <LibTooling>`. libclang and its
python bindings also support this (since clang 3.2); see
`CXCompilationDatabase.h </doxygen/group__COMPILATIONDB.html>`_.

Format
======

A compilation database is a JSON file, which consist of an array of
"command objects", where each command object specifies one way a
translation unit is compiled in the project.

Each command object contains the translation unit's main file, the
working directory of the compile run and the actual compile command.

Example:

::

    [
      { "directory": "/home/user/llvm/build",
        "arguments": ["/usr/bin/clang++", "-Irelative", "-DSOMEDEF=With spaces, quotes and \\-es.", "-c", "-o", "file.o", "file.cc"],
        "file": "file.cc" },

      { "directory": "/home/user/llvm/build",
        "command": "/usr/bin/clang++ -Irelative -DSOMEDEF=\"With spaces, quotes and \\-es.\" -c -o file.o file.cc",
        "file": "file2.cc" },

      ...
    ]

The contracts for each field in the command object are:

-  **directory:** The working directory of the compilation. All paths
   specified in the **command** or **file** fields must be either
   absolute or relative to this directory.
-  **file:** The main translation unit source processed by this
   compilation step. This is used by tools as the key into the
   compilation database. There can be multiple command objects for the
   same file, for example if the same source file is compiled with
   different configurations.
-  **arguments:** The compile command argv as list of strings.
   This should run the compilation step for the translation unit ``file``.
   ``arguments[0]`` should be the executable name, such as ``clang++``.
   Arguments should not be escaped, but ready to pass to ``execvp()``.
-  **command:** The compile command as a single shell-escaped string.
   Arguments may be shell quoted and escaped following platform conventions,
   with '``"``' and '``\``' being the only special characters. Shell expansion
   is not supported.

   Either **arguments** or **command** is required. **arguments** is preferred,
   as shell (un)escaping is a possible source of errors.
-  **output:** The name of the output created by this compilation step.
   This field is optional. It can be used to distinguish different processing
   modes of the same input file.

Build System Integration
========================

The convention is to name the file compile\_commands.json and put it at
the top of the build directory. Clang tools are pointed to the top of
the build directory to detect the file and use the compilation database
to parse C++ code in the source tree.

Alternatives
============
For simple projects, Clang tools also recognize a ``compile_flags.txt`` file.
This should contain one argument per line. The same flags will be used to
compile any file.

Example:

::

    -xc++
    -I
    libwidget/include/

Here ``-I libwidget/include`` is two arguments, and so becomes two lines.
Paths are relative to the directory containing ``compile_flags.txt``.
