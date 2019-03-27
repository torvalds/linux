/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#define	SOCK_BACKLOG		5

#define	CM_MSG_MAXLEN		8192
#define	CM_VERSION		1
#define	CM_VERSION_STR		"1.0"

#define	CM_TYPE_EOM		0
#define	CM_TYPE_ACK		1
#define	CM_TYPE_ERR		2
#define	CM_TYPE_NUL		3
#define	CM_TYPE_REQ_SET_PROP	4
#define	CM_TYPE_REQ_GET_PROP	5
#define	CM_TYPE_MAX		6

#define	CM_STATE_EOM		0
#define	CM_STATE_INIT		1
#define	CM_STATE_MSG_DISPATCH	2
#define	CM_STATE_MSG_RECV	3
#define	CM_STATE_ACK_WAIT	4

struct ctrl_msg_hdr {
	int	cm_version;
	size_t	cm_len;
	int	cm_type;
};

struct ctrl_msg_pl {
	char	*cp_ifname;
	char	*cp_key;

	size_t	cp_val_len;
	char	*cp_val;
};

int	csock_open(struct sockinfo *, mode_t);
int	csock_close(struct sockinfo *);
int	csock_listen(struct sockinfo *);
int	csock_accept(struct sockinfo *);
int	cm_send(int, char *);
int	cm_recv(int, char *);

size_t			cm_pl2bin(char *, struct ctrl_msg_pl *);
struct ctrl_msg_pl	*cm_bin2pl(char *, struct ctrl_msg_pl *);
size_t			cm_str2bin(char *, void *, size_t);
void			*cm_bin2str(char *, void *, size_t);
