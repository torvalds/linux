#ifndef LINUX_EXPORTFS_H
#define LINUX_EXPORTFS_H 1

#include <linux/types.h>

struct dentry;
struct inode;
struct super_block;
struct vfsmount;

/*
 * The fileid_type identifies how the file within the filesystem is encoded.
 * In theory this is freely set and parsed by the filesystem, but we try to
 * stick to conventions so we can share some generic code and don't confuse
 * sniffers like ethereal/wireshark.
 *
 * The filesystem must not use the value '0' or '0xff'.
 */
enum fid_type {
	/*
	 * The root, or export point, of the filesystem.
	 * (Never actually passed down to the filesystem.
	 */
	FILEID_ROOT = 0,

	/*
	 * 32bit inode number, 32 bit generation number.
	 */
	FILEID_INO32_GEN = 1,

	/*
	 * 32bit inode number, 32 bit generation number,
	 * 32 bit parent directory inode number.
	 */
	FILEID_INO32_GEN_PARENT = 2,

	/*
	 * 64 bit object ID, 64 bit root object ID,
	 * 32 bit generation number.
	 */
	FILEID_BTRFS_WITHOUT_PARENT = 0x4d,

	/*
	 * 64 bit object ID, 64 bit root object ID,
	 * 32 bit generation number,
	 * 64 bit parent object ID, 32 bit parent generation.
	 */
	FILEID_BTRFS_WITH_PARENT = 0x4e,

	/*
	 * 64 bit object ID, 64 bit root object ID,
	 * 32 bit generation number,
	 * 64 bit parent object ID, 32 bit parent generation,
	 * 64 bit parent root object ID.
	 */
	FILEID_BTRFS_WITH_PARENT_ROOT = 0x4f,

	/*
	 * 32 bit block number, 16 bit partition reference,
	 * 16 bit unused, 32 bit generation number.
	 */
	FILEID_UDF_WITHOUT_PARENT = 0x51,

	/*
	 * 32 bit block number, 16 bit partition reference,
	 * 16 bit unused, 32 bit generation number,
	 * 32 bit parent block number, 32 bit parent generation number
	 */
	FILEID_UDF_WITH_PARENT = 0x52,
};

struct fid {
	union {
		struct {
			u32 ino;
			u32 gen;
			u32 parent_ino;
			u32 parent_gen;
		} i32;
 		struct {
 			u32 block;
 			u16 partref;
 			u16 parent_partref;
 			u32 generation;
 			u32 parent_block;
 			u32 parent_generation;
 		} udf;
		__u32 raw[0];
	};
};

/**
 * struct export_operations - for nfsd to communicate with file systems
 * @encode_fh:      encode a file handle fragment from a dentry
 * @fh_to_dentry:   find the implied object and get a dentry for it
 * @fh_to_parent:   find the implied object's parent and get a dentry for it
 * @get_name:       find the name for a given inode in a given directory
 * @get_parent:     find the parent of a given directory
 * @commit_metadata: commit metadata changes to stable storage
 *
 * See Documentation/filesystems/nfs/Exporting for details on how to use
 * this interface correctly.
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
 * fh_to_dentry:
 *    @fh_to_dentry is given a &struct super_block (@sb) and a file handle
 *    fragment (@fh, @fh_len). It should return a &struct dentry which refers
 *    to the same file that the file handle fragment refers to.  If it cannot,
 *    it should return a %NULL pointer if the file was found but no acceptable
 *    &dentries were available, or an %ERR_PTR error code indicating why it
 *    couldn't be found (e.g. %ENOENT or %ENOMEM).  Any suitable dentry can be
 *    returned including, if necessary, a new dentry created with d_alloc_root.
 *    The caller can then find any other extant dentries by following the
 *    d_alias links.
 *
 * fh_to_parent:
 *    Same as @fh_to_dentry, except that it returns a pointer to the parent
 *    dentry if it was encoded into the filehandle fragment by @encode_fh.
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
 * commit_metadata:
 *    @commit_metadata should commit metadata changes to stable storage.
 *
 * Locking rules:
 *    get_parent is called with child->d_inode->i_mutex down
 *    get_name is not (which is possibly inconsistent)
 */

struct export_operations {
	int (*encode_fh)(struct dentry *de, __u32 *fh, int *max_len,
			int connectable);
	struct dentry * (*fh_to_dentry)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	struct dentry * (*fh_to_parent)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	int (*get_name)(struct dentry *parent, char *name,
			struct dentry *child);
	struct dentry * (*get_parent)(struct dentry *child);
	int (*commit_metadata)(struct inode *inode);
};

extern int exportfs_encode_fh(struct dentry *dentry, struct fid *fid,
	int *max_len, int connectable);
extern struct dentry *exportfs_decode_fh(struct vfsmount *mnt, struct fid *fid,
	int fh_len, int fileid_type, int (*acceptable)(void *, struct dentry *),
	void *context);

/*
 * Generic helpers for filesystems.
 */
extern struct dentry *generic_fh_to_dentry(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, u64 ino, u32 gen));
extern struct dentry *generic_fh_to_parent(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, u64 ino, u32 gen));

#endif /* LINUX_EXPORTFS_H */
