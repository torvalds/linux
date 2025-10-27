/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *
 */
#ifndef _COMMON_CIFS_GLOB_H
#define _COMMON_CIFS_GLOB_H

static inline void inc_rfc1001_len(void *buf, int count)
{
	be32_add_cpu((__be32 *)buf, count);
}

#define SMB1_VERSION_STRING	"1.0"
#define SMB20_VERSION_STRING    "2.0"
#define SMB21_VERSION_STRING	"2.1"
#define SMBDEFAULT_VERSION_STRING "default"
#define SMB3ANY_VERSION_STRING "3"
#define SMB30_VERSION_STRING	"3.0"
#define SMB302_VERSION_STRING	"3.02"
#define ALT_SMB302_VERSION_STRING "3.0.2"
#define SMB311_VERSION_STRING	"3.1.1"
#define ALT_SMB311_VERSION_STRING "3.11"

#define CIFS_DEFAULT_IOSIZE (1024 * 1024)

#endif	/* _COMMON_CIFS_GLOB_H */
