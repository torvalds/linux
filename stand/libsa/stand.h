/*
 * Copyright (c) 1998 Michael Smith.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 * From	$NetBSD: stand.h,v 1.22 1997/06/26 19:17:40 drochner Exp $	
 */

/*-
 * Copyright (c) 1993
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
 *	@(#)stand.h	8.1 (Berkeley) 6/11/93
 */

#ifndef	STAND_H
#define	STAND_H

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <sys/dirent.h>

/* this header intentionally exports NULL from <string.h> */
#include <string.h>
#define strcoll(a, b)	strcmp((a), (b))

#define CHK(fmt, args...)	printf("%s(%d): " fmt "\n", __func__, __LINE__ , ##args)
#define PCHK(fmt, args...)	{printf("%s(%d): " fmt "\n", __func__, __LINE__ , ##args); getchar();}

#include <sys/errno.h>

/* special stand error codes */
#define	EADAPT	(ELAST+1)	/* bad adaptor */
#define	ECTLR	(ELAST+2)	/* bad controller */
#define	EUNIT	(ELAST+3)	/* bad unit */
#define ESLICE	(ELAST+4)	/* bad slice */
#define	EPART	(ELAST+5)	/* bad partition */
#define	ERDLAB	(ELAST+6)	/* can't read disk label */
#define	EUNLAB	(ELAST+7)	/* unlabeled disk */
#define	EOFFSET	(ELAST+8)	/* relative seek not supported */
#define	ESALAST	(ELAST+8)	/* */

/* Partial signal emulation for sig_atomic_t */
#include <machine/signal.h>

struct open_file;

/*
 * This structure is used to define file system operations in a file system
 * independent way.
 *
 * XXX note that filesystem providers should export a pointer to their fs_ops
 *     struct, so that consumers can reference this and thus include the
 *     filesystems that they require.
 */
struct fs_ops {
    const char	*fs_name;
    int		(*fo_open)(const char *path, struct open_file *f);
    int		(*fo_close)(struct open_file *f);
    int		(*fo_read)(struct open_file *f, void *buf,
			   size_t size, size_t *resid);
    int		(*fo_write)(struct open_file *f, const void *buf,
			    size_t size, size_t *resid);
    off_t	(*fo_seek)(struct open_file *f, off_t offset, int where);
    int		(*fo_stat)(struct open_file *f, struct stat *sb);
    int		(*fo_readdir)(struct open_file *f, struct dirent *d);
};

/*
 * libstand-supplied filesystems
 */
extern struct fs_ops ufs_fsops;
extern struct fs_ops tftp_fsops;
extern struct fs_ops nfs_fsops;
extern struct fs_ops cd9660_fsops;
extern struct fs_ops nandfs_fsops;
extern struct fs_ops gzipfs_fsops;
extern struct fs_ops bzipfs_fsops;
extern struct fs_ops dosfs_fsops;
extern struct fs_ops ext2fs_fsops;
extern struct fs_ops splitfs_fsops;
extern struct fs_ops pkgfs_fsops;

/* where values for lseek(2) */
#define	SEEK_SET	0	/* set file offset to offset */
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#define	SEEK_END	2	/* set file offset to EOF plus offset */

/* 
 * Device switch
 */
struct devsw {
    const char	dv_name[8];
    int		dv_type;		/* opaque type constant, arch-dependant */
#define DEVT_NONE	0
#define DEVT_DISK	1
#define DEVT_NET	2
#define DEVT_CD		3
#define DEVT_ZFS	4
#define DEVT_FD		5
    int		(*dv_init)(void);	/* early probe call */
    int		(*dv_strategy)(void *devdata, int rw, daddr_t blk,
			size_t size, char *buf, size_t *rsize);
    int		(*dv_open)(struct open_file *f, ...);
    int		(*dv_close)(struct open_file *f);
    int		(*dv_ioctl)(struct open_file *f, u_long cmd, void *data);
    int		(*dv_print)(int verbose);	/* print device information */
    void	(*dv_cleanup)(void);
};

/*
 * libstand-supplied device switch
 */
extern struct devsw netdev;

extern int errno;

/*
 * Generic device specifier; architecture-dependent
 * versions may be larger, but should be allowed to
 * overlap.
 */
struct devdesc {
    struct devsw	*d_dev;
    int			d_unit;
    void		*d_opendata;
};

struct open_file {
    int			f_flags;	/* see F_* below */
    struct devsw	*f_dev;		/* pointer to device operations */
    void		*f_devdata;	/* device specific data */
    struct fs_ops	*f_ops;		/* pointer to file system operations */
    void		*f_fsdata;	/* file system specific data */
    off_t		f_offset;	/* current file offset */
    char		*f_rabuf;	/* readahead buffer pointer */
    size_t		f_ralen;	/* valid data in readahead buffer */
    off_t		f_raoffset;	/* consumer offset in readahead buffer */
#define SOPEN_RASIZE	512
};

