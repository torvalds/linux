/** @file
 * IPRT - Filesystem.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_fs_h
#define ___iprt_fs_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/time.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_fs    RTFs - Filesystem and Volume
 * @ingroup grp_rt
 * @{
 */


/** @name Filesystem Object Mode Flags.
 *
 * There are two sets of flags: the unix mode flags and the dos attributes.
 *
 * APIs returning mode flags will provide both sets.
 *
 * When specifying mode flags to any API at least one of them must be given. If
 * one set is missing the API will synthesize it from the one given if it
 * requires it.
 *
 * Both sets match their x86 ABIs, the DOS/NT one is simply shifted up 16 bits.
 * The DOS/NT range is bits 16 to 31 inclusively. The Unix range is bits 0 to 15
 * (inclusively).
 *
 * @remarks These constants have been comitted to a binary format and must not
 *          be changed in any incompatible ways.
 *
 * @{
 */

/** Set user id on execution (S_ISUID). */
#define RTFS_UNIX_ISUID             0004000U
/** Set group id on execution (S_ISGID). */
#define RTFS_UNIX_ISGID             0002000U
/** Sticky bit (S_ISVTX / S_ISTXT). */
#define RTFS_UNIX_ISTXT             0001000U

/** Owner RWX mask (S_IRWXU). */
#define RTFS_UNIX_IRWXU             0000700U
/** Owner readable (S_IRUSR). */
#define RTFS_UNIX_IRUSR             0000400U
/** Owner writable (S_IWUSR). */
#define RTFS_UNIX_IWUSR             0000200U
/** Owner executable (S_IXUSR). */
#define RTFS_UNIX_IXUSR             0000100U

/** Group RWX mask (S_IRWXG). */
#define RTFS_UNIX_IRWXG             0000070U
/** Group readable (S_IRGRP). */
#define RTFS_UNIX_IRGRP             0000040U
/** Group writable (S_IWGRP). */
#define RTFS_UNIX_IWGRP             0000020U
/** Group executable (S_IXGRP). */
#define RTFS_UNIX_IXGRP             0000010U

/** Other RWX mask (S_IRWXO). */
#define RTFS_UNIX_IRWXO             0000007U
/** Other readable (S_IROTH). */
#define RTFS_UNIX_IROTH             0000004U
/** Other writable (S_IWOTH). */
#define RTFS_UNIX_IWOTH             0000002U
/** Other executable (S_IXOTH). */
#define RTFS_UNIX_IXOTH             0000001U

/** All UNIX access permission bits (0777). */
#define RTFS_UNIX_ALL_ACCESS_PERMS  0000777U
/** All UNIX permission bits, including set id and sticky bits.  */
#define RTFS_UNIX_ALL_PERMS         0007777U

/** Named pipe (fifo) (S_IFIFO). */
#define RTFS_TYPE_FIFO              0010000U
/** Character device (S_IFCHR). */
#define RTFS_TYPE_DEV_CHAR          0020000U
/** Directory (S_IFDIR). */
#define RTFS_TYPE_DIRECTORY         0040000U
/** Block device (S_IFBLK). */
#define RTFS_TYPE_DEV_BLOCK         0060000U
/** Regular file (S_IFREG). */
#define RTFS_TYPE_FILE              0100000U
/** Symbolic link (S_IFLNK). */
#define RTFS_TYPE_SYMLINK           0120000U
/** Socket (S_IFSOCK). */
#define RTFS_TYPE_SOCKET            0140000U
/** Whiteout (S_IFWHT). */
#define RTFS_TYPE_WHITEOUT          0160000U
/** Type mask (S_IFMT). */
#define RTFS_TYPE_MASK              0170000U
/** The shift count to convert between RTFS_TYPE_MASK and DIRENTRYTYPE. */
#define RTFS_TYPE_DIRENTRYTYPE_SHIFT    12

