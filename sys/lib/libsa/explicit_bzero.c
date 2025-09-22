/*	$OpenBSD: explicit_bzero.c,v 1.1 2012/10/09 12:03:51 jsing Exp $	*/
/*
 * Public domain.
 * Written by Ted Unangst
 */

#include <lib/libsa/stand.h>

/*
 * explicit_bzero - don't let the compiler optimize away bzero
 */
void
explicit_bzero(void *p, size_t n)
{
	bzero(p, n);
}
