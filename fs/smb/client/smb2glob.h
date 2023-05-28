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
#define SMB2_OP_SET_DELETE 1
#define SMB2_OP_SET_INFO 2
#define SMB2_OP_QUERY_INFO 3
#define SMB2_OP_QUERY_DIR 4
#define SMB2_OP_MKDIR 5
#define SMB2_OP_RENAME 6
#define SMB2_OP_DELETE 7
#define SMB2_OP_HARDLINK 8
#define SMB2_OP_SET_EOF 9
#define SMB2_OP_RMDIR 10
#define SMB2_OP_POSIX_QUERY_INFO 11

/* Used when constructing chained read requests. */
#define CHAINED_REQUEST 1
#define START_OF_CHAIN 2
#define END_OF_CHAIN 4
#define RELATED_REQUEST 8

#endif	/* _SMB2_GLOB_H */
