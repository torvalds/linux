/// Use kmemdup rather than duplicating its implementation
///
// Confidence: High
// Copyright: (C) 2010 Nicolas Palix, DIKU.  GPLv2.
// Copyright: (C) 2010 Julia Lawall, DIKU.  GPLv2.
// Copyright: (C) 2010 Gilles Muller, INRIA/LiP6.  GPLv2.
// URL: http://coccinelle.lip6.fr/
// Comments:
// Options: -no_includes -include_headers

virtual patch

@r1@
expression from,to;
expression flag;
position p;
@@

   to = \(kmalloc@p\|kzalloc@p\)(strlen(from) + 1,flag);

@r2@
expression x,from,to;
expression flag,E1;
position p;
@@

    x = strlen(from) + 1;
    ... when != \( x = E1 \| from = E1 \)
    to = \(kmalloc@p\|kzalloc@p\)(x,flag);

@@
expression from,to,size,flag;
position p != {r1.p,r2.p};
statement S;
@@

-  to = \(kmalloc@p\|kzalloc@p\)(size,flag);
+  to = kmemdup(from,size,flag);
   if (to==NULL || ...) S
-  memcpy(to, from, size);
