/*
 *  linux/fs/ncpfs/symlink.c
 *
 *  Code for allowing symbolic links on NCPFS (i.e. NetWare)
 *  Symbolic links are not supported on native NetWare, so we use an
 *  infrequently-used flag (Sh) and store a two-word magic header in
 *  the file to make sure we don't accidentally use a non-link file
 *  as a link.
 *
 *  When using the NFS namespace, we set the mode to indicate a symlink and
 *  don't bother with the magic numbers.
 *
 *  from linux/fs/ext2/symlink.c
 *
 *  Copyright (C) 1998-99, Frank A. Vorstenbosch
 *
 *  ncpfs symlink handling code
 *  NLS support (c) 1999 Petr Vandrovec
 *  Modified 2000 Ben Harris, University of Cambridge for NFS NS meta-info
 *
 */


#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ncp_fs.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include "ncplib_kernel.h"


/* these magic numbers must appear in the symlink file -- this makes it a bit
   more resilient against the magic attributes being set on random files. */

#define NCP_SYMLINK_MAGIC0	cpu_to_le32(0x6c6d7973)     /* "symlnk->" */
#define NCP_SYMLINK_MAGIC1	cpu_to_le32(0x3e2d6b6e)

/* ----- read a symbolic link ------------------------------------------ */

static int ncp_symlink_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int error, length, len;
	char *link, *rawlink;
	char *buf = kmap(page);

	error = -ENOMEM;
	rawlink = kmalloc(NCP_MAX_SYMLINK_SIZE, GFP_KERNEL);
	if (!rawlink)
		goto fail;

	if (ncp_make_open(inode,O_RDONLY))
		goto failEIO;

	error=ncp_read_kernel(NCP_SERVER(inode),NCP_FINFO(inode)->file_handle,
                         0,NCP_MAX_SYMLINK_SIZE,rawlink,&length);

	ncp_inode_close(inode);
	/* Close file handle if no other users... */
	ncp_make_closed(inode);
	if (error)
		goto failEIO;

	if (NCP_FINFO(inode)->flags & NCPI_KLUDGE_SYMLINK) {
		if (length<NCP_MIN_SYMLINK_SIZE || 
		    ((__le32 *)rawlink)[0]!=NCP_SYMLINK_MAGIC0 ||
		    ((__le32 *)rawlink)[1]!=NCP_SYMLINK_MAGIC1)
		    	goto failEIO;
		link = rawlink + 8;
		length -= 8;
	} else {
		link = rawlink;
	}

	len = NCP_MAX_SYMLINK_SIZE;
	error = ncp_vol2io(NCP_SERVER(inode), buf, &len, link, length, 0);
	kfree(rawlink);
	if (error)
		goto fail;
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);
	return 0;

failEIO:
	error = -EIO;
	kfree(rawlink);
fail:
	SetPageError(page);
	kunmap(page);
	unlock_page(page);
	return error;
}

/*
 * symlinks can't do much...
 */
const struct address_space_operations ncp_symlink_aops = {
	.readpage	= ncp_symlink_readpage,
};
	
/* ----- create a new symbolic link -------------------------------------- */
 
int ncp_symlink(struct inode *dir, struct dentry *dentry, const char *symname) {
	struct inode *inode;
	char *rawlink;
	int length, err, i, outlen;
	int kludge;
	int mode;
	__le32 attr;
	unsigned int hdr;

	DPRINTK("ncp_symlink(dir=%p,dentry=%p,symname=%s)\n",dir,dentry,symname);

	if (ncp_is_nfs_extras(NCP_SERVER(dir), NCP_FINFO(dir)->volNumber))
		kludge = 0;
	else
#ifdef CONFIG_NCPFS_EXTRAS
	if (NCP_SERVER(dir)->m.flags & NCP_MOUNT_SYMLINKS)
		kludge = 1;
	else
#endif
	/* EPERM is returned by VFS if symlink procedure does not exist */
		return -EPERM;
  
	rawlink = kmalloc(NCP_MAX_SYMLINK_SIZE, GFP_KERNEL);
	if (!rawlink)
		return -ENOMEM;

	if (kludge) {
		mode = 0;
		attr = aSHARED | aHIDDEN;
		((__le32 *)rawlink)[0]=NCP_SYMLINK_MAGIC0;
		((__le32 *)rawlink)[1]=NCP_SYMLINK_MAGIC1;
		hdr = 8;
	} else {
		mode = S_IFLNK | S_IRWXUGO;
		attr = 0;
		hdr = 0;
	}			

	length = strlen(symname);
	/* map to/from server charset, do not touch upper/lower case as
	   symlink can point out of ncp filesystem */
	outlen = NCP_MAX_SYMLINK_SIZE - hdr;
	err = ncp_io2vol(NCP_SERVER(dir), rawlink + hdr, &outlen, symname, length, 0);
	if (err)
		goto failfree;

	outlen += hdr;

	err = -EIO;
	if (ncp_create_new(dir,dentry,mode,0,attr)) {
		goto failfree;
	}

	inode=dentry->d_inode;

	if (ncp_make_open(inode, O_WRONLY))
		goto failfree;

	if (ncp_write_kernel(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle, 
			     0, outlen, rawlink, &i) || i!=outlen) {
		goto fail;
	}

	ncp_inode_close(inode);
	ncp_make_closed(inode);
	kfree(rawlink);
	return 0;
fail:;
	ncp_inode_close(inode);
	ncp_make_closed(inode);
failfree:;
	kfree(rawlink);
	return err;
}

/* ----- EOF ----- */
