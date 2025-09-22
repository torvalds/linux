
====================
Libc++ ABI stability
====================

Libc++ aims to preserve a stable ABI to avoid subtle bugs when code built under the old ABI
is linked with code built under the new ABI. At the same time, libc++ wants to make
ABI-breaking improvements and bugfixes in scenarios where the user doesn't mind ABI breaks.

To support both cases, libc++ allows specifying an ABI version at
build time. The version is defined with CMake option ``LIBCXX_ABI_VERSION``.
Currently supported values are ``1`` (the stable default)
and ``2`` (the unstable "next" version). At some point "ABI version 2" will be
frozen and new ABI-breaking changes will start being applied to version ``3``;
but this has not happened yet.

To always use the most cutting-edge, most unstable ABI (which is currently ``2``
but at some point will become ``3``), set the CMake option ``LIBCXX_ABI_UNSTABLE``.

Internally, each ABI-changing feature is placed under its own C++ macro,
``_LIBCPP_ABI_XXX``. These macros' definitions are controlled by the C++ macro
``_LIBCPP_ABI_VERSION``, which is controlled by the ``LIBCXX_ABI_VERSION`` set
at build time. Libc++ does not intend users to interact with these C++ macros
directly.

-----------------
MSVC environments
-----------------

The exception to this is MSVC environments. Libc++ does not currently have users
that require a stable ABI in MSVC environments, so MSVC-only changes may be
applied unconditionally.
