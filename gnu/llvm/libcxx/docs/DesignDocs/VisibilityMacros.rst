========================
Symbol Visibility Macros
========================

.. contents::
   :local:

.. _visibility-macros:

Overview
========

Libc++ uses various "visibility" macros in order to provide a stable ABI in
both the library and the headers. These macros work by changing the
visibility and inlining characteristics of the symbols they are applied to.

The std namespace also has visibility attributes applied to avoid having to
add visibility macros in as many places. Namespace std has default
type_visibility to export RTTI and other type-specific information. Note that
type_visibility is only supported by Clang, so this doesn't replace
type-specific attributes. The only exception are enums, which GCC always gives
default visibility, thus removing the need for any annotations.

Visibility Macros
=================

**_LIBCPP_HIDDEN**
  Mark a symbol as hidden so it will not be exported from shared libraries.

**_LIBCPP_EXPORTED_FROM_ABI**
  Mark a symbol as being part of our ABI. This includes functions that are part
  of the libc++ library, type information and other symbols. On Windows,
  this macro applies `dllimport`/`dllexport` to the symbol, and on other
  platforms it gives the symbol default visibility.

**_LIBCPP_OVERRIDABLE_FUNC_VIS**
  Mark a symbol as being exported by the libc++ library, but allow it to be
  overridden locally. On non-Windows, this is equivalent to `_LIBCPP_FUNC_VIS`.
  This macro is applied to all `operator new` and `operator delete` overloads.

  **Windows Behavior**: Any symbol marked `dllimport` cannot be overridden
  locally, since `dllimport` indicates the symbol should be bound to a separate
  DLL. All `operator new` and `operator delete` overloads are required to be
  locally overridable, and therefore must not be marked `dllimport`. On Windows,
  this macro therefore expands to `__declspec(dllexport)` when building the
  library and has an empty definition otherwise.

**_LIBCPP_HIDE_FROM_ABI**
  Mark a function as not being part of the ABI of any final linked image that
  uses it.

**_LIBCPP_HIDE_FROM_ABI_AFTER_V1**
  Mark a function as being hidden from the ABI (per `_LIBCPP_HIDE_FROM_ABI`)
  when libc++ is built with an ABI version after ABI v1. This macro is used to
  maintain ABI compatibility for symbols that have been historically exported
  by libc++ in v1 of the ABI, but that we don't want to export in the future.

  This macro works as follows. When we build libc++, we either hide the symbol
  from the ABI (if the symbol is not part of the ABI in the version we're
  building), or we leave it included. From user code (i.e. when we're not
  building libc++), the macro always marks symbols as internal so that programs
  built using new libc++ headers stop relying on symbols that are removed from
  the ABI in a future version. Each time we release a new stable version of the
  ABI, we should create a new _LIBCPP_HIDE_FROM_ABI_AFTER_XXX macro, and we can
  use it to start removing symbols from the ABI after that stable version.

**_LIBCPP_TEMPLATE_VIS**
  Mark a type's typeinfo and vtable as having default visibility.
  This macro has no effect on the visibility of the type's member functions.

  **GCC Behavior**: GCC does not support Clang's `type_visibility(...)`
  attribute. With GCC the `visibility(...)` attribute is used and member
  functions are affected.

  **Windows Behavior**: DLLs do not support dllimport/export on class templates.
  The macro has an empty definition on this platform.

**_LIBCPP_EXTERN_TEMPLATE_TYPE_VIS**
  Mark the member functions, typeinfo, and vtable of the type named in
  an extern template declaration as being exported by the libc++ library.
  This attribute must be specified on all extern class template declarations.

  This macro is used to override the `_LIBCPP_TEMPLATE_VIS` attribute
  specified on the primary template and to export the member functions produced
  by the explicit instantiation in the dylib.

  **Windows Behavior**: `extern template` and `dllexport` are fundamentally
  incompatible *on a class template* on Windows; the former suppresses
  instantiation, while the latter forces it. Specifying both on the same
  declaration makes the class template be instantiated, which is not desirable
  inside headers. This macro therefore expands to `dllimport` outside of libc++
  but nothing inside of it (rather than expanding to `dllexport`); instead, the
  explicit instantiations themselves are marked as exported. Note that this
  applies *only* to extern *class* templates. Extern *function* templates obey
  regular import/export semantics, and applying `dllexport` directly to the
  extern template declaration (i.e. using `_LIBCPP_FUNC_VIS`) is the correct
  thing to do for them.

**_LIBCPP_CLASS_TEMPLATE_INSTANTIATION_VIS**
  Mark the member functions, typeinfo, and vtable of an explicit instantiation
  of a class template as being exported by the libc++ library. This attribute
  must be specified on all class template explicit instantiations.

  It is only necessary to mark the explicit instantiation itself (as opposed to
  the extern template declaration) as exported on Windows, as discussed above.
  On all other platforms, this macro has an empty definition.

**_LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS**
  Mark a symbol as hidden so it will not be exported from shared libraries. This
  is intended specifically for method templates of either classes marked with
  `_LIBCPP_TYPE_VIS` or classes with an extern template instantiation
  declaration marked with `_LIBCPP_EXTERN_TEMPLATE_TYPE_VIS`.

  When building libc++ with hidden visibility, we want explicit template
  instantiations to export members, which is consistent with existing Windows
  behavior. We also want classes annotated with `_LIBCPP_TYPE_VIS` to export
  their members, which is again consistent with existing Windows behavior.
  Both these changes are necessary for clients to be able to link against a
  libc++ DSO built with hidden visibility without encountering missing symbols.

  An unfortunate side effect, however, is that method templates of classes
  either marked `_LIBCPP_TYPE_VIS` or with extern template instantiation
  declarations marked with `_LIBCPP_EXTERN_TEMPLATE_TYPE_VIS` also get default
  visibility when instantiated. These methods are often implicitly instantiated
  inside other libraries which use the libc++ headers, and will therefore end up
  being exported from those libraries, since those implicit instantiations will
  receive default visibility. This is not acceptable for libraries that wish to
  control their visibility, and led to PR30642.

  Consequently, all such problematic method templates are explicitly marked
  either hidden (via this macro) or inline, so that they don't leak into client
  libraries. The problematic methods were found by running
  `bad-visibility-finder <https://github.com/smeenai/bad-visibility-finder>`_
  against the libc++ headers after making `_LIBCPP_TYPE_VIS` and
  `_LIBCPP_EXTERN_TEMPLATE_TYPE_VIS` expand to default visibility.

Links
=====

* `[cfe-dev] Visibility in libc++ - 1 <http://lists.llvm.org/pipermail/cfe-dev/2013-July/030610.html>`_
* `[cfe-dev] Visibility in libc++ - 2 <http://lists.llvm.org/pipermail/cfe-dev/2013-August/031195.html>`_
* `[libcxx] Visibility fixes for Windows <http://lists.llvm.org/pipermail/cfe-commits/Week-of-Mon-20130805/085461.html>`_
