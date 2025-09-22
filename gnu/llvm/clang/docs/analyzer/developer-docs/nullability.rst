==================
Nullability Checks
==================

This document is a high level description of the nullablility checks.
These checks intended to use the annotations that is described in this
RFC: https://discourse.llvm.org/t/rfc-nullability-qualifiers/35672
(`Mailman <https://lists.llvm.org/pipermail/cfe-dev/2015-March/041779.html>`_)

Let's consider the following 2 categories:

**1) nullable**

If a pointer ``p`` has a nullable annotation and no explicit null check or assert, we should warn in the following cases:

* ``p`` gets implicitly converted into nonnull pointer, for example, we are passing it to a function that takes a nonnull parameter.
* ``p`` gets dereferenced

Taking a branch on nullable pointers are the same like taking branch on null unspecified pointers.

Explicit cast from nullable to nonnull:

.. code-block:: cpp

  __nullable id foo;
  id bar = foo;
  takesNonNull((_nonnull) bar); // should not warn here (backward compatibility hack)
  anotherTakesNonNull(bar); // would be great to warn here, but not necessary(*)

Because bar corresponds to the same symbol all the time it is not easy to implement the checker that way the cast only suppress the first call but not the second. For this reason in the first implementation after a contradictory cast happens, I will treat bar as nullable unspecified, this way all of the warnings will be suppressed. Treating the symbol as nullable unspecified also has an advantage that in case the takesNonNull function body is being inlined, the will be no warning, when the symbol is dereferenced. In case I have time after the initial version I might spend additional time to try to find a more sophisticated solution, in which we would produce the second warning (*).

**2) nonnull**

* Dereferencing a nonnull, or sending message to it is ok.
* Converting nonnull to nullable is Ok.
* When there is an explicit cast from nonnull to nullable I will trust the cast (it is probable there for a reason, because this cast does not suppress any warnings or errors).
* But what should we do about null checks?:

.. code-block:: cpp

  __nonnull id takesNonnull(__nonnull id x) {
      if (x == nil) {
          // Defensive backward compatible code:
          ....
          return nil; // Should the analyzer cover this piece of code? Should we require the cast (__nonnull)nil?
      }
      ....
  }

There are these directions:

* We can either take the branch; this way the branch is analyzed
* Should we not warn about any nullability issues in that branch? Probably not, it is ok to break the nullability postconditions when the nullability preconditions are violated.
* We can assume that these pointers are not null and we lose coverage with the analyzer. (This can be implemented either in constraint solver or in the checker itself.)

Other Issues to keep in mind/take care of:

* Messaging:

  * Sending a message to a nullable pointer

    * Even though the method might return a nonnull pointer, when it was sent to a nullable pointer the return type will be nullable.
  	* The result is nullable unless the receiver is known to be non null.

  * Sending a message to an unspecified or nonnull pointer

    * If the pointer is not assumed to be nil, we should be optimistic and use the nullability implied by the method.

      * This will not happen automatically, since the AST will have null unspecified in this case.

Inlining
--------

A symbol may need to be treated differently inside an inlined body. For example, consider these conversions from nonnull to nullable in presence of inlining:

.. code-block:: cpp

  id obj = getNonnull();
  takesNullable(obj);
  takesNonnull(obj);

  void takesNullable(nullable id obj) {
     obj->ivar // we should assume obj is nullable and warn here
  }

With no special treatment, when the takesNullable is inlined the analyzer will not warn when the obj symbol is dereferenced. One solution for this is to reanalyze takesNullable as a top level function to get possible violations. The alternative method, deducing nullability information from the arguments after inlining is not robust enough (for example there might be more parameters with different nullability, but in the given path the two parameters might end up being the same symbol or there can be nested functions that take different view of the nullability of the same symbol). So the symbol will remain nonnull to avoid false positives but the functions that takes nullable parameters will be analyzed separately as well without inlining.

Annotations on multi level pointers
-----------------------------------

Tracking multiple levels of annotations for pointers pointing to pointers would make the checker more complicated, because this way a vector of nullability qualifiers would be needed to be tracked for each symbol. This is not a big caveat, since once the top level pointer is dereferenced, the symvol for the inner pointer will have the nullability information. The lack of multi level annotation tracking only observable, when multiple levels of pointers are passed to a function which has a parameter with multiple levels of annotations. So for now the checker support the top level nullability qualifiers only.:

.. code-block:: cpp

  int * __nonnull * __nullable p;
  int ** q = p;
  takesStarNullableStarNullable(q);

Implementation notes
--------------------

What to track?

* The checker would track memory regions, and to each relevant region a qualifier information would be attached which is either nullable, nonnull or null unspecified (or contradicted to suppress warnings for a specific region).
* On a branch, where a nullable pointer is known to be non null, the checker treat it as a same way as a pointer annotated as nonnull.
* When there is an explicit cast from a null unspecified to either nonnull or nullable I will trust the cast.
* Unannotated pointers are treated the same way as pointers annotated with nullability unspecified qualifier, unless the region is wrapped in ASSUME_NONNULL macros.
* We might want to implement a callback for entry points to top level functions, where the pointer nullability assumptions would be made.
