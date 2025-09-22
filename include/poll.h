/*	$OpenBSD: poll.h,v 1.3 2003/10/29 16:41:13 deraadt Exp $ */

/*
 * Written by Theo de Raadt, Public Domain
 *
 * Typical poll() implementations expect poll.h to be in /usr/include. 
 * However this is not a convenient place for the real definitions.
 */
#include <sys/poll.h>
