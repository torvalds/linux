/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Definitions for ADB (Apple Desktop Bus) support.
 */
#ifndef _UAPI__ADB_H
#define _UAPI__ADB_H

/* ADB commands */
#define ADB_BUSRESET		0
#define ADB_FLUSH(id)		(0x01 | ((id) << 4))
#define ADB_WRITEREG(id, reg)	(0x08 | (reg) | ((id) << 4))
#define ADB_READREG(id, reg)	(0x0C | (reg) | ((id) << 4))

/* ADB default device IDs (upper 4 bits of ADB command byte) */
#define ADB_DONGLE	1	/* "software execution control" devices */
#define ADB_KEYBOARD	2
#define ADB_MOUSE	3
#define ADB_TABLET	4
#define ADB_MODEM	5
#define ADB_MISC	7	/* maybe a monitor */

#define ADB_RET_OK	0
#define ADB_RET_TIMEOUT	3

/* The kind of ADB request. The controller may emulate some
   or all of those CUDA/PMU packet kinds */
#define ADB_PACKET	0
#define CUDA_PACKET	1
#define ERROR_PACKET	2
#define TIMER_PACKET	3
#define POWER_PACKET	4
#define MACIIC_PACKET	5
#define PMU_PACKET	6
#define ADB_QUERY	7

/* ADB queries */

/* ADB_QUERY_GETDEVINFO
 * Query ADB slot for device presence
 * data[2] = id, rep[0] = orig addr, rep[1] = handler_id
 */
#define ADB_QUERY_GETDEVINFO	1


#endif /* _UAPI__ADB_H */
