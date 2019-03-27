/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Common definitions and structures for SMB/CIFS protocol
 */
 
#ifndef _NETSMB_SMB_H_
#define _NETSMB_SMB_H_

#define	SMB_TCP_PORT	139
/*
 * SMB dialects that we have to deal with.
 */
enum smb_dialects { 
	SMB_DIALECT_NONE,
	SMB_DIALECT_CORE,		/* PC NETWORK PROGRAM 1.0, PCLAN1.0 */
	SMB_DIALECT_COREPLUS,		/* MICROSOFT NETWORKS 1.03 */
	SMB_DIALECT_LANMAN1_0,		/* MICROSOFT NETWORKS 3.0, LANMAN1.0 */
	SMB_DIALECT_LANMAN2_0,		/* LM1.2X002, DOS LM1.2X002, Samba */
	SMB_DIALECT_LANMAN2_1,		/* DOS LANMAN2.1, LANMAN2.1 */
	SMB_DIALECT_NTLM0_12		/* NT LM 0.12, Windows for Workgroups 3.1a,
					 * NT LANMAN 1.0 */
};

/*
 * Formats of data/string buffers
 */
#define	SMB_DT_DATA		1
#define	SMB_DT_DIALECT		2
#define	SMB_DT_PATHNAME		3
#define	SMB_DT_ASCII		4
#define	SMB_DT_VARIABLE		5

/*
 * SMB header
 */
#define	SMB_SIGNATURE		"\xFFSMB"
#define	SMB_SIGLEN		4
#define	SMB_HDRMID(p)		(le16toh(*(u_short*)((u_char*)(p) + 30)))
#define	SMB_HDRLEN		32
/*
 * bits in the smb_flags field
 */
#define	SMB_FLAGS_CASELESS	0x08
#define SMB_FLAGS_SERVER_RESP	0x80	/* indicates a response */

/*
 * bits in the smb_flags2 field
 */
#define	SMB_FLAGS2_KNOWS_LONG_NAMES	0x0001
#define	SMB_FLAGS2_KNOWS_EAS		0x0002	/* client know about EAs */
#define	SMB_FLAGS2_SECURITY_SIGNATURE	0x0004	/* check SMB integrity */
#define	SMB_FLAGS2_IS_LONG_NAME		0x0040	/* any path name is a long name */
#define	SMB_FLAGS2_EXT_SEC		0x0800	/* client aware of Extended
						 * Security negotiation */
#define	SMB_FLAGS2_DFS			0x1000	/* resolve paths in DFS */
#define	SMB_FLAGS2_PAGING_IO		0x2000	/* for exec */
#define	SMB_FLAGS2_ERR_STATUS		0x4000	/* 1 - status.status */
#define	SMB_FLAGS2_UNICODE		0x8000	/* use Unicode for all strings */

#define	SMB_UID_UNKNOWN		0xffff
#define	SMB_TID_UNKNOWN		0xffff

/*
 * Security mode bits
 */
#define SMB_SM_USER		0x01		/* server in the user security mode */
#define	SMB_SM_ENCRYPT		0x02		/* use challenge/response */
#define	SMB_SM_SIGS		0x04
#define	SMB_SM_SIGS_REQUIRE	0x08

/*
 * NTLM capabilities
 */
#define	SMB_CAP_RAW_MODE		0x0001
#define	SMB_CAP_MPX_MODE		0x0002
#define	SMB_CAP_UNICODE			0x0004
#define	SMB_CAP_LARGE_FILES		0x0008		/* 64 bit offsets supported */
#define	SMB_CAP_NT_SMBS			0x0010
#define	SMB_CAP_RPC_REMOTE_APIS		0x0020
#define	SMB_CAP_STATUS32		0x0040
#define	SMB_CAP_LEVEL_II_OPLOCKS	0x0080
#define	SMB_CAP_LOCK_AND_READ		0x0100
#define	SMB_CAP_NT_FIND			0x0200
#define	SMB_CAP_DFS			0x1000
#define	SMB_CAP_INFOLEVEL_PASSTHRU	0x2000
#define	SMB_CAP_LARGE_READX		0x4000
#define	SMB_CAP_LARGE_WRITEX		0x8000
#define	SMB_CAP_UNIX			0x00800000
#define	SMB_CAP_BULK_TRANSFER		0x20000000
#define	SMB_CAP_COMPRESSED_DATA		0x40000000
#define	SMB_CAP_EXT_SECURITY		0x80000000

