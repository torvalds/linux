/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Definitions for various global variables and structures
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 */
#ifndef _SMB2_GLOB_H
#define _SMB2_GLOB_H

/*
 *****************************************************************
 * Constants go here
 *****************************************************************
 */

/*
 * Identifiers for functions that use the open, operation, close pattern
 * in smb2inode.c:smb2_compound_op()
 */
enum smb2_compound_ops {
	SMB2_OP_SET_DELETE = 1,
	SMB2_OP_SET_INFO,
	SMB2_OP_QUERY_INFO,
	SMB2_OP_QUERY_DIR,
	SMB2_OP_MKDIR,
	SMB2_OP_RENAME,
	SMB2_OP_DELETE,
	SMB2_OP_HARDLINK,
	SMB2_OP_SET_EOF,
	SMB2_OP_RMDIR,
	SMB2_OP_POSIX_QUERY_INFO,
	SMB2_OP_SET_REPARSE
};

/* Used when constructing chained read requests. */
#define CHAINED_REQUEST 1
#define START_OF_CHAIN 2
#define END_OF_CHAIN 4
#define RELATED_REQUEST 8

#endif	/* _SMB2_GLOB_H */
