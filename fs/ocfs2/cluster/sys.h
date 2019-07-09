/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * sys.h
 *
 * Function prototypes for o2cb sysfs interface
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 */

#ifndef O2CLUSTER_SYS_H
#define O2CLUSTER_SYS_H

void o2cb_sys_shutdown(void);
int o2cb_sys_init(void);

#endif /* O2CLUSTER_SYS_H */
