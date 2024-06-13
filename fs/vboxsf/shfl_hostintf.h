/* SPDX-License-Identifier: MIT */
/*
 * VirtualBox Shared Folders: host interface definition.
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#ifndef SHFL_HOSTINTF_H
#define SHFL_HOSTINTF_H

#include <linux/vbox_vmmdev_types.h>

/* The max in/out buffer size for a FN_READ or FN_WRITE call */
#define SHFL_MAX_RW_COUNT           (16 * SZ_1M)

/*
 * Structures shared between guest and the service
 * can be relocated and use offsets to point to variable
 * length parts.
 *
 * Shared folders protocol works with handles.
 * Before doing any action on a file system object,
 * one have to obtain the object handle via a SHFL_FN_CREATE
 * request. A handle must be closed with SHFL_FN_CLOSE.
 */

enum {
	SHFL_FN_QUERY_MAPPINGS = 1,	/* Query mappings changes. */
	SHFL_FN_QUERY_MAP_NAME = 2,	/* Query map name. */
	SHFL_FN_CREATE = 3,		/* Open/create object. */
	SHFL_FN_CLOSE = 4,		/* Close object handle. */
	SHFL_FN_READ = 5,		/* Read object content. */
	SHFL_FN_WRITE = 6,		/* Write new object content. */
	SHFL_FN_LOCK = 7,		/* Lock/unlock a range in the object. */
	SHFL_FN_LIST = 8,		/* List object content. */
	SHFL_FN_INFORMATION = 9,	/* Query/set object information. */
	/* Note function number 10 is not used! */
	SHFL_FN_REMOVE = 11,		/* Remove object */
	SHFL_FN_MAP_FOLDER_OLD = 12,	/* Map folder (legacy) */
	SHFL_FN_UNMAP_FOLDER = 13,	/* Unmap folder */
	SHFL_FN_RENAME = 14,		/* Rename object */
	SHFL_FN_FLUSH = 15,		/* Flush file */
	SHFL_FN_SET_UTF8 = 16,		/* Select UTF8 filename encoding */
	SHFL_FN_MAP_FOLDER = 17,	/* Map folder */
	SHFL_FN_READLINK = 18,		/* Read symlink dest (as of VBox 4.0) */
	SHFL_FN_SYMLINK = 19,		/* Create symlink (as of VBox 4.0) */
	SHFL_FN_SET_SYMLINKS = 20,	/* Ask host to show symlinks (4.0+) */
};

/* Root handles for a mapping are of type u32, Root handles are unique. */
#define SHFL_ROOT_NIL		UINT_MAX

/* Shared folders handle for an opened object are of type u64. */
#define SHFL_HANDLE_NIL		ULLONG_MAX

/* Hardcoded maximum length (in chars) of a shared folder name. */
#define SHFL_MAX_LEN         (256)
/* Hardcoded maximum number of shared folder mapping available to the guest. */
#define SHFL_MAX_MAPPINGS    (64)

/** Shared folder string buffer structure. */
struct shfl_string {
	/** Allocated size of the string member in bytes. */
	u16 size;

	/** Length of string without trailing nul in bytes. */
	u16 length;

	/** UTF-8 or UTF-16 string. Nul terminated. */
	union {
		u8 utf8[2];
		u16 utf16[1];
		u16 ucs2[1]; /* misnomer, use utf16. */
	} string;
};
VMMDEV_ASSERT_SIZE(shfl_string, 6);

/* The size of shfl_string w/o the string part. */
#define SHFLSTRING_HEADER_SIZE  4

/* Calculate size of the string. */
static inline u32 shfl_string_buf_size(const struct shfl_string *string)
{
	return string ? SHFLSTRING_HEADER_SIZE + string->size : 0;
}

/* Set user id on execution (S_ISUID). */
#define SHFL_UNIX_ISUID             0004000U
/* Set group id on execution (S_ISGID). */
#define SHFL_UNIX_ISGID             0002000U
/* Sticky bit (S_ISVTX / S_ISTXT). */
#define SHFL_UNIX_ISTXT             0001000U

/* Owner readable (S_IRUSR). */
#define SHFL_UNIX_IRUSR             0000400U
/* Owner writable (S_IWUSR). */
#define SHFL_UNIX_IWUSR             0000200U
/* Owner executable (S_IXUSR). */
#define SHFL_UNIX_IXUSR             0000100U

