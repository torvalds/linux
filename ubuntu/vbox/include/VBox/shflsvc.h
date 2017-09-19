/** @file
 * Shared Folders: Common header for host service and guest clients.
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

#ifndef ___VBox_shflsvc_h
#define ___VBox_shflsvc_h

#include <VBox/types.h>
#include <VBox/VBoxGuest2.h>
#include <VBox/VMMDev.h>
#include <VBox/hgcmsvc.h>
#include <iprt/fs.h>


/** @name Some bit flag manipulation macros.
 * @{  */
#ifndef BIT_FLAG
#define BIT_FLAG(__Field,__Flag)       ((__Field) & (__Flag))
#endif

#ifndef BIT_FLAG_SET
#define BIT_FLAG_SET(__Field,__Flag)   ((__Field) |= (__Flag))
#endif

#ifndef BIT_FLAG_CLEAR
#define BIT_FLAG_CLEAR(__Field,__Flag) ((__Field) &= ~(__Flag))
#endif
/** @} */


/**
 * Structures shared between guest and the service
 * can be relocated and use offsets to point to variable
 * length parts.
 */

/**
 * Shared folders protocol works with handles.
 * Before doing any action on a file system object,
 * one have to obtain the object handle via a SHFL_FN_CREATE
 * request. A handle must be closed with SHFL_FN_CLOSE.
 */

/** Shared Folders service functions. (guest)
 *  @{
 */

/** Query mappings changes. */
#define SHFL_FN_QUERY_MAPPINGS      (1)
/** Query mappings changes. */
#define SHFL_FN_QUERY_MAP_NAME      (2)
/** Open/create object. */
#define SHFL_FN_CREATE              (3)
/** Close object handle. */
#define SHFL_FN_CLOSE               (4)
/** Read object content. */
#define SHFL_FN_READ                (5)
/** Write new object content. */
#define SHFL_FN_WRITE               (6)
/** Lock/unlock a range in the object. */
#define SHFL_FN_LOCK                (7)
/** List object content. */
#define SHFL_FN_LIST                (8)
/** Query/set object information. */
#define SHFL_FN_INFORMATION         (9)
/** Remove object */
#define SHFL_FN_REMOVE              (11)
/** Map folder (legacy) */
#define SHFL_FN_MAP_FOLDER_OLD      (12)
/** Unmap folder */
#define SHFL_FN_UNMAP_FOLDER        (13)
/** Rename object (possibly moving it to another directory) */
#define SHFL_FN_RENAME              (14)
/** Flush file */
#define SHFL_FN_FLUSH               (15)
/** @todo macl, a description, please. */
#define SHFL_FN_SET_UTF8            (16)
/** Map folder */
#define SHFL_FN_MAP_FOLDER          (17)
/** Read symlink destination (as of VBox 4.0) */
#define SHFL_FN_READLINK            (18)
/** Create symlink (as of VBox 4.0) */
#define SHFL_FN_SYMLINK             (19)
/** Ask host to show symlinks (as of VBox 4.0) */
#define SHFL_FN_SET_SYMLINKS        (20)

/** @} */

/** Shared Folders service functions. (host)
 *  @{
 */

/** Add shared folder mapping. */
#define SHFL_FN_ADD_MAPPING         (1)
/** Remove shared folder mapping. */
#define SHFL_FN_REMOVE_MAPPING      (2)
/** Set the led status light address. */
#define SHFL_FN_SET_STATUS_LED      (3)
/** Allow the guest to create symbolic links (as of VBox 4.0) */
#define SHFL_FN_ALLOW_SYMLINKS_CREATE (4)
/** @} */

/** Root handle for a mapping. Root handles are unique.
 *  @note
 *  Function parameters structures consider
 *  the root handle as 32 bit value. If the typedef
 *  will be changed, then function parameters must be
 *  changed accordingly. All those parameters are marked
 *  with SHFLROOT in comments.
 */
typedef uint32_t SHFLROOT;

#define SHFL_ROOT_NIL ((SHFLROOT)~0)


/** A shared folders handle for an opened object. */
typedef uint64_t SHFLHANDLE;

#define SHFL_HANDLE_NIL  ((SHFLHANDLE)~0LL)
#define SHFL_HANDLE_ROOT ((SHFLHANDLE)0LL)

/** Hardcoded maximum length (in chars) of a shared folder name. */
#define SHFL_MAX_LEN         (256)
/** Hardcoded maximum number of shared folder mapping available to the guest. */
#define SHFL_MAX_MAPPINGS    (64)

/** @name Shared Folders strings. They can be either UTF-8 or UTF-16.
 * @{
 */

/**
 * Shared folder string buffer structure.
 */
