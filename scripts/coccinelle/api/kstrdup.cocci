/// Use kstrdup rather than duplicating its implementation
///
// Confidence: High
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch

@@
expression from,to;
expression flag,E1,E2;
statement S;
@@

-  to = kmalloc(strlen(from) + 1,flag);
+  to = kstrdup(from, flag);
   ... when != \(from = E1 \| to = E1 \)
   if (to==NULL || ...) S
   ... when != \(from = E2 \| to = E2 \)
-  strcpy(to, from);

@@
expression x,from,to;
expression flag,E1,E2,E3;
statement S;
@@

-   x = strlen(from) + 1;
    ... when != \( x = E1 \| from = E1 \)
-   to = \(kmalloc\|kzalloc\)(x,flag);
+   to = kstrdup(from, flag);
    ... when != \(x = E2 \| from = E2 \| to = E2 \)
    if (to==NULL || ...) S
    ... when != \(x = E3 \| from = E3 \| to = E3 \)
-   memcpy(to, from, x);
