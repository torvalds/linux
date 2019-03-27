/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
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
 * 3. The names of the authors may not be used to endorse or promote
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/mac.h>
#include <sys/socket.h>

extern int __mac_get_fd(int fd, struct mac *mac_p);
extern int __mac_get_file(const char *path_p, struct mac *mac_p);
extern int __mac_get_link(const char *path_p, struct mac *mac_p);
extern int __mac_get_pid(pid_t pid, struct mac *mac_p);
extern int __mac_get_proc(struct mac *mac_p);

int
mac_get_fd(int fd, struct mac *label)
{

	return (__mac_get_fd(fd, label));
}

int
mac_get_file(const char *path, struct mac *label)
{

	return (__mac_get_file(path, label));
}

int
mac_get_link(const char *path, struct mac *label)
{

	return (__mac_get_link(path, label));
}

int
mac_get_peer(int fd, struct mac *label)
{
	socklen_t len;

	len = sizeof(*label);
	return (getsockopt(fd, SOL_SOCKET, SO_PEERLABEL, label, &len));
}

int
mac_get_pid(pid_t pid, struct mac *label)
{

	return (__mac_get_pid(pid, label));
}

int
mac_get_proc(struct mac *label)
{

	return (__mac_get_proc(label));
}
