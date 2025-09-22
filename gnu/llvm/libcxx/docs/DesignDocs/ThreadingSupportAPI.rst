=====================
Threading Support API
=====================

.. contents::
   :local:

Overview
========

Libc++ supports using multiple different threading models and configurations
to implement the threading parts of libc++, including ``<thread>`` and ``<mutex>``.
These different models provide entirely different interfaces from each
other. To address this libc++ wraps the underlying threading API in a new and
consistent API, which it uses internally to implement threading primitives.

The ``<__thread/support.h>`` header is where libc++ defines its internal
threading interface. It documents the functions and declarations required
to fullfil the internal threading interface.

External Threading API and the ``<__external_threading>`` header
================================================================

In order to support vendors with custom threading API's libc++ allows the
entire internal threading interface to be provided by an external,
vendor provided, header.

When ``_LIBCPP_HAS_THREAD_API_EXTERNAL`` is defined the ``<__thread/support.h>``
header simply forwards to the ``<__external_threading>`` header (which must exist).
It is expected that the ``<__external_threading>`` header provide the exact
interface normally provided by ``<__thread/support.h>``.

External Threading Library
==========================

libc++ can be compiled with its internal threading API delegating to an external
library. Such a configuration is useful for library vendors who wish to
distribute a thread-agnostic libc++ library, where the users of the library are
expected to provide the implementation of the libc++ internal threading API.

On a production setting, this would be achieved through a custom
``<__external_threading>`` header, which declares the libc++ internal threading
API but leaves out the implementation.

Threading Configuration Macros
==============================

**_LIBCPP_HAS_NO_THREADS**
  This macro is defined when libc++ is built without threading support. It
  should not be manually defined by the user.

**_LIBCPP_HAS_THREAD_API_EXTERNAL**
  This macro is defined when libc++ should use the ``<__external_threading>``
  header to provide the internal threading API. This macro overrides
  ``_LIBCPP_HAS_THREAD_API_PTHREAD``.

**_LIBCPP_HAS_THREAD_API_PTHREAD**
  This macro is defined when libc++ should use POSIX threads to implement the
  internal threading API.

**_LIBCPP_HAS_THREAD_API_C11**
  This macro is defined when libc++ should use C11 threads to implement the
  internal threading API.

**_LIBCPP_HAS_THREAD_API_WIN32**
  This macro is defined when libc++ should use Win32 threads to implement the
  internal threading API.