/** Unix attribute mask. */
#define RTFS_UNIX_MASK              0xffffU
/** The mask of all the NT, OS/2 and DOS attributes. */
#define RTFS_DOS_MASK               (0x7fffU << RTFS_DOS_SHIFT)

/** The shift value. */
#define RTFS_DOS_SHIFT              16
/** The mask of the OS/2 and DOS attributes. */
#define RTFS_DOS_MASK_OS2           (0x003fU << RTFS_DOS_SHIFT)
/** The mask of the NT attributes. */
#define RTFS_DOS_MASK_NT            (0x7fffU << RTFS_DOS_SHIFT)

/** Readonly object. */
#define RTFS_DOS_READONLY           (0x0001U << RTFS_DOS_SHIFT)
/** Hidden object. */
#define RTFS_DOS_HIDDEN             (0x0002U << RTFS_DOS_SHIFT)
/** System object. */
#define RTFS_DOS_SYSTEM             (0x0004U << RTFS_DOS_SHIFT)
/** Directory. */
#define RTFS_DOS_DIRECTORY          (0x0010U << RTFS_DOS_SHIFT)
/** Archived object.
 * This bit is set by the filesystem after each modification of a file. */
#define RTFS_DOS_ARCHIVED           (0x0020U << RTFS_DOS_SHIFT)
/** Undocumented / Reserved, used to be the FAT volume label. */
#define RTFS_DOS_NT_DEVICE          (0x0040U << RTFS_DOS_SHIFT)
/** Normal object, no other attribute set (NT). */
#define RTFS_DOS_NT_NORMAL          (0x0080U << RTFS_DOS_SHIFT)
/** Temporary object (NT). */
#define RTFS_DOS_NT_TEMPORARY       (0x0100U << RTFS_DOS_SHIFT)
/** Sparse file (NT). */
#define RTFS_DOS_NT_SPARSE_FILE     (0x0200U << RTFS_DOS_SHIFT)
/** Reparse point (NT). */
#define RTFS_DOS_NT_REPARSE_POINT   (0x0400U << RTFS_DOS_SHIFT)
/** Compressed object (NT).
 * For a directory, compression is the default for new files. */
#define RTFS_DOS_NT_COMPRESSED      (0x0800U << RTFS_DOS_SHIFT)
/** Physically offline data (NT).
 * MSDN say, don't mess with this one. */
#define RTFS_DOS_NT_OFFLINE         (0x1000U << RTFS_DOS_SHIFT)
/** Not content indexed by the content indexing service (NT). */
#define RTFS_DOS_NT_NOT_CONTENT_INDEXED (0x2000U << RTFS_DOS_SHIFT)
/** Encryped object (NT).
 * For a directory, encrypted is the default for new files. */
#define RTFS_DOS_NT_ENCRYPTED       (0x4000U << RTFS_DOS_SHIFT)

/** @} */


/** @name Filesystem Object Type Predicates.
 * @{ */
/** Checks the mode flags indicate a named pipe (fifo) (S_ISFIFO). */
#define RTFS_IS_FIFO(fMode)         ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_FIFO )
/** Checks the mode flags indicate a character device (S_ISCHR). */
#define RTFS_IS_DEV_CHAR(fMode)     ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_DEV_CHAR )
/** Checks the mode flags indicate a directory (S_ISDIR). */
#define RTFS_IS_DIRECTORY(fMode)    ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_DIRECTORY )
/** Checks the mode flags indicate a block device (S_ISBLK). */
#define RTFS_IS_DEV_BLOCK(fMode)    ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_DEV_BLOCK )
/** Checks the mode flags indicate a regular file (S_ISREG). */
#define RTFS_IS_FILE(fMode)         ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_FILE )
/** Checks the mode flags indicate a symbolic link (S_ISLNK). */
#define RTFS_IS_SYMLINK(fMode)      ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_SYMLINK )
/** Checks the mode flags indicate a socket (S_ISSOCK). */
#define RTFS_IS_SOCKET(fMode)       ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_SOCKET )
/** Checks the mode flags indicate a whiteout (S_ISWHT). */
#define RTFS_IS_WHITEOUT(fMode)     ( ((fMode) & RTFS_TYPE_MASK) == RTFS_TYPE_WHITEOUT )
/** @} */


