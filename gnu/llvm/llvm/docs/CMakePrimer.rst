============
CMake Primer
============

.. contents::
   :local:

.. warning::
   Disclaimer: This documentation is written by LLVM project contributors `not`
   anyone affiliated with the CMake project. This document may contain
   inaccurate terminology, phrasing, or technical details. It is provided with
   the best intentions.


Introduction
============

The LLVM project and many of the core projects built on LLVM build using CMake.
This document aims to provide a brief overview of CMake for developers modifying
LLVM projects or building their own projects on top of LLVM.

The official CMake language references is available in the cmake-language
manpage and `cmake-language online documentation
<https://cmake.org/cmake/help/v3.4/manual/cmake-language.7.html>`_.

10,000 ft View
==============

CMake is a tool that reads script files in its own language that describe how a
software project builds. As CMake evaluates the scripts it constructs an
internal representation of the software project. Once the scripts have been
fully processed, if there are no errors, CMake will generate build files to
actually build the project. CMake supports generating build files for a variety
of command line build tools as well as for popular IDEs.

When a user runs CMake it performs a variety of checks similar to how autoconf
worked historically. During the checks and the evaluation of the build
description scripts CMake caches values into the CMakeCache. This is useful
because it allows the build system to skip long-running checks during
incremental development. CMake caching also has some drawbacks, but that will be
discussed later.

Scripting Overview
==================

CMake's scripting language has a very simple grammar. Every language construct
is a command that matches the pattern _name_(_args_). Commands come in three
primary types: language-defined (commands implemented in C++ in CMake), defined
functions, and defined macros. The CMake distribution also contains a suite of
CMake modules that contain definitions for useful functionality.

The example below is the full CMake build for building a C++ "Hello World"
program. The example uses only CMake language-defined functions.

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.20.0)
   project(HelloWorld)
   add_executable(HelloWorld HelloWorld.cpp)

The CMake language provides control flow constructs in the form of foreach loops
and if blocks. To make the example above more complicated you could add an if
block to define "APPLE" when targeting Apple platforms:

.. code-block:: cmake

   cmake_minimum_required(VERSION 3.20.0)
   project(HelloWorld)
   add_executable(HelloWorld HelloWorld.cpp)
   if(APPLE)
     target_compile_definitions(HelloWorld PUBLIC APPLE)
   endif()

Variables, Types, and Scope
===========================

Dereferencing
-------------

In CMake variables are "stringly" typed. All variables are represented as
strings throughout evaluation. Wrapping a variable in ``${}`` dereferences it
and results in a literal substitution of the name for the value. CMake refers to
this as "variable evaluation" in their documentation. Dereferences are performed
*before* the command being called receives the arguments. This means
dereferencing a list results in multiple separate arguments being passed to the
command.

Variable dereferences can be nested and be used to model complex data. For
example:

.. code-block:: cmake

   set(var_name var1)
   set(${var_name} foo) # same as "set(var1 foo)"
   set(${${var_name}}_var bar) # same as "set(foo_var bar)"

Dereferencing an unset variable results in an empty expansion. It is a common
pattern in CMake to conditionally set variables knowing that it will be used in
code paths that the variable isn't set. There are examples of this throughout
the LLVM CMake build system.

An example of variable empty expansion is:

.. code-block:: cmake

   if(APPLE)
     set(extra_sources Apple.cpp)
   endif()
   add_executable(HelloWorld HelloWorld.cpp ${extra_sources})

In this example the ``extra_sources`` variable is only defined if you're
targeting an Apple platform. For all other targets the ``extra_sources`` will be
evaluated as empty before add_executable is given its arguments.

Lists
-----

In CMake lists are semi-colon delimited strings, and it is strongly advised that
you avoid using semi-colons in lists; it doesn't go smoothly. A few examples of
defining lists:

.. code-block:: cmake

   # Creates a list with members a, b, c, and d
   set(my_list a b c d)
   set(my_list "a;b;c;d")

   # Creates a string "a b c d"
   set(my_string "a b c d")

Lists of Lists
--------------

One of the more complicated patterns in CMake is lists of lists. Because a list
cannot contain an element with a semi-colon to construct a list of lists you
make a list of variable names that refer to other lists. For example:

.. code-block:: cmake

   set(list_of_lists a b c)
   set(a 1 2 3)
   set(b 4 5 6)
   set(c 7 8 9)

With this layout you can iterate through the list of lists printing each value
with the following code:

