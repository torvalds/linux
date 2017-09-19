/* $Id: RTErrConvertToErrno.cpp $ */
/** @file
 * IPRT - Convert iprt status codes to errno.
 */

/*
 * Copyright (C) 2007-2016 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/err.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/errno.h>


RTDECL(int) RTErrConvertToErrno(int iErr)
{
    /* very fast check for no error. */
    if (RT_SUCCESS(iErr))
        return 0;

    /*
     * Process error codes.
     *
     * (Use a switch and not a table since the numbers vary among compilers
     * and OSes. So we let the compiler switch optimizer handle speed issues.)
     *
     * This switch is arranged like the Linux i386 errno.h! It also mirrors the
     * conversions performed by RTErrConvertFromErrno with a few extra case since
     * there are far more IPRT status codes than Unix ones.
     */
    switch (iErr)
    {
#ifdef EPERM
        case VERR_ACCESS_DENIED:                    return EPERM;
#endif
#ifdef ENOENT
        case VERR_FILE_NOT_FOUND:                   return ENOENT;
#endif
#ifdef ESRCH
        case VERR_PROCESS_NOT_FOUND:                return ESRCH;
#endif
#ifdef EINTR
        case VERR_INTERRUPTED:                      return EINTR;
#endif
#ifdef EIO
        case VERR_DEV_IO_ERROR:                     return EIO;
#endif
#ifdef ENXIO
        //case VERR_DEV_IO_ERROR:                     return ENXIO;
#endif
#ifdef E2BIG
        case VERR_TOO_MUCH_DATA:                    return E2BIG;
#endif
#ifdef ENOEXEC
        case VERR_BAD_EXE_FORMAT:                   return ENOEXEC;
#endif
#ifdef EBADF
        case VERR_INVALID_HANDLE:                   return EBADF;
#endif
#ifdef ECHILD
        //case VERR_PROCESS_NOT_FOUND:                return ECHILD;
#endif
#ifdef EAGAIN
        case VERR_TRY_AGAIN:                        return EAGAIN;
#endif
#ifdef ENOMEM
        case VERR_NO_MEMORY:                        return ENOMEM;
#endif
#ifdef EACCES
        //case VERR_ACCESS_DENIED:                    return EACCES;
#endif
#ifdef EFAULT
        case VERR_INVALID_POINTER:                  return EFAULT;
#endif
#ifdef ENOTBLK
        //case ENOTBLK:           return VERR_;
#endif
#ifdef EBUSY
        case VERR_RESOURCE_BUSY:                    return EBUSY;
#endif
#ifdef EEXIST
        case VERR_ALREADY_EXISTS:                   return EEXIST;
#endif
#ifdef EXDEV
        case VERR_NOT_SAME_DEVICE:                  return EXDEV;
#endif
#ifdef ENODEV
        //case VERR_NOT_SUPPORTED:                    return ENODEV;
#endif
#ifdef ENOTDIR
        case VERR_NOT_A_DIRECTORY:
        case VERR_PATH_NOT_FOUND:                   return ENOTDIR;
#endif
#ifdef EISDIR
        case VERR_IS_A_DIRECTORY:                   return EISDIR;
#endif
#ifdef EINVAL
        case VERR_INVALID_PARAMETER:                return EINVAL;
#endif
#ifdef ENFILE
        case VERR_TOO_MANY_OPEN_FILES:              return ENFILE;
#endif
#ifdef EMFILE
        //case VERR_TOO_MANY_OPEN_FILES:              return EMFILE;
#endif
#ifdef ENOTTY
        case VERR_INVALID_FUNCTION:                 return ENOTTY;
#endif
#ifdef ETXTBSY
        case VERR_SHARING_VIOLATION:                return ETXTBSY;
#endif
#ifdef EFBIG
        case VERR_FILE_TOO_BIG:                     return EFBIG;
#endif
#ifdef ENOSPC
        case VERR_DISK_FULL:                        return ENOSPC;
#endif
#ifdef ESPIPE
        case VERR_SEEK_ON_DEVICE:                   return ESPIPE;
#endif
#ifdef EROFS
        case VERR_WRITE_PROTECT:                    return EROFS;
#endif
#ifdef EMLINK
        //case EMLINK:
#endif
#ifdef EPIPE
        case VERR_BROKEN_PIPE:                      return EPIPE;
#endif
#ifdef EDOM
        //case VERR_INVALID_PARAMETER:    return EDOM;
#endif
#ifdef ERANGE
        //case VERR_INVALID_PARAMETER:    return ERANGE;
#endif
#ifdef EDEADLK
        case VERR_DEADLOCK:                         return EDEADLK;
#endif
#ifdef ENAMETOOLONG
        case VERR_FILENAME_TOO_LONG:                return ENAMETOOLONG;
#endif
#ifdef ENOLCK
        case VERR_FILE_LOCK_FAILED:                 return ENOLCK;
#endif
#ifdef ENOSYS
        case VERR_NOT_IMPLEMENTED:
        case VERR_NOT_SUPPORTED:                    return ENOSYS;
#endif
#ifdef ENOTEMPTY
        case VERR_DIR_NOT_EMPTY:                    return ENOTEMPTY;
#endif
#ifdef ELOOP
        case VERR_TOO_MANY_SYMLINKS:                return ELOOP;
#endif
        //41??
#ifdef ENOMSG
        //case ENOMSG           42      /* No message of desired type */
#endif
#ifdef EIDRM
        //case EIDRM            43      /* Identifier removed */
#endif
#ifdef ECHRNG
        //case ECHRNG           44      /* Channel number out of range */
#endif
#ifdef EL2NSYNC
        //case EL2NSYNC 45      /* Level 2 not synchronized */
#endif
#ifdef EL3HLT
        //case EL3HLT           46      /* Level 3 halted */
#endif
#ifdef EL3RST
        //case EL3RST           47      /* Level 3 reset */
#endif
#ifdef ELNRNG
        //case ELNRNG           48      /* Link number out of range */
#endif
#ifdef EUNATCH
        //case EUNATCH          49      /* Protocol driver not attached */
#endif
#ifdef ENOCSI
        //case ENOCSI           50      /* No CSI structure available */
#endif
#ifdef EL2HLT
        //case EL2HLT           51      /* Level 2 halted */
#endif
#ifdef EBADE
        //case EBADE            52      /* Invalid exchange */
#endif
#ifdef EBADR
        //case EBADR            53      /* Invalid request descriptor */
#endif
#ifdef EXFULL
        //case EXFULL           54      /* Exchange full */
#endif
#ifdef ENOANO
        //case ENOANO           55      /* No anode */
#endif
#ifdef EBADRQC
        //case EBADRQC          56      /* Invalid request code */
#endif
#ifdef EBADSLT
        //case EBADSLT          57      /* Invalid slot */
#endif
        //case 58:
#ifdef EBFONT
        //case EBFONT           59      /* Bad font file format */
#endif
#ifdef ENOSTR
        //case ENOSTR           60      /* Device not a stream */
#endif
#ifdef ENODATA
        case VERR_NO_DATA:                          return ENODATA;
#endif
#ifdef ETIME
        //case ETIME            62      /* Timer expired */
#endif
#ifdef ENOSR
        //case ENOSR            63      /* Out of streams resources */
#endif
#ifdef ENONET
        case VERR_NET_NO_NETWORK:                   return ENONET;
#endif
#ifdef ENOPKG
        //case ENOPKG           65      /* Package not installed */
#endif
#ifdef EREMOTE
        //case EREMOTE          66      /* Object is remote */
#endif
#ifdef ENOLINK
        //case ENOLINK          67      /* Link has been severed */
#endif
#ifdef EADV
        //case EADV             68      /* Advertise error */
#endif
#ifdef ESRMNT
        //case ESRMNT           69      /* Srmount error */
#endif
#ifdef ECOMM
        //case ECOMM            70      /* Communication error on send */
#endif
#ifdef EPROTO
        //case EPROTO           71      /* Protocol error */
#endif
#ifdef EMULTIHOP
        //case EMULTIHOP        72      /* Multihop attempted */
#endif
#ifdef EDOTDOT
        //case EDOTDOT          73      /* RFS specific error */
#endif
#ifdef EBADMSG
        //case EBADMSG          74      /* Not a data message */
#endif
#ifdef EOVERFLOW
        //case VERR_TOO_MUCH_DATA:                    return EOVERFLOW;
#endif
#ifdef ENOTUNIQ
        case VERR_NET_NOT_UNIQUE_NAME:              return ENOTUNIQ;
#endif
#ifdef EBADFD
        //case VERR_INVALID_HANDLE:                   return EBADFD;
#endif
#ifdef EREMCHG
        //case EREMCHG          78      /* Remote address changed */
#endif
#ifdef ELIBACC
        //case ELIBACC          79      /* Can not access a needed shared library */
#endif
#ifdef ELIBBAD
        //case ELIBBAD          80      /* Accessing a corrupted shared library */
#endif
#ifdef ELIBSCN
        //case ELIBSCN          81      /* .lib section in a.out corrupted */
#endif
#ifdef ELIBMAX
        //case ELIBMAX          82      /* Attempting to link in too many shared libraries */
#endif
#ifdef ELIBEXEC
        //case ELIBEXEC 83      /* Cannot exec a shared library directly */
#endif
#ifdef EILSEQ
        case VERR_NO_TRANSLATION:                   return EILSEQ;
#endif
#ifdef ERESTART
        //case VERR_INTERRUPTED:                      return ERESTART;
#endif
#ifdef ESTRPIPE
        //case ESTRPIPE 86      /* Streams pipe error */
#endif
#ifdef EUSERS
        //case EUSERS           87      /* Too many users */
#endif
#ifdef ENOTSOCK
        case VERR_NET_NOT_SOCKET:                   return ENOTSOCK;
#endif
#ifdef EDESTADDRREQ
        case VERR_NET_DEST_ADDRESS_REQUIRED:        return EDESTADDRREQ;
#endif
#ifdef EMSGSIZE
        case VERR_NET_MSG_SIZE:                     return EMSGSIZE;
#endif
#ifdef EPROTOTYPE
        case VERR_NET_PROTOCOL_TYPE:                return EPROTOTYPE;
#endif
#ifdef ENOPROTOOPT
        case VERR_NET_PROTOCOL_NOT_AVAILABLE:       return ENOPROTOOPT;
#endif
#ifdef EPROTONOSUPPORT
        case VERR_NET_PROTOCOL_NOT_SUPPORTED:       return EPROTONOSUPPORT;
#endif
#ifdef ESOCKTNOSUPPORT
        case VERR_NET_SOCKET_TYPE_NOT_SUPPORTED:    return ESOCKTNOSUPPORT;
#endif
#ifdef EOPNOTSUPP
        case VERR_NET_OPERATION_NOT_SUPPORTED:      return EOPNOTSUPP;
#endif
#ifdef EPFNOSUPPORT
        case VERR_NET_PROTOCOL_FAMILY_NOT_SUPPORTED: return EPFNOSUPPORT;
#endif
#ifdef EAFNOSUPPORT
        case VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED: return EAFNOSUPPORT;
#endif
#ifdef EADDRINUSE
        case VERR_NET_ADDRESS_IN_USE:               return EADDRINUSE;
#endif
#ifdef EADDRNOTAVAIL
        case VERR_NET_ADDRESS_NOT_AVAILABLE:        return EADDRNOTAVAIL;
#endif
#ifdef ENETDOWN
        case VERR_NET_DOWN:                         return ENETDOWN;
#endif
#ifdef ENETUNREACH
        case VERR_NET_UNREACHABLE:                  return ENETUNREACH;
#endif
#ifdef ENETRESET
        case VERR_NET_CONNECTION_RESET:             return ENETRESET;
#endif
#ifdef ECONNABORTED
        case VERR_NET_CONNECTION_ABORTED:           return ECONNABORTED;
#endif
#ifdef ECONNRESET
        case VERR_NET_CONNECTION_RESET_BY_PEER:     return ECONNRESET;
#endif
#ifdef ENOBUFS
        case VERR_NET_NO_BUFFER_SPACE:              return ENOBUFS;
#endif
#ifdef EISCONN
        case VERR_NET_ALREADY_CONNECTED:            return EISCONN;
#endif
#ifdef ENOTCONN
        case VERR_NET_NOT_CONNECTED:                return ENOTCONN;
#endif
#ifdef ESHUTDOWN
        case VERR_NET_SHUTDOWN:                     return ESHUTDOWN;
#endif
#ifdef ETOOMANYREFS
        case VERR_NET_TOO_MANY_REFERENCES:          return ETOOMANYREFS;
#endif
#ifdef ETIMEDOUT
        case VERR_TIMEOUT:                          return ETIMEDOUT;
#endif
#ifdef ECONNREFUSED
        case VERR_NET_CONNECTION_REFUSED:           return ECONNREFUSED;
#endif
#ifdef EHOSTDOWN
        case VERR_NET_HOST_DOWN:                    return EHOSTDOWN;
#endif
#ifdef EHOSTUNREACH
        case VERR_NET_HOST_UNREACHABLE:             return EHOSTUNREACH;
#endif
#ifdef EALREADY
        case VERR_NET_ALREADY_IN_PROGRESS:          return EALREADY;
#endif
#ifdef EINPROGRESS
        case VERR_NET_IN_PROGRESS:                  return EINPROGRESS;
#endif
#ifdef ESTALE
        //case ESTALE           116     /* Stale NFS file handle */
#endif
#ifdef EUCLEAN
        //case EUCLEAN          117     /* Structure needs cleaning */
#endif
#ifdef ENOTNAM
        //case ENOTNAM          118     /* Not a XENIX named type file */
#endif
#ifdef ENAVAIL
        //case ENAVAIL          119     /* No XENIX semaphores available */
#endif
#ifdef EISNAM
        //case EISNAM           120     /* Is a named type file */
#endif
#ifdef EREMOTEIO
        //case EREMOTEIO        121     /* Remote I/O error */
#endif
#ifdef EDQUOT
        //case VERR_DISK_FULL:                        return EDQUOT;
#endif
#ifdef ENOMEDIUM
        case VERR_MEDIA_NOT_PRESENT:                return ENOMEDIUM;
#endif
#ifdef EMEDIUMTYPE
        case VERR_MEDIA_NOT_RECOGNIZED:             return EMEDIUMTYPE;
#endif

        /* Non-linux */

#ifdef EPROCLIM
        case VERR_MAX_PROCS_REACHED:                return EPROCLIM;
#endif
#ifdef EDOOFUS
        case VERR_INTERNAL_ERROR:
        case VERR_INTERNAL_ERROR_2:
        case VERR_INTERNAL_ERROR_3:                 return EDOOFUS;
#endif

        default:
            /* The idea here is that if you hit this, you will have to
               translate the status code yourself. */
            AssertMsgFailed(("Unhandled error code %Rrc\n", iErr));
#ifdef EPROTO
            return EPROTO;
#else
            return EINVAL;
#endif
    }
}
RT_EXPORT_SYMBOL(RTErrConvertToErrno);