/* Group readable (S_IRGRP). */
#define SHFL_UNIX_IRGRP             0000040U
/* Group writable (S_IWGRP). */
#define SHFL_UNIX_IWGRP             0000020U
/* Group executable (S_IXGRP). */
#define SHFL_UNIX_IXGRP             0000010U

/* Other readable (S_IROTH). */
#define SHFL_UNIX_IROTH             0000004U
/* Other writable (S_IWOTH). */
#define SHFL_UNIX_IWOTH             0000002U
/* Other executable (S_IXOTH). */
#define SHFL_UNIX_IXOTH             0000001U

/* Named pipe (fifo) (S_IFIFO). */
#define SHFL_TYPE_FIFO              0010000U
/* Character device (S_IFCHR). */
#define SHFL_TYPE_DEV_CHAR          0020000U
/* Directory (S_IFDIR). */
#define SHFL_TYPE_DIRECTORY         0040000U
/* Block device (S_IFBLK). */
#define SHFL_TYPE_DEV_BLOCK         0060000U
/* Regular file (S_IFREG). */
#define SHFL_TYPE_FILE              0100000U
/* Symbolic link (S_IFLNK). */
#define SHFL_TYPE_SYMLINK           0120000U
/* Socket (S_IFSOCK). */
#define SHFL_TYPE_SOCKET            0140000U
/* Whiteout (S_IFWHT). */
#define SHFL_TYPE_WHITEOUT          0160000U
/* Type mask (S_IFMT). */
#define SHFL_TYPE_MASK              0170000U

/* Checks the mode flags indicate a directory (S_ISDIR). */
#define SHFL_IS_DIRECTORY(m)   (((m) & SHFL_TYPE_MASK) == SHFL_TYPE_DIRECTORY)
/* Checks the mode flags indicate a symbolic link (S_ISLNK). */
#define SHFL_IS_SYMLINK(m)     (((m) & SHFL_TYPE_MASK) == SHFL_TYPE_SYMLINK)

/** The available additional information in a shfl_fsobjattr object. */
enum shfl_fsobjattr_add {
	/** No additional information is available / requested. */
	SHFLFSOBJATTRADD_NOTHING = 1,
	/**
	 * The additional unix attributes (shfl_fsobjattr::u::unix_attr) are
	 *  available / requested.
	 */
	SHFLFSOBJATTRADD_UNIX,
	/**
	 * The additional extended attribute size (shfl_fsobjattr::u::size) is
	 *  available / requested.
	 */
	SHFLFSOBJATTRADD_EASIZE,
	/**
	 * The last valid item (inclusive).
	 * The valid range is SHFLFSOBJATTRADD_NOTHING thru
	 * SHFLFSOBJATTRADD_LAST.
	 */
	SHFLFSOBJATTRADD_LAST = SHFLFSOBJATTRADD_EASIZE,

	/** The usual 32-bit hack. */
	SHFLFSOBJATTRADD_32BIT_SIZE_HACK = 0x7fffffff
};

/**
 * Additional unix Attributes, these are available when
 * shfl_fsobjattr.additional == SHFLFSOBJATTRADD_UNIX.
 */
struct shfl_fsobjattr_unix {
	/**
	 * The user owning the filesystem object (st_uid).
	 * This field is ~0U if not supported.
	 */
	u32 uid;

	/**
	 * The group the filesystem object is assigned (st_gid).
	 * This field is ~0U if not supported.
	 */
	u32 gid;

	/**
	 * Number of hard links to this filesystem object (st_nlink).
	 * This field is 1 if the filesystem doesn't support hardlinking or
	 * the information isn't available.
	 */
	u32 hardlinks;

	/**
	 * The device number of the device which this filesystem object resides
	 * on (st_dev). This field is 0 if this information is not available.
	 */
	u32 inode_id_device;

	/**
	 * The unique identifier (within the filesystem) of this filesystem
	 * object (st_ino). Together with inode_id_device, this field can be
	 * used as a OS wide unique id, when both their values are not 0.
	 * This field is 0 if the information is not available.
	 */
	u64 inode_id;

	/**
	 * User flags (st_flags).
	 * This field is 0 if this information is not available.
	 */
	u32 flags;

	/**
	 * The current generation number (st_gen).
	 * This field is 0 if this information is not available.
	 */
	u32 generation_id;