#pragma pack(1)
typedef struct _SHFLSTRING
{
    /** Allocated size of the String member in bytes. */
    uint16_t u16Size;

    /** Length of string without trailing nul in bytes. */
    uint16_t u16Length;

    /** UTF-8 or UTF-16 string. Nul terminated. */
    union
    {
        uint8_t  utf8[1];
        uint16_t ucs2[1];
    } String;
} SHFLSTRING;
#pragma pack()

#define SHFLSTRING_HEADER_SIZE RT_UOFFSETOF(SHFLSTRING, String)

/** Pointer to a shared folder string buffer. */
typedef SHFLSTRING *PSHFLSTRING;
/** Pointer to a const shared folder string buffer. */
typedef const SHFLSTRING *PCSHFLSTRING;

/** Calculate size of the string. */
DECLINLINE(uint32_t) ShflStringSizeOfBuffer(PCSHFLSTRING pString)
{
    return pString ? (uint32_t)(sizeof(SHFLSTRING) - sizeof(pString->String) + pString->u16Size) : 0;
}

DECLINLINE(uint32_t) ShflStringLength(PCSHFLSTRING pString)
{
    return pString ? pString->u16Length : 0;
}

DECLINLINE(PSHFLSTRING) ShflStringInitBuffer(void *pvBuffer, uint32_t u32Size)
{
    PSHFLSTRING pString = NULL;
    const uint32_t u32HeaderSize = SHFLSTRING_HEADER_SIZE;

    /*
     * Check that the buffer size is big enough to hold a zero sized string
     * and is not too big to fit into 16 bit variables.
     */
    if (u32Size >= u32HeaderSize && u32Size - u32HeaderSize <= 0xFFFF)
    {
        pString = (PSHFLSTRING)pvBuffer;
        pString->u16Size = (uint16_t)(u32Size - u32HeaderSize);
        pString->u16Length = 0;
        if (pString->u16Size >= sizeof(pString->String.ucs2[0]))
            pString->String.ucs2[0] = 0;
        else if (pString->u16Size >= sizeof(pString->String.utf8[0]))
            pString->String.utf8[0] = 0;
    }

    return pString;
}

/**
 * Validates a HGCM string output parameter.
 *
 * @returns true if valid, false if not.
 *
 * @param   pString     The string buffer pointer.
 * @param   cbBuf       The buffer size from the parameter.
 */
DECLINLINE(bool) ShflStringIsValidOut(PCSHFLSTRING pString, uint32_t cbBuf)
{
    if (RT_LIKELY(cbBuf > RT_UOFFSETOF(SHFLSTRING, String)))
        if (RT_LIKELY((uint32_t)pString->u16Size + RT_UOFFSETOF(SHFLSTRING, String) <= cbBuf))
            if (RT_LIKELY(pString->u16Length < pString->u16Size))
                return true;
    return false;
}

/**
 * Validates a HGCM string input parameter.
 *
 * @returns true if valid, false if not.
 *
 * @param   pString     The string buffer pointer.
 * @param   cbBuf       The buffer size from the parameter.
 * @param   fUtf8Not16  Set if UTF-8 encoding, clear if UTF-16 encoding.
 */
