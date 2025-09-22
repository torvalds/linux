/*	$OpenBSD: stand.h,v 1.72 2021/12/01 17:25:35 kettenis Exp $	*/
/*	$NetBSD: stand.h,v 1.18 1996/11/30 04:35:51 gwr Exp $	*/

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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/stdarg.h>
#include <sys/stdint.h>
#include "saerrno.h"

#ifndef NULL
#define	NULL	0
#endif

struct open_file;

/*
 * Useful macros
 */
/* don't define if libkern included */
#ifndef LIBKERN_INLINE
#define	max(a,b)	(((a)>(b))? (a) : (b))
#define	min(a,b)	(((a)>(b))? (b) : (a))
#endif

/*
 * This structure is used to define file system operations in a file system
 * independent way.
 */
struct fs_ops {
	int	(*open)(char *path, struct open_file *f);
	int	(*close)(struct open_file *f);
	int	(*read)(struct open_file *f, void *buf,
		    size_t size, size_t *resid);
	int	(*write)(struct open_file *f, void *buf,
		    size_t size, size_t *resid);
	off_t	(*seek)(struct open_file *f, off_t offset, int where);
	int	(*stat)(struct open_file *f, struct stat *sb);
	int	(*readdir)(struct open_file *f, char *);
	int	(*fchmod)(struct open_file *f, mode_t);
};

extern struct fs_ops file_system[];
extern int nfsys;

/* where values for lseek(2) */
#define	SEEK_SET	0	/* set file offset to offset */
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#define	SEEK_END	2	/* set file offset to EOF plus offset */

/* Device switch */
struct devsw {
	char	*dv_name;
	int	(*dv_strategy)(void *devdata, int rw,
				    daddr_t blk, size_t size,
				    void *buf, size_t *rsize);
	int	(*dv_open)(struct open_file *f, ...);
	int	(*dv_close)(struct open_file *f);
	int	(*dv_ioctl)(struct open_file *f, u_long cmd, void *data);
};

extern struct devsw devsw[];	/* device array */
extern int ndevs;		/* number of elements in devsw[] */

extern struct consdev *cn_tab;

struct open_file {
	int		f_flags;	/* see F_* below */
	struct devsw	*f_dev;		/* pointer to device operations */
	void		*f_devdata;	/* device specific data */
	struct fs_ops	*f_ops;		/* pointer to file system operations */
	void		*f_fsdata;	/* file system specific data */
	off_t		f_offset;	/* current file offset (F_RAW) */
};

#define	SOPEN_MAX	4
extern struct open_file files[];

/* f_flags values */
#define F_READ          0x0001 /* file opened for reading */
#define F_WRITE         0x0002 /* file opened for writing */
#define F_RAW           0x0004 /* raw device open - no file system */
#define F_NODEV         0x0008 /* network open - no device */
#define F_NOWRITE       0x0010 /* bootblock writing broken or unsupported */

#define isupper(c)	((c) >= 'A' && (c) <= 'Z')
#define islower(c)	((c) >= 'a' && (c) <= 'z')
#define isalpha(c)	(isupper(c)||islower(c))
#define tolower(c)	(isupper(c)?((c) - 'A' + 'a'):(c))
#define toupper(c)	(islower(c)?((c) - 'a' + 'A'):(c))
#define isspace(c)	((c) == ' ' || (c) == '\t')
#define isdigit(c)	((c) >= '0' && (c) <= '9')

#define	btochs(b,c,h,s,nh,ns)			\
	c = (b) / ((nh) * (ns));		\
	h = ((b) % ((nh) * (ns))) / (ns);	\
	s = ((b) % ((nh) * (ns))) % (ns);

void	*alloc(u_int);
void	free(void *, u_int);
struct	disklabel;
char	*getdisklabel(const char *, struct disklabel *);
u_int	dkcksum(const struct disklabel *);

#define BOOTRANDOM	"/etc/random.seed"
#define BOOTRANDOM_MAX	256	/* no point being greater than RC4STATE */
extern char rnddata[BOOTRANDOM_MAX];

void	printf(const char *, ...);
int	snprintf(char *, size_t, const char *, ...);
void	vprintf(const char *, __va_list);
void	twiddle(void);
void	getln(char *, size_t);
__dead void	panic(const char *, ...) __attribute__((noreturn));
__dead void	_rtt(void) __attribute__((noreturn));
#define	bzero(s,n)	((void)memset((s),0,(n)))
#define bcmp(s1,s2,n)	(memcmp((s2),(s1),(n)))
#define	bcopy(s1,s2,n)	((void)memmove((s2),(s1),(n)))
void	explicit_bzero(void *, size_t);
void	hexdump(const void *, size_t);
void	*memcpy(void *, const void *, size_t);
void	*memmove(void *, const void *, size_t);
int	memcmp(const void *, const void *, size_t);
char	*strncpy(char *, const char *, size_t);
int	strncmp(const char *, const char *, size_t);
int	strcmp(const char *, const char *);
size_t	strlen(const char *);
long	strtol(const char *, char **, int);
long long	strtoll(const char *, char **, int);
char	*strchr(const char *, int);
void	*memset(void *, int, size_t);
void	exit(void);
#define O_RDONLY        0x0000          /* open for reading only */
#define O_WRONLY        0x0001          /* open for writing only */
#define O_RDWR          0x0002          /* open for reading and writing */
int	open(const char *, int);
int	close(int);
void	closeall(void);
ssize_t	read(int, void *, size_t);
ssize_t	write(int, void *, size_t);
int	stat(const char *path, struct stat *sb);
int	fstat(int fd, struct stat *sb);
off_t	lseek(int, off_t, int);
int	opendir(const char *);
int	readdir(int, char *);
void	closedir(int);
int	nodev(void);
int	noioctl(struct open_file *, u_long, void *);
void	nullsys(void);

int	null_open(char *path, struct open_file *f);
int	null_close(struct open_file *f);
ssize_t	null_read(struct open_file *f, void *buf, size_t size, size_t *resid);
ssize_t	null_write(struct open_file *f, void *buf, size_t size, size_t *resid);
off_t	null_seek(struct open_file *f, off_t offset, int where);
int	null_stat(struct open_file *f, struct stat *sb);
int	null_readdir(struct open_file *f, char *name);
char	*ttyname(int); /* match userland decl, but ignore !0 */
dev_t	ttydev(char *);
void	cninit(void);
int	cnset(dev_t);
void	cnputc(int);
int	cngetc(void);
int	cnischar(void);
int	cnspeed(dev_t, int);
u_int	sleep(u_int);
void	usleep(u_int);
char	*ctime(const time_t *);

int	ioctl(int, u_long, char *);

void	putchar(int);
int	getchar(void);

#ifdef __INTERNAL_LIBSA_CREAD
int	oopen(const char *, int);
int	oclose(int);
ssize_t	oread(int, void *, size_t);
off_t	olseek(int, off_t, int);
#endif

/* Machine dependent functions */
int	devopen(struct open_file *, const char *, char **);
void	machdep_start(char *, int, char *, char *, char *);
time_t	getsecs(void);
