/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996  Peter Wemm <peter@FreeBSD.org>.
 * All rights reserved.
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

#ifndef _LIBUTIL_H_
#define	_LIBUTIL_H_

#include <sys/cdefs.h>
#include <sys/_types.h>
#include <sys/_stdint.h>

#ifndef _GID_T_DECLARED
typedef	__gid_t		gid_t;
#define	_GID_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
typedef	__mode_t	mode_t;
#define	_MODE_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
typedef	__pid_t		pid_t;
#define	_PID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef	__uid_t		uid_t;
#define	_UID_T_DECLARED
#endif

#define	PROPERTY_MAX_NAME	64
#define	PROPERTY_MAX_VALUE	512

/* For properties.c. */
typedef struct _property {
	struct _property *next;
	char	*name;
	char	*value;
} *properties;

/* Avoid pulling in all the include files for no need. */
struct in_addr;
struct pidfh;
struct sockaddr;
struct termios;
struct winsize;

__BEGIN_DECLS
char	*auth_getval(const char *_name);
void	clean_environment(const char * const *_white,
	    const char * const *_more_white);
int	expand_number(const char *_buf, uint64_t *_num);
int	extattr_namespace_to_string(int _attrnamespace, char **_string);
int	extattr_string_to_namespace(const char *_string, int *_attrnamespace);
int	flopen(const char *_path, int _flags, ...);
int	flopenat(int _dirfd, const char *_path, int _flags, ...);
int	forkpty(int *_amaster, char *_name,
	    struct termios *_termp, struct winsize *_winp);
void	hexdump(const void *_ptr, int _length, const char *_hdr, int _flags);
int	humanize_number(char *_buf, size_t _len, int64_t _number,
	    const char *_suffix, int _scale, int _flags);
struct kinfo_file *
	kinfo_getfile(pid_t _pid, int *_cntp);
struct kinfo_vmentry *
	kinfo_getvmmap(pid_t _pid, int *_cntp);
struct kinfo_vmobject *
	kinfo_getvmobject(int *_cntp);
struct kinfo_proc *
	kinfo_getallproc(int *_cntp);
struct kinfo_proc *
	kinfo_getproc(pid_t _pid);
int	kld_isloaded(const char *_name);
int	kld_load(const char *_name);
int	login_tty(int _fd);
int	openpty(int *_amaster, int *_aslave, char *_name,
	    struct termios *_termp, struct winsize *_winp);
int	pidfile_close(struct pidfh *_pfh);
int	pidfile_fileno(const struct pidfh *_pfh);
struct pidfh *
	pidfile_open(const char *_path, mode_t _mode, pid_t *_pidptr);
int	pidfile_remove(struct pidfh *_pfh);
int	pidfile_write(struct pidfh *_pfh);
void	properties_free(properties _list);
char	*property_find(properties _list, const char *_name);
properties
	properties_read(int _fd);
int	realhostname(char *_host, size_t _hsize, const struct in_addr *_ip);
int	realhostname_sa(char *_host, size_t _hsize, struct sockaddr *_addr,
	    int _addrlen);
int	_secure_path(const char *_path, uid_t _uid, gid_t _gid);
void	trimdomain(char *_fullhost, int _hostsize);
const char *
	uu_lockerr(int _uu_lockresult);
int	uu_lock(const char *_ttyname);
int	uu_unlock(const char *_ttyname);
int	uu_lock_txfr(const char *_ttyname, pid_t _pid);

/*
 * Conditionally prototype the following functions if the include
 * files upon which they depend have been included.
 */
#ifdef _STDIO_H_
char	*fparseln(FILE *_fp, size_t *_len, size_t *_lineno,
	    const char _delim[3], int _flags);
#endif

#ifdef _PWD_H_
int	pw_copy(int _ffd, int _tfd, const struct passwd *_pw,
	    struct passwd *_old_pw);
struct passwd
	*pw_dup(const struct passwd *_pw);
