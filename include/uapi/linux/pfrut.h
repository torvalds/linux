/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Platform Firmware Runtime Update header
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */
#ifndef __PFRUT_H__
#define __PFRUT_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define PFRUT_IOCTL_MAGIC 0xEE

/**
 * PFRU_IOC_SET_REV - _IOW(PFRUT_IOCTL_MAGIC, 0x01, unsigned int)
 *
 * Return:
 * * 0			- success
 * * -EFAULT		- fail to read the revision id
 * * -EINVAL		- user provides an invalid revision id
 *
 * Set the Revision ID for Platform Firmware Runtime Update.
 */
#define PFRU_IOC_SET_REV _IOW(PFRUT_IOCTL_MAGIC, 0x01, unsigned int)

/**
 * PFRU_IOC_STAGE - _IOW(PFRUT_IOCTL_MAGIC, 0x02, unsigned int)
 *
 * Return:
 * * 0			- success
 * * -EINVAL		- stage phase returns invalid result
 *
 * Stage a capsule image from communication buffer and perform authentication.
 */
#define PFRU_IOC_STAGE _IOW(PFRUT_IOCTL_MAGIC, 0x02, unsigned int)

/**
 * PFRU_IOC_ACTIVATE - _IOW(PFRUT_IOCTL_MAGIC, 0x03, unsigned int)
 *
 * Return:
 * * 0			- success
 * * -EINVAL		- activate phase returns invalid result
 *
 * Activate a previously staged capsule image.
 */
#define PFRU_IOC_ACTIVATE _IOW(PFRUT_IOCTL_MAGIC, 0x03, unsigned int)

/**
 * PFRU_IOC_STAGE_ACTIVATE - _IOW(PFRUT_IOCTL_MAGIC, 0x04, unsigned int)
 *
 * Return:
 * * 0			- success
 * * -EINVAL		- stage/activate phase returns invalid result.
 *
 * Perform both stage and activation action.
 */
#define PFRU_IOC_STAGE_ACTIVATE _IOW(PFRUT_IOCTL_MAGIC, 0x04, unsigned int)

/**
 * PFRU_IOC_QUERY_CAP - _IOR(PFRUT_IOCTL_MAGIC, 0x05,
 *			     struct pfru_update_cap_info)
 *
 * Return:
 * * 0			- success
 * * -EINVAL		- query phase returns invalid result
 * * -EFAULT		- the result fails to be copied to userspace
 *
 * Retrieve information on the Platform Firmware Runtime Update capability.
 * The information is a struct pfru_update_cap_info.
 */
#define PFRU_IOC_QUERY_CAP _IOR(PFRUT_IOCTL_MAGIC, 0x05, struct pfru_update_cap_info)

/**
 * struct pfru_payload_hdr - Capsule file payload header.
 *
 * @sig: Signature of this capsule file.
 * @hdr_version: Revision of this header structure.
 * @hdr_size: Size of this header, including the OemHeader bytes.
 * @hw_ver: The supported firmware version.
 * @rt_ver: Version of the code injection image.
 * @platform_id: A platform specific GUID to specify the platform what
 *               this capsule image support.
 */
struct pfru_payload_hdr {
	__u32 sig;
	__u32 hdr_version;
	__u32 hdr_size;
	__u32 hw_ver;
	__u32 rt_ver;
	__u8 platform_id[16];
};

enum pfru_dsm_status {
	DSM_SUCCEED = 0,
	DSM_FUNC_NOT_SUPPORT = 1,
	DSM_INVAL_INPUT = 2,
	DSM_HARDWARE_ERR = 3,
	DSM_RETRY_SUGGESTED = 4,
	DSM_UNKNOWN = 5,
	DSM_FUNC_SPEC_ERR = 6,
};

/**
 * struct pfru_update_cap_info - Runtime update capability information.
 *
 * @status: Indicator of whether this query succeed.
 * @update_cap: Bitmap to indicate whether the feature is supported.
 * @code_type: A buffer containing an image type GUID.
 * @fw_version: Platform firmware version.
 * @code_rt_version: Code injection runtime version for anti-rollback.
 * @drv_type: A buffer containing an image type GUID.
 * @drv_rt_version: The version of the driver update runtime code.
 * @drv_svn: The secure version number(SVN) of the driver update runtime code.
 * @platform_id: A buffer containing a platform ID GUID.
 * @oem_id: A buffer containing an OEM ID GUID.
 * @oem_info_len: Length of the buffer containing the vendor specific information.
 */
struct pfru_update_cap_info {
	__u32 status;
	__u32 update_cap;

	__u8 code_type[16];
	__u32 fw_version;
	__u32 code_rt_version;

	__u8 drv_type[16];
	__u32 drv_rt_version;
	__u32 drv_svn;

	__u8 platform_id[16];
	__u8 oem_id[16];

	__u32 oem_info_len;
};

/**
 * struct pfru_com_buf_info - Communication buffer information.
 *
 * @status: Indicator of whether this query succeed.
 * @ext_status: Implementation specific query result.
 * @addr_lo: Low 32bit physical address of the communication buffer to hold
 *           a runtime update package.
 * @addr_hi: High 32bit physical address of the communication buffer to hold
 *           a runtime update package.
 * @buf_size: Maximum size in bytes of the communication buffer.
 */
struct pfru_com_buf_info {
	__u32 status;
	__u32 ext_status;
	__u64 addr_lo;
	__u64 addr_hi;
	__u32 buf_size;
};

/**
 * struct pfru_updated_result - Platform firmware runtime update result information.
 * @status: Indicator of whether this update succeed.
 * @ext_status: Implementation specific update result.
 * @low_auth_time: Low 32bit value of image authentication time in nanosecond.
 * @high_auth_time: High 32bit value of image authentication time in nanosecond.
 * @low_exec_time: Low 32bit value of image execution time in nanosecond.
 * @high_exec_time: High 32bit value of image execution time in nanosecond.
 */
struct pfru_updated_result {
	__u32 status;
	__u32 ext_status;
	__u64 low_auth_time;
	__u64 high_auth_time;
	__u64 low_exec_time;
	__u64 high_exec_time;
};

#endif /* __PFRUT_H__ */