	/**
	 * The device number of a char. or block device type object (st_rdev).
	 * This field is 0 if the file isn't a char. or block device or when
	 * the OS doesn't use the major+minor device idenfication scheme.
	 */
	u32 device;
} __packed;

/** Extended attribute size. */
struct shfl_fsobjattr_easize {
	/** Size of EAs. */
	s64 cb;
} __packed;

/** Shared folder filesystem object attributes. */
struct shfl_fsobjattr {
	/** Mode flags (st_mode). SHFL_UNIX_*, SHFL_TYPE_*, and SHFL_DOS_*. */
	u32 mode;

	/** The additional attributes available. */
	enum shfl_fsobjattr_add additional;

	/**
	 * Additional attributes.
	 *
	 * Unless explicitly specified to an API, the API can provide additional
	 * data as it is provided by the underlying OS.
	 */
	union {
		struct shfl_fsobjattr_unix unix_attr;
		struct shfl_fsobjattr_easize size;
	} __packed u;
} __packed;
VMMDEV_ASSERT_SIZE(shfl_fsobjattr, 44);

struct shfl_timespec {
	s64 ns_relative_to_unix_epoch;
};

/** Filesystem object information structure. */
struct shfl_fsobjinfo {
	/**
	 * Logical size (st_size).
	 * For normal files this is the size of the file.
	 * For symbolic links, this is the length of the path name contained
	 * in the symbolic link.
	 * For other objects this fields needs to be specified.
	 */
	s64 size;

	/** Disk allocation size (st_blocks * DEV_BSIZE). */
	s64 allocated;

	/** Time of last access (st_atime). */
	struct shfl_timespec access_time;

	/** Time of last data modification (st_mtime). */
	struct shfl_timespec modification_time;

	/**
	 * Time of last status change (st_ctime).
	 * If not available this is set to modification_time.
	 */
	struct shfl_timespec change_time;

	/**
	 * Time of file birth (st_birthtime).
	 * If not available this is set to change_time.
	 */
	struct shfl_timespec birth_time;

	/** Attributes. */
	struct shfl_fsobjattr attr;

} __packed;
VMMDEV_ASSERT_SIZE(shfl_fsobjinfo, 92);

/**
 * result of an open/create request.
 * Along with handle value the result code
 * identifies what has happened while
 * trying to open the object.
 */
enum shfl_create_result {
	SHFL_NO_RESULT,
	/** Specified path does not exist. */
	SHFL_PATH_NOT_FOUND,
	/** Path to file exists, but the last component does not. */
	SHFL_FILE_NOT_FOUND,
	/** File already exists and either has been opened or not. */
	SHFL_FILE_EXISTS,
	/** New file was created. */
	SHFL_FILE_CREATED,
	/** Existing file was replaced or overwritten. */
	SHFL_FILE_REPLACED
};

/* No flags. Initialization value. */
#define SHFL_CF_NONE                  (0x00000000)

/*
 * Only lookup the object, do not return a handle. When this is set all other
 * flags are ignored.
 */
#define SHFL_CF_LOOKUP                (0x00000001)

/*
 * Open parent directory of specified object.
 * Useful for the corresponding Windows FSD flag
 * and for opening paths like \\dir\\*.* to search the 'dir'.
 */
#define SHFL_CF_OPEN_TARGET_DIRECTORY (0x00000002)

/* Create/open a directory. */
#define SHFL_CF_DIRECTORY             (0x00000004)

/*
 *  Open/create action to do if object exists
 *  and if the object does not exists.
 *  REPLACE file means atomically DELETE and CREATE.
 *  OVERWRITE file means truncating the file to 0 and
 *  setting new size.
 *  When opening an existing directory REPLACE and OVERWRITE
 *  actions are considered invalid, and cause returning
 *  FILE_EXISTS with NIL handle.
 */
#define SHFL_CF_ACT_MASK_IF_EXISTS      (0x000000f0)
#define SHFL_CF_ACT_MASK_IF_NEW         (0x00000f00)

/* What to do if object exists. */
#define SHFL_CF_ACT_OPEN_IF_EXISTS      (0x00000000)
#define SHFL_CF_ACT_FAIL_IF_EXISTS      (0x00000010)
#define SHFL_CF_ACT_REPLACE_IF_EXISTS   (0x00000020)
#define SHFL_CF_ACT_OVERWRITE_IF_EXISTS (0x00000030)

