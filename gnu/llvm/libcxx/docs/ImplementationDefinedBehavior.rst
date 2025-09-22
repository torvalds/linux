.. _implementation-defined-behavior:

===============================
Implementation-defined behavior
===============================

Contains the implementation details of the implementation-defined behavior in
libc++. Implementation-defined is mandated to be documented by the Standard.

.. note:
   This page is far from complete.


Implementation-defined behavior
===============================

Updating the Time Zone Database
-------------------------------

The Standard allows implementations to automatically update the
*remote time zone database*. Libc++ opts not to do that. Instead calling

 - ``std::chrono::remote_version()`` will update the version information of the
   *remote time zone database*,
 - ``std::chrono::reload_tzdb()``, if needed, will update the entire
   *remote time zone database*.

This offers a way for users to update the *remote time zone database* and
give them full control over the process.


`[ostream.formatted.print]/3 <http://eel.is/c++draft/ostream.formatted.print#3>`_ A terminal capable of displaying Unicode
--------------------------------------------------------------------------------------------------------------------------

The Standard specifies that the manner in which a stream is determined to refer
to a terminal capable of displaying Unicode is implementation-defined. This is
used for ``std::print`` and similar functions taking an ``ostream&`` argument.

Libc++ determines that a stream is Unicode-capable terminal by:

* First it determines whether the stream's ``rdbuf()`` has an underlying
  ``FILE*``. This is ``true`` in the following cases:

  * The stream is ``std::cout``, ``std::cerr``, or ``std::clog``.

  * A ``std::basic_filebuf<CharT, Traits>`` derived from ``std::filebuf``.

* The way to determine whether this ``FILE*`` refers to a terminal capable of
  displaying Unicode is the same as specified for `void vprint_unicode(FILE*
  stream, string_view fmt, format_args args);
  <http://eel.is/c++draft/print.fun#7>`_. This function is used for other
  ``std::print`` overloads that don't take an ``ostream&`` argument.

`[sf.cmath] <https://wg21.link/sf.cmath>`_ Mathematical Special Functions: Large indices
----------------------------------------------------------------------------------------

Most functions within the Mathematical Special Functions section contain integral indices.
The Standard specifies the result for larger indices as implementation-defined.
Libc++ pursuits reasonable results by choosing the same formulas as for indices below that threshold.
E.g.

- ``std::hermite(unsigned n, T x)`` for ``n >= 128``


Listed in the index of implementation-defined behavior
======================================================

The order of the entries matches the entries in the
`draft of the Standard <http://eel.is/c++draft/impldefindex>`_.