DECLINLINE(bool) ShflStringIsValidIn(PCSHFLSTRING pString, uint32_t cbBuf, bool fUtf8Not16)
{
    int rc;
    if (RT_LIKELY(cbBuf > RT_UOFFSETOF(SHFLSTRING, String)))
    {
        if (RT_LIKELY((uint32_t)pString->u16Size + RT_UOFFSETOF(SHFLSTRING, String) <= cbBuf))
        {
            if (fUtf8Not16)
            {
                /* UTF-8: */
                if (RT_LIKELY(pString->u16Length < pString->u16Size))
                {
                    rc = RTStrValidateEncodingEx((const char *)&pString->String.utf8[0], pString->u16Length + 1,
                                                 RTSTR_VALIDATE_ENCODING_EXACT_LENGTH | RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                    if (RT_SUCCESS(rc))
                        return true;
                }
            }
            else
            {
                /* UTF-16: */
                if (RT_LIKELY(!(pString->u16Length & 1)))
                {
                    if (RT_LIKELY((uint32_t)sizeof(RTUTF16) + pString->u16Length <= pString->u16Size))
                    {
                        rc = RTUtf16ValidateEncodingEx(&pString->String.ucs2[0], pString->u16Length / 2 + 1,
                                                       RTSTR_VALIDATE_ENCODING_EXACT_LENGTH
                                                       | RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED);
                        if (RT_SUCCESS(rc))
                            return true;
                    }
                }
            }
        }
    }
    return false;
}

/**
 * Validates an optional HGCM string input parameter.
 *
 * @returns true if valid, false if not.
 *
 * @param   pString     The string buffer pointer. Can be NULL.
 * @param   cbBuf       The buffer size from the parameter.
 * @param   fUtf8Not16  Set if UTF-8 encoding, clear if UTF-16 encoding.
 */
DECLINLINE(bool) ShflStringIsValidOrNullIn(PCSHFLSTRING pString, uint32_t cbBuf, bool fUtf8Not16)
{
    if (pString)
        return ShflStringIsValidIn(pString, cbBuf, fUtf8Not16);
    if (RT_LIKELY(cbBuf == 0))
        return true;
    return false;
}

/** @} */


/**
 * The available additional information in a SHFLFSOBJATTR object.
 */
typedef enum SHFLFSOBJATTRADD
{
    /** No additional information is available / requested. */
    SHFLFSOBJATTRADD_NOTHING = 1,
    /** The additional unix attributes (SHFLFSOBJATTR::u::Unix) are
     *  available / requested. */
    SHFLFSOBJATTRADD_UNIX,
    /** The additional extended attribute size (SHFLFSOBJATTR::u::EASize) is
     *  available / requested. */
    SHFLFSOBJATTRADD_EASIZE,
    /** The last valid item (inclusive).
     * The valid range is SHFLFSOBJATTRADD_NOTHING thru
     * SHFLFSOBJATTRADD_LAST. */
    SHFLFSOBJATTRADD_LAST = SHFLFSOBJATTRADD_EASIZE,

    /** The usual 32-bit hack. */
    SHFLFSOBJATTRADD_32BIT_SIZE_HACK = 0x7fffffff
} SHFLFSOBJATTRADD;


/* Assert sizes of the IRPT types we're using below. */
AssertCompileSize(RTFMODE,      4);
AssertCompileSize(RTFOFF,       8);
AssertCompileSize(RTINODE,      8);
AssertCompileSize(RTTIMESPEC,   8);
AssertCompileSize(RTDEV,        4);
AssertCompileSize(RTUID,        4);

/**
 * Shared folder filesystem object attributes.
 */
#pragma pack(1)
typedef struct SHFLFSOBJATTR
{
    /** Mode flags (st_mode). RTFS_UNIX_*, RTFS_TYPE_*, and RTFS_DOS_*.
     * @remarks We depend on a number of RTFS_ defines to remain unchanged.
     *          Fortuntately, these are depending on windows, dos and unix
     *          standard values, so this shouldn't be much of a pain. */
    RTFMODE         fMode;

    /** The additional attributes available. */
    SHFLFSOBJATTRADD  enmAdditional;

    /**
     * Additional attributes.
     *
     * Unless explicitly specified to an API, the API can provide additional
     * data as it is provided by the underlying OS.
     */
    union SHFLFSOBJATTRUNION
    {
        /** Additional Unix Attributes
         * These are available when SHFLFSOBJATTRADD is set in fUnix.
         */
         struct SHFLFSOBJATTRUNIX
         {
            /** The user owning the filesystem object (st_uid).
             * This field is ~0U if not supported. */
            RTUID           uid;

            /** The group the filesystem object is assigned (st_gid).
             * This field is ~0U if not supported. */
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
        } Unix;

        /**
         * Extended attribute size.
         */
        struct SHFLFSOBJATTREASIZE
        {
            /** Size of EAs. */
            RTFOFF          cb;
        } EASize;
    } u;
} SHFLFSOBJATTR;
#pragma pack()
AssertCompileSize(SHFLFSOBJATTR, 44);
/** Pointer to a shared folder filesystem object attributes structure. */
typedef SHFLFSOBJATTR *PSHFLFSOBJATTR;
/** Pointer to a const shared folder filesystem object attributes structure. */
typedef const SHFLFSOBJATTR *PCSHFLFSOBJATTR;


/**
 * Filesystem object information structure.
 */
#pragma pack(1)
typedef struct SHFLFSOBJINFO
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

   /** Time of last access (st_atime).
    * @remarks  Here (and other places) we depend on the IPRT timespec to
    *           remain unchanged. */
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
   SHFLFSOBJATTR Attr;

} SHFLFSOBJINFO;
#pragma pack()
AssertCompileSize(SHFLFSOBJINFO, 92);
/** Pointer to a shared folder filesystem object information structure. */
typedef SHFLFSOBJINFO *PSHFLFSOBJINFO;
/** Pointer to a const shared folder filesystem object information
 *  structure. */
typedef const SHFLFSOBJINFO *PCSHFLFSOBJINFO;


