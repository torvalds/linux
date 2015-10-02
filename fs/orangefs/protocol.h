#include <linux/types.h>
#include <linux/spinlock_types.h>
#include <linux/slab.h>
#include <linux/ioctl.h>

extern struct client_debug_mask *cdm_array;
extern char *debug_help_string;
extern int help_string_initialized;
extern struct dentry *debug_dir;
extern struct dentry *help_file_dentry;
extern struct dentry *client_debug_dentry;
extern const struct file_operations debug_help_fops;
extern int client_all_index;
extern int client_verbose_index;
extern int cdm_element_count;
#define DEBUG_HELP_STRING_SIZE 4096
#define HELP_STRING_UNINITIALIZED \
	"Client Debug Keywords are unknown until the first time\n" \
	"the client is started after boot.\n"
#define ORANGEFS_KMOD_DEBUG_HELP_FILE "debug-help"
#define ORANGEFS_KMOD_DEBUG_FILE "kernel-debug"
#define ORANGEFS_CLIENT_DEBUG_FILE "client-debug"
#define PVFS2_VERBOSE "verbose"
#define PVFS2_ALL "all"

/* pvfs2-config.h ***********************************************************/
#define PVFS2_VERSION_MAJOR 2
#define PVFS2_VERSION_MINOR 9
#define PVFS2_VERSION_SUB 0

/* khandle stuff  ***********************************************************/

/*
 * The 2.9 core will put 64 bit handles in here like this:
 *    1234 0000 0000 5678
 * The 3.0 and beyond cores will put 128 bit handles in here like this:
 *    1234 5678 90AB CDEF
 * The kernel module will always use the first four bytes and
 * the last four bytes as an inum.
 */
struct pvfs2_khandle {
	unsigned char u[16];
}  __aligned(8);

/*
 * kernel version of an object ref.
 */
struct pvfs2_object_kref {
	struct pvfs2_khandle khandle;
	__s32 fs_id;
	__s32 __pad1;
};

/*
 * compare 2 khandles assumes little endian thus from large address to
 * small address
 */
static inline int PVFS_khandle_cmp(const struct pvfs2_khandle *kh1,
				   const struct pvfs2_khandle *kh2)
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

static inline void PVFS_khandle_to(const struct pvfs2_khandle *kh,
				   void *p, int size)
{

	memset(p, 0, size);
	memcpy(p, kh->u, 16);

}

static inline void PVFS_khandle_from(struct pvfs2_khandle *kh,
				     void *p, int size)
{
	memset(kh, 0, 16);
	memcpy(kh->u, p, 16);

}

/* pvfs2-types.h ************************************************************/
typedef __u32 PVFS_uid;
typedef __u32 PVFS_gid;
typedef __s32 PVFS_fs_id;
typedef __u32 PVFS_permissions;
typedef __u64 PVFS_time;
typedef __s64 PVFS_size;
typedef __u64 PVFS_flags;
typedef __u64 PVFS_ds_position;
typedef __s32 PVFS_error;
typedef __s64 PVFS_offset;

#define PVFS2_SUPER_MAGIC 0x20030528

/* PVFS2 error codes are a signed 32-bit integer. Error codes are negative, but
 * the sign is stripped before decoding. */

/* Bit 31 is not used since it is the sign. */

/* Bit 30 specifies that this is a PVFS2 error. A PVFS2 error is either an
 * encoded errno value or a PVFS2 protocol error. */
#define PVFS_ERROR_BIT (1 << 30)

/* Bit 29 specifies that this is a PVFS2 protocol error and not an encoded
 * errno value. */
#define PVFS_NON_ERRNO_ERROR_BIT (1 << 29)

/* Bits 9, 8, and 7 specify the error class, which encodes the section of
 * server code the error originated in for logging purposes. It is not used
 * in the kernel except to be masked out. */
#define PVFS_ERROR_CLASS_BITS 0x380

/* Bits 6 - 0 are reserved for the actual error code. */
#define PVFS_ERROR_NUMBER_BITS 0x7f

