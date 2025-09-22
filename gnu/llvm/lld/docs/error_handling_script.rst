=====================
Error Handling Script
=====================

LLD provides the ability to hook into some error handling routines through a
user-provided script specified with ``--error-handling-script=<path to the script>``
when certain errors are encountered. This document specifies the requirements of
such a script.

Generic Requirements
====================

The script is expected to be available in the ``PATH`` or to be provided using a
full path. It must be executable. It is executed in the same environment as the
parent process.

Arguments
=========

LLD calls the error handling script using the following arguments::

    error-handling-script <tag> <tag-specific-arguments...>

The following tags are supported:

- ``missing-lib``: indicates that LLD failed to find a library. The library name
  is specified as the second argument, e.g. ``error-handling-script missing-lib
  mylib``

- ``undefined-symbol``: indicates that given symbol is marked as undefined. The
  unmangled symbol name is specified as the second argument, e.g.
  ``error-handling-script undefined-symbol mysymbol``

Return Value
============

Upon success, the script is expected to return 0. A non-zero value is
interpreted as an error and reported to the user. In both cases, LLD still
reports the original error.