/**
 * Copy file system objinfo from IPRT to shared folder format.
 *
 * @param   pDst                The shared folder structure.
 * @param   pSrc                The IPRT structure.
 */
DECLINLINE(void) vbfsCopyFsObjInfoFromIprt(PSHFLFSOBJINFO pDst, PCRTFSOBJINFO pSrc)
{
    pDst->cbObject          = pSrc->cbObject;
    pDst->cbAllocated       = pSrc->cbAllocated;
    pDst->AccessTime        = pSrc->AccessTime;
    pDst->ModificationTime  = pSrc->ModificationTime;
    pDst->ChangeTime        = pSrc->ChangeTime;
    pDst->BirthTime         = pSrc->BirthTime;
    pDst->Attr.fMode        = pSrc->Attr.fMode;
    RT_ZERO(pDst->Attr.u);
    switch (pSrc->Attr.enmAdditional)
    {
        default:
        case RTFSOBJATTRADD_NOTHING:
            pDst->Attr.enmAdditional        = SHFLFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX:
            pDst->Attr.enmAdditional        = SHFLFSOBJATTRADD_UNIX;
            pDst->Attr.u.Unix.uid           = pSrc->Attr.u.Unix.uid;
            pDst->Attr.u.Unix.gid           = pSrc->Attr.u.Unix.gid;
            pDst->Attr.u.Unix.cHardlinks    = pSrc->Attr.u.Unix.cHardlinks;
            pDst->Attr.u.Unix.INodeIdDevice = pSrc->Attr.u.Unix.INodeIdDevice;
            pDst->Attr.u.Unix.INodeId       = pSrc->Attr.u.Unix.INodeId;
            pDst->Attr.u.Unix.fFlags        = pSrc->Attr.u.Unix.fFlags;
            pDst->Attr.u.Unix.GenerationId  = pSrc->Attr.u.Unix.GenerationId;
            pDst->Attr.u.Unix.Device        = pSrc->Attr.u.Unix.Device;
            break;

        case RTFSOBJATTRADD_EASIZE:
            pDst->Attr.enmAdditional        = SHFLFSOBJATTRADD_EASIZE;
            pDst->Attr.u.EASize.cb          = pSrc->Attr.u.EASize.cb;
            break;
    }
}


/** Result of an open/create request.
 *  Along with handle value the result code
 *  identifies what has happened while
 *  trying to open the object.
 */
typedef enum _SHFLCREATERESULT
{
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
} SHFLCREATERESULT;


/** Open/create flags.
 *  @{
 */

/** No flags. Initialization value. */
#define SHFL_CF_NONE                  (0x00000000)

/** Lookup only the object, do not return a handle. All other flags are ignored. */
#define SHFL_CF_LOOKUP                (0x00000001)

/** Open parent directory of specified object.
 *  Useful for the corresponding Windows FSD flag
 *  and for opening paths like \\dir\\*.* to search the 'dir'.
 *  @todo possibly not needed???
 */
#define SHFL_CF_OPEN_TARGET_DIRECTORY (0x00000002)

/** Create/open a directory. */
#define SHFL_CF_DIRECTORY             (0x00000004)

/** Open/create action to do if object exists
 *  and if the object does not exists.
 *  REPLACE file means atomically DELETE and CREATE.
 *  OVERWRITE file means truncating the file to 0 and
 *  setting new size.
 *  When opening an existing directory REPLACE and OVERWRITE
 *  actions are considered invalid, and cause returning
 *  FILE_EXISTS with NIL handle.
 */
#define SHFL_CF_ACT_MASK_IF_EXISTS      (0x000000F0)
#define SHFL_CF_ACT_MASK_IF_NEW         (0x00000F00)

/** What to do if object exists. */
#define SHFL_CF_ACT_OPEN_IF_EXISTS      (0x00000000)
#define SHFL_CF_ACT_FAIL_IF_EXISTS      (0x00000010)
#define SHFL_CF_ACT_REPLACE_IF_EXISTS   (0x00000020)
#define SHFL_CF_ACT_OVERWRITE_IF_EXISTS (0x00000030)

/** What to do if object does not exist. */
#define SHFL_CF_ACT_CREATE_IF_NEW       (0x00000000)
#define SHFL_CF_ACT_FAIL_IF_NEW         (0x00000100)

/** Read/write requested access for the object. */
#define SHFL_CF_ACCESS_MASK_RW          (0x00003000)

/** No access requested. */
#define SHFL_CF_ACCESS_NONE             (0x00000000)
/** Read access requested. */
#define SHFL_CF_ACCESS_READ             (0x00001000)
/** Write access requested. */
#define SHFL_CF_ACCESS_WRITE            (0x00002000)
/** Read/Write access requested. */
#define SHFL_CF_ACCESS_READWRITE        (SHFL_CF_ACCESS_READ | SHFL_CF_ACCESS_WRITE)

