/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#ifndef AUTOFS_IOCTL_H
#define	AUTOFS_IOCTL_H

#define	AUTOFS_PATH		"/dev/autofs"

struct autofs_daemon_request {
	/*
	 * Request identifier.
	 */
	int		adr_id;

	/*
	 * The "from" field, containing map name.  For example,
	 * when accessing '/net/192.168.1.3/tank/vm/', that would
	 * be '-hosts'.
	 */
	char		adr_from[MAXPATHLEN];

	/*
	 * Full path to the node being looked up; for requests that result
	 * in actual mount it is the full mount path.
	 */
	char		adr_path[MAXPATHLEN];

	/*
	 * Prefix, which is basically the mountpoint from auto_master(5).
	 * In example above that would be "/net"; for direct maps it is "/".
	 */
	char		adr_prefix[MAXPATHLEN];

	/*
	 * Map key, also used as command argument for dynamic maps; in example
	 * above that would be '192.168.1.3'.
	 */
	char		adr_key[MAXPATHLEN];

	/*
	 * Mount options from auto_master(5).
	 */
	char		adr_options[MAXPATHLEN];
};

/*
 * Compatibility with 10.1-RELEASE automountd(8).
 */
struct autofs_daemon_done_101 {
	/*
	 * Identifier, copied from adr_id.
	 */
	int		add_id;

	/*
	 * Error number, possibly returned to userland.
	 */
	int		add_error;
};

struct autofs_daemon_done {
	/*
	 * Identifier, copied from adr_id.
	 */
	int		add_id;

	/*
	 * Set to 1 if the map may contain wildcard entries;
	 * otherwise autofs will do negative caching.
	 */
	int		add_wildcards;

	/*
	 * Error number, possibly returned to userland.
	 */
	int		add_error;

	/*
	 * Reserved for future use.
	 */
	int		add_spare[7];
};

#define	AUTOFSREQUEST	_IOR('I', 0x01, struct autofs_daemon_request)
#define	AUTOFSDONE101	_IOW('I', 0x02, struct autofs_daemon_done_101)
#define	AUTOFSDONE	_IOW('I', 0x03, struct autofs_daemon_done)

#endif /* !AUTOFS_IOCTL_H */
