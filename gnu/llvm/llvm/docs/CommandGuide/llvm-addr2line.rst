llvm-addr2line - a drop-in replacement for addr2line
====================================================

.. program:: llvm-addr2line

SYNOPSIS
--------

:program:`llvm-addr2line` [*options*]

DESCRIPTION
-----------

:program:`llvm-addr2line` is an alias for the :manpage:`llvm-symbolizer(1)`
tool with different defaults. The goal is to make it a drop-in replacement for
GNU's :program:`addr2line`.

Here are some of those differences:

-  ``llvm-addr2line`` interprets all addresses as hexadecimal and ignores an
   optional ``0x`` prefix, whereas ``llvm-symbolizer`` attempts to determine
   the base from the literal's prefix and defaults to decimal if there is no
   prefix.

-  ``llvm-addr2line`` defaults not to print function names. Use `-f`_ to enable
   that.

-  ``llvm-addr2line`` defaults not to demangle function names. Use `-C`_ to
   switch the demangling on.

-  ``llvm-addr2line`` defaults not to print inlined frames. Use `-i`_ to show
   inlined frames for a source code location in an inlined function.

-  ``llvm-addr2line`` uses `--output-style=GNU`_ by default.

-  ``llvm-addr2line`` parses options from the environment variable
   ``LLVM_ADDR2LINE_OPTS`` instead of from ``LLVM_SYMBOLIZER_OPTS``.

SEE ALSO
--------

:manpage:`llvm-symbolizer(1)`

.. _-f: llvm-symbolizer.html#llvm-symbolizer-opt-f
.. _-C: llvm-symbolizer.html#llvm-symbolizer-opt-c
.. _-i: llvm-symbolizer.html#llvm-symbolizer-opt-i
.. _--output-style=GNU: llvm-symbolizer.html#llvm-symbolizer-opt-output-style