/** Requested share access for the object. */
#define SHFL_CF_ACCESS_MASK_DENY        (0x0000C000)

/** Allow any access. */
#define SHFL_CF_ACCESS_DENYNONE         (0x00000000)
/** Do not allow read. */
#define SHFL_CF_ACCESS_DENYREAD         (0x00004000)
/** Do not allow write. */
#define SHFL_CF_ACCESS_DENYWRITE        (0x00008000)
/** Do not allow access. */
#define SHFL_CF_ACCESS_DENYALL          (SHFL_CF_ACCESS_DENYREAD | SHFL_CF_ACCESS_DENYWRITE)

/** Requested access to attributes of the object. */
#define SHFL_CF_ACCESS_MASK_ATTR        (0x00030000)

/** No access requested. */
#define SHFL_CF_ACCESS_ATTR_NONE        (0x00000000)
/** Read access requested. */
#define SHFL_CF_ACCESS_ATTR_READ        (0x00010000)
/** Write access requested. */
#define SHFL_CF_ACCESS_ATTR_WRITE       (0x00020000)
/** Read/Write access requested. */
#define SHFL_CF_ACCESS_ATTR_READWRITE   (SHFL_CF_ACCESS_ATTR_READ | SHFL_CF_ACCESS_ATTR_WRITE)

/** The file is opened in append mode. Ignored if SHFL_CF_ACCESS_WRITE is not set. */
#define SHFL_CF_ACCESS_APPEND           (0x00040000)

/** @} */

#pragma pack(1)
typedef struct _SHFLCREATEPARMS
{
    /* Returned handle of opened object. */
    SHFLHANDLE Handle;

    /* Returned result of the operation */
    SHFLCREATERESULT Result;

    /* SHFL_CF_* */
    uint32_t CreateFlags;

    /* Attributes of object to create and
     * returned actual attributes of opened/created object.
     */
    SHFLFSOBJINFO Info;

} SHFLCREATEPARMS;
#pragma pack()

typedef SHFLCREATEPARMS *PSHFLCREATEPARMS;


/** Shared Folders mappings.
 *  @{
 */

/** The mapping has been added since last query. */
#define SHFL_MS_NEW        (1)
/** The mapping has been deleted since last query. */
#define SHFL_MS_DELETED    (2)

typedef struct _SHFLMAPPING
{
    /** Mapping status. */
    uint32_t u32Status;
    /** Root handle. */
    SHFLROOT root;
} SHFLMAPPING;
/** Pointer to a SHFLMAPPING structure. */
typedef SHFLMAPPING *PSHFLMAPPING;

/** @} */

/** Shared Folder directory information
 *  @{
 */

typedef struct _SHFLDIRINFO
{
    /** Full information about the object. */
    SHFLFSOBJINFO   Info;
    /** The length of the short field (number of RTUTF16 chars).
     * It is 16-bit for reasons of alignment. */
    uint16_t        cucShortName;
    /** The short name for 8.3 compatibility.
     * Empty string if not available.
     */
    RTUTF16         uszShortName[14];
    /** @todo malc, a description, please. */
    SHFLSTRING      name;
} SHFLDIRINFO, *PSHFLDIRINFO;


/**
 * Shared folder filesystem properties.
 */
typedef struct SHFLFSPROPERTIES
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
} SHFLFSPROPERTIES;
AssertCompileSize(SHFLFSPROPERTIES, 12);
/** Pointer to a shared folder filesystem properties structure. */
typedef SHFLFSPROPERTIES *PSHFLFSPROPERTIES;
/** Pointer to a const shared folder filesystem properties structure. */
typedef SHFLFSPROPERTIES const *PCSHFLFSPROPERTIES;


/**
 * Copy file system properties from IPRT to shared folder format.
 *
 * @param   pDst                The shared folder structure.
 * @param   pSrc                The IPRT structure.
 */
DECLINLINE(void) vbfsCopyFsPropertiesFromIprt(PSHFLFSPROPERTIES pDst, PCRTFSPROPERTIES pSrc)
{
    RT_ZERO(*pDst);                     /* zap the implicit padding. */
    pDst->cbMaxComponent   = pSrc->cbMaxComponent;
    pDst->fRemote          = pSrc->fRemote;
    pDst->fCaseSensitive   = pSrc->fCaseSensitive;
    pDst->fReadOnly        = pSrc->fReadOnly;
    pDst->fSupportsUnicode = pSrc->fSupportsUnicode;
    pDst->fCompressed      = pSrc->fCompressed;
    pDst->fFileCompression = pSrc->fFileCompression;
}


