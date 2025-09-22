=====================
Header Removal Policy
=====================

Policy
------

Libc++ is in the process of splitting larger headers into smaller modular
headers. This makes it possible to remove these large headers from other
headers. For example, instead of including ``<algorithm>`` entirely it is
possible to only include the headers for the algorithms used. When the
Standard indirectly adds additional header includes, using the smaller headers
aids reducing the growth of top-level headers. For example ``<atomic>`` uses
``std::chrono::nanoseconds`` and included ``<chrono>``. In C++20 ``<chrono>``
requires ``<format>`` which adds several other headers (like ``<string>``,
``<optional>``, ``<tuple>``) which are not needed in ``<atomic>``.

The benefit of using minimal headers is that the size of libc++'s top-level
headers becomes smaller. This improves the compilation time when users include
a top-level header. It also avoids header inclusion cycles and makes it easier
to port headers to platforms with reduced functionality.

A disadvantage is that users unknowingly depend on these transitive includes.
Thus removing an include might break their build after upgrading a newer
version of libc++. For example, ``<algorithm>`` is often forgotten but using
algorithms will still work through those transitive includes. This problem is
solved by modules, however in practice most people do not use modules (yet).

To ease the removal of transitive includes in libc++, libc++ will remove
unnecessary transitive includes in newly supported C++ versions. This means
that users will have to fix their missing includes in order to upgrade to a
newer version of the Standard. Libc++ also reserves the right to remove
transitive includes at any other time, however new language versions will be
used as a convenient way to perform bulk removals of transitive includes.

For libc++ developers, this means that any transitive include removal must be
guarded by something of the form:

.. code-block:: cpp

   #if !defined(_LIBCPP_REMOVE_TRANSITIVE_INCLUDES) && _LIBCPP_STD_VER <= 20
   #  include <algorithm>
   #  include <iterator>
   #  include <utility>
   #endif

When users define ``_LIBCPP_REMOVE_TRANSITIVE_INCLUDES``, libc++ will not
include transitive headers, regardless of the language version. This can be
useful for users to aid the transition to a newer language version, or by users
who simply want to make sure they include what they use in their code.


Rationale
---------

Removing headers is not only an issue for software developers, but also for
vendors. When a vendor updates libc++ several of their upstream packages might
fail to compile, forcing them to fix these packages or file a bug with their
upstream packages. Usually upgrading software to a new language standard is
done explicitly by software developers. This means they most likely will
discover and fix the missing includes, lessening the burden for the vendors.
