================
PSTL integration
================

The PSTL (or Parallel STL) project is quite complex in its current form and does not provide everything that libc++
requires, for example ``_LIBCPP_HIDE_FROM_ABI`` or similar annotations and including granularized headers. Furthermore,
the PSTL provides various layers of indirection that make sense in a generic implementation of the parallel algorithms,
but are unnecessarily complex in the context of a single standard library implementation. Because of these drawbacks, we
decided to adopt a modified PSTL in libc++. Specifically, the goals of the modified PSTL are

- No ``<__pstl_algorithm>`` and similar glue headers -- instead, the implementation files are included directly in
  ``<algorithm>`` and friends.
- No ``<pstl/internal/algorithm_impl.h>`` and ``<pstl/internal/algorithm_fwd.h>`` headers and friends -- these contain
  the implementation and forward declarations for internal functions respectively. The implementation lives inside
  ``<__algorithm/pstl_any_of.h>`` and friends, and the forward declarations are not needed inside libc++.
- No ``<pstl/internal/glue_algorithm_defs.h>`` and ``<pstl/internal/glue_algorithm_impl.h>`` headers and friends --
  these contain the public API. It lives inside ``<__algorithm/pstl_any_of.h>`` and friends instead.
- The headers implementing backends are kept with as few changes as possible to make it easier to keep the backends in
  sync with the backends from the original PSTL.
- The configuration headers ``__pstl_config_site.in`` and ``pstl_config.h`` are removed, and any required configuration
  is done inside ``__config_site.in`` and ``__config`` respectively.
- libc++-style tests for the public PSTL API