typedef struct _SHFLVOLINFO
{
    RTFOFF         ullTotalAllocationBytes;
    RTFOFF         ullAvailableAllocationBytes;
    uint32_t       ulBytesPerAllocationUnit;
    uint32_t       ulBytesPerSector;
    uint32_t       ulSerial;
    SHFLFSPROPERTIES fsProperties;
} SHFLVOLINFO, *PSHFLVOLINFO;

/** @} */

/** Function parameter structures.
 *  @{
 */

/**
 * SHFL_FN_QUERY_MAPPINGS
 */
/** Validation mask.  Needs to be adjusted
  * whenever a new SHFL_MF_ flag is added. */
#define SHFL_MF_MASK       (0x00000011)
/** UC2 enconded strings. */
#define SHFL_MF_UCS2       (0x00000000)
/** Guest uses UTF8 strings, if not set then the strings are unicode (UCS2). */
#define SHFL_MF_UTF8       (0x00000001)
/** Just handle the auto-mounted folders. */
#define SHFL_MF_AUTOMOUNT  (0x00000010)

/** Type of guest system. For future system dependent features. */
#define SHFL_MF_SYSTEM_MASK    (0x0000FF00)
#define SHFL_MF_SYSTEM_NONE    (0x00000000)
#define SHFL_MF_SYSTEM_WINDOWS (0x00000100)
#define SHFL_MF_SYSTEM_LINUX   (0x00000200)

/** Parameters structure. */
typedef struct _VBoxSFQueryMappings
{
    VBoxGuestHGCMCallInfo callInfo;

    /** 32bit, in:
     * Flags describing various client needs.
     */
    HGCMFunctionParameter flags;

    /** 32bit, in/out:
     * Number of mappings the client expects.
     * This is the number of elements in the
     * mappings array.
     */
    HGCMFunctionParameter numberOfMappings;

    /** pointer, in/out:
     * Points to array of SHFLMAPPING structures.
     */
    HGCMFunctionParameter mappings;

} VBoxSFQueryMappings;

/** Number of parameters */
#define SHFL_CPARMS_QUERY_MAPPINGS (3)



/**
 * SHFL_FN_QUERY_MAP_NAME
 */

/** Parameters structure. */
typedef struct _VBoxSFQueryMapName
{
    VBoxGuestHGCMCallInfo callInfo;

    /** 32bit, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in/out:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter name;

} VBoxSFQueryMapName;

/** Number of parameters */
#define SHFL_CPARMS_QUERY_MAP_NAME (2)

/**
 * SHFL_FN_MAP_FOLDER_OLD
 */

/** Parameters structure. */
typedef struct _VBoxSFMapFolder_Old
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter path;

    /** pointer, out: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in: RTUTF16
     * Path delimiter
     */
    HGCMFunctionParameter delimiter;

} VBoxSFMapFolder_Old;

/** Number of parameters */
#define SHFL_CPARMS_MAP_FOLDER_OLD (3)

/**
 * SHFL_FN_MAP_FOLDER
 */

/** Parameters structure. */
typedef struct _VBoxSFMapFolder
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter path;

    /** pointer, out: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in: RTUTF16
     * Path delimiter
     */
    HGCMFunctionParameter delimiter;

    /** pointer, in: SHFLROOT
     * Case senstive flag
     */
    HGCMFunctionParameter fCaseSensitive;

} VBoxSFMapFolder;

/** Number of parameters */
#define SHFL_CPARMS_MAP_FOLDER (4)

/**
 * SHFL_FN_UNMAP_FOLDER
 */

/** Parameters structure. */
typedef struct _VBoxSFUnmapFolder
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

} VBoxSFUnmapFolder;

/** Number of parameters */
#define SHFL_CPARMS_UNMAP_FOLDER (1)


/**
 * SHFL_FN_CREATE
 */

/** Parameters structure. */
typedef struct _VBoxSFCreate
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter path;

    /** pointer, in/out:
     * Points to SHFLCREATEPARMS buffer.
     */
    HGCMFunctionParameter parms;

} VBoxSFCreate;

/** Number of parameters */
#define SHFL_CPARMS_CREATE (3)


/**
 * SHFL_FN_CLOSE
 */

/** Parameters structure. */
typedef struct _VBoxSFClose
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;


    /** value64, in:
     * SHFLHANDLE of object to close.
     */
    HGCMFunctionParameter handle;

} VBoxSFClose;

/** Number of parameters */
#define SHFL_CPARMS_CLOSE (2)


