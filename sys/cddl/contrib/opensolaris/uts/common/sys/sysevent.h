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
 * Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SYSEVENT_H
#define	_SYS_SYSEVENT_H

#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	NULL
#if defined(_LP64) && !defined(__cplusplus)
#define	NULL    0L
#else
#define	NULL	0
#endif
#endif

/* Internal registration class and subclass */
#define	EC_ALL		"register_all_classes"
#define	EC_SUB_ALL	"register_all_subclasses"

/*
 * Event allocation/enqueuing sleep/nosleep flags
 */
#define	SE_SLEEP		0
#define	SE_NOSLEEP		1

/* Framework error codes */
#define	SE_EINVAL		1	/* Invalid argument */
#define	SE_ENOMEM		2	/* Unable to allocate memory */
#define	SE_EQSIZE		3	/* Maximum event q size exceeded */
#define	SE_EFAULT		4	/* Copy fault */
#define	SE_NOTFOUND		5	/* Attribute not found */
#define	SE_NO_TRANSPORT		6	/* sysevent transport down */

/* Internal data types */

#define	SE_DATA_TYPE_BYTE	DATA_TYPE_BYTE
#define	SE_DATA_TYPE_INT16	DATA_TYPE_INT16
#define	SE_DATA_TYPE_UINT16	DATA_TYPE_UINT16
#define	SE_DATA_TYPE_INT32	DATA_TYPE_INT32
#define	SE_DATA_TYPE_UINT32	DATA_TYPE_UINT32
#define	SE_DATA_TYPE_INT64	DATA_TYPE_INT64
#define	SE_DATA_TYPE_UINT64	DATA_TYPE_UINT64
#define	SE_DATA_TYPE_STRING	DATA_TYPE_STRING
#define	SE_DATA_TYPE_BYTES	DATA_TYPE_BYTE_ARRAY
#define	SE_DATA_TYPE_TIME	DATA_TYPE_HRTIME

#define	SE_KERN_PID	0

#define	SUNW_VENDOR	"SUNW"
#define	SE_USR_PUB	"usr:"
#define	SE_KERN_PUB	"kern:"
#define	SUNW_KERN_PUB	SUNW_VENDOR ":" SE_KERN_PUB
#define	SUNW_USR_PUB	SUNW_VENDOR ":" SE_USR_PUB

/*
 * Event header and attribute value limits
 */
#define	MAX_ATTR_NAME	1024
#define	MAX_STRING_SZ	1024
#define	MAX_BYTE_ARRAY	1024

#define	MAX_CLASS_LEN		64
#define	MAX_SUBCLASS_LEN	64
#define	MAX_PUB_LEN		128
#define	MAX_CHNAME_LEN		128
#define	MAX_SUBID_LEN		16

/*
 * Limit for the event payload size
 */
#define	MAX_EV_SIZE_LEN		(SHRT_MAX/4)

/* Opaque sysevent_t data type */
typedef void *sysevent_t;

/* Opaque channel bind data type */
typedef void evchan_t;

/* sysevent attribute list */
typedef nvlist_t sysevent_attr_list_t;

/* sysevent attribute name-value pair */
typedef nvpair_t sysevent_attr_t;

/* Unique event identifier */
typedef struct sysevent_id {
	uint64_t eid_seq;
	hrtime_t eid_ts;
} sysevent_id_t;

/* Event attribute value structures */
typedef struct sysevent_bytes {
	int32_t	size;
	uchar_t	*data;
} sysevent_bytes_t;

typedef struct sysevent_value {
	int32_t		value_type;		/* data type */
	union {
		uchar_t		sv_byte;
		int16_t		sv_int16;
		uint16_t	sv_uint16;
		int32_t		sv_int32;
		uint32_t	sv_uint32;
		int64_t		sv_int64;
		uint64_t	sv_uint64;
		hrtime_t	sv_time;
		char		*sv_string;
		sysevent_bytes_t	sv_bytes;
	} value;
} sysevent_value_t;

/*
 * The following flags determine the memory allocation semantics to use for
 * kernel event buffer allocation by userland and kernel versions of
 * sysevent_evc_publish().
 *
 * EVCH_SLEEP and EVCH_NOSLEEP respectively map to KM_SLEEP and KM_NOSLEEP.
 * EVCH_TRYHARD is a kernel-only publish flag that allow event allocation
 * routines to use use alternate kmem caches in situations where free memory
 * may be low.  Kernel callers of sysevent_evc_publish() must set flags to
 * one of EVCH_SLEEP, EVCH_NOSLEEP or EVCH_TRYHARD.  Userland callers of
 * sysevent_evc_publish() must set flags to one of EVCH_SLEEP or EVCH_NOSLEEP.
 *
 * EVCH_QWAIT determines whether or not we should wait for slots in the event
 * queue at publication time.  EVCH_QWAIT may be used by kernel and userland
 * publishers and must be used in conjunction with any of one of EVCH_SLEEP,
 * EVCH_NOSLEEP or EVCH_TRYHARD (kernel-only).
 */

#define	EVCH_NOSLEEP	0x0001	/* No sleep on kmem_alloc() */
#define	EVCH_SLEEP	0x0002	/* Sleep on kmem_alloc() */
#define	EVCH_TRYHARD	0x0004	/* May use alternate kmem cache for alloc */
#define	EVCH_QWAIT	0x0008	/* Wait for slot in event queue */

