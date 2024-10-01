/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2020 IBM Corp.
 *
 */
#ifndef _NX_H
#define _NX_H

#include <stdbool.h>

#define	NX_FUNC_COMP_842	1
#define NX_FUNC_COMP_GZIP	2

#ifndef __aligned
#define __aligned(x)	__attribute__((aligned(x)))
#endif

struct nx842_func_args {
	bool use_crc;
	bool decompress;		/* true decompress; false compress */
	bool move_data;
	int timeout;			/* seconds */
};

struct nxbuf_t {
	int len;
	char *buf;
};

/* @function should be EFT (aka 842), GZIP etc */
void *nx_function_begin(int function, int pri);

int nx_function(void *handle, struct nxbuf_t *in, struct nxbuf_t *out,
		void *arg);

int nx_function_end(void *handle);

#endif	/* _NX_H */
