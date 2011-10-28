/// Many iterators have the property that the first argument is always bound
/// to a real list element, never NULL.  False positives arise for some
/// iterators that do not have this property, or in cases when the loop
/// cursor is reassigned.  The latter should only happen when the matched
/// code is on the way to a loop exit (break, goto, or return).
///
// Confidence: Moderate
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch

@@
iterator I;
expression x,E,E1,E2;
statement S,S1,S2;
@@

I(x,...) { <...
(
- if (x == NULL && ...) S
|
- if (x != NULL || ...)
  S
|
- (x == NULL) ||
  E
|
- (x != NULL) &&
  E
|
- (x == NULL && ...) ? E1 :
  E2
|
- (x != NULL || ...) ?
  E1
- : E2
|
- if (x == NULL && ...) S1 else
  S2
|
- if (x != NULL || ...)
  S1
- else S2
|
+ BAD(
  x == NULL
+ )
|
+ BAD(
  x != NULL
+ )
)
  ...> }