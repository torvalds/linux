/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 */

#ifndef O2CLUSTER_QUORUM_H
#define O2CLUSTER_QUORUM_H

void o2quo_init(void);
void o2quo_exit(void);

void o2quo_hb_up(u8 node);
void o2quo_hb_down(u8 node);
void o2quo_hb_still_up(u8 node);
void o2quo_conn_up(u8 node);
void o2quo_conn_err(u8 node);
void o2quo_disk_timeout(void);

#endif /* O2CLUSTER_QUORUM_H */
