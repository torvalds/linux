===================
``noexcept`` Policy
===================

Extended applications of ``noexcept``
-------------------------------------

As of version 13 libc++ may mark functions that do not throw (i.e.,
"Throws: Nothing") as ``noexcept``. This has two primary consequences:
first, functions might not report precondition violations by throwing.
Second, user-provided functions, such as custom predicates or custom
traits, which throw might not be propagated up to the caller (unless
specified otherwise by the Standard).
