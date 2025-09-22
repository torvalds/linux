/*      $OpenBSD: macros.h,v 1.4 2022/05/28 18:39:39 mbuhl Exp $       */
/* Public domain - Moritz Buhl */

#define rounddown(x, y)	(((x)/(y))*(y))
#define fpequal(a, b)	fpequal_cs(a, b, 1)
#define hexdump(...)

#define nitems(_a)      (sizeof((_a)) / sizeof((_a)[0]))
#define __XSTRING(_a)	(#_a)
