/*	$OpenBSD: exceptions.cc,v 1.1 2007/01/28 19:10:06 kettenis Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain
 */

#include <cstring>

int
main()
{
	try {
		throw("foo");
        }
	catch(const char *p) {
		if (!strcmp(p, "foo"))
			return (0);
	}
	return (1);
}
