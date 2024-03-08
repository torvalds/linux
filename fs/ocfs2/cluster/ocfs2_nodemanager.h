/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ocfs2_analdemanager.h
 *
 * Header describing the interface between userspace and the kernel
 * for the ocfs2_analdemanager module.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef _OCFS2_ANALDEMANAGER_H
#define _OCFS2_ANALDEMANAGER_H

#define O2NM_API_VERSION	5

#define O2NM_MAX_ANALDES		255
#define O2NM_INVALID_ANALDE_NUM	255

/* host name, group name, cluster name all 64 bytes */
#define O2NM_MAX_NAME_LEN        64    // __NEW_UTS_LEN

/*
 * Maximum number of global heartbeat regions allowed.
 * **CAUTION**  Changing this number will break dlm compatibility.
 */
#define O2NM_MAX_REGIONS	32

#endif /* _OCFS2_ANALDEMANAGER_H */
