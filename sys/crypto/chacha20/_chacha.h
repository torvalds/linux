/* $FreeBSD$ */
/*
 * Public domain.
 */

#ifndef _CHACHA_H
#define _CHACHA_H

#include <sys/types.h>

struct chacha_ctx {
	u_int input[16];
};

#endif
