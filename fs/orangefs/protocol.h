#include <linux/spinlock_types.h>
#include <linux/types.h>
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

/* copy a khandle to a field of arbitrary size */
static inline void PVFS_khandle_to(const struct pvfs2_khandle *kh,
				   void *p, int size)
{
	int i;
	unsigned char *c = p;

	memset(p, 0, size);

	for (i = 0; i < 16 && i < size; i++)
		c[i] = kh->u[i];
}

/* copy a khandle from a field of arbitrary size */
static inline void PVFS_khandle_from(struct pvfs2_khandle *kh,
				     void *p, int size)
{
	int i;
	unsigned char *c = p;

	memset(kh, 0, 16);

	for (i = 0; i < 16 && i < size; i++)
		kh->u[i] = c[i];
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
#define PVFS_ERROR_BIT           (1 << 30)
#define PVFS_NON_ERRNO_ERROR_BIT (1 << 29)
#define IS_PVFS_ERROR(__error)   ((__error)&(PVFS_ERROR_BIT))
#define IS_PVFS_NON_ERRNO_ERROR(__error)  \
(((__error)&(PVFS_NON_ERRNO_ERROR_BIT)) && IS_PVFS_ERROR(__error))
#define PVFS_ERROR_TO_ERRNO(__error) PVFS_get_errno_mapping(__error)

/* 7 bits are used for the errno mapped error codes */
#define PVFS_ERROR_CODE(__error) \
((__error) & (__s32)(0x7f|PVFS_ERROR_BIT))
#define PVFS_ERROR_CLASS(__error) \
((__error) & ~((__s32)(0x7f|PVFS_ERROR_BIT|PVFS_NON_ERRNO_ERROR_BIT)))
#define PVFS_NON_ERRNO_ERROR_CODE(__error) \
((__error) & (__s32)(127|PVFS_ERROR_BIT|PVFS_NON_ERRNO_ERROR_BIT))

/* PVFS2 error codes, compliments of asm/errno.h */
#define PVFS_EPERM            E(1)	/* Operation not permitted */
#define PVFS_ENOENT           E(2)	/* No such file or directory */
#define PVFS_EINTR            E(3)	/* Interrupted system call */
#define PVFS_EIO              E(4)	/* I/O error */
#define PVFS_ENXIO            E(5)	/* No such device or address */
#define PVFS_EBADF            E(6)	/* Bad file number */
#define PVFS_EAGAIN           E(7)	/* Try again */
#define PVFS_ENOMEM           E(8)	/* Out of memory */
#define PVFS_EFAULT           E(9)	/* Bad address */
#define PVFS_EBUSY           E(10)	/* Device or resource busy */
#define PVFS_EEXIST          E(11)	/* File exists */
#define PVFS_ENODEV          E(12)	/* No such device */
#define PVFS_ENOTDIR         E(13)	/* Not a directory */
#define PVFS_EISDIR          E(14)	/* Is a directory */
#define PVFS_EINVAL          E(15)	/* Invalid argument */
#define PVFS_EMFILE          E(16)	/* Too many open files */
#define PVFS_EFBIG           E(17)	/* File too large */
#define PVFS_ENOSPC          E(18)	/* No space left on device */
#define PVFS_EROFS           E(19)	/* Read-only file system */
#define PVFS_EMLINK          E(20)	/* Too many links */
#define PVFS_EPIPE           E(21)	/* Broken pipe */
#define PVFS_EDEADLK         E(22)	/* Resource deadlock would occur */
#define PVFS_ENAMETOOLONG    E(23)	/* File name too long */
#define PVFS_ENOLCK          E(24)	/* No record locks available */
#define PVFS_ENOSYS          E(25)	/* Function not implemented */
#define PVFS_ENOTEMPTY       E(26)	/* Directory not empty */
					/*
#define PVFS_ELOOP           E(27)	 * Too many symbolic links encountered
					 */
#define PVFS_EWOULDBLOCK     E(28)	/* Operation would block */
#define PVFS_ENOMSG          E(29)	/* No message of desired type */
#define PVFS_EUNATCH         E(30)	/* Protocol driver not attached */
#define PVFS_EBADR           E(31)	/* Invalid request descriptor */
#define PVFS_EDEADLOCK       E(32)
#define PVFS_ENODATA         E(33)	/* No data available */
#define PVFS_ETIME           E(34)	/* Timer expired */
#define PVFS_ENONET          E(35)	/* Machine is not on the network */
#define PVFS_EREMOTE         E(36)	/* Object is remote */
#define PVFS_ECOMM           E(37)	/* Communication error on send */
#define PVFS_EPROTO          E(38)	/* Protocol error */
#define PVFS_EBADMSG         E(39)	/* Not a data message */
					/*
#define PVFS_EOVERFLOW       E(40)	 * Value too large for defined data
					 * type
					 */
					/*
#define PVFS_ERESTART        E(41)	 * Interrupted system call should be
					 * restarted
					 */
#define PVFS_EMSGSIZE        E(42)	/* Message too long */
#define PVFS_EPROTOTYPE      E(43)	/* Protocol wrong type for socket */
#define PVFS_ENOPROTOOPT     E(44)	/* Protocol not available */
#define PVFS_EPROTONOSUPPORT E(45)	/* Protocol not supported */
					/*
#define PVFS_EOPNOTSUPP      E(46)	 * Operation not supported on transport
					 * endpoint
					 */
#define PVFS_EADDRINUSE      E(47)	/* Address already in use */
#define PVFS_EADDRNOTAVAIL   E(48)	/* Cannot assign requested address */
#define PVFS_ENETDOWN        E(49)	/* Network is down */
#define PVFS_ENETUNREACH     E(50)	/* Network is unreachable */
					/*
#define PVFS_ENETRESET       E(51)	 * Network dropped connection because
					 * of reset
					 */
#define PVFS_ENOBUFS         E(52)	/* No buffer space available */
#define PVFS_ETIMEDOUT       E(53)	/* Connection timed out */
#define PVFS_ECONNREFUSED    E(54)	/* Connection refused */
#define PVFS_EHOSTDOWN       E(55)	/* Host is down */
#define PVFS_EHOSTUNREACH    E(56)	/* No route to host */
#define PVFS_EALREADY        E(57)	/* Operation already in progress */
#define PVFS_EACCES          E(58)	/* Access not allowed */
#define PVFS_ECONNRESET      E(59)	/* Connection reset by peer */
#define PVFS_ERANGE          E(60)	/* Math out of range or buf too small */

/***************** non-errno/pvfs2 specific error codes *****************/
#define PVFS_ECANCEL    (1|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDEVINIT   (2|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EDETAIL    (3|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EHOSTNTFD  (4|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_EADDRNTFD  (5|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ENORECVR   (6|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ETRYAGAIN  (7|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ENOTPVFS   (8|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))
#define PVFS_ESECURITY  (9|(PVFS_NON_ERRNO_ERROR_BIT|PVFS_ERROR_BIT))

/*
 * NOTE: PLEASE DO NOT ARBITRARILY ADD NEW ERRNO ERROR CODES!
 *
 * IF YOU CHOOSE TO ADD A NEW ERROR CODE (DESPITE OUR PLEA), YOU ALSO
 * NEED TO INCREMENT PVFS_ERRNO MAX (BELOW) AND ADD A MAPPING TO A
 * UNIX ERRNO VALUE IN THE MACROS BELOW (USED IN
 * src/common/misc/errno-mapping.c and the kernel module)
 */
#define PVFS_ERRNO_MAX          61

#define PVFS_ERROR_BMI    (1 << 7)	/* BMI-specific error */
#define PVFS_ERROR_TROVE  (2 << 7)	/* Trove-specific error */
#define PVFS_ERROR_FLOW   (3 << 7)
#define PVFS_ERROR_SM     (4 << 7)	/* state machine specific error */
#define PVFS_ERROR_SCHED  (5 << 7)
#define PVFS_ERROR_CLIENT (6 << 7)
#define PVFS_ERROR_DEV    (7 << 7)	/* device file interaction */

#define PVFS_ERROR_CLASS_BITS	\
	(PVFS_ERROR_BMI    |	\
	 PVFS_ERROR_TROVE  |	\
	 PVFS_ERROR_FLOW   |	\
	 PVFS_ERROR_SM     |	\
	 PVFS_ERROR_SCHED  |	\
	 PVFS_ERROR_CLIENT |	\
	 PVFS_ERROR_DEV)

#define DECLARE_ERRNO_MAPPING()                       \
__s32 PINT_errno_mapping[PVFS_ERRNO_MAX + 1] = { \
	0,     /* leave this one empty */                 \
	EPERM, /* 1 */                                    \
	ENOENT,                                           \
	EINTR,                                            \
	EIO,                                              \
	ENXIO,                                            \
	EBADF,                                            \
	EAGAIN,                                           \
	ENOMEM,                                           \
	EFAULT,                                           \
	EBUSY, /* 10 */                                   \
	EEXIST,                                           \
	ENODEV,                                           \
	ENOTDIR,                                          \
	EISDIR,                                           \
	EINVAL,                                           \
	EMFILE,                                           \
	EFBIG,                                            \
	ENOSPC,                                           \
	EROFS,                                            \
	EMLINK, /* 20 */                                  \
	EPIPE,                                            \
	EDEADLK,                                          \
	ENAMETOOLONG,                                     \
	ENOLCK,                                           \
	ENOSYS,                                           \
	ENOTEMPTY,                                        \
	ELOOP,                                            \
	EWOULDBLOCK,                                      \
	ENOMSG,                                           \
	EUNATCH, /* 30 */                                 \
	EBADR,                                            \
	EDEADLOCK,                                        \
	ENODATA,                                          \
	ETIME,                                            \
	ENONET,                                           \
	EREMOTE,                                          \
	ECOMM,                                            \
	EPROTO,                                           \
	EBADMSG,                                          \
	EOVERFLOW, /* 40 */                               \
	ERESTART,                                         \
	EMSGSIZE,                                         \
	EPROTOTYPE,                                       \
	ENOPROTOOPT,                                      \
	EPROTONOSUPPORT,                                  \
	EOPNOTSUPP,                                       \
	EADDRINUSE,                                       \
	EADDRNOTAVAIL,                                    \
	ENETDOWN,                                         \
	ENETUNREACH, /* 50 */                             \
	ENETRESET,                                        \
	ENOBUFS,                                          \
	ETIMEDOUT,                                        \
	ECONNREFUSED,                                     \
	EHOSTDOWN,                                        \
	EHOSTUNREACH,                                     \
	EALREADY,                                         \
	EACCES,                                           \
	ECONNRESET,   /* 59 */                            \
	ERANGE,                                           \
	0         /* PVFS_ERRNO_MAX */                    \
};                                                    \
const char *PINT_non_errno_strerror_mapping[] = {     \
	"Success", /* 0 */                                \
	"Operation cancelled (possibly due to timeout)",  \
	"Device initialization failed",                   \
	"Detailed per-server errors are available",       \
	"Unknown host",                                   \
	"No address associated with name",                \
	"Unknown server error",                           \
	"Host name lookup failure",                       \
	"Path contains non-PVFS elements",                \
	"Security error",                                 \
};                                                    \
__s32 PINT_non_errno_mapping[] = {               \
	0,     /* leave this one empty */                 \
	PVFS_ECANCEL,   /* 1 */                           \
	PVFS_EDEVINIT,  /* 2 */                           \
	PVFS_EDETAIL,   /* 3 */                           \
	PVFS_EHOSTNTFD, /* 4 */                           \
	PVFS_EADDRNTFD, /* 5 */                           \
	PVFS_ENORECVR,  /* 6 */                           \
	PVFS_ETRYAGAIN, /* 7 */                           \
	PVFS_ENOTPVFS,  /* 8 */                           \
	PVFS_ESECURITY, /* 9 */                           \
}

/*
 *   NOTE: PVFS_get_errno_mapping will convert a PVFS_ERROR_CODE to an
 *   errno value.  If the error code is a pvfs2 specific error code
 *   (i.e. a PVFS_NON_ERRNO_ERROR_CODE), PVFS_get_errno_mapping will
 *   return an index into the PINT_non_errno_strerror_mapping array which
 *   can be used for getting the pvfs2 specific strerror message given
 *   the error code.  if the value is not a recognized error code, the
 *   passed in value will be returned unchanged.
 */
#define DECLARE_ERRNO_MAPPING_AND_FN()					\
extern __s32 PINT_errno_mapping[];					\
extern __s32 PINT_non_errno_mapping[];				\
extern const char *PINT_non_errno_strerror_mapping[];			\
__s32 PVFS_get_errno_mapping(__s32 error)			\
{									\
	__s32 ret = error, mask = 0;				\
	__s32 positive = ((error > -1) ? 1 : 0);			\
	if (IS_PVFS_NON_ERRNO_ERROR((positive ? error : -error))) {	\
		mask = (PVFS_NON_ERRNO_ERROR_BIT |			\
			PVFS_ERROR_BIT |				\
			PVFS_ERROR_CLASS_BITS);				\
		ret = PVFS_NON_ERRNO_ERROR_CODE(((positive ?		\
						     error :		\
						     abs(error))) &	\
						 ~mask);		\
	}								\
	else if (IS_PVFS_ERROR((positive ? error : -error))) {		\
		mask = (PVFS_ERROR_BIT |				\
			PVFS_ERROR_CLASS_BITS);				\
		ret = PINT_errno_mapping[PVFS_ERROR_CODE(((positive ?	\
								error :	\
								abs(error))) & \
							  ~mask)];	\
	}								\
	return ret;							\
}									\
__s32 PVFS_errno_to_error(int err)					\
{									\
	__s32 e = 0;						\
									\
	for (; e < PVFS_ERRNO_MAX; ++e)					\
		if (PINT_errno_mapping[e] == err)			\
			return e | PVFS_ERROR_BIT;			\
									\
	return err;							\
}									\
DECLARE_ERRNO_MAPPING()

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
#define PVFS_util_min(x1, x2) (((x1) > (x2)) ? (x2) : (x1))
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