#define	SOPEN_MAX	64
extern struct open_file files[];

/* f_flags values */
#define	F_READ		0x0001	/* file opened for reading */
#define	F_WRITE		0x0002	/* file opened for writing */
#define	F_RAW		0x0004	/* raw device open - no file system */
#define F_NODEV		0x0008	/* network open - no device */
#define	F_MASK		0xFFFF
/* Mode modifier for strategy() */
#define	F_NORA		(0x01 << 16)	/* Disable Read-Ahead */

#define isascii(c)	(((c) & ~0x7F) == 0)

static __inline int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

static __inline int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

static __inline int isspace(int c)
{
    return c == ' ' || (c >= 0x9 && c <= 0xd);
}

static __inline int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

static __inline int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static __inline int isalpha(int c)
{
    return isupper(c) || islower(c);
}

static __inline int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

static __inline int iscntrl(int c)
{
	return (c >= 0 && c < ' ') || c == 127;
}

static __inline int isgraph(int c)
{
	return c >= '!' && c <= '~';
}

static __inline int ispunct(int c)
{
	return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
	    (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

static __inline int toupper(int c)
{
    return islower(c) ? c - 'a' + 'A' : c;
}

static __inline int tolower(int c)
{
    return isupper(c) ? c - 'A' + 'a' : c;
}

/* sbrk emulation */
extern void	setheap(void *base, void *top);
extern char	*sbrk(int incr);

extern void	*reallocf(void *ptr, size_t size);
extern void	mallocstats(void);

extern int	printf(const char *fmt, ...) __printflike(1, 2);
extern int	asprintf(char **buf, const char *cfmt, ...) __printflike(2, 3);
extern int	sprintf(char *buf, const char *cfmt, ...) __printflike(2, 3);
extern int	snprintf(char *buf, size_t size, const char *cfmt, ...) __printflike(3, 4);
extern int	vprintf(const char *fmt, __va_list);
extern int	vsprintf(char *buf, const char *cfmt, __va_list);
extern int	vsnprintf(char *buf, size_t size, const char *cfmt, __va_list);

extern void	twiddle(u_int callerdiv);
extern void	twiddle_divisor(u_int globaldiv);

extern void	ngets(char *, int);
#define gets(x)	ngets((x), 0)
extern int	fgetstr(char *buf, int size, int fd);

extern int	open(const char *, int);
#define	O_RDONLY	0x0
#define O_WRONLY	0x1
#define O_RDWR		0x2
/* NOT IMPLEMENTED */
#define	O_CREAT		0x0200		/* create if nonexistent */
#define	O_TRUNC		0x0400		/* truncate to zero length */
extern int	close(int);
extern void	closeall(void);
extern ssize_t	read(int, void *, size_t);
extern ssize_t	write(int, const void *, size_t);
extern struct	dirent *readdirfd(int);

extern void	srandom(unsigned int);
extern long	random(void);
    
/* imports from stdlib, locally modified */
extern char	*optarg;			/* getopt(3) external variables */
extern int	optind, opterr, optopt, optreset;
extern int	getopt(int, char * const [], const char *);

/* pager.c */
extern void	pager_open(void);
extern void	pager_close(void);
extern int	pager_output(const char *lines);
extern int	pager_file(const char *fname);

/* No signal state to preserve */
#define setjmp	_setjmp
#define longjmp	_longjmp

/* environment.c */
#define EV_DYNAMIC	(1<<0)		/* value was dynamically allocated, free if changed/unset */
#define EV_VOLATILE	(1<<1)		/* value is volatile, make a copy of it */
#define EV_NOHOOK	(1<<2)		/* don't call hook when setting */

struct env_var;
typedef char	*(ev_format_t)(struct env_var *ev);
typedef int	(ev_sethook_t)(struct env_var *ev, int flags,
		    const void *value);
typedef int	(ev_unsethook_t)(struct env_var *ev);

struct env_var
{
    char		*ev_name;
    int			ev_flags;
    void		*ev_value;
    ev_sethook_t	*ev_sethook;
    ev_unsethook_t	*ev_unsethook;
    struct env_var	*ev_next, *ev_prev;
};
extern struct env_var	*environ;

extern struct env_var	*env_getenv(const char *name);
extern int		env_setenv(const char *name, int flags,
				   const void *value, ev_sethook_t sethook,
				   ev_unsethook_t unsethook);
extern char		*getenv(const char *name);
extern int		setenv(const char *name, const char *value,
			       int overwrite);
extern int		putenv(char *string);
extern int		unsetenv(const char *name);

extern ev_sethook_t	env_noset;		/* refuse set operation */
extern ev_unsethook_t	env_nounset;		/* refuse unset operation */

/* stdlib.h routines */
extern int		abs(int a);
extern void		abort(void) __dead2;
extern long		strtol(const char * __restrict, char ** __restrict, int);
extern long long	strtoll(const char * __restrict, char ** __restrict, int);
extern unsigned long	strtoul(const char * __restrict, char ** __restrict, int);
extern unsigned long long strtoull(const char * __restrict, char ** __restrict, int);

/* BCD conversions (undocumented) */
extern u_char const	bcd2bin_data[];
extern u_char const	bin2bcd_data[];
extern char const	hex2ascii_data[];

#define	bcd2bin(bcd)	(bcd2bin_data[bcd])
#define	bin2bcd(bin)	(bin2bcd_data[bin])
#define	hex2ascii(hex)	(hex2ascii_data[hex])
#define	validbcd(bcd)	(bcd == 0 || (bcd > 0 && bcd <= 0x99 && bcd2bin_data[bcd] != 0))

/* min/max (undocumented) */
static __inline int imax(int a, int b) { return (a > b ? a : b); }
static __inline int imin(int a, int b) { return (a < b ? a : b); }
static __inline long lmax(long a, long b) { return (a > b ? a : b); }
static __inline long lmin(long a, long b) { return (a < b ? a : b); }
static __inline u_int max(u_int a, u_int b) { return (a > b ? a : b); }
static __inline u_int min(u_int a, u_int b) { return (a < b ? a : b); }
static __inline quad_t qmax(quad_t a, quad_t b) { return (a > b ? a : b); }
static __inline quad_t qmin(quad_t a, quad_t b) { return (a < b ? a : b); }
static __inline u_long ulmax(u_long a, u_long b) { return (a > b ? a : b); }
static __inline u_long ulmin(u_long a, u_long b) { return (a < b ? a : b); }

/* null functions for device/filesystem switches (undocumented) */
extern int	nodev(void);
extern int	noioctl(struct open_file *, u_long, void *);
extern void	nullsys(void);

extern int	null_open(const char *path, struct open_file *f);
extern int	null_close(struct open_file *f);
extern int	null_read(struct open_file *f, void *buf, size_t size, size_t *resid);
extern int	null_write(struct open_file *f, const void *buf, size_t size, size_t *resid);
extern off_t	null_seek(struct open_file *f, off_t offset, int where);
extern int	null_stat(struct open_file *f, struct stat *sb);
extern int	null_readdir(struct open_file *f, struct dirent *d);


/* 
 * Machine dependent functions and data, must be provided or stubbed by 
 * the consumer 
 */
extern void		exit(int) __dead2;
extern int		getchar(void);
extern int		ischar(void);
extern void		putchar(int);
extern int		devopen(struct open_file *, const char *, const char **);
extern int		devclose(struct open_file *f);
extern void		panic(const char *, ...) __dead2 __printflike(1, 2);
extern void		panic_action(void) __weak_symbol __dead2;
extern time_t		getsecs(void);
extern struct fs_ops	*file_system[];
extern struct fs_ops	*exclusive_file_system;
extern struct devsw	*devsw[];

/*
 * Expose byteorder(3) functions.
 */
#ifndef _BYTEORDER_PROTOTYPED
#define	_BYTEORDER_PROTOTYPED
extern uint32_t		htonl(uint32_t);
extern uint16_t		htons(uint16_t);
extern uint32_t		ntohl(uint32_t);
extern uint16_t		ntohs(uint16_t);
#endif

#ifndef _BYTEORDER_FUNC_DEFINED
#define	_BYTEORDER_FUNC_DEFINED
#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)
#endif

void *Malloc(size_t, const char *, int);
void *Calloc(size_t, size_t, const char *, int);
void *Realloc(void *, size_t, const char *, int);
void Free(void *, const char *, int);

#ifdef DEBUG_MALLOC
#define malloc(x)	Malloc(x, __FILE__, __LINE__)
#define calloc(x, y)	Calloc(x, y, __FILE__, __LINE__)
#define free(x)		Free(x, __FILE__, __LINE__)
#define realloc(x, y)	Realloc(x, y, __FILE__, __LINE__)
#else
#define malloc(x)	Malloc(x, NULL, 0)
#define calloc(x, y)	Calloc(x, y, NULL, 0)
#define free(x)		Free(x, NULL, 0)
#define realloc(x, y)	Realloc(x, y, NULL, 0)
#endif

#endif	/* STAND_H */