.. code-block:: cmake

   foreach(list_name IN LISTS list_of_lists)
     foreach(value IN LISTS ${list_name})
       message(${value})
     endforeach()
   endforeach()

You'll notice that the inner foreach loop's list is doubly dereferenced. This is
because the first dereference turns ``list_name`` into the name of the sub-list
(a, b, or c in the example), then the second dereference is to get the value of
the list.

This pattern is used throughout CMake, the most common example is the compiler
flags options, which CMake refers to using the following variable expansions:
CMAKE_${LANGUAGE}_FLAGS and CMAKE_${LANGUAGE}_FLAGS_${CMAKE_BUILD_TYPE}.

Other Types
-----------

Variables that are cached or specified on the command line can have types
associated with them. The variable's type is used by CMake's UI tool to display
the right input field. A variable's type generally doesn't impact evaluation,
however CMake does have special handling for some variables such as PATH.
You can read more about the special handling in `CMake's set documentation
<https://cmake.org/cmake/help/v3.5/command/set.html#set-cache-entry>`_.

Scope
-----

CMake inherently has a directory-based scoping. Setting a variable in a
CMakeLists file, will set the variable for that file, and all subdirectories.
Variables set in a CMake module that is included in a CMakeLists file will be
set in the scope they are included from, and all subdirectories.

When a variable that is already set is set again in a subdirectory it overrides
the value in that scope and any deeper subdirectories.

The CMake set command provides two scope-related options. PARENT_SCOPE sets a
variable into the parent scope, and not the current scope. The CACHE option sets
the variable in the CMakeCache, which results in it being set in all scopes. The
CACHE option will not set a variable that already exists in the CACHE unless the
FORCE option is specified.

In addition to directory-based scope, CMake functions also have their own scope.
This means variables set inside functions do not bleed into the parent scope.
This is not true of macros, and it is for this reason LLVM prefers functions
over macros whenever reasonable.

.. note::
  Unlike C-based languages, CMake's loop and control flow blocks do not have
  their own scopes.

Control Flow
============

CMake features the same basic control flow constructs you would expect in any
scripting language, but there are a few quirks because, as with everything in
CMake, control flow constructs are commands.

If, ElseIf, Else
----------------

.. note::
  For the full documentation on the CMake if command go
  `here <https://cmake.org/cmake/help/v3.4/command/if.html>`_. That resource is
  far more complete.

In general CMake if blocks work the way you'd expect:

.. code-block:: cmake

  if(<condition>)
    message("do stuff")
  elseif(<condition>)
    message("do other stuff")
  else()
    message("do other other stuff")
  endif()

The single most important thing to know about CMake's if blocks coming from a C
background is that they do not have their own scope. Variables set inside
conditional blocks persist after the ``endif()``.

Loops
-----

The most common form of the CMake ``foreach`` block is:

.. code-block:: cmake

  foreach(var ...)
    message("do stuff")
  endforeach()

The variable argument portion of the ``foreach`` block can contain dereferenced
lists, values to iterate, or a mix of both:

.. code-block:: cmake

  foreach(var foo bar baz)
    message(${var})
  endforeach()
  # prints:
  #  foo
  #  bar
  #  baz

  set(my_list 1 2 3)
  foreach(var ${my_list})
    message(${var})
  endforeach()
  # prints:
  #  1
  #  2
  #  3

  foreach(var ${my_list} out_of_bounds)
    message(${var})
  endforeach()
  # prints:
  #  1
  #  2
  #  3
  #  out_of_bounds

There is also a more modern CMake foreach syntax. The code below is equivalent
to the code above:

.. code-block:: cmake

  foreach(var IN ITEMS foo bar baz)
    message(${var})
  endforeach()
  # prints:
  #  foo
  #  bar
  #  baz

  set(my_list 1 2 3)
  foreach(var IN LISTS my_list)
    message(${var})
  endforeach()
  # prints:
  #  1
  #  2
  #  3

  foreach(var IN LISTS my_list ITEMS out_of_bounds)
    message(${var})
  endforeach()
  # prints:
  #  1
  #  2
  #  3
  #  out_of_bounds

Similar to the conditional statements, these generally behave how you would
expect, and they do not have their own scope.

CMake also supports ``while`` loops, although they are not widely used in LLVM.

Modules, Functions and Macros
=============================

Modules
-------

Modules are CMake's vehicle for enabling code reuse. CMake modules are just
CMake script files. They can contain code to execute on include as well as
definitions for commands.

In CMake macros and functions are universally referred to as commands, and they
are the primary method of defining code that can be called multiple times.

