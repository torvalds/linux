/*
 *   fs/cifs/smbfsctl.h: SMB, CIFS, SMB2 FSCTL definitions
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2013
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* IOCTL information */
/*
 * List of ioctl/fsctl function codes that are or could be useful in the
 * future to remote clients like cifs or SMB2/SMB3 client.  This is probably
 * a slightly larger set of fsctls that NTFS local filesystem could handle,
 * including the seven below that we do not have struct definitions for.
 * Even with protocol definitions for most of these now available, we still
 * need to do some experimentation to identify which are practical to do
 * remotely.  Some of the following, such as the encryption/compression ones
 * could be invoked from tools via a specialized hook into the VFS rather
 * than via the standard vfs entry points
 *
 * See MS-SMB2 Section 2.2.31 (last checked June 2013, all of that list are
 * below). Additional detail on less common ones can be found in MS-FSCC
 * section 2.3.
 */

/*
 * FSCTL values are 32 bits and are constructed as
 * <device 16bits> <access 2bits> <function 12bits> <method 2bits>
 */
/* Device */
#define FSCTL_DEVICE_DFS                 (0x0006 << 16)
#define FSCTL_DEVICE_FILE_SYSTEM         (0x0009 << 16)
#define FSCTL_DEVICE_NAMED_PIPE          (0x0011 << 16)
#define FSCTL_DEVICE_NETWORK_FILE_SYSTEM (0x0014 << 16)
#define FSCTL_DEVICE_MASK                0xffff0000
/* Access */
#define FSCTL_DEVICE_ACCESS_FILE_ANY_ACCESS        (0x00 << 14)
#define FSCTL_DEVICE_ACCESS_FILE_READ_ACCESS       (0x01 << 14)
#define FSCTL_DEVICE_ACCESS_FILE_WRITE_ACCESS      (0x02 << 14)
#define FSCTL_DEVICE_ACCESS_FILE_READ_WRITE_ACCESS (0x03 << 14)
#define FSCTL_DEVICE_ACCESS_MASK                   0x0000c000
/* Function */
#define FSCTL_DEVICE_FUNCTION_MASK       0x00003ffc
/* Method */
#define FSCTL_DEVICE_METHOD_BUFFERED   0x00
#define FSCTL_DEVICE_METHOD_IN_DIRECT  0x01
#define FSCTL_DEVICE_METHOD_OUT_DIRECT 0x02
#define FSCTL_DEVICE_METHOD_NEITHER    0x03
#define FSCTL_DEVICE_METHOD_MASK       0x00000003


