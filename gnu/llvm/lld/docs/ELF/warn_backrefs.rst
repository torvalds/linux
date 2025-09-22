--warn-backrefs
===============

``--warn-backrefs`` gives a warning when an undefined symbol reference is
resolved by a definition in an archive to the left of it on the command line.

A linker such as GNU ld makes a single pass over the input files from left to
right maintaining the set of undefined symbol references from the files loaded
so far. When encountering an archive or an object file surrounded by
``--start-lib`` and ``--end-lib`` that archive will be searched for resolving
symbol definitions; this may result in input files being loaded, updating the
set of undefined symbol references. When all resolving definitions have been
loaded from the archive, the linker moves on the next file and will not return
to it.  This means that if an input file to the right of a archive cannot have
an undefined symbol resolved by a archive to the left of it. For example:

    ld def.a ref.o

will result in an ``undefined reference`` error. If there are no cyclic
references, the archives can be ordered in such a way that there are no
backward references. If there are cyclic references then the ``--start-group``
and ``--end-group`` options can be used, or the same archive can be placed on
the command line twice.

LLD remembers the symbol table of archives that it has previously seen, so if
there is a reference from an input file to the right of an archive, LLD will
still search that archive for resolving any undefined references. This means
that an archive only needs to be included once on the command line and the
``--start-group`` and ``--end-group`` options are redundant.

A consequence of the differing archive searching semantics is that the same
linker command line can result in different outcomes. A link may succeed with
LLD that will fail with GNU ld, or even worse both links succeed but they have
selected different objects from different archives that both define the same
symbols.

The ``warn-backrefs`` option provides information that helps identify cases
where LLD and GNU ld archive selection may differ.

    | % ld.lld --warn-backrefs ... -lB -lA
    | ld.lld: warning: backward reference detected: system in A.a(a.o) refers to B.a(b.o)

    | % ld.lld --warn-backrefs ... --start-lib B/b.o --end-lib --start-lib A/a.o --end-lib
    | ld.lld: warning: backward reference detected: system in A/a.o refers to B/b.o

    # To suppress the warning, you can specify --warn-backrefs-exclude=<glob> to match B/b.o or B.a(b.o)

The ``--warn-backrefs`` option can also provide a check to enforce a
topological order of archives, which can be useful to detect layering
violations (albeit unable to catch all cases). There are two cases where GNU ld
will result in an ``undefined reference`` error:

* If adding the dependency does not form a cycle: conceptually ``A`` is higher
  level library while ``B`` is at a lower level. When you are developing an
  application ``P`` which depends on ``A``, but does not directly depend on
  ``B``, your link may fail surprisingly with ``undefined symbol:
  symbol_defined_in_B`` if the used/linked part of ``A`` happens to need some
  components of ``B``. It is inappropriate for ``P`` to add a dependency on
  ``B`` since ``P`` does not use ``B`` directly.
* If adding the dependency forms a cycle, e.g. ``B->C->A ~> B``. ``A``
  is supposed to be at the lowest level while ``B`` is supposed to be at the
  highest level. When you are developing ``C_test`` testing ``C``, your link may
  fail surprisingly with ``undefined symbol`` if there is somehow a dependency on
  some components of ``B``. You could fix the issue by adding the missing
  dependency (``B``), however, then every test (``A_test``, ``B_test``,
  ``C_test``) will link against every library. This breaks the motivation
  of splitting ``B``, ``C`` and ``A`` into separate libraries and makes binaries
  unnecessarily large. Moreover, the layering violation makes lower-level
  libraries (e.g. ``A``) vulnerable to changes to higher-level libraries (e.g.
  ``B``, ``C``).

Resolution:

* Add a dependency from ``A`` to ``B``.
* The reference may be unintended and can be removed.
* The dependency may be intentionally omitted because there are multiple
  libraries like ``B``.  Consider linking ``B`` with object semantics by
  surrounding it with ``--whole-archive`` and ``--no-whole-archive``.
* In the case of circular dependency, sometimes merging the libraries are the best.

There are two cases like a library sandwich where GNU ld will select a
different object.

* ``A.a B A2.so``: ``A.a`` may be used as an interceptor (e.g. it provides some
  optimized libc functions and ``A2`` is libc).  ``B`` does not need to know
  about ``A.a``, and ``A.a`` may be pulled into the link by other part of the
  program. For linker portability, consider ``--whole-archive`` and
  ``--no-whole-archive``.

* ``A.a B A2.a``: similar to the above case but ``--warn-backrefs`` does not
  flag the problem, because ``A2.a`` may be a replicate of ``A.a``, which is
  redundant but benign. In some cases ``A.a`` and ``B`` should be surrounded by
  a pair of ``--start-group`` and ``--end-group``. This is especially common
  among system libraries (e.g.  ``-lc __isnanl references -lm``, ``-lc
  _IO_funlockfile references -lpthread``, ``-lc __gcc_personality_v0 references
  -lgcc_eh``, and ``-lpthread _Unwind_GetCFA references -lunwind``).

  In C++, this is likely an ODR violation. We probably need a dedicated option
  for ODR detection.
