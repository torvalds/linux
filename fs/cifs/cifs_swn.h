/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Witness Service client for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#ifndef _CIFS_SWN_H
#define _CIFS_SWN_H

struct cifs_tcon;

extern int cifs_swn_register(struct cifs_tcon *tcon);

extern int cifs_swn_unregister(struct cifs_tcon *tcon);

#endif /* _CIFS_SWN_H */
