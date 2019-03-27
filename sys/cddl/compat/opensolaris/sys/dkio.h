/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _OPENSOLARIS_SYS_DKIO_H_
#define	_OPENSOLARIS_SYS_DKIO_H_

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Disk io control commands
 * Warning: some other ioctls with the DIOC prefix exist elsewhere.
 * The Generic DKIOC numbers are from	0   -  50.
 *	The Floppy Driver uses		51  - 100.
 *	The Hard Disk (except SCSI)	101 - 106.	(these are obsolete)
 *	The CDROM Driver		151 - 200.
 *	The USCSI ioctl			201 - 250.
 */
#define	DKIOC		(0x04 << 8)

/*
 * The following ioctls are generic in nature and need to be
 * suported as appropriate by all disk drivers
 */
#define	DKIOCGGEOM	(DKIOC|1)		/* Get geometry */
#define	DKIOCINFO	(DKIOC|3)		/* Get info */
#define	DKIOCEJECT	(DKIOC|6)		/* Generic 'eject' */
#define	DKIOCGVTOC	(DKIOC|11)		/* Get VTOC */
#define	DKIOCSVTOC	(DKIOC|12)		/* Set VTOC & Write to Disk */

/*
 * Disk Cache Controls.  These ioctls should be supported by
 * all disk drivers.
 *
 * DKIOCFLUSHWRITECACHE when used from user-mode ignores the ioctl
 * argument, but it should be passed as NULL to allow for future
 * reinterpretation.  From user-mode, this ioctl request is synchronous.
 *
 * When invoked from within the kernel, the arg can be NULL to indicate
 * a synchronous request or can be the address of a struct dk_callback
 * to request an asynchronous callback when the flush request is complete.
 * In this case, the flag to the ioctl must include FKIOCTL and the
 * dkc_callback field of the pointed to struct must be non-null or the
 * request is made synchronously.
 *
 * In the callback case: if the ioctl returns 0, a callback WILL be performed.
 * If the ioctl returns non-zero, a callback will NOT be performed.
 * NOTE: In some cases, the callback may be done BEFORE the ioctl call
 * returns.  The caller's locking strategy should be prepared for this case.
 */
#define	DKIOCFLUSHWRITECACHE	(DKIOC|34)	/* flush cache to phys medium */

struct dk_callback {
	void (*dkc_callback)(void *dkc_cookie, int error);
	void *dkc_cookie;
};

#ifdef	__cplusplus
}
#endif

#endif /* _OPENSOLARIS_SYS_DKIO_H_ */