/**
 * Filesystem type IDs returned by RTFsQueryType.
 *
 * This enum is subject to changes and must not be used as part of any ABI or
 * binary format (file, network, etc).
 *
 * @remarks When adding new entries, please update RTFsTypeName().  Also, try
 *          add them to the most natural group.
 */
typedef enum RTFSTYPE
{
    /** Unknown file system. */
    RTFSTYPE_UNKNOWN = 0,

    /** Universal Disk Format. */
    RTFSTYPE_UDF,
    /** ISO 9660, aka Compact Disc File System (CDFS). */
    RTFSTYPE_ISO9660,
    /** Filesystem in Userspace. */
    RTFSTYPE_FUSE,
    /** VirtualBox shared folders.  */
    RTFSTYPE_VBOXSHF,

    /* Linux: */
    RTFSTYPE_EXT,
    RTFSTYPE_EXT2,
    RTFSTYPE_EXT3,
    RTFSTYPE_EXT4,
    RTFSTYPE_XFS,
    RTFSTYPE_CIFS,
    RTFSTYPE_SMBFS,
    RTFSTYPE_TMPFS,
    RTFSTYPE_SYSFS,
    RTFSTYPE_PROC,
    RTFSTYPE_OCFS2,
    RTFSTYPE_BTRFS,

    /* Windows: */
    /** New Technology File System. */
    RTFSTYPE_NTFS,
    /** FAT12, FAT16 and FAT32 lumped into one basket.
     * The partition size limit of FAT12 and FAT16 will be the factor
     * limiting the file size (except, perhaps for the 64KB cluster case on
     * non-Windows hosts). */
    RTFSTYPE_FAT,

    /* Solaris: */
    /** Zettabyte File System.  */
    RTFSTYPE_ZFS,
    /** Unix File System. */
    RTFSTYPE_UFS,
    /** Network File System. */
    RTFSTYPE_NFS,

    /* Mac OS X: */
    /** Hierarchical File System. */
    RTFSTYPE_HFS,
    /** @todo RTFSTYPE_HFS_PLUS? */
    RTFSTYPE_AUTOFS,
    RTFSTYPE_DEVFS,

    /* *BSD: */

    /* OS/2: */
    /** High Performance File System. */
    RTFSTYPE_HPFS,
    /** Journaled File System (v2).  */
    RTFSTYPE_JFS,

    /** The end of valid Filesystem types IDs. */
    RTFSTYPE_END,
    /** The usual 32-bit type blow up. */
    RTFSTYPE_32BIT_HACK = 0x7fffffff
} RTFSTYPE;
/** Pointer to a Filesystem type ID. */
typedef RTFSTYPE *PRTFSTYPE;


/**
 * The available additional information in a RTFSOBJATTR object.
 */
typedef enum RTFSOBJATTRADD
{
    /** No additional information is available / requested. */
    RTFSOBJATTRADD_NOTHING = 1,
    /** The additional unix attributes (RTFSOBJATTR::u::Unix) are available /
     *  requested. */
    RTFSOBJATTRADD_UNIX,
    /** The additional unix attributes (RTFSOBJATTR::u::UnixOwner) are
     * available / requested. */
    RTFSOBJATTRADD_UNIX_OWNER,
    /** The additional unix attributes (RTFSOBJATTR::u::UnixGroup) are
     * available / requested. */
    RTFSOBJATTRADD_UNIX_GROUP,
    /** The additional extended attribute size (RTFSOBJATTR::u::EASize) is available / requested. */
    RTFSOBJATTRADD_EASIZE,
    /** The last valid item (inclusive).
     * The valid range is RTFSOBJATTRADD_NOTHING thru RTFSOBJATTRADD_LAST.  */
    RTFSOBJATTRADD_LAST = RTFSOBJATTRADD_EASIZE,

    /** The usual 32-bit hack. */
    RTFSOBJATTRADD_32BIT_SIZE_HACK = 0x7fffffff
} RTFSOBJATTRADD;

