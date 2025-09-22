/*	$OpenBSD: main.c,v 1.17 2025/05/10 09:44:39 visa Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

int
main(int argc, char **argv)
{
	extern char *__progname;
	int n, ret, c;

	ret = 0;
	while ((c = getopt(argc, argv, "efFiIjlpPrR:stT:u")) != -1) {
		switch (c) {
		case 'e':
			ret |= do_exec(argv[0]);
			break;
		case 'f':
			ret |= check_inheritance();
			break;
		case 'F':
			ret |= do_fdpass();
			break;
		case 'i':
			ret |= do_timer();
			break;
		case 'I':
			ret |= do_invalid_timer();
			break;
		case 'j':
			ret |= do_reset_timer();
			break;
		case 'l':
			ret |= do_flock();
			break;
		case 'p':
			ret |= do_pipe();
			break;
		case 'P':
			ret |= do_process();
			break;
		case 'r':
			ret |= do_random();
			break;
		case 'R':
			n = strtonum(optarg, 1, INT_MAX, NULL);
			ret |= do_regress(n);
			break;
		case 's':
			ret |= do_signal();
			break;
		case 't':
			ret |= do_tun();
			break;
		case 'T':
			n = strtonum(optarg, 1, INT_MAX, NULL);
			ret |= do_pty(n);
			break;
		case 'u':
			ret |= do_user();
			break;
		default:
			fprintf(stderr, "usage: %s -[fFiIlpPrstTu] [-R n]\n",
			    __progname);
			exit(1);
		}
	}

	return (ret);
}
