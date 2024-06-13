/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DECOMPRESS_UNZSTD_H
#define LINUX_DECOMPRESS_UNZSTD_H

int unzstd(unsigned char *inbuf, long len,
	   long (*fill)(void*, unsigned long),
	   long (*flush)(void*, unsigned long),
	   unsigned char *output,
	   long *pos,
	   void (*error_fn)(char *x));
#endif
