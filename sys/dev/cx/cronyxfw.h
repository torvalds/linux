/*-
 * Cronyx firmware definitions.
 *
 * Copyright (C) 1996 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: cronyxfw.h,v 1.1.2.1 2003/11/12 17:09:49 rik Exp $
 * $FreeBSD$
 */
#define CRONYX_DAT_MAGIC 2001107011L	/* firmware file magic */

typedef struct _cr_dat_tst {
	long start;			/* verify start */
	long end;			/* verify end */
} cr_dat_tst_t;

typedef struct {                        /* firmware file header */
	unsigned long magic;            /* firmware magic */
	long hdrsz;			/* header size in bytes */
	long len;			/* firmware data size in bits */
	long ntest;			/* number of tests */
	unsigned long sum;              /* header+tests+data checksum */
	char version[8];                /* firmware version number */
	char date[8];                   /* date when compiled */
} cr_dat_t;
