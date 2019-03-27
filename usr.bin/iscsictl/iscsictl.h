/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
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

#ifndef ISCSICTL_H
#define	ISCSICTL_H

#include <sys/queue.h>
#include <stdbool.h>
#include <libutil.h>

#define	DEFAULT_CONFIG_PATH		"/etc/iscsi.conf"
#define	DEFAULT_IQN			"iqn.1994-09.org.freebsd:"

#define	MAX_NAME_LEN			223

#define	AUTH_METHOD_UNSPECIFIED		0
#define	AUTH_METHOD_NONE		1
#define	AUTH_METHOD_CHAP		2

#define	DIGEST_UNSPECIFIED		0
#define	DIGEST_NONE			1
#define	DIGEST_CRC32C			2

#define	SESSION_TYPE_UNSPECIFIED	0
#define	SESSION_TYPE_NORMAL		1
#define	SESSION_TYPE_DISCOVERY		2

#define	PROTOCOL_UNSPECIFIED		0
#define	PROTOCOL_ISCSI			1
#define	PROTOCOL_ISER			2

#define	ENABLE_UNSPECIFIED		0
#define	ENABLE_ON			1
#define	ENABLE_OFF			2

struct target {
	TAILQ_ENTRY(target)	t_next;
	struct conf		*t_conf;
	char			*t_nickname;
	char			*t_name;
	char			*t_address;
	char			*t_initiator_name;
	char			*t_initiator_address;
	char			*t_initiator_alias;
	int			t_header_digest;
	int			t_data_digest;
	int			t_auth_method;
	int			t_session_type;
	int			t_enable;
	int			t_protocol;
	char			*t_offload;
	char			*t_user;
	char			*t_secret;
	char			*t_mutual_user;
	char			*t_mutual_secret;
};

struct conf {
	TAILQ_HEAD(, target)	conf_targets;
};

struct conf	*conf_new(void);
struct conf	*conf_new_from_file(const char *path);
void		conf_delete(struct conf *conf);
void		conf_verify(struct conf *conf);

struct target	*target_new(struct conf *conf);
struct target	*target_find(struct conf *conf, const char *nickname);
void		target_delete(struct target *ic);

void		print_periphs(int session_id);

bool		valid_iscsi_name(const char *name);
int		parse_enable(const char *enable);

#endif /* !ISCSICTL_H */
