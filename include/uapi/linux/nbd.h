/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/*
 * 1999 Copyright (C) Pavel Machek, pavel@ucw.cz. This code is GPL.
 * 1999/11/04 Copyright (C) 1999 VMware, Inc. (Regis "HPReg" Duchesne)
 *            Made nbd_end_request() use the io_request_lock
 * 2001 Copyright (C) Steven Whitehouse
 *            New nbd_end_request() for compatibility with new linux block
 *            layer code.
 * 2003/06/24 Louis D. Langholtz <ldl@aros.net>
 *            Removed unneeded blksize_bits field from nbd_device struct.
 *            Cleanup PARANOIA usage & code.
 * 2004/02/19 Paul Clements
 *            Removed PARANOIA, plus various cleanup and comments
 * 2023 Copyright Red Hat
 *            Link to userspace extensions, favor cookie over handle.
 */

#ifndef _UAPILINUX_NBD_H
#define _UAPILINUX_NBD_H

#include <linux/types.h>

#define NBD_SET_SOCK	_IO( 0xab, 0 )
#define NBD_SET_BLKSIZE	_IO( 0xab, 1 )
#define NBD_SET_SIZE	_IO( 0xab, 2 )
#define NBD_DO_IT	_IO( 0xab, 3 )
#define NBD_CLEAR_SOCK	_IO( 0xab, 4 )
#define NBD_CLEAR_QUE	_IO( 0xab, 5 )
#define NBD_PRINT_DEBUG	_IO( 0xab, 6 )
#define NBD_SET_SIZE_BLOCKS	_IO( 0xab, 7 )
#define NBD_DISCONNECT  _IO( 0xab, 8 )
#define NBD_SET_TIMEOUT _IO( 0xab, 9 )
#define NBD_SET_FLAGS   _IO( 0xab, 10)

/*
 * See also https://github.com/NetworkBlockDevice/nbd/blob/master/doc/proto.md
 * for additional userspace extensions not yet utilized in the kernel module.
 */

enum {
	NBD_CMD_READ = 0,
	NBD_CMD_WRITE = 1,
	NBD_CMD_DISC = 2,
	NBD_CMD_FLUSH = 3,
	NBD_CMD_TRIM = 4,
	/* userspace defines additional extension commands */
	NBD_CMD_WRITE_ZEROES = 6,
};

/* values for flags field, these are server interaction specific. */
#define NBD_FLAG_HAS_FLAGS	(1 << 0) /* nbd-server supports flags */
#define NBD_FLAG_READ_ONLY	(1 << 1) /* device is read-only */
#define NBD_FLAG_SEND_FLUSH	(1 << 2) /* can flush writeback cache */
#define NBD_FLAG_SEND_FUA	(1 << 3) /* send FUA (forced unit access) */
#define NBD_FLAG_ROTATIONAL	(1 << 4) /* device is rotational */
#define NBD_FLAG_SEND_TRIM	(1 << 5) /* send trim/discard */
#define NBD_FLAG_SEND_WRITE_ZEROES (1 << 6) /* supports WRITE_ZEROES */
/* there is a gap here to match userspace */
#define NBD_FLAG_CAN_MULTI_CONN	(1 << 8)	/* Server supports multiple connections per export. */

/* values for cmd flags in the upper 16 bits of request type */
#define NBD_CMD_FLAG_FUA	(1 << 16) /* FUA (forced unit access) op */
#define NBD_CMD_FLAG_NO_HOLE	(1 << 17) /* Do not punch a hole for WRITE_ZEROES */

/* These are client behavior specific flags. */
#define NBD_CFLAG_DESTROY_ON_DISCONNECT	(1 << 0) /* delete the nbd device on
						    disconnect. */
#define NBD_CFLAG_DISCONNECT_ON_CLOSE (1 << 1) /* disconnect the nbd device on
						*  close by last opener.
						*/

/* userspace doesn't need the nbd_device structure */

/* These are sent over the network in the request/reply magic fields */

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC 0x67446698
/* Do *not* use magics: 0x12560953 0x96744668. */
/* magic 0x668e33ef for structured reply not supported by kernel yet */

/*
 * This is the packet used for communication between client and
 * server. All data are in network byte order.
 */
struct nbd_request {
	__be32 magic;	/* NBD_REQUEST_MAGIC	*/
	__be32 type;	/* See NBD_CMD_*	*/
	union {
		__be64 cookie;	/* Opaque identifier for request	*/
		char handle[8];	/* older spelling of cookie		*/
	};
	__be64 from;
	__be32 len;
} __attribute__((packed));

/*
 * This is the reply packet that nbd-server sends back to the client after
 * it has completed an I/O request (or an error occurs).
 */
struct nbd_reply {
	__be32 magic;		/* NBD_REPLY_MAGIC	*/
	__be32 error;		/* 0 = ok, else error	*/
	union {
		__be64 cookie;	/* Opaque identifier from request	*/
		char handle[8];	/* older spelling of cookie		*/
	};
};
#endif /* _UAPILINUX_NBD_H */
