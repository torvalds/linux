.. _BuildingCompilerRT:

============================
Darwin Sanitizers Stable ABI
============================

Some OSes like Darwin want to include the AddressSanitizer runtime by establishing a stable ASan ABI. lib/asan_abi contains a secondary stable ABI for Darwin use and potentially others. The Stable ABI has minimal impact on the community, prioritizing stability over performance.

The Stable ABI is isolated by a “shim” layer which maps the unstable ABI to the stable ABI. It consists of a static library (libclang_rt.asan_abi_osx.a) that contains simple mappings of the existing ASan ABI to the smaller Stable ABI. After linking with the static shim library, only calls to the Stable ABI remain. 

  Sample content of the shim:

  .. code-block:: c

    void __asan_load1(uptr p) { __asan_abi_loadn(p, 1, true); }
    void __asan_load2(uptr p) { __asan_abi_loadn(p, 2, true); }
    void __asan_noabort_load16(uptr p) { __asan_abi_loadn(p, 16, false); }
    void __asan_poison_cxx_array_cookie(uptr p) { __asan_abi_pac(p); }

The shim library is only used when ``-fsanitize-stable-abi`` is specified in the Clang driver and the emitted instrumentation favors runtime calls over inline expansion.

Maintenance
-----------

The maintenance burden on the sanitizer developer community should be negligible. Stable ABI tests should always pass for non-Darwin platforms. Changes to the existing ABI requiring changes to the shim should been infrequent as the existing ASan ABI has long been relatively stable anyway. Rarely, when a change that impacts the contract between LLVM and the shim occurs, some simple responses should suffice. Among such foreseeable changes are: 1) changes to a function signature, 2) additions of new functions, or 3) deprecation of an existing function.

  Following are some examples of reasonable responses to such changes:

  * An existing ABI function is changed to return the input parameter on success or NULL on failure. In this scenario, a reasonable change to the shim would be to modify the function signature appropriately and to simply guess at a common-sense implementation.

    .. code-block:: c

      uptr __asan_load1(uptr p) { __asan_abi_loadn(p, 1, true); return p; }

  * An additional function is added for performance reasons. It has a very similar function signature to other similarly named functions and logically is an extension of that same pattern. In this case it would make sense to apply the same logic as the existing entry points:

    .. code-block:: c

      void __asan_load128(uptr p) { __asan_abi_loadn(p, 128, true); }

  * An entry point is added to the existing ABI for which there is no obvious stable ABI implementation: In this case, doing nothing in a no-op stub would be acceptable, assuming existing features of ASan can still work without an actual implementation of this new function.

    .. code-block:: c

      void __asan_prefetch(uptr p) { }

  * An entrypoint in the existing ABI is deprecated and/or deleted:

    .. code-block:: c

      (Delete the entrypoint from the shim.)
