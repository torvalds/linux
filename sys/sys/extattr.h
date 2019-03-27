/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2001 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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
/*
 * Developed by the TrustedBSD Project.
 * Support for extended filesystem attributes.
 */

#ifndef _SYS_EXTATTR_H_
#define	_SYS_EXTATTR_H_

/*
 * Defined name spaces for extended attributes.  Numeric constants are passed
 * via system calls, but a user-friendly string is also defined.
 */
#define	EXTATTR_NAMESPACE_EMPTY		0x00000000
#define	EXTATTR_NAMESPACE_EMPTY_STRING	"empty"
#define	EXTATTR_NAMESPACE_USER		0x00000001
#define	EXTATTR_NAMESPACE_USER_STRING	"user"
#define	EXTATTR_NAMESPACE_SYSTEM	0x00000002
#define	EXTATTR_NAMESPACE_SYSTEM_STRING	"system"

/*
 * The following macro is designed to initialize an array that maps
 * extended-attribute namespace values to their names, e.g.:
 *
 * char *extattr_namespace_names[] = EXTATTR_NAMESPACE_NAMES;
 */
#define EXTATTR_NAMESPACE_NAMES { \
	EXTATTR_NAMESPACE_EMPTY_STRING, \
	EXTATTR_NAMESPACE_USER_STRING, \
	EXTATTR_NAMESPACE_SYSTEM_STRING }

#define	EXTATTR_MAXNAMELEN	NAME_MAX

#ifdef _KERNEL
#include <sys/types.h>

struct thread;
struct ucred;
struct vnode;
int	extattr_check_cred(struct vnode *vp, int attrnamespace,
	    struct ucred *cred, struct thread *td, accmode_t accmode);

#else
#include <sys/cdefs.h>

struct iovec;

__BEGIN_DECLS
int	extattrctl(const char *_path, int _cmd, const char *_filename,
	    int _attrnamespace, const char *_attrname);
int	extattr_delete_fd(int _fd, int _attrnamespace, const char *_attrname);
int	extattr_delete_file(const char *_path, int _attrnamespace,
	    const char *_attrname);
int	extattr_delete_link(const char *_path, int _attrnamespace,
	    const char *_attrname);
ssize_t	extattr_get_fd(int _fd, int _attrnamespace, const char *_attrname,
	    void *_data, size_t _nbytes);
ssize_t	extattr_get_file(const char *_path, int _attrnamespace,
	    const char *_attrname, void *_data, size_t _nbytes);
ssize_t	extattr_get_link(const char *_path, int _attrnamespace,
	    const char *_attrname, void *_data, size_t _nbytes);
ssize_t	extattr_list_fd(int _fd, int _attrnamespace, void *_data,
	    size_t _nbytes);
ssize_t	extattr_list_file(const char *_path, int _attrnamespace, void *_data,
	    size_t _nbytes);
ssize_t	extattr_list_link(const char *_path, int _attrnamespace, void *_data,
	    size_t _nbytes);
ssize_t	extattr_set_fd(int _fd, int _attrnamespace, const char *_attrname,
	    const void *_data, size_t _nbytes);
ssize_t	extattr_set_file(const char *_path, int _attrnamespace,
	    const char *_attrname, const void *_data, size_t _nbytes);
ssize_t	extattr_set_link(const char *_path, int _attrnamespace,
	    const char *_attrname, const void *_data, size_t _nbytes);
__END_DECLS

#endif /* !_KERNEL */
#endif /* !_SYS_EXTATTR_H_ */
