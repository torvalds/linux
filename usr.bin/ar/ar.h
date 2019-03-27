/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Kai Wang
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
 */

#define	BSDAR_VERSION	"1.1.0"

/*
 * ar(1) options.
 */
#define AR_A	0x0001		/* position-after */
#define AR_B	0x0002		/* position-before */
#define AR_C	0x0004		/* creating new archive */
#define AR_CC	0x0008		/* do not overwrite when extracting */
#define AR_J	0x0010		/* bzip2 compression */
#define AR_O	0x0020		/* preserve original mtime when extracting */
#define AR_S	0x0040		/* write archive symbol table */
#define AR_SS	0x0080		/* do not write archive symbol table */
#define AR_TR	0x0100		/* only keep first 15 chars for member name */
#define AR_U	0x0200		/* only extract or update newer members.*/
#define AR_V	0x0400		/* verbose mode */
#define AR_Z	0x0800		/* gzip compression */
#define AR_D	0x1000		/* insert dummy mode, mtime, uid and gid */

#define DEF_BLKSZ 10240		/* default block size */

/*
 * Convenient wrapper for general libarchive error handling.
 */
#define	AC(CALL) do {							\
	if ((CALL))							\
		bsdar_errc(bsdar, EX_SOFTWARE, archive_errno(a), "%s",	\
		    archive_error_string(a));				\
} while (0)

/*
 * In-memory representation of archive member(object).
 */
struct ar_obj {
	char		 *name;		/* member name */
	void		 *maddr;	/* mmap start address */
	uid_t		  uid;		/* user id */
	gid_t		  gid;		/* group id */
	mode_t		  md;		/* octal file permissions */
	size_t		  size;		/* member size */
	time_t		  mtime;	/* modification time */
	int		  fd;		/* file descriptor */
	dev_t		  dev;		/* inode's device */
	ino_t		  ino;		/* inode's number */

	TAILQ_ENTRY(ar_obj) objs;
};

/*
 * Structure encapsulates the "global" data for "ar" program.
 */
struct bsdar {
	const char	 *filename;	/* archive name. */
	const char	 *addlib;	/* target of ADDLIB. */
	const char	 *posarg;	/* position arg for modifiers -a, -b. */
	char		  mode;		/* program mode */
	int		  options;	/* command line options */

	const char	 *progname;	/* program name */
	int		  argc;
	char		**argv;

	/*
	 * Fields for the archive string table.
	 */
	char		 *as;		/* buffer for archive string table. */
	size_t		  as_sz;	/* current size of as table. */
	size_t		  as_cap;	/* capacity of as table buffer. */

	/*
	 * Fields for the archive symbol table.
	 */
	uint32_t	  s_cnt;	/* current number of symbols. */
	uint32_t	 *s_so;		/* symbol offset table. */
	size_t		  s_so_cap;	/* capacity of so table buffer. */
	char		 *s_sn;		/* symbol name table */
	size_t		  s_sn_cap;	/* capacity of sn table buffer. */
	size_t		  s_sn_sz;	/* current size of sn table. */
	/* Current member's offset (relative to the end of pseudo members.) */
	off_t		  rela_off;

	TAILQ_HEAD(, ar_obj) v_obj;	/* object(member) list */
};

void	bsdar_errc(struct bsdar *, int _eval, int _code,
	    const char *fmt, ...) __dead2;
void	bsdar_warnc(struct bsdar *, int _code, const char *fmt, ...);
void	ar_mode_d(struct bsdar *bsdar);
void	ar_mode_m(struct bsdar *bsdar);
void	ar_mode_p(struct bsdar *bsdar);
void	ar_mode_q(struct bsdar *bsdar);
void	ar_mode_r(struct bsdar *bsdar);
void	ar_mode_s(struct bsdar *bsdar);
void	ar_mode_t(struct bsdar *bsdar);
void	ar_mode_x(struct bsdar *bsdar);
void	ar_mode_A(struct bsdar *bsdar);
void	ar_mode_script(struct bsdar *ar);
