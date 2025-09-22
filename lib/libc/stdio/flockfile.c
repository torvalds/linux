/*	$OpenBSD: flockfile.c,v 1.11 2025/08/08 15:58:53 yasuoka Exp $	*/

#include <stdio.h>
#include "local.h"

void
flockfile(FILE *fp)
{
	FLOCKFILE(fp);
}
DEF_WEAK(flockfile);


int
ftrylockfile(FILE *fp)
{
	if (__isthreaded)
		return __rcmtx_enter_try(&fp->_lock) ? 0 : 1;

	return 0;
}
DEF_WEAK(ftrylockfile);

void
funlockfile(FILE *fp)
{
	FUNLOCKFILE(fp);
}
DEF_WEAK(funlockfile);
