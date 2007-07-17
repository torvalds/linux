#ifndef LINUX_EXPORTFS_H
#define LINUX_EXPORTFS_H 1

#include <linux/types.h>

struct dentry;
struct super_block;


/**
 * struct export_operations - for nfsd to communicate with file systems
 * @decode_fh:      decode a file handle fragment and return a &struct dentry
 * @encode_fh:      encode a file handle fragment from a dentry
 * @get_name:       find the name for a given inode in a given directory
 * @get_parent:     find the parent of a given directory
 * @get_dentry:     find a dentry for the inode given a file handle sub-fragment
 * @find_exported_dentry:
 *	set by the exporting module to a standard helper function.
 *
 * Description:
 *    The export_operations structure provides a means for nfsd to communicate
 *    with a particular exported file system  - particularly enabling nfsd and
 *    the filesystem to co-operate when dealing with file handles.
 *
 *    export_operations contains two basic operation for dealing with file
 *    handles, decode_fh() and encode_fh(), and allows for some other
 *    operations to be defined which standard helper routines use to get
 *    specific information from the filesystem.
 *
 *    nfsd encodes information use to determine which filesystem a filehandle
 *    applies to in the initial part of the file handle.  The remainder, termed
 *    a file handle fragment, is controlled completely by the filesystem.  The
 *    standard helper routines assume that this fragment will contain one or
 *    two sub-fragments, one which identifies the file, and one which may be
 *    used to identify the (a) directory containing the file.
 *
 *    In some situations, nfsd needs to get a dentry which is connected into a
 *    specific part of the file tree.  To allow for this, it passes the
 *    function acceptable() together with a @context which can be used to see
 *    if the dentry is acceptable.  As there can be multiple dentrys for a
 *    given file, the filesystem should check each one for acceptability before
 *    looking for the next.  As soon as an acceptable one is found, it should
 *    be returned.
 *
 * decode_fh:
 *    @decode_fh is given a &struct super_block (@sb), a file handle fragment
 *    (@fh, @fh_len) and an acceptability testing function (@acceptable,
 *    @context).  It should return a &struct dentry which refers to the same
 *    file that the file handle fragment refers to,  and which passes the
 *    acceptability test.  If it cannot, it should return a %NULL pointer if
 *    the file was found but no acceptable &dentries were available, or a
 *    %ERR_PTR error code indicating why it couldn't be found (e.g. %ENOENT or
 *    %ENOMEM).
 *
 * encode_fh:
 *    @encode_fh should store in the file handle fragment @fh (using at most
 *    @max_len bytes) information that can be used by @decode_fh to recover the
 *    file refered to by the &struct dentry @de.  If the @connectable flag is
 *    set, the encode_fh() should store sufficient information so that a good
 *    attempt can be made to find not only the file but also it's place in the
 *    filesystem.   This typically means storing a reference to de->d_parent in
 *    the filehandle fragment.  encode_fh() should return the number of bytes
 *    stored or a negative error code such as %-ENOSPC
 *
 * get_name:
 *    @get_name should find a name for the given @child in the given @parent
 *    directory.  The name should be stored in the @name (with the
 *    understanding that it is already pointing to a a %NAME_MAX+1 sized
 *    buffer.   get_name() should return %0 on success, a negative error code
 *    or error.  @get_name will be called without @parent->i_mutex held.
 *
 * get_parent:
 *    @get_parent should find the parent directory for the given @child which
 *    is also a directory.  In the event that it cannot be found, or storage
 *    space cannot be allocated, a %ERR_PTR should be returned.
 *
 * get_dentry:
 *    Given a &super_block (@sb) and a pointer to a file-system specific inode
 *    identifier, possibly an inode number, (@inump) get_dentry() should find
 *    the identified inode and return a dentry for that inode.  Any suitable
 *    dentry can be returned including, if necessary, a new dentry created with
 *    d_alloc_root.  The caller can then find any other extant dentrys by
 *    following the d_alias links.  If a new dentry was created using
 *    d_alloc_root, DCACHE_NFSD_DISCONNECTED should be set, and the dentry
 *    should be d_rehash()ed.
 *
 *    If the inode cannot be found, either a %NULL pointer or an %ERR_PTR code
 *    can be returned.  The @inump will be whatever was passed to
 *    nfsd_find_fh_dentry() in either the @obj or @parent parameters.
 *
 * Locking rules:
 *    get_parent is called with child->d_inode->i_mutex down
 *    get_name is not (which is possibly inconsistent)
 */

struct export_operations {
	struct dentry *(*decode_fh)(struct super_block *sb, __u32 *fh,
			int fh_len, int fh_type,
			int (*acceptable)(void *context, struct dentry *de),
			void *context);
	int (*encode_fh)(struct dentry *de, __u32 *fh, int *max_len,
			int connectable);
	int (*get_name)(struct dentry *parent, char *name,
			struct dentry *child);
	struct dentry * (*get_parent)(struct dentry *child);
	struct dentry * (*get_dentry)(struct super_block *sb, void *inump);

	/* This is set by the exporting module to a standard helper */
	struct dentry * (*find_exported_dentry)(
			struct super_block *sb, void *obj, void *parent,
			int (*acceptable)(void *context, struct dentry *de),
			void *context);
};

extern struct dentry *find_exported_dentry(struct super_block *sb, void *obj,
	void *parent, int (*acceptable)(void *context, struct dentry *de),
	void *context);

#endif /* LINUX_EXPORTFS_H */
