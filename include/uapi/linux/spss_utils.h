/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_SPSS_UTILS_H_
#define _UAPI_SPSS_UTILS_H_

#include <linux/types.h>    /* __u32, _Bool */
#include <linux/ioctl.h>    /* ioctl() */

/**
 * @brief - Secure Processor Utilities interface to user space
 *
 * The kernel spss_utils driver interface to user space via IOCTL
 * and SYSFS (device attributes).
 */

#define SPSS_IOC_MAGIC  'S'

/* ---------- set fw cmac --------------------------------- */
#define OLD_NUM_SPU_UEFI_APPS	3 /* Obsolete */

#define CMAC_SIZE_IN_WORDS	4

/* Obsolete struct, keep for backward compatible */
struct spss_ioc_set_fw_cmac {
	__u32 cmac[CMAC_SIZE_IN_WORDS];
	__u32 apps_cmac[OLD_NUM_SPU_UEFI_APPS][CMAC_SIZE_IN_WORDS];
} __packed;

#define SPSS_IOC_SET_FW_CMAC \
	_IOWR(SPSS_IOC_MAGIC, 1, struct spss_ioc_set_fw_cmac)

/* ---------- wait for event ------------------------------ */
enum spss_event_id {
	/* signaled from user */
	SPSS_EVENT_ID_PIL_CALLED	= 0,
	SPSS_EVENT_ID_NVM_READY		= 1,
	SPSS_EVENT_ID_SPU_READY		= 2,
	SPSS_NUM_USER_EVENTS,

	/* signaled from kernel */
	SPSS_EVENT_ID_SPU_POWER_DOWN	= 6,
	SPSS_EVENT_ID_SPU_POWER_UP	= 7,
	SPSS_NUM_EVENTS,
};

enum spss_event_status {
	EVENT_STATUS_SIGNALED		= 0xAAAA,
	EVENT_STATUS_NOT_SIGNALED	= 0xFFFF,
	EVENT_STATUS_TIMEOUT		= 0xEEE1,
	EVENT_STATUS_ABORTED		= 0xEEE2,
};

struct spss_ioc_wait_for_event {
	__u32 event_id;      /* input */
	__u32 timeout_sec;   /* input */
	__u32 status;        /* output */
} __packed;

#define SPSS_IOC_WAIT_FOR_EVENT \
	_IOWR(SPSS_IOC_MAGIC, 2, struct spss_ioc_wait_for_event)

/* ---------- signal event ------------------------------ */
struct spss_ioc_signal_event {
	__u32 event_id;      /* input */
	__u32 status;        /* output */
} __packed;

#define SPSS_IOC_SIGNAL_EVENT \
	_IOWR(SPSS_IOC_MAGIC, 3, struct spss_ioc_signal_event)

/* ---------- is event signaled ------------------------------ */
struct spss_ioc_is_signaled {
	__u32 event_id;      /* input */
	__u32 status;        /* output */
} __attribute__((packed));

#define SPSS_IOC_IS_EVENT_SIGNALED \
	_IOWR(SPSS_IOC_MAGIC, 4, struct spss_ioc_is_signaled)

/* ---------- set ssr state ------------------------------ */

#define SPSS_IOC_SET_SSR_STATE \
	_IOWR(SPSS_IOC_MAGIC, 5, __u32)

/* ---------- set fw and apps cmac --------------------------------- */

/* Asym , Crypt , Keym + 3rd party uefi apps */
#define MAX_SPU_UEFI_APPS	8

/** use variable-size-array for future growth */
struct spss_ioc_set_fw_and_apps_cmac {
	__u64	cmac_buf_ptr;
	/*
	 * expected cmac_buf_size is:
	 * (1+MAX_SPU_UEFI_APPS)*CMAC_SIZE_IN_WORDS*sizeof(uint32_t)
	 */
	__u32	cmac_buf_size;
	__u32	num_of_cmacs;
} __attribute__((packed));

#define SPSS_IOC_SET_FW_AND_APPS_CMAC \
	_IOWR(SPSS_IOC_MAGIC, 6, struct spss_ioc_set_fw_and_apps_cmac)

#endif /* _UAPI_SPSS_UTILS_H_ */
