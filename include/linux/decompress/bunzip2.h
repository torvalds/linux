/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DECOMPRESS_BUNZIP2_H
#define DECOMPRESS_BUNZIP2_H

int bunzip2(unsigned char *inbuf, long len,
	    long (*fill)(void*, unsigned long),
	    long (*flush)(void*, unsigned long),
	    unsigned char *output,
	    long *pos,
	    void(*error)(char *x));
#endif