/** The number of bytes reserved for the additional attribute union. */
#define RTFSOBJATTRUNION_MAX_SIZE       128

/**
 * Additional Unix Attributes (RTFSOBJATTRADD_UNIX).
 */
typedef struct RTFSOBJATTRUNIX
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_UID if not supported. */
    RTUID           uid;

    /** The group the filesystem object is assigned (st_gid).
     * This field is NIL_GID if not supported. */
    RTGID           gid;

    /** Number of hard links to this filesystem object (st_nlink).
     * This field is 1 if the filesystem doesn't support hardlinking or
     * the information isn't available.
     */
    uint32_t        cHardlinks;

    /** The device number of the device which this filesystem object resides on (st_dev).
     * This field is 0 if this information is not available. */
    RTDEV           INodeIdDevice;

    /** The unique identifier (within the filesystem) of this filesystem object (st_ino).
     * Together with INodeIdDevice, this field can be used as a OS wide unique id
     * when both their values are not 0.
     * This field is 0 if the information is not available. */
    RTINODE         INodeId;

    /** User flags (st_flags).
     * This field is 0 if this information is not available. */
    uint32_t        fFlags;

    /** The current generation number (st_gen).
     * This field is 0 if this information is not available. */
    uint32_t        GenerationId;

    /** The device number of a character or block device type object (st_rdev).
     * This field is 0 if the file isn't of a character or block device type and
     * when the OS doesn't subscribe to the major+minor device idenfication scheme. */
    RTDEV           Device;
} RTFSOBJATTRUNIX;


/**
 * Additional Unix Attributes (RTFSOBJATTRADD_UNIX_OWNER).
 *
 * @remarks This interface is mainly for TAR.
 */
typedef struct RTFSOBJATTRUNIXOWNER
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_UID if not supported. */
    RTUID           uid;
    /** The user name.
     * Empty if not available or not supported, truncated if too long. */
    char            szName[RTFSOBJATTRUNION_MAX_SIZE - sizeof(RTUID)];
} RTFSOBJATTRUNIXOWNER;


/**
 * Additional Unix Attributes (RTFSOBJATTRADD_UNIX_GROUP).
 *
 * @remarks This interface is mainly for TAR.
 */
typedef struct RTFSOBJATTRUNIXGROUP
{
    /** The user owning the filesystem object (st_uid).
     * This field is NIL_GID if not supported. */
    RTGID           gid;
    /** The group name.
     * Empty if not available or not supported, truncated if too long. */
    char            szName[RTFSOBJATTRUNION_MAX_SIZE - sizeof(RTGID)];
} RTFSOBJATTRUNIXGROUP;


/**
 * Filesystem object attributes.
 */
