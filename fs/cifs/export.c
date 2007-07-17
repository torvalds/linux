/*
 *   fs/cifs/export.c
 *
 *   Copyright (C) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Common Internet FileSystem (CIFS) client
 * 
 *   Operations related to support for exporting files via NFSD
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
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
 
 /* 
  * See Documentation/filesystems/Exporting
  * and examples in fs/exportfs
  */

#include <linux/fs.h>
#include <linux/exportfs.h>
 
#ifdef CONFIG_CIFS_EXPERIMENTAL
 
static struct dentry *cifs_get_parent(struct dentry *dentry)
{
 	/* BB need to add code here eventually to enable export via NFSD */
 	return ERR_PTR(-EACCES);
}
 
struct export_operations cifs_export_ops = {
 	.get_parent = cifs_get_parent,
/*	Following five export operations are unneeded so far and can default */ 	
/* 	.get_dentry =
 	.get_name =
 	.find_exported_dentry =
 	.decode_fh = 
 	.encode_fs =  */
 };
 
#endif /* EXPERIMENTAL */
 
