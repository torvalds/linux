/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_debug.h#2 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer debugging interface.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_DEBUG_H_
#define	_CTL_DEBUG_H_

/*
 * Debugging flags.
 */
typedef enum {
	CTL_DEBUG_NONE		= 0x00,	/* no debugging */
	CTL_DEBUG_INFO		= 0x01,	/* SCSI errors */
	CTL_DEBUG_CDB		= 0x02,	/* SCSI CDBs and tasks */
	CTL_DEBUG_CDB_DATA	= 0x04	/* SCSI CDB DATA */
} ctl_debug_flags;

#ifdef	CAM_CTL_DEBUG
#define	CTL_DEBUG_PRINT(X)		\
	do {				\
		printf("ctl_debug: ");	\
		printf X;		\
	} while (0)
#else /* CAM_CTL_DEBUG */
#define	CTL_DEBUG_PRINT(X)
#endif /* CAM_CTL_DEBUG */

#endif	/* _CTL_DEBUG_H_ */
