#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>

/* pvfs2-config.h ***********************************************************/
#define ORANGEFS_VERSION_MAJOR 2
#define ORANGEFS_VERSION_MINOR 9
#define ORANGEFS_VERSION_SUB 0

/* khandle stuff  ***********************************************************/

/*
 * The 2.9 core will put 64 bit handles in here like this:
 *    1234 0000 0000 5678
 * The 3.0 and beyond cores will put 128 bit handles in here like this:
 *    1234 5678 90AB CDEF
 * The kernel module will always use the first four bytes and
 * the last four bytes as an inum.
 */
struct orangefs_khandle {
	unsigned char u[16];
}  __aligned(8);

/*
 * kernel version of an object ref.
 */
struct orangefs_object_kref {
	struct orangefs_khandle khandle;
	__s32 fs_id;
	__s32 __pad1;
};

/*
 * compare 2 khandles assumes little endian thus from large address to
 * small address
 */
static inline int ORANGEFS_khandle_cmp(const struct orangefs_khandle *kh1,
				   const struct orangefs_khandle *kh2)
{
	int i;

	for (i = 15; i >= 0; i--) {
		if (kh1->u[i] > kh2->u[i])
			return 1;
		if (kh1->u[i] < kh2->u[i])
			return -1;
	}

	return 0;
}

static inline void ORANGEFS_khandle_to(const struct orangefs_khandle *kh,
				   void *p, int size)
{

	memcpy(p, kh->u, 16);
	memset(p + 16, 0, size - 16);

}

static inline void ORANGEFS_khandle_from(struct orangefs_khandle *kh,
				     void *p, int size)
{
	memset(kh, 0, 16);
	memcpy(kh->u, p, 16);

}

/* pvfs2-types.h ************************************************************/
typedef __u32 ORANGEFS_uid;
typedef __u32 ORANGEFS_gid;
typedef __s32 ORANGEFS_fs_id;
typedef __u32 ORANGEFS_permissions;
typedef __u64 ORANGEFS_time;
typedef __s64 ORANGEFS_size;
typedef __u64 ORANGEFS_flags;
typedef __u64 ORANGEFS_ds_position;
typedef __s32 ORANGEFS_error;
typedef __s64 ORANGEFS_offset;

#define ORANGEFS_SUPER_MAGIC 0x20030528

/*
 * ORANGEFS error codes are a signed 32-bit integer. Error codes are negative, but
 * the sign is stripped before decoding.
 */

/* Bit 31 is not used since it is the sign. */

/*
 * Bit 30 specifies that this is a ORANGEFS error. A ORANGEFS error is either an
 * encoded errno value or a ORANGEFS protocol error.
 */
#define ORANGEFS_ERROR_BIT (1 << 30)

/*
 * Bit 29 specifies that this is a ORANGEFS protocol error and not an encoded
 * errno value.
 */
#define ORANGEFS_NON_ERRNO_ERROR_BIT (1 << 29)

/*
 * Bits 9, 8, and 7 specify the error class, which encodes the section of
 * server code the error originated in for logging purposes. It is not used
 * in the kernel except to be masked out.
 */
#define ORANGEFS_ERROR_CLASS_BITS 0x380

/* Bits 6 - 0 are reserved for the actual error code. */
#define ORANGEFS_ERROR_NUMBER_BITS 0x7f

/* Encoded errno values decoded by PINT_errno_mapping in orangefs-utils.c. */

/* Our own ORANGEFS protocol error codes. */
#define ORANGEFS_ECANCEL    (1|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_EDEVINIT   (2|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_EDETAIL    (3|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_EHOSTNTFD  (4|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_EADDRNTFD  (5|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_ENORECVR   (6|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_ETRYAGAIN  (7|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_ENOTPVFS   (8|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)
#define ORANGEFS_ESECURITY  (9|ORANGEFS_NON_ERRNO_ERROR_BIT|ORANGEFS_ERROR_BIT)

/* permission bits */
#define ORANGEFS_O_EXECUTE (1 << 0)
#define ORANGEFS_O_WRITE   (1 << 1)
#define ORANGEFS_O_READ    (1 << 2)
#define ORANGEFS_G_EXECUTE (1 << 3)
#define ORANGEFS_G_WRITE   (1 << 4)
#define ORANGEFS_G_READ    (1 << 5)
#define ORANGEFS_U_EXECUTE (1 << 6)
#define ORANGEFS_U_WRITE   (1 << 7)
#define ORANGEFS_U_READ    (1 << 8)
/* no ORANGEFS_U_VTX (sticky bit) */
#define ORANGEFS_G_SGID    (1 << 10)
#define ORANGEFS_U_SUID    (1 << 11)

