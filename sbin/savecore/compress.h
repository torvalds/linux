/*	$OpenBSD: compress.h,v 1.1 2016/09/03 13:05:08 tedu Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/stat.h>

struct z_info {
	u_int32_t mtime;	/* timestamp */
	u_int32_t crc;		/* crc */
	u_int32_t hlen;		/* header length */
	u_int64_t total_in;	/* # bytes in */
	u_int64_t total_out;	/* # bytes out */
};

/*
 * making it any bigger does not affect perfomance very much.
 * actually this value is just a little bit better than 8192.
 */
#define Z_BUFSIZE 16384

enum program_mode {
	MODE_COMP,
	MODE_DECOMP,
	MODE_CAT
} pmode;

/*
 * exit codes for compress
 */
#define	SUCCESS	0
#define	FAILURE	1
#define	WARNING	2

extern char null_magic[];

extern void *z_open(int, const char *, char *, int, u_int32_t, int);
extern FILE *zopen(const char *, const char *,int);
extern int zread(void *, char *, int);
extern int zwrite(void *, const char *, int);
extern int z_close(void *, struct z_info *, const char *, struct stat *);


extern void *gz_open(int, const char *, char *, int, u_int32_t, int);
extern int gz_read(void *, char *, int);
extern int gz_write(void *, const char *, int);
extern int gz_close(void *, struct z_info *, const char *, struct stat *);
extern int gz_flush(void *, int);

extern void *lzh_open(int, const char *, char *, int, u_int32_t, int);
extern int lzh_read(void *, char *, int);
extern int lzh_write(void *, const char *, int);
extern int lzh_close(void *, struct z_info *);
extern int lzh_flush(void *, int);

extern void *null_open(int, const char *, char *, int, u_int32_t, int);
extern int null_read(void *, char *, int);
extern int null_write(void *, const char *, int);
extern int null_close(void *, struct z_info *, const char *, struct stat *);
extern int null_flush(void *, int);

extern void setfile(const char *, int, struct stat *);   
