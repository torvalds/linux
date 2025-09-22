/*	$OpenBSD: system.c,v 1.2 2001/11/11 19:57:43 marc Exp $ */
/*
 *	Placed in the PUBLIC DOMAIN
 */ 

/*
 * system checks the threads system interface and that waitpid/wait4
 * works correctly.
 */

#include <stdlib.h>
#include "test.h"

int
main(int argc, char **argv)
{
    ASSERT(system("ls") == 0);
    SUCCEED;
}
