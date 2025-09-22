=======================
Extended C++03 Support
=======================

.. contents::
   :local:

Overview
========

libc++ is an implementation of the C++ standard library targeting C++11 or later.

In C++03, the library implements the C++11 standard using C++11 language extensions provided
by Clang.

This document tracks the C++11 extensions libc++ requires, the C++11 extensions it provides,
and how to write minimal C++11 inside libc++.

Required C++11 Compiler Extensions
==================================

Clang provides a large subset of C++11 in C++03 as an extension. The features
libc++ expects Clang  to provide are:

* Variadic templates.
* RValue references and perfect forwarding.
* Alias templates
* defaulted and deleted Functions.
* reference qualified Functions
* ``auto``

There are also features that Clang *does not* provide as an extension in C++03
mode. These include:

* ``constexpr`` and ``noexcept``
*  Trailing return types.
* ``>>`` without a space.


Provided C++11 Library Extensions
=================================

.. warning::
  The C++11 extensions libc++ provides in C++03 are currently undergoing change. Existing extensions
  may be removed in the future. New users are strongly discouraged depending on these extension
  in new code.

  This section will be updated once the libc++ developer community has further discussed the
  future of C++03 with libc++.


Using Minimal C++11 in libc++
=============================

This section is for developers submitting patches to libc++. It describes idioms that should be
used in libc++ code, even in C++03, and the reasons behind them.


Use Alias Templates over Class Templates
----------------------------------------

Alias templates should be used instead of class templates in metaprogramming. Unlike class templates,
Alias templates do not produce a new instantiation every time they are used. This significantly
decreases the amount of memory used by the compiler.

For example, libc++ should not use ``add_const`` internally. Instead it should use an alias template
like

.. code-block:: cpp

  template <class _Tp>
  using _AddConst = const _Tp;

Use Default Template Parameters for SFINAE
------------------------------------------

There are three places in a function declaration that SFINAE may occur: In the template parameter list,
in the function parameter list, and in the return type. For example:

.. code-block:: cpp

  template <class _Tp, class _ = enable_if_t</*...*/ >
  void foo(_Tp); // #1

  template <class _Tp>
  void bar(_Tp, enable_if_t</*...*/>* = nullptr); // # 2

  template <class _Tp>
  enable_if_t</*...*/> baz(_Tp); // # 3

Using default template parameters for SFINAE (#1) should always be preferred.

Option #2 has two problems. First, users can observe and accidentally pass values to the SFINAE
function argument. Second, the default argument creates a live variable, which causes debug
information to be emitted containing the text of the SFINAE.

Option #3 can also cause more debug information to be emitted than is needed, because the function
return type will appear in the debug information.

Use ``unique_ptr`` when allocating memory
------------------------------------------

The standard library often needs to allocate memory and then construct a user type in it.
If the users constructor throws, the library needs to deallocate that memory. The idiomatic way to
achieve this is with ``unique_ptr``.

``__builtin_new_allocator`` is an example of this idiom. Example usage would look like:

.. code-block:: cpp

  template <class T>
  T* __create() {
    using _UniquePtr = unique_ptr<void*, __default_new_allocator::__default_new_deleter>;
    _UniquePtr __p = __default_new_allocator::__allocate_bytes(sizeof(T), alignof(T));
    T* __res = ::new(__p.get()) T();
    (void)__p.release();
    return __res;
  }