/* Encoded errno values are decoded by PINT_errno_mapping in pvfs2-utils.c. */

/* Our own PVFS2 protocol error codes. */
#define PVFS_ECANCEL    (1|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_EDEVINIT   (2|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_EDETAIL    (3|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_EHOSTNTFD  (4|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_EADDRNTFD  (5|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_ENORECVR   (6|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_ETRYAGAIN  (7|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_ENOTPVFS   (8|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)
#define PVFS_ESECURITY  (9|PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT)

/* permission bits */
#define PVFS_O_EXECUTE (1 << 0)
#define PVFS_O_WRITE   (1 << 1)
#define PVFS_O_READ    (1 << 2)
#define PVFS_G_EXECUTE (1 << 3)
#define PVFS_G_WRITE   (1 << 4)
#define PVFS_G_READ    (1 << 5)
#define PVFS_U_EXECUTE (1 << 6)
#define PVFS_U_WRITE   (1 << 7)
#define PVFS_U_READ    (1 << 8)
/* no PVFS_U_VTX (sticky bit) */
#define PVFS_G_SGID    (1 << 10)
#define PVFS_U_SUID    (1 << 11)

/* definition taken from stdint.h */
#define INT32_MAX (2147483647)
#define PVFS_ITERATE_START    (INT32_MAX - 1)
#define PVFS_ITERATE_END      (INT32_MAX - 2)
#define PVFS_ITERATE_NEXT     (INT32_MAX - 3)
#define PVFS_READDIR_START PVFS_ITERATE_START
#define PVFS_READDIR_END   PVFS_ITERATE_END
#define PVFS_IMMUTABLE_FL FS_IMMUTABLE_FL
#define PVFS_APPEND_FL    FS_APPEND_FL
#define PVFS_NOATIME_FL   FS_NOATIME_FL
#define PVFS_MIRROR_FL    0x01000000ULL
#define PVFS_O_EXECUTE (1 << 0)
#define PVFS_FS_ID_NULL       ((__s32)0)
#define PVFS_ATTR_SYS_UID                   (1 << 0)
#define PVFS_ATTR_SYS_GID                   (1 << 1)
#define PVFS_ATTR_SYS_PERM                  (1 << 2)
#define PVFS_ATTR_SYS_ATIME                 (1 << 3)
#define PVFS_ATTR_SYS_CTIME                 (1 << 4)
#define PVFS_ATTR_SYS_MTIME                 (1 << 5)
#define PVFS_ATTR_SYS_TYPE                  (1 << 6)
#define PVFS_ATTR_SYS_ATIME_SET             (1 << 7)
#define PVFS_ATTR_SYS_MTIME_SET             (1 << 8)
#define PVFS_ATTR_SYS_SIZE                  (1 << 20)
#define PVFS_ATTR_SYS_LNK_TARGET            (1 << 24)
#define PVFS_ATTR_SYS_DFILE_COUNT           (1 << 25)
#define PVFS_ATTR_SYS_DIRENT_COUNT          (1 << 26)
#define PVFS_ATTR_SYS_BLKSIZE               (1 << 28)
#define PVFS_ATTR_SYS_MIRROR_COPIES_COUNT   (1 << 29)
#define PVFS_ATTR_SYS_COMMON_ALL	\
	(PVFS_ATTR_SYS_UID	|	\
	 PVFS_ATTR_SYS_GID	|	\
	 PVFS_ATTR_SYS_PERM	|	\
	 PVFS_ATTR_SYS_ATIME	|	\
	 PVFS_ATTR_SYS_CTIME	|	\
	 PVFS_ATTR_SYS_MTIME	|	\
	 PVFS_ATTR_SYS_TYPE)

#define PVFS_ATTR_SYS_ALL_SETABLE		\
(PVFS_ATTR_SYS_COMMON_ALL-PVFS_ATTR_SYS_TYPE)

