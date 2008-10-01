/*
 *  smb.h
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_H
#define _LINUX_SMB_H

#include <linux/types.h>
#include <linux/magic.h>
#ifdef __KERNEL__
#include <linux/time.h>
#endif

enum smb_protocol { 
	SMB_PROTOCOL_NONE, 
	SMB_PROTOCOL_CORE, 
	SMB_PROTOCOL_COREPLUS, 
	SMB_PROTOCOL_LANMAN1, 
	SMB_PROTOCOL_LANMAN2, 
	SMB_PROTOCOL_NT1 
};

enum smb_case_hndl {
	SMB_CASE_DEFAULT,
	SMB_CASE_LOWER,
	SMB_CASE_UPPER
};

struct smb_dskattr {
        __u16 total;
        __u16 allocblocks;
        __u16 blocksize;
        __u16 free;
};

struct smb_conn_opt {

        /* The socket */
	unsigned int fd;

	enum smb_protocol protocol;
	enum smb_case_hndl case_handling;

	/* Connection-Options */

	__u32              max_xmit;
	__u16              server_uid;
	__u16              tid;

        /* The following are LANMAN 1.0 options */
        __u16              secmode;
        __u16              maxmux;
        __u16              maxvcs;
        __u16              rawmode;
        __u32              sesskey;

	/* The following are NT LM 0.12 options */
	__u32              maxraw;
	__u32              capabilities;
	__s16              serverzone;
};

#ifdef __KERNEL__

#define SMB_NLS_MAXNAMELEN 20
struct smb_nls_codepage {
	char local_name[SMB_NLS_MAXNAMELEN];
	char remote_name[SMB_NLS_MAXNAMELEN];
};


#define SMB_MAXNAMELEN 255
#define SMB_MAXPATHLEN 1024

/*
 * Contains all relevant data on a SMB networked file.
 */
struct smb_fattr {
	__u16 attr;

	unsigned long	f_ino;
	umode_t		f_mode;
	nlink_t		f_nlink;
	uid_t		f_uid;
	gid_t		f_gid;
	dev_t		f_rdev;
	loff_t		f_size;
	struct timespec	f_atime;
	struct timespec f_mtime;
	struct timespec f_ctime;
	unsigned long	f_blocks;
	int		f_unix;
};

enum smb_conn_state {
	CONN_VALID,		/* everything's fine */
	CONN_INVALID,		/* Something went wrong, but did not
				   try to reconnect yet. */
	CONN_RETRIED,		/* Tried a reconnection, but was refused */
	CONN_RETRYING		/* Currently trying to reconnect */
};

#define SMB_HEADER_LEN   37     /* includes everything up to, but not
                                 * including smb_bcc */

#define SMB_INITIAL_PACKET_SIZE		4000
#define SMB_MAX_PACKET_SIZE		32768

/* reserve this much space for trans2 parameters. Shouldn't have to be more
   than 10 or so, but OS/2 seems happier like this. */
#define SMB_TRANS2_MAX_PARAM 64

#endif
#endif
