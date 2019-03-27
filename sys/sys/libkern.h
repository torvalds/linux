/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)libkern.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _SYS_LIBKERN_H_
#define	_SYS_LIBKERN_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/systm.h>
#endif

#ifndef	LIBKERN_INLINE
#define	LIBKERN_INLINE  static __inline
#define	LIBKERN_BODY
#endif

/* BCD conversions. */
extern u_char const	bcd2bin_data[];
extern u_char const	bin2bcd_data[];
extern char const	hex2ascii_data[];

#define	LIBKERN_LEN_BCD2BIN	154
#define	LIBKERN_LEN_BIN2BCD	100
#define	LIBKERN_LEN_HEX2ASCII	36

static inline u_char
bcd2bin(int bcd)
{

	KASSERT(bcd >= 0 && bcd < LIBKERN_LEN_BCD2BIN,
	    ("invalid bcd %d", bcd));
	return (bcd2bin_data[bcd]);
}

static inline u_char
bin2bcd(int bin)
{

	KASSERT(bin >= 0 && bin < LIBKERN_LEN_BIN2BCD,
	    ("invalid bin %d", bin));
	return (bin2bcd_data[bin]);
}

static inline char
hex2ascii(int hex)
{

	KASSERT(hex >= 0 && hex < LIBKERN_LEN_HEX2ASCII,
	    ("invalid hex %d", hex));
	return (hex2ascii_data[hex]);
}

static inline bool
validbcd(int bcd)
{

	return (bcd == 0 || (bcd > 0 && bcd <= 0x99 && bcd2bin_data[bcd] != 0));
}

static __inline int imax(int a, int b) { return (a > b ? a : b); }
static __inline int imin(int a, int b) { return (a < b ? a : b); }
static __inline long lmax(long a, long b) { return (a > b ? a : b); }
static __inline long lmin(long a, long b) { return (a < b ? a : b); }
static __inline u_int max(u_int a, u_int b) { return (a > b ? a : b); }
static __inline u_int min(u_int a, u_int b) { return (a < b ? a : b); }
static __inline quad_t qmax(quad_t a, quad_t b) { return (a > b ? a : b); }
static __inline quad_t qmin(quad_t a, quad_t b) { return (a < b ? a : b); }
static __inline u_quad_t uqmax(u_quad_t a, u_quad_t b) { return (a > b ? a : b); }
static __inline u_quad_t uqmin(u_quad_t a, u_quad_t b) { return (a < b ? a : b); }
static __inline u_long ulmax(u_long a, u_long b) { return (a > b ? a : b); }
static __inline u_long ulmin(u_long a, u_long b) { return (a < b ? a : b); }
static __inline __uintmax_t ummax(__uintmax_t a, __uintmax_t b)
{

	return (a > b ? a : b);
}
static __inline __uintmax_t ummin(__uintmax_t a, __uintmax_t b)
{

	return (a < b ? a : b);
}
static __inline off_t omax(off_t a, off_t b) { return (a > b ? a : b); }
static __inline off_t omin(off_t a, off_t b) { return (a < b ? a : b); }

static __inline int abs(int a) { return (a < 0 ? -a : a); }
static __inline long labs(long a) { return (a < 0 ? -a : a); }
static __inline quad_t qabs(quad_t a) { return (a < 0 ? -a : a); }

#define	ARC4_ENTR_NONE	0	/* Don't have entropy yet. */
#define	ARC4_ENTR_HAVE	1	/* Have entropy. */
#define	ARC4_ENTR_SEED	2	/* Reseeding. */
extern int arc4rand_iniseed_state;

/* Prototypes for non-quad routines. */
struct malloc_type;
uint32_t arc4random(void);
void	 arc4random_buf(void *, size_t);
void	 arc4rand(void *, u_int, int);
int	 timingsafe_bcmp(const void *, const void *, size_t);
void	*bsearch(const void *, const void *, size_t,
	    size_t, int (*)(const void *, const void *));