/* definition taken from stdint.h */
#define INT32_MAX (2147483647)
#define ORANGEFS_ITERATE_START    (INT32_MAX - 1)
#define ORANGEFS_ITERATE_END      (INT32_MAX - 2)
#define ORANGEFS_ITERATE_NEXT     (INT32_MAX - 3)
#define ORANGEFS_READDIR_START ORANGEFS_ITERATE_START
#define ORANGEFS_READDIR_END   ORANGEFS_ITERATE_END
#define ORANGEFS_IMMUTABLE_FL FS_IMMUTABLE_FL
#define ORANGEFS_APPEND_FL    FS_APPEND_FL
#define ORANGEFS_NOATIME_FL   FS_NOATIME_FL
#define ORANGEFS_MIRROR_FL    0x01000000ULL
#define ORANGEFS_O_EXECUTE (1 << 0)
#define ORANGEFS_FS_ID_NULL       ((__s32)0)
#define ORANGEFS_ATTR_SYS_UID                   (1 << 0)
#define ORANGEFS_ATTR_SYS_GID                   (1 << 1)
#define ORANGEFS_ATTR_SYS_PERM                  (1 << 2)
#define ORANGEFS_ATTR_SYS_ATIME                 (1 << 3)
#define ORANGEFS_ATTR_SYS_CTIME                 (1 << 4)
#define ORANGEFS_ATTR_SYS_MTIME                 (1 << 5)
#define ORANGEFS_ATTR_SYS_TYPE                  (1 << 6)
#define ORANGEFS_ATTR_SYS_ATIME_SET             (1 << 7)
#define ORANGEFS_ATTR_SYS_MTIME_SET             (1 << 8)
#define ORANGEFS_ATTR_SYS_SIZE                  (1 << 20)
#define ORANGEFS_ATTR_SYS_LNK_TARGET            (1 << 24)
#define ORANGEFS_ATTR_SYS_DFILE_COUNT           (1 << 25)
#define ORANGEFS_ATTR_SYS_DIRENT_COUNT          (1 << 26)
#define ORANGEFS_ATTR_SYS_BLKSIZE               (1 << 28)
#define ORANGEFS_ATTR_SYS_MIRROR_COPIES_COUNT   (1 << 29)
#define ORANGEFS_ATTR_SYS_COMMON_ALL	\
	(ORANGEFS_ATTR_SYS_UID	|	\
	 ORANGEFS_ATTR_SYS_GID	|	\
	 ORANGEFS_ATTR_SYS_PERM	|	\
	 ORANGEFS_ATTR_SYS_ATIME	|	\
	 ORANGEFS_ATTR_SYS_CTIME	|	\
	 ORANGEFS_ATTR_SYS_MTIME	|	\
	 ORANGEFS_ATTR_SYS_TYPE)

#define ORANGEFS_ATTR_SYS_ALL_SETABLE		\
(ORANGEFS_ATTR_SYS_COMMON_ALL-ORANGEFS_ATTR_SYS_TYPE)

#define ORANGEFS_ATTR_SYS_ALL_NOHINT			\
	(ORANGEFS_ATTR_SYS_COMMON_ALL		|	\
	 ORANGEFS_ATTR_SYS_SIZE			|	\
	 ORANGEFS_ATTR_SYS_LNK_TARGET		|	\
	 ORANGEFS_ATTR_SYS_DFILE_COUNT		|	\
	 ORANGEFS_ATTR_SYS_MIRROR_COPIES_COUNT	|	\
	 ORANGEFS_ATTR_SYS_DIRENT_COUNT		|	\
	 ORANGEFS_ATTR_SYS_BLKSIZE)

#define ORANGEFS_XATTR_REPLACE 0x2
#define ORANGEFS_XATTR_CREATE  0x1
#define ORANGEFS_MAX_SERVER_ADDR_LEN 256
#define ORANGEFS_NAME_MAX                256
/*
 * max extended attribute name len as imposed by the VFS and exploited for the
 * upcall request types.
 * NOTE: Please retain them as multiples of 8 even if you wish to change them
 * This is *NECESSARY* for supporting 32 bit user-space binaries on a 64-bit
 * kernel. Due to implementation within DBPF, this really needs to be
 * ORANGEFS_NAME_MAX, which it was the same value as, but no reason to let it
 * break if that changes in the future.
 */