int	pw_edit(int _notsetuid);
int	pw_equal(const struct passwd *_pw1, const struct passwd *_pw2);
void	pw_fini(void);
int	pw_init(const char *_dir, const char *_master);
void	pw_initpwd(struct passwd *_pw);
char	*pw_make(const struct passwd *_pw);
char	*pw_make_v7(const struct passwd *_pw);
int	pw_mkdb(const char *_user);
int	pw_lock(void);
struct passwd *
	pw_scan(const char *_line, int _flags);
const char *
	pw_tempname(void);
int	pw_tmp(int _mfd);
#endif

#ifdef _GRP_H_
int 	gr_copy(int __ffd, int _tfd, const struct group *_gr,
	    struct group *_old_gr);
struct group *
	gr_dup(const struct group *_gr);
struct group *
	gr_add(const struct group *_gr, const char *_newmember);
int	gr_equal(const struct group *_gr1, const struct group *_gr2);
void	gr_fini(void);
int	gr_init(const char *_dir, const char *_master);
int	gr_lock(void);
char	*gr_make(const struct group *_gr);
int	gr_mkdb(void);
struct group *
	gr_scan(const char *_line);
int	gr_tmp(int _mdf);
#endif

#ifdef _UFS_UFS_QUOTA_H_
struct fstab;
struct quotafile;
int	quota_check_path(const struct quotafile *_qf, const char *_path);
void	quota_close(struct quotafile *_qf);
int	quota_convert(struct quotafile *_qf, int _wordsize);
const char *
	quota_fsname(const struct quotafile *_qf);
int	quota_maxid(struct quotafile *_qf);
int	quota_off(struct quotafile *_qf);
int	quota_on(struct quotafile *_qf);
struct quotafile *
	quota_open(struct fstab *_fs, int _quotatype, int _openflags);
const char *
	quota_qfname(const struct quotafile *_qf);
int	quota_read(struct quotafile *_qf, struct dqblk *_dqb, int _id);
int	quota_write_limits(struct quotafile *_qf, struct dqblk *_dqb, int _id);
int	quota_write_usage(struct quotafile *_qf, struct dqblk *_dqb, int _id);
#endif

__END_DECLS

/* fparseln(3) */
#define	FPARSELN_UNESCESC	0x01
#define	FPARSELN_UNESCCONT	0x02
#define	FPARSELN_UNESCCOMM	0x04
#define	FPARSELN_UNESCREST	0x08
#define	FPARSELN_UNESCALL	0x0f

/* Flags for hexdump(3). */
#define	HD_COLUMN_MASK		0xff
#define	HD_DELIM_MASK		0xff00
#define	HD_OMIT_COUNT		(1 << 16)
#define	HD_OMIT_HEX		(1 << 17)
#define	HD_OMIT_CHARS		(1 << 18)

/* Values for humanize_number(3)'s flags parameter. */
#define	HN_DECIMAL		0x01
#define	HN_NOSPACE		0x02
#define	HN_B			0x04
#define	HN_DIVISOR_1000		0x08
#define	HN_IEC_PREFIXES		0x10

/* Values for humanize_number(3)'s scale parameter. */
#define	HN_GETSCALE		0x10
#define	HN_AUTOSCALE		0x20

/* Return values from realhostname(). */
#define	HOSTNAME_FOUND		0
#define	HOSTNAME_INCORRECTNAME	1
#define	HOSTNAME_INVALIDADDR	2
#define	HOSTNAME_INVALIDNAME	3

/* Flags for pw_scan(). */
#define	PWSCAN_MASTER		0x01
#define	PWSCAN_WARN		0x02

/* Return values from uu_lock(). */
#define	UU_LOCK_INUSE		1
#define	UU_LOCK_OK		0
#define	UU_LOCK_OPEN_ERR	(-1)
#define	UU_LOCK_READ_ERR	(-2)
#define	UU_LOCK_CREAT_ERR	(-3)
#define	UU_LOCK_WRITE_ERR	(-4)
#define	UU_LOCK_LINK_ERR	(-5)
#define	UU_LOCK_TRY_ERR		(-6)
#define	UU_LOCK_OWNER_ERR	(-7)

#endif /* !_LIBUTIL_H_ */
