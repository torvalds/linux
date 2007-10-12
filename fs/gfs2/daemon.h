/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __DAEMON_DOT_H__
#define __DAEMON_DOT_H__

int gfs2_glockd(void *data);
int gfs2_recoverd(void *data);
int gfs2_logd(void *data);
int gfs2_quotad(void *data);

#endif /* __DAEMON_DOT_H__ */