/*
 * File attributes
 */
#define	SMB_FA_RDONLY		0x01
#define	SMB_FA_HIDDEN		0x02
#define	SMB_FA_SYSTEM		0x04
#define	SMB_FA_VOLUME		0x08
#define	SMB_FA_DIR		0x10
#define	SMB_FA_ARCHIVE		0x20

/*
 * Extended file attributes
 */
#define	SMB_EFA_RDONLY		0x0001
#define	SMB_EFA_HIDDEN		0x0002
#define	SMB_EFA_SYSTEM		0x0004
#define	SMB_EFA_DIRECTORY	0x0010
#define	SMB_EFA_ARCHIVE		0x0020
#define	SMB_EFA_NORMAL		0x0080
#define	SMB_EFA_TEMPORARY	0x0100
#define	SMB_EFA_COMPRESSED	0x0800
#define	SMB_EFA_POSIX_SEMANTICS	0x01000000
#define	SMB_EFA_BACKUP_SEMANTICS 0x02000000
#define	SMB_EFA_DELETE_ON_CLOSE	0x04000000
#define	SMB_EFA_SEQUENTIAL_SCAN	0x08000000
#define	SMB_EFA_RANDOM_ACCESS	0x10000000
#define	SMB_EFA_NO_BUFFERING	0x20000000
#define	SMB_EFA_WRITE_THROUGH	0x80000000

/*
 * Access Mode Encoding
 */
#define	SMB_AM_OPENREAD		0x0000
#define	SMB_AM_OPENWRITE	0x0001
#define	SMB_AM_OPENRW		0x0002
#define	SMB_AM_OPENEXEC		0x0003
#define	SMB_SM_COMPAT		0x0000
#define	SMB_SM_EXCLUSIVE	0x0010
#define	SMB_SM_DENYWRITE	0x0020
#define	SMB_SM_DENYREADEXEC	0x0030
#define	SMB_SM_DENYNONE		0x0040

/*
 * SMB commands
 */
#define	SMB_COM_CREATE_DIRECTORY        0x00
#define	SMB_COM_DELETE_DIRECTORY        0x01
#define	SMB_COM_OPEN                    0x02
#define	SMB_COM_CREATE                  0x03
#define	SMB_COM_CLOSE                   0x04
#define	SMB_COM_FLUSH                   0x05
#define	SMB_COM_DELETE                  0x06
#define	SMB_COM_RENAME                  0x07
#define	SMB_COM_QUERY_INFORMATION       0x08
#define	SMB_COM_SET_INFORMATION         0x09
#define	SMB_COM_READ                    0x0A
#define	SMB_COM_WRITE                   0x0B
#define	SMB_COM_LOCK_BYTE_RANGE         0x0C
#define	SMB_COM_UNLOCK_BYTE_RANGE       0x0D
#define	SMB_COM_CREATE_TEMPORARY        0x0E
#define	SMB_COM_CREATE_NEW              0x0F
#define	SMB_COM_CHECK_DIRECTORY         0x10
#define	SMB_COM_PROCESS_EXIT            0x11
#define	SMB_COM_SEEK                    0x12
#define	SMB_COM_LOCK_AND_READ           0x13
#define	SMB_COM_WRITE_AND_UNLOCK        0x14
#define	SMB_COM_READ_RAW                0x1A
#define	SMB_COM_READ_MPX                0x1B
#define	SMB_COM_READ_MPX_SECONDARY      0x1C
#define	SMB_COM_WRITE_RAW               0x1D
#define	SMB_COM_WRITE_MPX               0x1E
#define	SMB_COM_WRITE_COMPLETE          0x20
#define	SMB_COM_SET_INFORMATION2        0x22
#define	SMB_COM_QUERY_INFORMATION2      0x23
#define	SMB_COM_LOCKING_ANDX            0x24
#define	SMB_COM_TRANSACTION             0x25
#define	SMB_COM_TRANSACTION_SECONDARY   0x26
#define	SMB_COM_IOCTL                   0x27
#define	SMB_COM_IOCTL_SECONDARY         0x28
#define	SMB_COM_COPY                    0x29
#define	SMB_COM_MOVE                    0x2A
#define	SMB_COM_ECHO                    0x2B
#define	SMB_COM_WRITE_AND_CLOSE         0x2C
#define	SMB_COM_OPEN_ANDX               0x2D
#define	SMB_COM_READ_ANDX               0x2E
#define	SMB_COM_WRITE_ANDX              0x2F
#define	SMB_COM_CLOSE_AND_TREE_DISC     0x31
#define	SMB_COM_TRANSACTION2            0x32
#define	SMB_COM_TRANSACTION2_SECONDARY  0x33
#define	SMB_COM_FIND_CLOSE2             0x34
#define	SMB_COM_FIND_NOTIFY_CLOSE       0x35
#define	SMB_COM_TREE_CONNECT		0x70
#define	SMB_COM_TREE_DISCONNECT         0x71
#define	SMB_COM_NEGOTIATE               0x72
#define	SMB_COM_SESSION_SETUP_ANDX      0x73
#define	SMB_COM_LOGOFF_ANDX             0x74
#define	SMB_COM_TREE_CONNECT_ANDX       0x75
#define	SMB_COM_QUERY_INFORMATION_DISK  0x80
#define	SMB_COM_SEARCH                  0x81
#define	SMB_COM_FIND                    0x82
#define	SMB_COM_FIND_UNIQUE             0x83
#define	SMB_COM_NT_TRANSACT             0xA0
#define	SMB_COM_NT_TRANSACT_SECONDARY   0xA1
#define	SMB_COM_NT_CREATE_ANDX          0xA2
#define	SMB_COM_NT_CANCEL               0xA4
#define	SMB_COM_OPEN_PRINT_FILE         0xC0
#define	SMB_COM_WRITE_PRINT_FILE        0xC1
#define	SMB_COM_CLOSE_PRINT_FILE        0xC2
#define	SMB_COM_GET_PRINT_QUEUE         0xC3
#define	SMB_COM_READ_BULK               0xD8
#define	SMB_COM_WRITE_BULK              0xD9
#define	SMB_COM_WRITE_BULK_DATA         0xDA