/*
 * Meaning of flags for subscribe. Bits 8 to 15 are dedicated to
 * the consolidation private interface, so flags defined here are restricted
 * to the LSB.
 *
 * EVCH_SUB_KEEP indicates that this subscription should persist even if
 * this subscriber id should die unexpectedly; matching events will be
 * queued (up to a limit) and will be delivered if/when we restart again
 * with the same subscriber id.
 */
#define	EVCH_SUB_KEEP		0x01

/*
 * Subscriptions may be wildcarded, but we limit the number of
 * wildcards permitted.
 */
#define	EVCH_WILDCARD_MAX	10

/*
 * Used in unsubscribe to indicate all subscriber ids for a channel.
 */
#define	EVCH_ALLSUB		"all_subs"

/*
 * Meaning of flags parameter of channel bind function
 *
 * EVCH_CREAT indicates to create a channel if not already present.
 *
 * EVCH_HOLD_PEND indicates that events should be published to this
 * channel even if there are no matching subscribers present; when
 * a subscriber belatedly binds to the channel and registers their
 * subscriptions they will receive events that predate their bind.
 * If the channel is closed, however, with no remaining bindings then
 * the channel is destroyed.
 *
 * EVCH_HOLD_PEND_INDEF is a stronger version of EVCH_HOLD_PEND -
 * even if the channel has no remaining bindings it will not be
 * destroyed so long as events remain unconsumed.  This is suitable for
 * use with short-lived event producers that may bind to (create) the
 * channel and exit before the intended consumer has started.
 */
#define	EVCH_CREAT		0x0001
#define	EVCH_HOLD_PEND		0x0002
#define	EVCH_HOLD_PEND_INDEF	0x0004
#define	EVCH_B_FLAGS		0x0007	/* All valid bits */

/*
 * Meaning of commands of evc_control function
 */
#define	EVCH_GET_CHAN_LEN_MAX	 1	/* Get event queue length limit */
#define	EVCH_GET_CHAN_LEN	 2	/* Get event queue length */
#define	EVCH_SET_CHAN_LEN	 3	/* Set event queue length */
#define	EVCH_CMD_LAST		 EVCH_SET_CHAN_LEN	/* Last command */

#ifdef illumos
/*
 * Shared user/kernel event channel interface definitions
 */
extern int sysevent_evc_bind(const char *, evchan_t **, uint32_t);
extern int sysevent_evc_unbind(evchan_t *);
extern int sysevent_evc_subscribe(evchan_t *, const char *, const char *,
    int (*)(sysevent_t *, void *), void *, uint32_t);
extern int sysevent_evc_unsubscribe(evchan_t *, const char *);
extern int sysevent_evc_publish(evchan_t *, const char *, const char *,
    const char *, const char *, nvlist_t *, uint32_t);
extern int sysevent_evc_control(evchan_t *, int, ...);
extern int sysevent_evc_setpropnvl(evchan_t *, nvlist_t *);
extern int sysevent_evc_getpropnvl(evchan_t *, nvlist_t **);
#endif	/* illumos */

#ifndef	_KERNEL

#ifdef illumos
/*
 * Userland-only event channel interfaces
 */

#include <door.h>

typedef struct sysevent_subattr sysevent_subattr_t;

extern sysevent_subattr_t *sysevent_subattr_alloc(void);
extern void sysevent_subattr_free(sysevent_subattr_t *);

extern void sysevent_subattr_thrattr(sysevent_subattr_t *, pthread_attr_t *);
extern void sysevent_subattr_sigmask(sysevent_subattr_t *, sigset_t *);

extern void sysevent_subattr_thrcreate(sysevent_subattr_t *,
    door_xcreate_server_func_t *, void *);
extern void sysevent_subattr_thrsetup(sysevent_subattr_t *,
    door_xcreate_thrsetup_func_t *, void *);

extern int sysevent_evc_xsubscribe(evchan_t *, const char *, const char *,
    int (*)(sysevent_t *, void *), void *, uint32_t, sysevent_subattr_t *);
#endif	/* illumos */

#else

/*
 * Kernel log_event interfaces.
 */
extern int log_sysevent(sysevent_t *, int, sysevent_id_t *);

extern sysevent_t *sysevent_alloc(char *, char *, char *, int);
extern void sysevent_free(sysevent_t *);
extern int sysevent_add_attr(sysevent_attr_list_t **, char *,
    sysevent_value_t *, int);
extern void sysevent_free_attr(sysevent_attr_list_t *);
extern int sysevent_attach_attributes(sysevent_t *, sysevent_attr_list_t *);
extern void sysevent_detach_attributes(sysevent_t *);
#ifdef illumos
extern char *sysevent_get_class_name(sysevent_t *);
extern char *sysevent_get_subclass_name(sysevent_t *);
extern uint64_t sysevent_get_seq(sysevent_t *);
extern void sysevent_get_time(sysevent_t *, hrtime_t *);
extern size_t sysevent_get_size(sysevent_t *);
extern char *sysevent_get_pub(sysevent_t *);
extern int sysevent_get_attr_list(sysevent_t *, nvlist_t **);
#endif	/* illumos */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSEVENT_H */
