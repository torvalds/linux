-z start-stop-gc
================

If your ``-Wl,--gc-sections`` build fail with a linker error like this:

    error: undefined symbol: __start_meta
    >>> referenced by {{.*}}
    >>> the encapsulation symbol needs to be retained under --gc-sections properly; consider -z nostart-stop-gc (see https://lld.llvm.org/start-stop-gc) 

it is likely your C identifier name sections are not properly annotated to
suffice under ``--gc-sections``.

``__start_meta`` and ``__stop_meta`` are sometimes called encapsulation
symbols. In October 2015, GNU ld switched behavior and made a ``__start_meta``
reference from a live section retain all ``meta`` input sections. This
conservative behavior works for existing code which does not take GC into fair
consideration, but unnecessarily increases sizes for modern metadata section
usage which desires precise GC.

GNU ld 2.37 added ``-z start-stop-gc`` to restore the traditional behavior
ld.lld 13.0.0 defaults to ``-z start-stop-gc`` and supports ``-z nostart-stop-gc``
to switch to the conservative behavior.

The Apple ld64 linker has a similar ``section$start`` feature and always
allowed GC (like ``-z start-stop-gc``).

Annotate C identifier name sections
-----------------------------------

A C identifier name section (``meta``) sometimes depends on another section.
Let that section reference ``meta`` via a relocation.

.. code-block:: c

  asm(".pushsection .init_array,\"aw\",@init_array\n" \
      ".reloc ., R_AARCH64_NONE, meta\n"              \
      ".popsection\n")

If a relocation is inconvenient, consider using ``__attribute__((retain))``
(GCC 11 with modern binutils, Clang 13).

.. code-block:: c

  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wattributes"
  __attribute__((retain,used,section("meta")))
  static const char dummy[0];
  #pragma GCC diagnostic pop

GCC before 11 and Clang before 13 do not recognize ``__attribute__((retain))``,
so ``-Wattributes`` may need to be ignored. On ELF targets,
``__attribute__((used))`` prevents compiler discarding, but does not affect
linker ``--gc-sections``.

In a macro, you may use:

.. code-block:: c

  _Pragma("GCC diagnostic push")
  _Pragma("GCC diagnostic ignored \"-Wattributes\"")
  ...
  _Pragma("GCC diagnostic pop")

If you use the ``SECTIONS`` command in a linker script, use
`the ``KEEP`` keyword <https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html>`_, e.g.
``meta : { KEEP(*(meta)) }``