#define ORANGEFS_MAX_XATTR_NAMELEN   ORANGEFS_NAME_MAX	/* Not the same as
						 * XATTR_NAME_MAX defined
						 * by <linux/xattr.h>
						 */
#define ORANGEFS_MAX_XATTR_VALUELEN  8192	/* Not the same as XATTR_SIZE_MAX
					 * defined by <linux/xattr.h>
					 */
#define ORANGEFS_MAX_XATTR_LISTLEN   16	/* Not the same as XATTR_LIST_MAX
					 * defined by <linux/xattr.h>
					 */
/*
 * ORANGEFS I/O operation types, used in both system and server interfaces.
 */
enum ORANGEFS_io_type {
	ORANGEFS_IO_READ = 1,
	ORANGEFS_IO_WRITE = 2
};

/*
 * If this enum is modified the server parameters related to the precreate pool
 * batch and low threshold sizes may need to be modified  to reflect this
 * change.
 */
enum orangefs_ds_type {
	ORANGEFS_TYPE_NONE = 0,
	ORANGEFS_TYPE_METAFILE = (1 << 0),
	ORANGEFS_TYPE_DATAFILE = (1 << 1),
	ORANGEFS_TYPE_DIRECTORY = (1 << 2),
	ORANGEFS_TYPE_SYMLINK = (1 << 3),
	ORANGEFS_TYPE_DIRDATA = (1 << 4),
	ORANGEFS_TYPE_INTERNAL = (1 << 5)	/* for the server's private use */
};

/*
 * ORANGEFS_certificate simply stores a buffer with the buffer size.
 * The buffer can be converted to an OpenSSL X509 struct for use.
 */
struct ORANGEFS_certificate {
	__u32 buf_size;
	unsigned char *buf;
};

/*
 * A credential identifies a user and is signed by the client/user
 * private key.
 */
struct ORANGEFS_credential {
	__u32 userid;	/* user id */
	__u32 num_groups;	/* length of group_array */
	__u32 *group_array;	/* groups for which the user is a member */
	char *issuer;		/* alias of the issuing server */
	__u64 timeout;	/* seconds after epoch to time out */
	__u32 sig_size;	/* length of the signature in bytes */
	unsigned char *signature;	/* digital signature */
	struct ORANGEFS_certificate certificate;	/* user certificate buffer */
};
#define extra_size_ORANGEFS_credential (ORANGEFS_REQ_LIMIT_GROUPS	*	\
				    sizeof(__u32)		+	\
				    ORANGEFS_REQ_LIMIT_ISSUER	+	\
				    ORANGEFS_REQ_LIMIT_SIGNATURE	+	\
				    extra_size_ORANGEFS_certificate)

/* This structure is used by the VFS-client interaction alone */
struct ORANGEFS_keyval_pair {
	char key[ORANGEFS_MAX_XATTR_NAMELEN];
	__s32 key_sz;	/* __s32 for portable, fixed-size structures */
	__s32 val_sz;
	char val[ORANGEFS_MAX_XATTR_VALUELEN];
};

/* pvfs2-sysint.h ***********************************************************/
/* Describes attributes for a file, directory, or symlink. */
struct ORANGEFS_sys_attr_s {
	__u32 owner;
	__u32 group;
	__u32 perms;
	__u64 atime;
	__u64 mtime;
	__u64 ctime;
	__s64 size;

	/* NOTE: caller must free if valid */
	char *link_target;

	/* Changed to __s32 so that size of structure does not change */
	__s32 dfile_count;

	/* Changed to __s32 so that size of structure does not change */
	__s32 distr_dir_servers_initial;

	/* Changed to __s32 so that size of structure does not change */
	__s32 distr_dir_servers_max;

	/* Changed to __s32 so that size of structure does not change */
	__s32 distr_dir_split_size;

	__u32 mirror_copies_count;

	/* NOTE: caller must free if valid */
	char *dist_name;

	/* NOTE: caller must free if valid */
	char *dist_params;

	__s64 dirent_count;
	enum orangefs_ds_type objtype;
	__u64 flags;
	__u32 mask;
	__s64 blksize;
};

#define ORANGEFS_LOOKUP_LINK_NO_FOLLOW 0

/* pint-dev.h ***************************************************************/

/* parameter structure used in ORANGEFS_DEV_DEBUG ioctl command */
struct dev_mask_info_s {
	enum {
		KERNEL_MASK,
		CLIENT_MASK,
	} mask_type;
	__u64 mask_value;
};