#define PVFS_ATTR_SYS_ALL_NOHINT			\
	(PVFS_ATTR_SYS_COMMON_ALL		|	\
	 PVFS_ATTR_SYS_SIZE			|	\
	 PVFS_ATTR_SYS_LNK_TARGET		|	\
	 PVFS_ATTR_SYS_DFILE_COUNT		|	\
	 PVFS_ATTR_SYS_MIRROR_COPIES_COUNT	|	\
	 PVFS_ATTR_SYS_DIRENT_COUNT		|	\
	 PVFS_ATTR_SYS_BLKSIZE)
#define PVFS_XATTR_REPLACE 0x2
#define PVFS_XATTR_CREATE  0x1
#define PVFS_MAX_SERVER_ADDR_LEN 256
#define PVFS_NAME_MAX            256
/*
 * max extended attribute name len as imposed by the VFS and exploited for the
 * upcall request types.
 * NOTE: Please retain them as multiples of 8 even if you wish to change them
 * This is *NECESSARY* for supporting 32 bit user-space binaries on a 64-bit
 * kernel. Due to implementation within DBPF, this really needs to be
 * PVFS_NAME_MAX, which it was the same value as, but no reason to let it
 * break if that changes in the future.
 */
#define PVFS_MAX_XATTR_NAMELEN   PVFS_NAME_MAX	/* Not the same as
						 * XATTR_NAME_MAX defined
						 * by <linux/xattr.h>
						 */
#define PVFS_MAX_XATTR_VALUELEN  8192	/* Not the same as XATTR_SIZE_MAX
					 * defined by <linux/xattr.h>
					 */
#define PVFS_MAX_XATTR_LISTLEN   16	/* Not the same as XATTR_LIST_MAX
					 * defined by <linux/xattr.h>
					 */
/*
 * PVFS I/O operation types, used in both system and server interfaces.
 */
enum PVFS_io_type {
	PVFS_IO_READ = 1,
	PVFS_IO_WRITE = 2
};

/*
 * If this enum is modified the server parameters related to the precreate pool
 * batch and low threshold sizes may need to be modified  to reflect this
 * change.
 */
enum pvfs2_ds_type {
	PVFS_TYPE_NONE = 0,
	PVFS_TYPE_METAFILE = (1 << 0),
	PVFS_TYPE_DATAFILE = (1 << 1),
	PVFS_TYPE_DIRECTORY = (1 << 2),
	PVFS_TYPE_SYMLINK = (1 << 3),
	PVFS_TYPE_DIRDATA = (1 << 4),
	PVFS_TYPE_INTERNAL = (1 << 5)	/* for the server's private use */
};

/*
 * PVFS_certificate simply stores a buffer with the buffer size.
 * The buffer can be converted to an OpenSSL X509 struct for use.
 */
struct PVFS_certificate {
	__u32 buf_size;
	unsigned char *buf;
};

/*
 * A credential identifies a user and is signed by the client/user
 * private key.
 */
struct PVFS_credential {
	__u32 userid;	/* user id */
	__u32 num_groups;	/* length of group_array */
	__u32 *group_array;	/* groups for which the user is a member */
	char *issuer;		/* alias of the issuing server */
	__u64 timeout;	/* seconds after epoch to time out */
	__u32 sig_size;	/* length of the signature in bytes */
	unsigned char *signature;	/* digital signature */
	struct PVFS_certificate certificate;	/* user certificate buffer */
};
#define extra_size_PVFS_credential (PVFS_REQ_LIMIT_GROUPS	*	\
				    sizeof(__u32)		+	\
				    PVFS_REQ_LIMIT_ISSUER	+	\
				    PVFS_REQ_LIMIT_SIGNATURE	+	\
				    extra_size_PVFS_certificate)

/* This structure is used by the VFS-client interaction alone */
struct PVFS_keyval_pair {
	char key[PVFS_MAX_XATTR_NAMELEN];
	__s32 key_sz;	/* __s32 for portable, fixed-size structures */
	__s32 val_sz;
	char val[PVFS_MAX_XATTR_VALUELEN];
};

/* pvfs2-sysint.h ***********************************************************/
/* Describes attributes for a file, directory, or symlink. */
struct PVFS_sys_attr_s {
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
	enum pvfs2_ds_type objtype;
	__u64 flags;
	__u32 mask;
	__s64 blksize;
};