typedef struct RTFSOBJATTR
{
    /** Mode flags (st_mode). RTFS_UNIX_*, RTFS_TYPE_*, and RTFS_DOS_*. */
    RTFMODE         fMode;

    /** The additional attributes available. */
    RTFSOBJATTRADD  enmAdditional;

    /**
     * Additional attributes.
     *
     * Unless explicitly specified to an API, the API can provide additional
     * data as it is provided by the underlying OS.
     */
    union RTFSOBJATTRUNION
    {
        /** Additional Unix Attributes - RTFSOBJATTRADD_UNIX. */
        RTFSOBJATTRUNIX         Unix;
        /** Additional Unix Owner Attributes - RTFSOBJATTRADD_UNIX_OWNER. */
        RTFSOBJATTRUNIXOWNER    UnixOwner;
        /** Additional Unix Group Attributes - RTFSOBJATTRADD_UNIX_GROUP. */
        RTFSOBJATTRUNIXGROUP    UnixGroup;

        /**
         * Extended attribute size is available when RTFS_DOS_HAVE_EA_SIZE is set.
         */
        struct RTFSOBJATTREASIZE
        {
            /** Size of EAs. */
            RTFOFF          cb;
        } EASize;
        /** Reserved space. */
        uint8_t         abReserveSpace[128];
    } u;
} RTFSOBJATTR;
/** Pointer to a filesystem object attributes structure. */
typedef RTFSOBJATTR *PRTFSOBJATTR;
/** Pointer to a const filesystem object attributes structure. */
typedef const RTFSOBJATTR *PCRTFSOBJATTR;


/**
 * Filesystem object information structure.
 *
 * This is returned by the RTPathQueryInfo(), RTFileQueryInfo() and RTDirRead() APIs.
 */
typedef struct RTFSOBJINFO
{
   /** Logical size (st_size).
    * For normal files this is the size of the file.
    * For symbolic links, this is the length of the path name contained
    * in the symbolic link.
    * For other objects this fields needs to be specified.
    */
   RTFOFF       cbObject;

   /** Disk allocation size (st_blocks * DEV_BSIZE). */
   RTFOFF       cbAllocated;

   /** Time of last access (st_atime). */
   RTTIMESPEC   AccessTime;

   /** Time of last data modification (st_mtime). */
   RTTIMESPEC   ModificationTime;

   /** Time of last status change (st_ctime).
    * If not available this is set to ModificationTime.
    */
   RTTIMESPEC   ChangeTime;

   /** Time of file birth (st_birthtime).
    * If not available this is set to ChangeTime.
    */
   RTTIMESPEC   BirthTime;

   /** Attributes. */
   RTFSOBJATTR  Attr;

} RTFSOBJINFO;
/** Pointer to a filesystem object information structure. */
typedef RTFSOBJINFO *PRTFSOBJINFO;
/** Pointer to a const filesystem object information structure. */
typedef const RTFSOBJINFO *PCRTFSOBJINFO;


#ifdef IN_RING3

/**
 * Query the sizes of a filesystem.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pcbTotal        Where to store the total filesystem space. (Optional)
 * @param   pcbFree         Where to store the remaining free space in the filesystem. (Optional)
 * @param   pcbBlock        Where to store the block size. (Optional)
 * @param   pcbSector       Where to store the sector size. (Optional)
 *
 * @sa      RTFileQueryFsSizes
 */
RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, PRTFOFF pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector);

/**
 * Query the mountpoint of a filesystem.
 *
 * @returns iprt status code.
 * @returns VERR_BUFFER_OVERFLOW if cbMountpoint isn't enough.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pszMountpoint   Where to store the mountpoint path.
 * @param   cbMountpoint    Size of the buffer pointed to by pszMountpoint.
 */
RTR3DECL(int) RTFsQueryMountpoint(const char *pszFsPath, char *pszMountpoint, size_t cbMountpoint);

/**
 * Query the label of a filesystem.
 *
 * @returns iprt status code.
 * @returns VERR_BUFFER_OVERFLOW if cbLabel isn't enough.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pszLabel        Where to store the label.
 * @param   cbLabel         Size of the buffer pointed to by pszLabel.
 */
RTR3DECL(int) RTFsQueryLabel(const char *pszFsPath, char *pszLabel, size_t cbLabel);

/**
 * Query the serial number of a filesystem.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pu32Serial      Where to store the serial number.
 */
RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial);

/**
 * Query the name of the filesystem driver.
 *
 * @returns iprt status code.
 * @returns VERR_BUFFER_OVERFLOW if cbFsDriver isn't enough.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pszFsDriver     Where to store the filesystem driver name.
 * @param   cbFsDriver      Size of the buffer pointed to by pszFsDriver.
 */