#ifndef	HAVE_INLINE_FFS
int	 ffs(int);
#endif
#ifndef	HAVE_INLINE_FFSL
int	 ffsl(long);
#endif
#ifndef	HAVE_INLINE_FFSLL
int	 ffsll(long long);
#endif
#ifndef	HAVE_INLINE_FLS
int	 fls(int);
#endif
#ifndef	HAVE_INLINE_FLSL
int	 flsl(long);
#endif
#ifndef	HAVE_INLINE_FLSLL
int	 flsll(long long);
#endif
#define	bitcount64(x)	__bitcount64((uint64_t)(x))
#define	bitcount32(x)	__bitcount32((uint32_t)(x))
#define	bitcount16(x)	__bitcount16((uint16_t)(x))
#define	bitcountl(x)	__bitcountl((u_long)(x))
#define	bitcount(x)	__bitcount((u_int)(x))

int	 fnmatch(const char *, const char *, int);
int	 locc(int, char *, u_int);
void	*memchr(const void *s, int c, size_t n);
void	*memcchr(const void *s, int c, size_t n);
void	*memmem(const void *l, size_t l_len, const void *s, size_t s_len);
void	 qsort(void *base, size_t nmemb, size_t size,
	    int (*compar)(const void *, const void *));
void	 qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
	    int (*compar)(void *, const void *, const void *));
u_long	 random(void);
int	 scanc(u_int, const u_char *, const u_char *, int);
void	 srandom(u_long);
int	 strcasecmp(const char *, const char *);
char	*strcat(char * __restrict, const char * __restrict);
char	*strchr(const char *, int);
int	 strcmp(const char *, const char *);
char	*strcpy(char * __restrict, const char * __restrict);
size_t	 strcspn(const char * __restrict, const char * __restrict) __pure;
char	*strdup_flags(const char *__restrict, struct malloc_type *, int);
char	*strdup(const char *__restrict, struct malloc_type *);
char	*strncat(char *, const char *, size_t);
char	*strndup(const char *__restrict, size_t, struct malloc_type *);
size_t	 strlcat(char *, const char *, size_t);
size_t	 strlcpy(char *, const char *, size_t);
size_t	 strlen(const char *);
int	 strncasecmp(const char *, const char *, size_t);
int	 strncmp(const char *, const char *, size_t);
char	*strncpy(char * __restrict, const char * __restrict, size_t);
size_t	 strnlen(const char *, size_t);
char	*strrchr(const char *, int);
char	*strsep(char **, const char *delim);
size_t	 strspn(const char *, const char *);
char	*strstr(const char *, const char *);
int	 strvalid(const char *, size_t);

extern const uint32_t crc32_tab[];

static __inline uint32_t
crc32_raw(const void *buf, size_t size, uint32_t crc)
{
	const uint8_t *p = (const uint8_t *)buf;

	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return (crc);
}

static __inline uint32_t
crc32(const void *buf, size_t size)
{
	uint32_t crc;

	crc = crc32_raw(buf, size, ~0U);
	return (crc ^ ~0U);
}

uint32_t
calculate_crc32c(uint32_t crc32c, const unsigned char *buffer,
    unsigned int length);
#ifdef _KERNEL
#if defined(__amd64__) || defined(__i386__)
uint32_t sse42_crc32c(uint32_t, const unsigned char *, unsigned);
#endif
#if defined(__aarch64__)
uint32_t armv8_crc32c(uint32_t, const unsigned char *, unsigned int);
#endif
#endif

static __inline char *
index(const char *p, int ch)
{

	return (strchr(p, ch));
}

static __inline char *
rindex(const char *p, int ch)
{

	return (strrchr(p, ch));
}

/* fnmatch() return values. */
#define	FNM_NOMATCH	1	/* Match failed. */

/* fnmatch() flags. */
#define	FNM_NOESCAPE	0x01	/* Disable backslash escaping. */
#define	FNM_PATHNAME	0x02	/* Slash must be matched by slash. */
#define	FNM_PERIOD	0x04	/* Period must be matched by period. */
#define	FNM_LEADING_DIR	0x08	/* Ignore /<tail> after Imatch. */
#define	FNM_CASEFOLD	0x10	/* Case insensitive search. */
#define	FNM_IGNORECASE	FNM_CASEFOLD
#define	FNM_FILE_NAME	FNM_PATHNAME

#endif /* !_SYS_LIBKERN_H_ */
