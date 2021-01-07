/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Witness Service client for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#ifndef _CIFS_SWN_H
#define _CIFS_SWN_H

struct cifs_tcon;
struct sk_buff;
struct genl_info;

extern int cifs_swn_register(struct cifs_tcon *tcon);

extern int cifs_swn_unregister(struct cifs_tcon *tcon);

extern int cifs_swn_notify(struct sk_buff *skb, struct genl_info *info);

extern void cifs_swn_dump(struct seq_file *m);

extern void cifs_swn_check(void);

#endif /* _CIFS_SWN_H */