RTR3DECL(int) RTFsQueryDriver(const char *pszFsPath, char *pszFsDriver, size_t cbFsDriver);

/**
 * Query the name of the filesystem the file is located on.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.  It must exist.
 *                          In case this is a symlink, the file it refers to is
 *                          evaluated.
 * @param   penmType        Where to store the filesystem type, this is always
 *                          set.  See RTFSTYPE for the values.
 */
RTR3DECL(int) RTFsQueryType(const char *pszFsPath, PRTFSTYPE penmType);

#endif /* IN_RING3 */

/**
 * Gets the name of a filesystem type.
 *
 * @returns Pointer to a read-only string containing the name.
 * @param   enmType         A valid filesystem ID.  If outside the valid range,
 *                          the returned string will be pointing to a static
 *                          memory buffer which will be changed on subsequent
 *                          calls to this function by any thread.
 */
RTDECL(const char *) RTFsTypeName(RTFSTYPE enmType);

/**
 * Filesystem properties.
 */
typedef struct RTFSPROPERTIES
{
    /** The maximum size of a filesystem object name.
     * This does not include the '\\0'. */
    uint32_t cbMaxComponent;

    /** True if the filesystem is remote.
     * False if the filesystem is local. */
    bool    fRemote;

    /** True if the filesystem is case sensitive.
     * False if the filesystem is case insensitive. */
    bool    fCaseSensitive;

    /** True if the filesystem is mounted read only.
     * False if the filesystem is mounted read write. */
    bool    fReadOnly;

    /** True if the filesystem can encode unicode object names.
     * False if it can't. */
    bool    fSupportsUnicode;

    /** True if the filesystem is compresses.
     * False if it isn't or we don't know. */
    bool    fCompressed;

    /** True if the filesystem compresses of individual files.
     * False if it doesn't or we don't know. */
    bool    fFileCompression;

    /** @todo more? */
} RTFSPROPERTIES;
/** Pointer to a filesystem properties structure. */
typedef RTFSPROPERTIES *PRTFSPROPERTIES;
/** Pointer to a const filesystem properties structure. */
typedef RTFSPROPERTIES const *PCRTFSPROPERTIES;

#ifdef IN_RING3

/**
 * Query the properties of a mounted filesystem.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pProperties     Where to store the properties.
 */
RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties);

/**
 * Checks if the given volume is case sensitive or not.
 *
 * This may be misleading in some cases as we lack the necessary APIs to query
 * the information on some system (or choose not to use them) and are instead
 * returning the general position on case sensitive file name of the system.
 *
 * @returns @c true if case sensitive, @c false if not.
 * @param   pszFsPath       Path within the mounted file system.
 */
RTR3DECL(bool) RTFsIsCaseSensitive(const char *pszFsPath);

/**
 * Mountpoint enumerator callback.
 *
 * @returns iprt status code. Failure terminates the enumeration.
 * @param   pszMountpoint   The mountpoint name.
 * @param   pvUser          The user argument.
 */
typedef DECLCALLBACK(int) FNRTFSMOUNTPOINTENUM(const char *pszMountpoint, void *pvUser);
/** Pointer to a FNRTFSMOUNTPOINTENUM(). */
typedef FNRTFSMOUNTPOINTENUM *PFNRTFSMOUNTPOINTENUM;

/**
 * Enumerate mount points.
 *
 * @returns iprt status code.
 * @param   pfnCallback     The callback function.
 * @param   pvUser          The user argument to the callback.
 */
RTR3DECL(int) RTFsMountpointsEnum(PFNRTFSMOUNTPOINTENUM pfnCallback, void *pvUser);


#endif /* IN_RING3 */

/** @} */

RT_C_DECLS_END

#endif /* !___iprt_fs_h */