/* What to do if object does not exist. */
#define SHFL_CF_ACT_CREATE_IF_NEW       (0x00000000)
#define SHFL_CF_ACT_FAIL_IF_NEW         (0x00000100)

/* Read/write requested access for the object. */
#define SHFL_CF_ACCESS_MASK_RW          (0x00003000)

/* No access requested. */
#define SHFL_CF_ACCESS_NONE             (0x00000000)
/* Read access requested. */
#define SHFL_CF_ACCESS_READ             (0x00001000)
/* Write access requested. */
#define SHFL_CF_ACCESS_WRITE            (0x00002000)
/* Read/Write access requested. */
#define SHFL_CF_ACCESS_READWRITE	(0x00003000)

/* Requested share access for the object. */
#define SHFL_CF_ACCESS_MASK_DENY        (0x0000c000)

/* Allow any access. */
#define SHFL_CF_ACCESS_DENYNONE         (0x00000000)
/* Do not allow read. */
#define SHFL_CF_ACCESS_DENYREAD         (0x00004000)
/* Do not allow write. */
#define SHFL_CF_ACCESS_DENYWRITE        (0x00008000)
/* Do not allow access. */
#define SHFL_CF_ACCESS_DENYALL          (0x0000c000)

/* Requested access to attributes of the object. */
#define SHFL_CF_ACCESS_MASK_ATTR        (0x00030000)

/* No access requested. */
#define SHFL_CF_ACCESS_ATTR_NONE        (0x00000000)
/* Read access requested. */
#define SHFL_CF_ACCESS_ATTR_READ        (0x00010000)
/* Write access requested. */
#define SHFL_CF_ACCESS_ATTR_WRITE       (0x00020000)
/* Read/Write access requested. */
#define SHFL_CF_ACCESS_ATTR_READWRITE   (0x00030000)

/*
 * The file is opened in append mode.
 * Ignored if SHFL_CF_ACCESS_WRITE is not set.
 */
#define SHFL_CF_ACCESS_APPEND           (0x00040000)

/** Create parameters buffer struct for SHFL_FN_CREATE call */
struct shfl_createparms {
	/** Returned handle of opened object. */
	u64 handle;

	/** Returned result of the operation */
	enum shfl_create_result result;

	/** SHFL_CF_* */
	u32 create_flags;

	/**
	 * Attributes of object to create and
	 * returned actual attributes of opened/created object.
	 */
	struct shfl_fsobjinfo info;
} __packed;

/** Shared Folder directory information */
struct shfl_dirinfo {
	/** Full information about the object. */
	struct shfl_fsobjinfo info;
	/**
	 * The length of the short field (number of UTF16 chars).
	 * It is 16-bit for reasons of alignment.
	 */
	u16 short_name_len;
	/**
	 * The short name for 8.3 compatibility.
	 * Empty string if not available.
	 */
	u16 short_name[14];
	struct shfl_string name;
};

/** Shared folder filesystem properties. */
struct shfl_fsproperties {
	/**
	 * The maximum size of a filesystem object name.
	 * This does not include the '\\0'.
	 */
	u32 max_component_len;

	/**
	 * True if the filesystem is remote.
	 * False if the filesystem is local.
	 */
	bool remote;

	/**
	 * True if the filesystem is case sensitive.
	 * False if the filesystem is case insensitive.
	 */
	bool case_sensitive;

	/**
	 * True if the filesystem is mounted read only.
	 * False if the filesystem is mounted read write.
	 */
	bool read_only;

	/**
	 * True if the filesystem can encode unicode object names.
	 * False if it can't.
	 */
	bool supports_unicode;

	/**
	 * True if the filesystem is compresses.
	 * False if it isn't or we don't know.
	 */
	bool compressed;

	/**
	 * True if the filesystem compresses of individual files.
	 * False if it doesn't or we don't know.
	 */
	bool file_compression;
};
VMMDEV_ASSERT_SIZE(shfl_fsproperties, 12);

struct shfl_volinfo {
	s64 total_allocation_bytes;
	s64 available_allocation_bytes;
	u32 bytes_per_allocation_unit;
	u32 bytes_per_sector;
	u32 serial;
	struct shfl_fsproperties properties;
};


