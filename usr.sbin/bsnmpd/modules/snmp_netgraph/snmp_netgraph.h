/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY FRAUNHOFER FOKUS
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * FRAUNHOFER FOKUS OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Netgraph interface for SNMPd. Exported stuff.
 */
#ifndef SNMP_NETGRAPH_H_
#define SNMP_NETGRAPH_H_

#include <netgraph/ng_message.h>

extern ng_ID_t	snmp_node;
extern u_char *snmp_nodename;

typedef void ng_cookie_f(const struct ng_mesg *, const char *, ng_ID_t, void *);
typedef void ng_hook_f(const char *, const u_char *, size_t, void *);

void *ng_register_cookie(const struct lmodule *, u_int32_t cookie,
    ng_ID_t, ng_cookie_f *, void *);
void ng_unregister_cookie(void *reg);

void *ng_register_hook(const struct lmodule *, const char *,
    ng_hook_f *, void *);
void ng_unregister_hook(void *reg);

void ng_unregister_module(const struct lmodule *);

int ng_output(const char *path, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);
int ng_output_node(const char *node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);
int ng_output_id(ng_ID_t node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);

struct ng_mesg *ng_dialog(const char *path, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);
struct ng_mesg *ng_dialog_node(const char *node, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);
struct ng_mesg *ng_dialog_id(ng_ID_t id, u_int cookie, u_int opcode,
    const void *arg, size_t arglen);

int ng_send_data(const char *hook, const void *sndbuf, size_t sndlen);

ng_ID_t ng_mkpeer_id(ng_ID_t, const char *name, const char *type,
    const char *hook, const char *peerhook);
int ng_connect_node(const char *node, const char *ourhook, const char *peerhook);
int ng_connect_id(ng_ID_t id, const char *ourhook, const char *peerhook);
int ng_connect2_id(ng_ID_t id, ng_ID_t peer, const char *ourhook,
    const char *peerhook);
int ng_connect2_tee_id(ng_ID_t id, ng_ID_t peer, const char *ourhook,
    const char *peerhook);
int ng_rmhook(const char *ourhook);
int ng_rmhook_id(ng_ID_t, const char *);
int ng_rmhook_tee_id(ng_ID_t, const char *);
int ng_shutdown_id(ng_ID_t);

ng_ID_t ng_next_node_id(ng_ID_t node, const char *type, const char *hook);
ng_ID_t ng_node_id(const char *path);
ng_ID_t ng_node_id_node(const char *node);
ng_ID_t ng_node_name(ng_ID_t, char *);
ng_ID_t ng_node_type(ng_ID_t, char *);
int ng_peer_hook_id(ng_ID_t, const char *, char *);

#endif
