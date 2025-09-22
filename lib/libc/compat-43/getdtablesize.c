/*	$OpenBSD: getdtablesize.c,v 1.4 2005/08/08 08:05:33 espie Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <unistd.h>

int
getdtablesize(void)
{
	return sysconf(_SC_OPEN_MAX);
}