struct dev_mask2_info_s {
	__u64 mask1_value;
	__u64 mask2_value;
};

/* pvfs2-util.h *************************************************************/
__s32 ORANGEFS_util_translate_mode(int mode);

/* pvfs2-debug.h ************************************************************/
#include "orangefs-debug.h"

/* pvfs2-internal.h *********************************************************/
#define llu(x) (unsigned long long)(x)
#define lld(x) (long long)(x)

/* pint-dev-shared.h ********************************************************/
#define ORANGEFS_DEV_MAGIC 'k'

#define ORANGEFS_READDIR_DEFAULT_DESC_COUNT  5

#define DEV_GET_MAGIC           0x1
#define DEV_GET_MAX_UPSIZE      0x2
#define DEV_GET_MAX_DOWNSIZE    0x3
#define DEV_MAP                 0x4
#define DEV_REMOUNT_ALL         0x5
#define DEV_DEBUG               0x6
#define DEV_UPSTREAM            0x7
#define DEV_CLIENT_MASK         0x8
#define DEV_CLIENT_STRING       0x9
#define DEV_MAX_NR              0xa

/* supported ioctls, codes are with respect to user-space */
enum {
	ORANGEFS_DEV_GET_MAGIC = _IOW(ORANGEFS_DEV_MAGIC, DEV_GET_MAGIC, __s32),
	ORANGEFS_DEV_GET_MAX_UPSIZE =
	    _IOW(ORANGEFS_DEV_MAGIC, DEV_GET_MAX_UPSIZE, __s32),
	ORANGEFS_DEV_GET_MAX_DOWNSIZE =
	    _IOW(ORANGEFS_DEV_MAGIC, DEV_GET_MAX_DOWNSIZE, __s32),
	ORANGEFS_DEV_MAP = _IO(ORANGEFS_DEV_MAGIC, DEV_MAP),
	ORANGEFS_DEV_REMOUNT_ALL = _IO(ORANGEFS_DEV_MAGIC, DEV_REMOUNT_ALL),
	ORANGEFS_DEV_DEBUG = _IOR(ORANGEFS_DEV_MAGIC, DEV_DEBUG, __s32),
	ORANGEFS_DEV_UPSTREAM = _IOW(ORANGEFS_DEV_MAGIC, DEV_UPSTREAM, int),
	ORANGEFS_DEV_CLIENT_MASK = _IOW(ORANGEFS_DEV_MAGIC,
				    DEV_CLIENT_MASK,
				    struct dev_mask2_info_s),
	ORANGEFS_DEV_CLIENT_STRING = _IOW(ORANGEFS_DEV_MAGIC,
				      DEV_CLIENT_STRING,
				      char *),
	ORANGEFS_DEV_MAXNR = DEV_MAX_NR,
};

/*
 * version number for use in communicating between kernel space and user
 * space. Zero signifies the upstream version of the kernel module.
 */
#define ORANGEFS_KERNEL_PROTO_VERSION 0
#define ORANGEFS_MINIMUM_USERSPACE_VERSION 20903

/*
 * describes memory regions to map in the ORANGEFS_DEV_MAP ioctl.
 * NOTE: See devorangefs-req.c for 32 bit compat structure.
 * Since this structure has a variable-sized layout that is different
 * on 32 and 64 bit platforms, we need to normalize to a 64 bit layout
 * on such systems before servicing ioctl calls from user-space binaries
 * that may be 32 bit!
 */
struct ORANGEFS_dev_map_desc {
	void *ptr;
	__s32 total_size;
	__s32 size;
	__s32 count;
};

/* gossip.h *****************************************************************/

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, fmt, ...)					\
do {									\
	if (0)								\
		printk(KERN_DEBUG fmt, ##__VA_ARGS__);			\
} while (0)
#else
extern __u64 orangefs_gossip_debug_mask;

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, fmt, ...)					\
do {									\
	if (orangefs_gossip_debug_mask & (mask))			\
		printk(KERN_DEBUG fmt, ##__VA_ARGS__);			\
} while (0)
#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, fmt, ...)					\
	gossip_debug(mask, "%s: " fmt, __func__, ##__VA_ARGS__)

#define gossip_err pr_err
#define gossip_lerr(fmt, ...)						\
	gossip_err("%s line %d: " fmt,					\
		   __FILE__, __LINE__, ##__VA_ARGS__)
