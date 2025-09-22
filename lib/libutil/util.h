/*	$OpenBSD: util.h,v 1.38 2022/05/11 17:23:56 millert Exp $	*/
/*	$NetBSD: util.h,v 1.2 1996/05/16 07:00:22 thorpej Exp $	*/

/*-
 * Copyright (c) 1995
 *	The Regents of the University of California.  All rights reserved.
 * Portions Copyright (c) 1996, Jason Downs.  All rights reserved.
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
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/types.h>

/*
 * fparseln() specific operation flags.
 */
#define FPARSELN_UNESCESC	0x01
#define FPARSELN_UNESCCONT	0x02
#define FPARSELN_UNESCCOMM	0x04
#define FPARSELN_UNESCREST	0x08
#define FPARSELN_UNESCALL	0x0f

/*
 * opendev() specific operation flags.
 */
#define OPENDEV_PART	0x01		/* Try to open the raw partition. */
#define OPENDEV_BLCK	0x04		/* Open block, not character device. */

/*
 * uu_lock(3) specific flags.
 */
#define UU_LOCK_INUSE (1)
#define UU_LOCK_OK (0)
#define UU_LOCK_OPEN_ERR (-1)
#define UU_LOCK_READ_ERR (-2)
#define UU_LOCK_CREAT_ERR (-3)
#define UU_LOCK_WRITE_ERR (-4)
#define UU_LOCK_LINK_ERR (-5)
#define UU_LOCK_TRY_ERR (-6)
#define UU_LOCK_OWNER_ERR (-7)

/*
 * fmt_scaled(3) specific flags.
 */
#define	FMT_SCALED_STRSIZE	7	/* minus sign, 4 digits, suffix, null byte */

/*
 * stub struct definitions.
 */
struct __sFILE;
struct login_cap;
struct passwd;
struct termios;
struct utmp;
struct winsize;

__BEGIN_DECLS
char   *fparseln(struct __sFILE *, size_t *, size_t *, const char[3], int);
void	login(struct utmp *);
int	login_tty(int);
int	logout(const char *);
void	logwtmp(const char *, const char *, const char *);
int	opendev(const char *, int, int, char **);
int	pidfile(const char *);
void	pw_setdir(const char *);
char   *pw_file(const char *);
int	pw_lock(int);
int	pw_mkdb(char *, int);
int	pw_abort(void);
void	pw_init(void);
void	pw_edit(int, const char *);
void	pw_prompt(void);
void	pw_copy(int, int, const struct passwd *, const struct passwd *);
int	pw_scan(char *, struct passwd *, int *);
__dead void pw_error(const char *, int, int);
int	getptmfd(void);
int	openpty(int *, int *, char *, const struct termios *,
	    const struct winsize *);
int	fdopenpty(int, int *, int *, char *, const struct termios *,
	    const struct winsize *);
int	opendisk(const char *, int, char *, size_t, int);
pid_t	forkpty(int *, char *, const struct termios *, const struct winsize *);
pid_t	fdforkpty(int, int *, char *, const struct termios *,
	    const struct winsize *);
int	getmaxpartitions(void);
int	getrawpartition(void);
void	login_fbtab(const char *, uid_t, gid_t);
int	login_check_expire(struct __sFILE *, struct passwd *, char *, int);
char   *readlabelfs(char *, int);
const char *uu_lockerr(int);
int     uu_lock(const char *);
int	uu_lock_txfr(const char *, pid_t);
int     uu_unlock(const char *);
int	fmt_scaled(long long, char *);
int	scan_scaled(char *, long long *);
int	isduid(const char *, int);
int	pkcs5_pbkdf2(const char *, size_t, const uint8_t *, size_t,
    uint8_t *, size_t, unsigned int);
int	bcrypt_pbkdf(const char *, size_t, const uint8_t *, size_t,
    uint8_t *, size_t, unsigned int);

__END_DECLS

#endif /* !_UTIL_H_ */
