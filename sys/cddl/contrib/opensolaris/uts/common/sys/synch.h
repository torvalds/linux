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
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_SYNCH_H
#define	_SYS_SYNCH_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef _ASM
#include <sys/types.h>
#include <sys/int_types.h>
#endif /* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
/*
 * Thread and LWP mutexes have the same type
 * definitions.
 *
 * NOTE:
 *
 * POSIX requires that <pthread.h> define the structures pthread_mutex_t
 * and pthread_cond_t.  Although these structures are identical to mutex_t
 * (lwp_mutex_t) and cond_t (lwp_cond_t), defined here, a typedef of these
 * types would require including <synch.h> in <pthread.h>, pulling in
 * non-posix symbols/constants, violating POSIX namespace restrictions.  Hence,
 * pthread_mutex_t/pthread_cond_t have been redefined (in <sys/types.h>).
 * Any modifications done to mutex_t/lwp_mutex_t or cond_t/lwp_cond_t must
 * also be done to pthread_mutex_t/pthread_cond_t.
 */
typedef struct _lwp_mutex {
	struct {
		uint16_t	flag1;
		uint8_t		flag2;
		uint8_t		ceiling;
		union {
			uint16_t bcptype;
			struct {
				uint8_t	count_type1;
				uint8_t	count_type2;
			} mtype_rcount;
		} mbcp_type_un;
		uint16_t	magic;
	} flags;
	union {
		struct {
			uint8_t	pad[8];
		} lock64;
		struct {
			uint32_t ownerpid;
			uint32_t lockword;
		} lock32;
		upad64_t owner64;
	} lock;
	upad64_t data;
} lwp_mutex_t;

/*
 * Thread and LWP condition variables have the same
 * type definition.
 * NOTE:
 * The layout of the following structure should be kept in sync with the
 * layout of pthread_cond_t in sys/types.h. See NOTE above for lwp_mutex_t.
 */
typedef struct _lwp_cond {
	struct {
		uint8_t		flag[4];
		uint16_t 	type;
		uint16_t 	magic;
	} flags;
	upad64_t data;
} lwp_cond_t;

/*
 * LWP semaphores
 */
typedef struct _lwp_sema {
	uint32_t	count;		/* semaphore count */
	uint16_t 	type;
	uint16_t 	magic;
	uint8_t		flags[8];	/* last byte reserved for waiters */
	upad64_t	data;		/* optional data */
} lwp_sema_t;

/*
 * Thread and LWP rwlocks have the same type definition.
 * NOTE: The layout of this structure should be kept in sync with the layout
 * of the correponding structure of pthread_rwlock_t in sys/types.h.
 * Also, because we have to deal with C++, there is an identical structure
 * for rwlock_t in head/sync.h that we cannot change.
 */
typedef struct _lwp_rwlock {
	int32_t		readers;	/* rwstate word */
	uint16_t	type;
	uint16_t	magic;
	lwp_mutex_t	mutex;		/* used with process-shared rwlocks */
	lwp_cond_t	readercv;	/* used only to indicate ownership */
	lwp_cond_t	writercv;	/* used only to indicate ownership */
} lwp_rwlock_t;

#endif /* _ASM */
/*
 * Definitions of synchronization types.
 */
#define	USYNC_THREAD	0x00		/* private to a process */
#define	USYNC_PROCESS	0x01		/* shared by processes */

/* Keep the following values in sync with pthread.h */
#define	LOCK_NORMAL		0x00		/* same as USYNC_THREAD */
#define	LOCK_SHARED		0x01		/* same as USYNC_PROCESS */
#define	LOCK_ERRORCHECK		0x02		/* error check lock */
#define	LOCK_RECURSIVE		0x04		/* recursive lock */
#define	LOCK_PRIO_INHERIT	0x10		/* priority inheritance lock */
#define	LOCK_PRIO_PROTECT	0x20		/* priority ceiling lock */
#define	LOCK_ROBUST		0x40		/* robust lock */

/*
 * USYNC_PROCESS_ROBUST is a deprecated historical type.  It is mapped
 * into (USYNC_PROCESS | LOCK_ROBUST) by mutex_init().  Application code
 * should be revised to use (USYNC_PROCESS | LOCK_ROBUST) rather than this.
 */
#define	USYNC_PROCESS_ROBUST	0x08

/*
 * lwp_mutex_t flags
 */
#define	LOCK_OWNERDEAD		0x1
#define	LOCK_NOTRECOVERABLE	0x2
#define	LOCK_INITED		0x4
#define	LOCK_UNMAPPED		0x8

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SYNCH_H */
