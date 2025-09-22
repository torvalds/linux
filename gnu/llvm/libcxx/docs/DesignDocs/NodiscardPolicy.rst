===================================================
Guidelines for applying ``[[nodiscard]]`` in libc++
===================================================

Libc++ adds ``[[nodiscard]]`` to functions in a lot of places. The standards
committee has decided to not have a recommended practice where to put them, so
this document lists where ``[[nodiscard]]`` should be applied in libc++.

When should ``[[nodiscard]]`` be added to functions?
====================================================

``[[nodiscard]]`` should be applied to functions

- where discarding the return value is most likely a correctness issue.
  For example a locking constructor in ``unique_lock``.

- where discarding the return value likely points to the user wanting to do
  something different. For example ``vector::empty()``, which probably should
  have been ``vector::clear()``.

  This can help spotting bugs easily which otherwise may take a very long time
  to find.

- which return a constant. For example ``numeric_limits::min()``.
- which only observe a value. For example ``string::size()``.

  Code that discards values from these kinds of functions is dead code. It can
  either be removed, or the programmer meant to do something different.

- where discarding the value is most likely a misuse of the function. For
  example ``find``.

  This protects programmers from assuming too much about how the internals of
  a function work, making code more robust in the presence of future
  optimizations.

What should be done when adding ``[[nodiscard]]`` to a function?
================================================================

Applications of ``[[nodiscard]]`` are code like any other code, so we aim to
test them. This can be done with a ``.verify.cpp`` test. Many examples are
available. Just look for tests with the suffix ``.nodiscard.verify.cpp``.
