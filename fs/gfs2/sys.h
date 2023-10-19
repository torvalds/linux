/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __SYS_DOT_H__
#define __SYS_DOT_H__

#include <linux/spinlock.h>
struct gfs2_sbd;

int gfs2_sys_fs_add(struct gfs2_sbd *sdp);
void gfs2_sys_fs_del(struct gfs2_sbd *sdp);

int gfs2_sys_init(void);
void gfs2_sys_uninit(void);

int gfs2_recover_set(struct gfs2_sbd *sdp, unsigned jid);

#endif /* __SYS_DOT_H__ */