/**
 * SHFL_FN_READ
 */

/** Parameters structure. */
typedef struct _VBoxSFRead
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to read from.
     */
    HGCMFunctionParameter handle;

    /** value64, in:
     * Offset to read from.
     */
    HGCMFunctionParameter offset;

    /** value64, in/out:
     * Bytes to read/How many were read.
     */
    HGCMFunctionParameter cb;

    /** pointer, out:
     * Buffer to place data to.
     */
    HGCMFunctionParameter buffer;

} VBoxSFRead;

/** Number of parameters */
#define SHFL_CPARMS_READ (5)



/**
 * SHFL_FN_WRITE
 */

/** Parameters structure. */
typedef struct _VBoxSFWrite
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to write to.
     */
    HGCMFunctionParameter handle;

    /** value64, in:
     * Offset to write to.
     */
    HGCMFunctionParameter offset;

    /** value64, in/out:
     * Bytes to write/How many were written.
     */
    HGCMFunctionParameter cb;

    /** pointer, in:
     * Data to write.
     */
    HGCMFunctionParameter buffer;

} VBoxSFWrite;

/** Number of parameters */
#define SHFL_CPARMS_WRITE (5)



/**
 * SHFL_FN_LOCK
 */

/** Lock owner is the HGCM client. */

/** Lock mode bit mask. */
#define SHFL_LOCK_MODE_MASK  (0x3)
/** Cancel lock on the given range. */
#define SHFL_LOCK_CANCEL     (0x0)
/** Acquire read only lock. Prevent write to the range. */
#define SHFL_LOCK_SHARED     (0x1)
/** Acquire write lock. Prevent both write and read to the range. */
#define SHFL_LOCK_EXCLUSIVE  (0x2)

/** Do not wait for lock if it can not be acquired at the time. */
#define SHFL_LOCK_NOWAIT     (0x0)
/** Wait and acquire lock. */
#define SHFL_LOCK_WAIT       (0x4)

/** Lock the specified range. */
#define SHFL_LOCK_PARTIAL    (0x0)
/** Lock entire object. */
#define SHFL_LOCK_ENTIRE     (0x8)

/** Parameters structure. */
typedef struct _VBoxSFLock
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to be locked.
     */
    HGCMFunctionParameter handle;

    /** value64, in:
     * Starting offset of lock range.
     */
    HGCMFunctionParameter offset;

    /** value64, in:
     * Length of range.
     */
    HGCMFunctionParameter length;

    /** value32, in:
     * Lock flags SHFL_LOCK_*.
     */
    HGCMFunctionParameter flags;

} VBoxSFLock;

/** Number of parameters */
#define SHFL_CPARMS_LOCK (5)



/**
 * SHFL_FN_FLUSH
 */

/** Parameters structure. */
typedef struct _VBoxSFFlush
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to be locked.
     */
    HGCMFunctionParameter handle;

} VBoxSFFlush;

/** Number of parameters */
#define SHFL_CPARMS_FLUSH (2)

/**
 * SHFL_FN_LIST
 */

/** Listing information includes variable length RTDIRENTRY[EX] structures. */

/** @todo might be necessary for future. */
#define SHFL_LIST_NONE          0
#define SHFL_LIST_RETURN_ONE        1

/** Parameters structure. */
typedef struct _VBoxSFList
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to be listed.
     */
    HGCMFunctionParameter handle;

    /** value32, in:
     * List flags SHFL_LIST_*.
     */
    HGCMFunctionParameter flags;

    /** value32, in/out:
     * Bytes to be used for listing information/How many bytes were used.
     */
    HGCMFunctionParameter cb;

    /** pointer, in/optional
     * Points to SHFLSTRING buffer that specifies a search path.
     */
    HGCMFunctionParameter path;

    /** pointer, out:
     * Buffer to place listing information to. (SHFLDIRINFO)
     */
    HGCMFunctionParameter buffer;

    /** value32, in/out:
     * Indicates a key where the listing must be resumed.
     * in: 0 means start from begin of object.
     * out: 0 means listing completed.
     */
    HGCMFunctionParameter resumePoint;

    /** pointer, out:
     * Number of files returned
     */
    HGCMFunctionParameter cFiles;

} VBoxSFList;

/** Number of parameters */
#define SHFL_CPARMS_LIST (8)



/**
 * SHFL_FN_READLINK
 */

/** Parameters structure. */
typedef struct _VBoxSFReadLink
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter path;

    /** pointer, out:
     * Buffer to place data to.
     */
    HGCMFunctionParameter buffer;

} VBoxSFReadLink;

/** Number of parameters */
#define SHFL_CPARMS_READLINK (3)