/*
 * TRANS2 commands
 */
#define	SMB_TRANS2_OPEN2			0x00
#define	SMB_TRANS2_FIND_FIRST2			0x01
#define	SMB_TRANS2_FIND_NEXT2			0x02
#define	SMB_TRANS2_QUERY_FS_INFORMATION		0x03
#define	SMB_TRANS2_QUERY_PATH_INFORMATION	0x05
#define	SMB_TRANS2_SET_PATH_INFORMATION		0x06
#define	SMB_TRANS2_QUERY_FILE_INFORMATION	0x07
#define	SMB_TRANS2_SET_FILE_INFORMATION		0x08
#define	SMB_TRANS2_FSCTL			0x09
#define	SMB_TRANS2_IOCTL2			0x0A
#define	SMB_TRANS2_FIND_NOTIFY_FIRST		0x0B
#define	SMB_TRANS2_FIND_NOTIFY_NEXT		0x0C
#define	SMB_TRANS2_CREATE_DIRECTORY		0x0D
#define	SMB_TRANS2_SESSION_SETUP		0x0E
#define	SMB_TRANS2_GET_DFS_REFERRAL		0x10
#define	SMB_TRANS2_REPORT_DFS_INCONSISTENCY	0x11

/*
 * SMB_TRANS2_QUERY_FS_INFORMATION levels
 */
#define SMB_INFO_ALLOCATION		1
#define SMB_INFO_VOLUME			2
#define SMB_QUERY_FS_VOLUME_INFO	0x102
#define SMB_QUERY_FS_SIZE_INFO		0x103
#define SMB_QUERY_FS_DEVICE_INFO	0x104
#define SMB_QUERY_FS_ATTRIBUTE_INFO	0x105

/*
 * SMB_TRANS2_QUERY_PATH levels
 */
