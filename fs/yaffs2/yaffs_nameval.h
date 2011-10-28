/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __NAMEVAL_H__
#define __NAMEVAL_H__

#include "yportenv.h"

int nval_del(char *xb, int xb_size, const YCHAR * name);
int nval_set(char *xb, int xb_size, const YCHAR * name, const char *buf,
	     int bsize, int flags);
int nval_get(const char *xb, int xb_size, const YCHAR * name, char *buf,
	     int bsize);
int nval_list(const char *xb, int xb_size, char *buf, int bsize);
int nval_hasvalues(const char *xb, int xb_size);
#endif