/**
 * SHFL_FN_INFORMATION
 */

/** Mask of Set/Get bit. */
#define SHFL_INFO_MODE_MASK    (0x1)
/** Get information */
#define SHFL_INFO_GET          (0x0)
/** Set information */
#define SHFL_INFO_SET          (0x1)

/** Get name of the object. */
#define SHFL_INFO_NAME         (0x2)
/** Set size of object (extend/trucate); only applies to file objects */
#define SHFL_INFO_SIZE         (0x4)
/** Get/Set file object info. */
#define SHFL_INFO_FILE         (0x8)
/** Get volume information. */
#define SHFL_INFO_VOLUME       (0x10)

/** @todo different file info structures */


/** Parameters structure. */
typedef struct _VBoxSFInformation
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** value64, in:
     * SHFLHANDLE of object to be listed.
     */
    HGCMFunctionParameter handle;

    /** value32, in:
     * SHFL_INFO_*
     */
    HGCMFunctionParameter flags;

    /** value32, in/out:
     * Bytes to be used for information/How many bytes were used.
     */
    HGCMFunctionParameter cb;

    /** pointer, in/out:
     * Information to be set/get (SHFLFSOBJINFO or SHFLSTRING). Do not forget
     * to set the SHFLFSOBJINFO::Attr::enmAdditional for Get operation as well.
     */
    HGCMFunctionParameter info;

} VBoxSFInformation;

/** Number of parameters */
#define SHFL_CPARMS_INFORMATION (5)


/**
 * SHFL_FN_REMOVE
 */

#define SHFL_REMOVE_FILE        (0x1)
#define SHFL_REMOVE_DIR         (0x2)
#define SHFL_REMOVE_SYMLINK     (0x4)

/** Parameters structure. */
typedef struct _VBoxSFRemove
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in:
     * Points to SHFLSTRING buffer.
     */
    HGCMFunctionParameter path;

    /** value32, in:
     * remove flags (file/directory)
     */
    HGCMFunctionParameter flags;

} VBoxSFRemove;

#define SHFL_CPARMS_REMOVE  (3)


/**
 * SHFL_FN_RENAME
 */

#define SHFL_RENAME_FILE                (0x1)
#define SHFL_RENAME_DIR                 (0x2)
#define SHFL_RENAME_REPLACE_IF_EXISTS   (0x4)

/** Parameters structure. */
typedef struct _VBoxSFRename
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in:
     * Points to SHFLSTRING src.
     */
    HGCMFunctionParameter src;

    /** pointer, in:
     * Points to SHFLSTRING dest.
     */
    HGCMFunctionParameter dest;

    /** value32, in:
     * rename flags (file/directory)
     */
    HGCMFunctionParameter flags;

} VBoxSFRename;

#define SHFL_CPARMS_RENAME  (4)


/**
 * SHFL_FN_SYMLINK
 */

/** Parameters structure. */
typedef struct _VBoxSFSymlink
{
    VBoxGuestHGCMCallInfo callInfo;

    /** pointer, in: SHFLROOT
     * Root handle of the mapping which name is queried.
     */
    HGCMFunctionParameter root;

    /** pointer, in:
     * Points to SHFLSTRING of path for the new symlink.
     */
    HGCMFunctionParameter newPath;

    /** pointer, in:
     * Points to SHFLSTRING of destination for symlink.
     */
    HGCMFunctionParameter oldPath;

    /** pointer, out:
     * Information about created symlink.
     */
    HGCMFunctionParameter info;

} VBoxSFSymlink;

#define SHFL_CPARMS_SYMLINK  (4)



/**
 * SHFL_FN_ADD_MAPPING
 * Host call, no guest structure is used.
 */

/** mapping is writable */
#define SHFL_ADD_MAPPING_F_WRITABLE         (RT_BIT_32(0))
/** mapping is automounted by the guest */
#define SHFL_ADD_MAPPING_F_AUTOMOUNT        (RT_BIT_32(1))
/** allow the guest to create symlinks */
#define SHFL_ADD_MAPPING_F_CREATE_SYMLINKS  (RT_BIT_32(2))
/** mapping is actually missing on the host */
#define SHFL_ADD_MAPPING_F_MISSING          (RT_BIT_32(3))

#define SHFL_CPARMS_ADD_MAPPING  (3)

/**
 * SHFL_FN_REMOVE_MAPPING
 * Host call, no guest structure is used.
 */

#define SHFL_CPARMS_REMOVE_MAPPING (1)


/**
 * SHFL_FN_SET_STATUS_LED
 * Host call, no guest structure is used.
 */

#define SHFL_CPARMS_SET_STATUS_LED (1)

/** @} */

#endif