#define	SMB_QUERY_FILE_STANDARD			1
#define	SMB_QUERY_FILE_EA_SIZE			2
#define	SMB_QUERY_FILE_EAS_FROM_LIST		3
#define	SMB_QUERY_FILE_ALL_EAS			4
#define	SMB_QUERY_FILE_IS_NAME_VALID		6
#define	SMB_QUERY_FILE_BASIC_INFO		0x101
#define	SMB_QUERY_FILE_STANDARD_INFO		0x102
#define	SMB_QUERY_FILE_EA_INFO			0x103
#define	SMB_QUERY_FILE_NAME_INFO		0x104
#define	SMB_QUERY_FILE_ALL_INFO			0x107
#define	SMB_QUERY_FILE_ALT_NAME_INFO		0x108
#define	SMB_QUERY_FILE_STREAM_INFO		0x109
#define	SMB_QUERY_FILE_COMPRESSION_INFO		0x10b
#define	SMB_QUERY_FILE_UNIX_BASIC		0x200
#define	SMB_QUERY_FILE_UNIX_LINK		0x201
#define	SMB_QUERY_FILE_MAC_DT_GET_APPL		0x306
#define	SMB_QUERY_FILE_MAC_DT_GET_ICON		0x307
#define	SMB_QUERY_FILE_MAC_DT_GET_ICON_INFO	0x308

/*
 * SMB_TRANS2_FIND_FIRST2 information levels
 */
#define SMB_INFO_STANDARD		1
#define SMB_INFO_QUERY_EA_SIZE		2
#define SMB_INFO_QUERY_EAS_FROM_LIST	3
#define SMB_FIND_FILE_DIRECTORY_INFO	0x101
#define SMB_FIND_FULL_DIRECTORY_INFO	0x102
#define SMB_FIND_FILE_NAMES_INFO	0x103
#define SMB_FIND_BOTH_DIRECTORY_INFO	0x104

/*
 * Set PATH/FILE information levels
 */
#define	SMB_SET_FILE_BASIC_INFO		0x101
#define	SMB_SET_FILE_END_OF_FILE_INFO	0x104

/*
 * LOCKING_ANDX LockType flags
 */
#define SMB_LOCKING_ANDX_SHARED_LOCK	0x01
#define SMB_LOCKING_ANDX_OPLOCK_RELEASE	0x02
#define SMB_LOCKING_ANDX_CHANGE_LOCKTYPE 0x04
#define SMB_LOCKING_ANDX_CANCEL_LOCK	0x08
#define SMB_LOCKING_ANDX_LARGE_FILES	0x10

/*
 * Some names length limitations. Some of them aren't declared by specs,
 * but we need reasonable limits.
 */
#define SMB_MAXSRVNAMELEN	15	/* NetBIOS limit */
#define SMB_MAXUSERNAMELEN	128
#define SMB_MAXPASSWORDLEN	128
#define	SMB_MAXSHARENAMELEN	128
#define	SMB_MAXPKTLEN		0x1FFFF
#define	SMB_MAXCHALLENGELEN	8
#define	SMB_MAXFNAMELEN		255	/* Keep in sync with MAXNAMLEN */

#define	SMB_MAXRCN		3	/* number of reconnect attempts */

/*
 * Error classes
 */
#define SMBSUCCESS	0x00
#define ERRDOS		0x01
#define ERRSRV		0x02
#define ERRHRD		0x03	/* Error is a hardware error. */
#define ERRCMD		0xFF	/* Command was not in the "SMB" format. */

/*
 * Error codes for the ERRDOS class
 */
#define ERRbadfunc	1	/* Invalid function */
#define ERRbadfile	2	/* File not found (last component) */
#define ERRbadpath	3	/* Directory invalid */
#define ERRnofids	4	/* Too many open files */
#define ERRnoaccess	5	/* Access denied */
#define ERRbadfid	6	/* Invalid file handle */
#define ERRbadmcb	7	/* Memory control blocks destroyed (huh ?) */
#define ERRnomem	8	/* Insufficient memory */
#define ERRbadmem	9	/* Invalid memory block address */
#define ERRbadenv	10	/* Invalid environment */
#define ERRbadformat	11	/* Invalid format */
#define ERRbadaccess	12	/* Invalid open mode */
#define ERRbaddata	13	/* Invalid data */
#define ERRbaddrive	15	/* Invalid drive specified */
#define ERRremcd	16	/* An attempt to delete current directory */
#define ERRdiffdevice	17	/* cross fs rename/move */
#define ERRnofiles	18	/* no more files found in file search */
#define ERRbadshare	32	/* Share mode can't be granted */
#define ERRlock		33	/* A lock request conflicts with existing lock */
#define ERRunsup	50	/* unsupported - Win 95 */
#define ERRnoipc	66	/* ipc unsupported */
#define ERRnosuchshare	67	/* invalid share name */
#define ERRfilexists	80	/* The file named in the request already exists */
#define	ERRquota	112	/* W2K returns this if quota space exceeds */
#define ERRcannotopen	110	/* cannot open the file */
#define ERRinvalidname	123
#define ERRunknownlevel 124
#define ERRnotlocked	158	/* region was not locked by this context */
#define ERRrename	183
#define ERRbadpipe	230	/* named pipe invalid */
#define ERRpipebusy	231	/* all pipe instances are busy */
#define ERRpipeclosing	232	/* close in progress */
#define ERRnotconnected	233	/* nobody on other end of pipe */
#define ERRmoredata	234	/* more data to be returned */
#define ERRbaddirectory	267	/* invalid directory name */
#define ERReasunsupported	282	/* extended attributes not supported */
#define ERRunknownipc	2142
#define ERRbuftoosmall	2123
#define ERRnosuchprintjob	2151