#define FSCTL_DFS_GET_REFERRALS      0x00060194
#define FSCTL_DFS_GET_REFERRALS_EX   0x000601B0
#define FSCTL_REQUEST_OPLOCK_LEVEL_1 0x00090000
#define FSCTL_REQUEST_OPLOCK_LEVEL_2 0x00090004
#define FSCTL_REQUEST_BATCH_OPLOCK   0x00090008
#define FSCTL_LOCK_VOLUME            0x00090018
#define FSCTL_UNLOCK_VOLUME          0x0009001C
#define FSCTL_IS_PATHNAME_VALID      0x0009002C /* BB add struct */
#define FSCTL_GET_COMPRESSION        0x0009003C /* BB add struct */
#define FSCTL_SET_COMPRESSION        0x0009C040 /* BB add struct */
#define FSCTL_QUERY_FAT_BPB          0x00090058 /* BB add struct */
/* Verify the next FSCTL number, we had it as 0x00090090 before */
#define FSCTL_FILESYSTEM_GET_STATS   0x00090060 /* BB add struct */
#define FSCTL_GET_NTFS_VOLUME_DATA   0x00090064 /* BB add struct */
#define FSCTL_GET_RETRIEVAL_POINTERS 0x00090073 /* BB add struct */
#define FSCTL_IS_VOLUME_DIRTY        0x00090078 /* BB add struct */
#define FSCTL_ALLOW_EXTENDED_DASD_IO 0x00090083 /* BB add struct */
#define FSCTL_REQUEST_FILTER_OPLOCK  0x0009008C
#define FSCTL_FIND_FILES_BY_SID      0x0009008F /* BB add struct */
#define FSCTL_SET_OBJECT_ID          0x00090098 /* BB add struct */
#define FSCTL_GET_OBJECT_ID          0x0009009C /* BB add struct */
#define FSCTL_DELETE_OBJECT_ID       0x000900A0 /* BB add struct */
#define FSCTL_SET_REPARSE_POINT      0x000900A4 /* BB add struct */
#define FSCTL_GET_REPARSE_POINT      0x000900A8 /* BB add struct */
#define FSCTL_DELETE_REPARSE_POINT   0x000900AC /* BB add struct */
#define FSCTL_SET_OBJECT_ID_EXTENDED 0x000900BC /* BB add struct */
#define FSCTL_CREATE_OR_GET_OBJECT_ID 0x000900C0 /* BB add struct */
#define FSCTL_SET_SPARSE             0x000900C4 /* BB add struct */
#define FSCTL_SET_ZERO_DATA          0x000980C8
#define FSCTL_SET_ENCRYPTION         0x000900D7 /* BB add struct */
#define FSCTL_ENCRYPTION_FSCTL_IO    0x000900DB /* BB add struct */
#define FSCTL_WRITE_RAW_ENCRYPTED    0x000900DF /* BB add struct */
#define FSCTL_READ_RAW_ENCRYPTED     0x000900E3 /* BB add struct */
#define FSCTL_READ_FILE_USN_DATA     0x000900EB /* BB add struct */
#define FSCTL_WRITE_USN_CLOSE_RECORD 0x000900EF /* BB add struct */
#define FSCTL_SIS_COPYFILE           0x00090100 /* BB add struct */
#define FSCTL_RECALL_FILE            0x00090117 /* BB add struct */
#define FSCTL_QUERY_SPARING_INFO     0x00090138 /* BB add struct */
#define FSCTL_SET_ZERO_ON_DEALLOC    0x00090194 /* BB add struct */
#define FSCTL_SET_SHORT_NAME_BEHAVIOR 0x000901B4 /* BB add struct */
#define FSCTL_GET_INTEGRITY_INFORMATION 0x0009027C
#define FSCTL_QUERY_ALLOCATED_RANGES 0x000940CF /* BB add struct */
#define FSCTL_SET_DEFECT_MANAGEMENT  0x00098134 /* BB add struct */
#define FSCTL_FILE_LEVEL_TRIM        0x00098208 /* BB add struct */
#define FSCTL_DUPLICATE_EXTENTS_TO_FILE 0x00098344
#define FSCTL_SIS_LINK_FILES         0x0009C104
#define FSCTL_SET_INTEGRITY_INFORMATION 0x0009C280
#define FSCTL_PIPE_PEEK              0x0011400C /* BB add struct */
#define FSCTL_PIPE_TRANSCEIVE        0x0011C017 /* BB add struct */
/* strange that the number for this op is not sequential with previous op */
#define FSCTL_PIPE_WAIT              0x00110018 /* BB add struct */
/* Enumerate previous versions of a file */
#define FSCTL_SRV_ENUMERATE_SNAPSHOTS 0x00144064
/* Retrieve an opaque file reference for server-side data movement ie copy */
#define FSCTL_SRV_REQUEST_RESUME_KEY 0x00140078
#define FSCTL_LMR_REQUEST_RESILIENCY 0x001401D4
#define FSCTL_LMR_GET_LINK_TRACK_INF 0x001400E8 /* BB add struct */
#define FSCTL_LMR_SET_LINK_TRACK_INF 0x001400EC /* BB add struct */
#define FSCTL_VALIDATE_NEGOTIATE_INFO 0x00140204
/* Perform server-side data movement */
#define FSCTL_SRV_COPYCHUNK 0x001440F2
#define FSCTL_SRV_COPYCHUNK_WRITE 0x001480F2
#define FSCTL_QUERY_NETWORK_INTERFACE_INFO 0x001401FC /* BB add struct */
#define FSCTL_SRV_READ_HASH          0x001441BB /* BB add struct */

/* See FSCC 2.1.2.5 */
#define IO_REPARSE_TAG_MOUNT_POINT   0xA0000003
#define IO_REPARSE_TAG_HSM           0xC0000004
#define IO_REPARSE_TAG_SIS           0x80000007
#define IO_REPARSE_TAG_HSM2          0x80000006
#define IO_REPARSE_TAG_DRIVER_EXTENDER 0x80000005
/* Used by the DFS filter. See MS-DFSC */
#define IO_REPARSE_TAG_DFS           0x8000000A
/* Used by the DFS filter See MS-DFSC */
#define IO_REPARSE_TAG_DFSR          0x80000012
#define IO_REPARSE_TAG_FILTER_MANAGER 0x8000000B
/* See section MS-FSCC 2.1.2.4 */
#define IO_REPARSE_TAG_SYMLINK       0xA000000C
#define IO_REPARSE_TAG_DEDUP         0x80000013
#define IO_REPARSE_APPXSTREAM	     0xC0000014
/* NFS symlinks, Win 8/SMB3 and later */
#define IO_REPARSE_TAG_NFS           0x80000014

/* fsctl flags */
/* If Flags is set to this value, the request is an FSCTL not ioctl request */
#define SMB2_0_IOCTL_IS_FSCTL		0x00000001