/** SHFL_FN_MAP_FOLDER Parameters structure. */
struct shfl_map_folder {
	/**
	 * pointer, in:
	 * Points to struct shfl_string buffer.
	 */
	struct vmmdev_hgcm_function_parameter path;

	/**
	 * pointer, out: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in: UTF16
	 * Path delimiter
	 */
	struct vmmdev_hgcm_function_parameter delimiter;

	/**
	 * pointer, in: SHFLROOT (u32)
	 * Case senstive flag
	 */
	struct vmmdev_hgcm_function_parameter case_sensitive;

};

/* Number of parameters */
#define SHFL_CPARMS_MAP_FOLDER (4)


/** SHFL_FN_UNMAP_FOLDER Parameters structure. */
struct shfl_unmap_folder {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

};

/* Number of parameters */
#define SHFL_CPARMS_UNMAP_FOLDER (1)


/** SHFL_FN_CREATE Parameters structure. */
struct shfl_create {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in:
	 * Points to struct shfl_string buffer.
	 */
	struct vmmdev_hgcm_function_parameter path;

	/**
	 * pointer, in/out:
	 * Points to struct shfl_createparms buffer.
	 */
	struct vmmdev_hgcm_function_parameter parms;

};

/* Number of parameters */
#define SHFL_CPARMS_CREATE (3)


/** SHFL_FN_CLOSE Parameters structure. */
struct shfl_close {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * value64, in:
	 * SHFLHANDLE (u64) of object to close.
	 */
	struct vmmdev_hgcm_function_parameter handle;

};

/* Number of parameters */
#define SHFL_CPARMS_CLOSE (2)


/** SHFL_FN_READ Parameters structure. */
struct shfl_read {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * value64, in:
	 * SHFLHANDLE (u64) of object to read from.
	 */
	struct vmmdev_hgcm_function_parameter handle;

	/**
	 * value64, in:
	 * Offset to read from.
	 */
	struct vmmdev_hgcm_function_parameter offset;

	/**
	 * value64, in/out:
	 * Bytes to read/How many were read.
	 */
	struct vmmdev_hgcm_function_parameter cb;

	/**
	 * pointer, out:
	 * Buffer to place data to.
	 */
	struct vmmdev_hgcm_function_parameter buffer;

};

/* Number of parameters */
#define SHFL_CPARMS_READ (5)


/** SHFL_FN_WRITE Parameters structure. */
struct shfl_write {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * value64, in:
	 * SHFLHANDLE (u64) of object to write to.
	 */
	struct vmmdev_hgcm_function_parameter handle;

	/**
	 * value64, in:
	 * Offset to write to.
	 */
	struct vmmdev_hgcm_function_parameter offset;

	/**
	 * value64, in/out:
	 * Bytes to write/How many were written.
	 */
	struct vmmdev_hgcm_function_parameter cb;

	/**
	 * pointer, in:
	 * Data to write.
	 */
	struct vmmdev_hgcm_function_parameter buffer;

};

/* Number of parameters */
#define SHFL_CPARMS_WRITE (5)


/*
 * SHFL_FN_LIST
 * Listing information includes variable length RTDIRENTRY[EX] structures.
 */

#define SHFL_LIST_NONE			0
#define SHFL_LIST_RETURN_ONE		1

/** SHFL_FN_LIST Parameters structure. */
struct shfl_list {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * value64, in:
	 * SHFLHANDLE (u64) of object to be listed.
	 */
	struct vmmdev_hgcm_function_parameter handle;

	/**
	 * value32, in:
	 * List flags SHFL_LIST_*.
	 */
	struct vmmdev_hgcm_function_parameter flags;

	/**
	 * value32, in/out:
	 * Bytes to be used for listing information/How many bytes were used.
	 */
	struct vmmdev_hgcm_function_parameter cb;

	/**
	 * pointer, in/optional
	 * Points to struct shfl_string buffer that specifies a search path.
	 */
	struct vmmdev_hgcm_function_parameter path;

	/**
	 * pointer, out:
	 * Buffer to place listing information to. (struct shfl_dirinfo)
	 */
	struct vmmdev_hgcm_function_parameter buffer;

	/**
	 * value32, in/out:
	 * Indicates a key where the listing must be resumed.
	 * in: 0 means start from begin of object.
	 * out: 0 means listing completed.
	 */
	struct vmmdev_hgcm_function_parameter resume_point;

