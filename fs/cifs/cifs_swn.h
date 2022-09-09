/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Witness Service client for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#ifndef _CIFS_SWN_H
#define _CIFS_SWN_H
#include "cifsglob.h"

struct cifs_tcon;
struct sk_buff;
struct genl_info;

#ifdef CONFIG_CIFS_SWN_UPCALL
extern int cifs_swn_register(struct cifs_tcon *tcon);

extern int cifs_swn_unregister(struct cifs_tcon *tcon);

extern int cifs_swn_notify(struct sk_buff *skb, struct genl_info *info);

extern void cifs_swn_dump(struct seq_file *m);

extern void cifs_swn_check(void);

static inline bool cifs_swn_set_server_dstaddr(struct TCP_Server_Info *server)
{
	if (server->use_swn_dstaddr) {
		server->dstaddr = server->swn_dstaddr;
		return true;
	}
	return false;
}

static inline void cifs_swn_reset_server_dstaddr(struct TCP_Server_Info *server)
{
	server->use_swn_dstaddr = false;
}

#else

static inline int cifs_swn_register(struct cifs_tcon *tcon) { return 0; }
static inline int cifs_swn_unregister(struct cifs_tcon *tcon) { return 0; }
static inline int cifs_swn_notify(struct sk_buff *s, struct genl_info *i) { return 0; }
static inline void cifs_swn_dump(struct seq_file *m) {}
static inline void cifs_swn_check(void) {}
static inline bool cifs_swn_set_server_dstaddr(struct TCP_Server_Info *server) { return false; }
static inline void cifs_swn_reset_server_dstaddr(struct TCP_Server_Info *server) {}

#endif /* CONFIG_CIFS_SWN_UPCALL */
#endif /* _CIFS_SWN_H */
