/*	$OpenBSD: s_llroundf.c,v 1.1 2008/07/21 20:29:14 martynas Exp $	*/
/* $NetBSD: llroundf.c,v 1.2 2004/10/13 15:18:32 drochner Exp $ */

/*
 * Written by Matthias Drochner <drochner@NetBSD.org>.
 * Public domain.
 */

#define LROUNDNAME llroundf
#define RESTYPE long long int
#define RESTYPE_MIN LLONG_MIN
#define RESTYPE_MAX LLONG_MAX

#include "s_lroundf.c"
