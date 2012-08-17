/*
 *   fs/cifs/smb2glob.h
 *
 *   Definitions for various global variables and structures
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
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
 * in smb2inode.c:smb2_open_op_close()
 */
#define SMB2_OP_SET_DELETE 1
#define SMB2_OP_SET_INFO 2
#define SMB2_OP_QUERY_INFO 3
#define SMB2_OP_QUERY_DIR 4
#define SMB2_OP_MKDIR 5
#define SMB2_OP_RENAME 6
#define SMB2_OP_DELETE 7

#endif	/* _SMB2_GLOB_H */