In LLVM we have several CMake modules that are included as part of our
distribution for developers who don't build our project from source. Those
modules are the fundamental pieces needed to build LLVM-based projects with
CMake. We also rely on modules as a way of organizing the build system's
functionality for maintainability and re-use within LLVM projects.

Argument Handling
-----------------

When defining a CMake command handling arguments is very useful. The examples
in this section will all use the CMake ``function`` block, but this all applies
to the ``macro`` block as well.

CMake commands can have named arguments that are required at every call site. In
addition, all commands will implicitly accept a variable number of extra
arguments (In C parlance, all commands are varargs functions). When a command is
invoked with extra arguments (beyond the named ones) CMake will store the full
list of arguments (both named and unnamed) in a list named ``ARGV``, and the
sublist of unnamed arguments in ``ARGN``. Below is a trivial example of
providing a wrapper function for CMake's built in function ``add_dependencies``.

.. code-block:: cmake

   function(add_deps target)
     add_dependencies(${target} ${ARGN})
   endfunction()

This example defines a new macro named ``add_deps`` which takes a required first
argument, and just calls another function passing through the first argument and
all trailing arguments.

CMake provides a module ``CMakeParseArguments`` which provides an implementation
of advanced argument parsing. We use this all over LLVM, and it is recommended
for any function that has complex argument-based behaviors or optional
arguments. CMake's official documentation for the module is in the
``cmake-modules`` manpage, and is also available at the
`cmake-modules online documentation
<https://cmake.org/cmake/help/v3.4/module/CMakeParseArguments.html>`_.

.. note::
  As of CMake 3.5 the cmake_parse_arguments command has become a native command
  and the CMakeParseArguments module is empty and only left around for
  compatibility.

Functions Vs Macros
-------------------

Functions and Macros look very similar in how they are used, but there is one
fundamental difference between the two. Functions have their own scope, and
macros don't. This means variables set in macros will bleed out into the calling
scope. That makes macros suitable for defining very small bits of functionality
only.

The other difference between CMake functions and macros is how arguments are
passed. Arguments to macros are not set as variables, instead dereferences to
the parameters are resolved across the macro before executing it. This can
result in some unexpected behavior if using unreferenced variables. For example:

.. code-block:: cmake

   macro(print_list my_list)
     foreach(var IN LISTS my_list)
       message("${var}")
     endforeach()
   endmacro()

   set(my_list a b c d)
   set(my_list_of_numbers 1 2 3 4)
   print_list(my_list_of_numbers)
   # prints:
   # a
   # b
   # c
   # d

Generally speaking this issue is uncommon because it requires using
non-dereferenced variables with names that overlap in the parent scope, but it
is important to be aware of because it can lead to subtle bugs.

LLVM Project Wrappers
=====================

LLVM projects provide lots of wrappers around critical CMake built-in commands.
We use these wrappers to provide consistent behaviors across LLVM components
and to reduce code duplication.

We generally (but not always) follow the convention that commands prefaced with
``llvm_`` are intended to be used only as building blocks for other commands.
Wrapper commands that are intended for direct use are generally named following
with the project in the middle of the command name (i.e. ``add_llvm_executable``
is the wrapper for ``add_executable``). The LLVM ``add_*`` wrapper functions are
all defined in ``AddLLVM.cmake`` which is installed as part of the LLVM
distribution. It can be included and used by any LLVM sub-project that requires
LLVM.

.. note::

   Not all LLVM projects require LLVM for all use cases. For example compiler-rt
   can be built without LLVM, and the compiler-rt sanitizer libraries are used
   with GCC.

Useful Built-in Commands
========================

CMake has a bunch of useful built-in commands. This document isn't going to
go into details about them because The CMake project has excellent
documentation. To highlight a few useful functions see:

* `add_custom_command <https://cmake.org/cmake/help/v3.4/command/add_custom_command.html>`_
* `add_custom_target <https://cmake.org/cmake/help/v3.4/command/add_custom_target.html>`_
* `file <https://cmake.org/cmake/help/v3.4/command/file.html>`_
* `list <https://cmake.org/cmake/help/v3.4/command/list.html>`_
* `math <https://cmake.org/cmake/help/v3.4/command/math.html>`_
* `string <https://cmake.org/cmake/help/v3.4/command/string.html>`_

The full documentation for CMake commands is in the ``cmake-commands`` manpage
and available on `CMake's website <https://cmake.org/cmake/help/v3.4/manual/cmake-commands.7.html>`_
