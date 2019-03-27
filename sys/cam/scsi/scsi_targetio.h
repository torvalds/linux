/*-
 * Ioctl definitions for the SCSI Target Driver
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Nate Lawson.
 * Copyright (c) 1998 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CAM_SCSI_SCSI_TARGETIO_H_
#define _CAM_SCSI_SCSI_TARGETIO_H_
#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

/*
 * CCBs (ATIO, CTIO, INOT, REL_SIMQ) are sent to the kernel driver
 * by writing one or more pointers.  The user receives notification
 * of CCB completion through poll/select/kqueue and then calls
 * read(2) which outputs pointers to the completed CCBs.
 */

/*
 * Enable and disable a target mode instance.  For enabling, the path_id,
 * target_id, and lun_id fields must be set.  The grp6/7_len fields
 * specify the length of vendor-specific CDBs the target expects and
 * should normally be set to 0.  On successful completion
 * of enable, the specified target instance will answer selection.
 * Disable causes the target instance to abort any outstanding commands
 * and stop accepting new ones.  The aborted CCBs will be returned to
 * the user via read(2) or discarded if the user closes the device.
 * The user can then re-enable the device for a new path.
 */
struct ioc_enable_lun {
	path_id_t	path_id;
	target_id_t	target_id;
	lun_id_t	lun_id;
	int		grp6_len;
	int		grp7_len;
};
#define TARGIOCENABLE	_IOW('C', 5, struct ioc_enable_lun)
#define TARGIOCDISABLE	 _IO('C', 6)

/*
 * Set/clear debugging for this target mode instance
 */
#define	TARGIOCDEBUG	_IOW('C', 7, int)

TAILQ_HEAD(ccb_queue, ccb_hdr);

#endif /* _CAM_SCSI_SCSI_TARGETIO_H_ */