#define PVFS2_LOOKUP_LINK_NO_FOLLOW 0
#define PVFS2_LOOKUP_LINK_FOLLOW    1

/* pint-dev.h ***************************************************************/

/* parameter structure used in PVFS_DEV_DEBUG ioctl command */
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
__s32 PVFS_util_translate_mode(int mode);

/* pvfs2-debug.h ************************************************************/
#include "pvfs2-debug.h"

/* pvfs2-internal.h *********************************************************/
#define llu(x) (unsigned long long)(x)
#define lld(x) (long long)(x)

/* pint-dev-shared.h ********************************************************/
#define PVFS_DEV_MAGIC 'k'

#define PVFS2_READDIR_DEFAULT_DESC_COUNT  5

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
	PVFS_DEV_GET_MAGIC = _IOW(PVFS_DEV_MAGIC, DEV_GET_MAGIC, __s32),
	PVFS_DEV_GET_MAX_UPSIZE =
	    _IOW(PVFS_DEV_MAGIC, DEV_GET_MAX_UPSIZE, __s32),
	PVFS_DEV_GET_MAX_DOWNSIZE =
	    _IOW(PVFS_DEV_MAGIC, DEV_GET_MAX_DOWNSIZE, __s32),
	PVFS_DEV_MAP = _IO(PVFS_DEV_MAGIC, DEV_MAP),
	PVFS_DEV_REMOUNT_ALL = _IO(PVFS_DEV_MAGIC, DEV_REMOUNT_ALL),
	PVFS_DEV_DEBUG = _IOR(PVFS_DEV_MAGIC, DEV_DEBUG, __s32),
	PVFS_DEV_UPSTREAM = _IOW(PVFS_DEV_MAGIC, DEV_UPSTREAM, int),
	PVFS_DEV_CLIENT_MASK = _IOW(PVFS_DEV_MAGIC,
				    DEV_CLIENT_MASK,
				    struct dev_mask2_info_s),
	PVFS_DEV_CLIENT_STRING = _IOW(PVFS_DEV_MAGIC,
				      DEV_CLIENT_STRING,
				      char *),
	PVFS_DEV_MAXNR = DEV_MAX_NR,
};

/*
 * version number for use in communicating between kernel space and user
 * space
 */
/*
#define PVFS_KERNEL_PROTO_VERSION			\
		((PVFS2_VERSION_MAJOR * 10000)	+	\
		 (PVFS2_VERSION_MINOR * 100)	+	\
		 PVFS2_VERSION_SUB)
*/
#define PVFS_KERNEL_PROTO_VERSION 0

/*
 * describes memory regions to map in the PVFS_DEV_MAP ioctl.
 * NOTE: See devpvfs2-req.c for 32 bit compat structure.
 * Since this structure has a variable-sized layout that is different
 * on 32 and 64 bit platforms, we need to normalize to a 64 bit layout
 * on such systems before servicing ioctl calls from user-space binaries
 * that may be 32 bit!
 */
struct PVFS_dev_map_desc {
	void *ptr;
	__s32 total_size;
	__s32 size;
	__s32 count;
};

/* gossip.h *****************************************************************/

#ifdef GOSSIP_DISABLE_DEBUG
#define gossip_debug(mask, format, f...) do {} while (0)
#else
extern __u64 gossip_debug_mask;
extern struct client_debug_mask client_debug_mask;

/* try to avoid function call overhead by checking masks in macro */
#define gossip_debug(mask, format, f...)			\
do {								\
	if (gossip_debug_mask & mask)				\
		printk(format, ##f);				\
} while (0)
#endif /* GOSSIP_DISABLE_DEBUG */

/* do file and line number printouts w/ the GNU preprocessor */
#define gossip_ldebug(mask, format, f...)				\
		gossip_debug(mask, "%s: " format, __func__, ##f)

#define gossip_err printk
#define gossip_lerr(format, f...)					\
		gossip_err("%s line %d: " format,			\
			   __FILE__,					\
			   __LINE__,					\
			   ##f)