	/**
	 * pointer, out:
	 * Number of files returned
	 */
	struct vmmdev_hgcm_function_parameter file_count;
};

/* Number of parameters */
#define SHFL_CPARMS_LIST (8)


/** SHFL_FN_READLINK Parameters structure. */
struct shfl_readLink {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in:
	 * Points to struct shfl_string buffer.
	 */
	struct vmmdev_hgcm_function_parameter path;

	/**
	 * pointer, out:
	 * Buffer to place data to.
	 */
	struct vmmdev_hgcm_function_parameter buffer;

};

/* Number of parameters */
#define SHFL_CPARMS_READLINK (3)


/* SHFL_FN_INFORMATION */

/* Mask of Set/Get bit. */
#define SHFL_INFO_MODE_MASK    (0x1)
/* Get information */
#define SHFL_INFO_GET          (0x0)
/* Set information */
#define SHFL_INFO_SET          (0x1)

/* Get name of the object. */
#define SHFL_INFO_NAME         (0x2)
/* Set size of object (extend/trucate); only applies to file objects */
#define SHFL_INFO_SIZE         (0x4)
/* Get/Set file object info. */
#define SHFL_INFO_FILE         (0x8)
/* Get volume information. */
#define SHFL_INFO_VOLUME       (0x10)

/** SHFL_FN_INFORMATION Parameters structure. */
struct shfl_information {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * value64, in:
	 * SHFLHANDLE (u64) of object to be listed.
	 */
	struct vmmdev_hgcm_function_parameter handle;

	/**
	 * value32, in:
	 * SHFL_INFO_*
	 */
	struct vmmdev_hgcm_function_parameter flags;

	/**
	 * value32, in/out:
	 * Bytes to be used for information/How many bytes were used.
	 */
	struct vmmdev_hgcm_function_parameter cb;

	/**
	 * pointer, in/out:
	 * Information to be set/get (shfl_fsobjinfo or shfl_string). Do not
	 * forget to set the shfl_fsobjinfo::attr::additional for a get
	 * operation as well.
	 */
	struct vmmdev_hgcm_function_parameter info;

};

/* Number of parameters */
#define SHFL_CPARMS_INFORMATION (5)


/* SHFL_FN_REMOVE */

#define SHFL_REMOVE_FILE        (0x1)
#define SHFL_REMOVE_DIR         (0x2)
#define SHFL_REMOVE_SYMLINK     (0x4)

/** SHFL_FN_REMOVE Parameters structure. */
struct shfl_remove {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in:
	 * Points to struct shfl_string buffer.
	 */
	struct vmmdev_hgcm_function_parameter path;

	/**
	 * value32, in:
	 * remove flags (file/directory)
	 */
	struct vmmdev_hgcm_function_parameter flags;

};

#define SHFL_CPARMS_REMOVE  (3)


/* SHFL_FN_RENAME */

#define SHFL_RENAME_FILE                (0x1)
#define SHFL_RENAME_DIR                 (0x2)
#define SHFL_RENAME_REPLACE_IF_EXISTS   (0x4)

/** SHFL_FN_RENAME Parameters structure. */
struct shfl_rename {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in:
	 * Points to struct shfl_string src.
	 */
	struct vmmdev_hgcm_function_parameter src;

	/**
	 * pointer, in:
	 * Points to struct shfl_string dest.
	 */
	struct vmmdev_hgcm_function_parameter dest;

	/**
	 * value32, in:
	 * rename flags (file/directory)
	 */
	struct vmmdev_hgcm_function_parameter flags;

};

#define SHFL_CPARMS_RENAME  (4)


/** SHFL_FN_SYMLINK Parameters structure. */
struct shfl_symlink {
	/**
	 * pointer, in: SHFLROOT (u32)
	 * Root handle of the mapping which name is queried.
	 */
	struct vmmdev_hgcm_function_parameter root;

	/**
	 * pointer, in:
	 * Points to struct shfl_string of path for the new symlink.
	 */
	struct vmmdev_hgcm_function_parameter new_path;

	/**
	 * pointer, in:
	 * Points to struct shfl_string of destination for symlink.
	 */
	struct vmmdev_hgcm_function_parameter old_path;

	/**
	 * pointer, out:
	 * Information about created symlink.
	 */
	struct vmmdev_hgcm_function_parameter info;

};

#define SHFL_CPARMS_SYMLINK  (4)

#endif