/*
 * Error codes for the ERRSRV class
 */
#define ERRerror	1	/* Non-specific error code */
#define ERRbadpw	2	/* Bad password */
#define ERRbadtype	3	/* reserved */
#define ERRaccess	4	/* The client doesn't have enough access rights */
#define ERRinvnid	5	/* The Tid specified in a command is invalid */
#define ERRinvnetname	6	/* Invalid server name in the tree connect */
#define ERRinvdevice	7	/* Printer and not printer devices are mixed */
#define ERRqfull	49	/* Print queue full */
#define ERRqtoobig	50	/* Print queue full - no space */
#define ERRinvpfid	52	/* Invalid print file FID */
#define ERRsmbcmd	64	/* The server did not recognize the command */
#define ERRsrverror	65	/* The server encountered and internal error */
#define ERRfilespecs	67	/* The Fid and path name contains an invalid combination */
#define ERRbadpermits	69	/* Access mode invalid */
#define ERRsetattrmode	71	/* Attribute mode invalid */
#define ERRpaused	81	/* Server is paused */
#define ERRmsgoff	82	/* Not receiving messages */
#define ERRnoroom	83	/* No room to buffer message */
#define ERRrmuns	87	/* Too many remote user names */
#define ERRtimeout	88	/* Operation timed out */
#define ERRnoresource	89	/* No resources currently available for request */
#define ERRtoomanyuids	90      /* Too many UIDs active on this session */
#define ERRbaduid	91	/* The UID is not known in this session */
#define ERRusempx	250	/* Temporarily unable to support Raw, use MPX mode */
#define ERRusestd	251	/* Temporarily unable to support Raw, use standard r/w */
#define ERRcontmpx	252	/* Continue in MPX mode */
#define ERRbadPassword	254
#define	ERRaccountExpired 2239
#define	ERRbadClient	2240	/* Cannot access the server from this workstation */
#define	ERRbadLogonTime	2241	/* Cannot access the server at this time **/
#define	ERRpasswordExpired 2242
#define ERRnosupport	65535	/* Invalid function */

/*
 * Error codes for the ERRHRD class
 */
#define ERRnowrite	19	/* write protected media */
#define ERRbadunit	20	/* Unknown unit */
#define ERRnotready	21	/* Drive not ready */
#define ERRbadcmd	22	/* Unknown command */
#define ERRdata		23	/* Data error (CRC) */
#define ERRbadreq	24	/* Bad request structure length */
#define ERRseek		25	/* Seek error */
#define ERRbadmedia	26	/* Unknown media type */
#define ERRbadsector	27	/* Sector not found */
#define ERRnopaper	28	/* Printer out of paper */
#define ERRwrite	29	/* Write fault */
#define ERRread		30	/* Read fault */
#define ERRgeneral	31	/* General failure */
#define	ERRbadshare	32	/* An open conflicts with an existing open */
#define	ERRlock		33	/* lock/unlock conflict */
#define ERRwrongdisk	34	/* The wrong disk was found in a drive */
#define ERRFCBunavail	35	/* No FCBs available */
#define ERRsharebufexc	36	/* A sharing buffer has been exceeded */
#define ERRdiskfull	39

/*
 * RAP error codes (it seems that they returned not only by RAP)
 */
#define	SMB_ERROR_ACCESS_DENIED		5
#define	SMB_ERROR_NETWORK_ACCESS_DENIED	65
#define	SMB_ERROR_MORE_DATA		234

typedef u_int16_t	smbfh;

#endif /* _NETSMB_SMB_H_ */
