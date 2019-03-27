/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2002 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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
 * Userland interface for Mandatory Access Control.  Loosely based on the
 * POSIX.1e API.  More information may be found at:
 *
 * http://www.TrustedBSD.org/
 */

#ifndef _SYS_MAC_H_
#define	_SYS_MAC_H_

#ifndef _POSIX_MAC
#define	_POSIX_MAC
#endif

/*
 * MAC framework-related constants and limits.
 */
#define	MAC_MAX_POLICY_NAME		32
#define	MAC_MAX_LABEL_ELEMENT_NAME	32
#define	MAC_MAX_LABEL_ELEMENT_DATA	4096
#define	MAC_MAX_LABEL_BUF_LEN		8192

/*
 * struct mac is the data structure used to carry MAC labels in system calls
 * and ioctls between userspace and the kernel.
 */
struct mac {
	size_t		 m_buflen;
	char		*m_string;
};

typedef struct mac	*mac_t;

#ifndef _KERNEL

/*
 * Location of the userland MAC framework configuration file.  mac.conf
 * set defaults for MAC-aware applications.
 */
#define	MAC_CONFFILE	"/etc/mac.conf"

/*
 * Extended non-POSIX.1e interfaces that offer additional services available
 * from the userland and kernel MAC frameworks.
 */
__BEGIN_DECLS
int	 mac_execve(char *fname, char **argv, char **envv, mac_t _label);
int	 mac_free(mac_t _label);
int	 mac_from_text(mac_t *_label, const char *_text);
int	 mac_get_fd(int _fd, mac_t _label);
int	 mac_get_file(const char *_path, mac_t _label);
int	 mac_get_link(const char *_path, mac_t _label);
int	 mac_get_peer(int _fd, mac_t _label);
int	 mac_get_pid(pid_t _pid, mac_t _label);
int	 mac_get_proc(mac_t _label);
int	 mac_is_present(const char *_policyname);
int	 mac_prepare(mac_t *_label, const char *_elements);
int	 mac_prepare_file_label(mac_t *_label);
int	 mac_prepare_ifnet_label(mac_t *_label);
int	 mac_prepare_process_label(mac_t *_label);
int	 mac_prepare_type(mac_t *_label, const char *_type);
int	 mac_set_fd(int _fildes, const mac_t _label);
int	 mac_set_file(const char *_path, mac_t _label);
int	 mac_set_link(const char *_path, mac_t _label);
int	 mac_set_proc(const mac_t _label);
int	 mac_syscall(const char *_policyname, int _call, void *_arg);
int	 mac_to_text(mac_t mac, char **_text);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_MAC_H_ */
