/*	$OpenBSD: dirent.h,v 1.35 2024/04/15 15:47:58 florian Exp $	*/
/*	$NetBSD: dirent.h,v 1.9 1995/03/26 20:13:37 jtc Exp $	*/

/*-
 * Copyright (c) 1989, 1993
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
 *	@(#)dirent.h	8.2 (Berkeley) 7/28/94
 */

#ifndef _DIRENT_H_
#define _DIRENT_H_

#include <sys/cdefs.h>

/*
 * POSIX doesn't mandate this, but X/Open XPG 4.2 does.
 */
#if __BSD_VISIBLE || __XPG_VISIBLE >= 400
#include <sys/types.h>
#else
#include <sys/_types.h>
#endif

/*
 * The kernel defines the format of directory entries returned by 
 * the getdents(2) system call.
 */
#include <sys/dirent.h>

#if __BSD_VISIBLE || __XPG_VISIBLE
#define	d_ino		d_fileno	/* backward compatibility */
#endif

typedef struct _dirdesc DIR;

#if __BSD_VISIBLE

/* definitions for library routines operating on directories. */
#define	DIRBLKSIZ	1024

#include <sys/_null.h>

#endif /* __BSD_VISIBLE */

__BEGIN_DECLS
DIR *opendir(const char *);
#if __POSIX_VISIBLE >= 200809
DIR *fdopendir(int);
#endif
struct dirent *readdir(DIR *);
void rewinddir(DIR *);
int closedir(DIR *);
#if __BSD_VISIBLE
int getdents(int, void *, size_t)
		__attribute__ ((__bounded__(__string__,2,3)));
#endif /* __BSD_VISIBLE */
#if __XPG_VISIBLE
long telldir(DIR *);
void seekdir(DIR *, long);
#endif
#if __POSIX_VISIBLE >= 199506 || __XPG_VISIBLE >= 500
int readdir_r(DIR *__restrict, struct dirent *__restrict,
    struct dirent **__restrict);
#endif
#if __POSIX_VISIBLE >= 200809
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *),
    int (*)(const struct dirent **, const struct dirent **));
int scandirat(int, const char *, struct dirent ***,
    int (*)(const struct dirent *),
    int (*)(const struct dirent **, const struct dirent **));
int alphasort(const struct dirent **, const struct dirent **);
#elif __BSD_VISIBLE
int scandir(const char *, struct dirent ***, int (*)(struct dirent *),
    int (*)(const void *, const void *));
int alphasort(const void *, const void *);
#endif
#if __POSIX_VISIBLE >= 200809 || __XPG_VISIBLE > 600
int dirfd(DIR *);
#endif
__END_DECLS

#endif /* !_DIRENT_H_ */
