/*	$OpenBSD: ctors.c,v 1.3 2003/07/31 21:48:02 deraadt Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org>, 2002 Public Domain.
 */
void foo(void) __attribute__((constructor));

int constructed = 0;

void
foo(void)
{
	constructed = 1;
}

int
main(int argc, char *argv[])
{
	return !constructed;
}
