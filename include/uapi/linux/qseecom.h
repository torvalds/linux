/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2017, 2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QSEECOM_H_
#define _QSEECOM_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define MAX_ION_FD  4
#define MAX_APP_NAME_SIZE  64
#define QSEECOM_HASH_SIZE  32

/* qseecom_ta_heap allocation retry delay (ms) and max attempt count */
#define QSEECOM_TA_ION_ALLOCATE_DELAY           50
#define QSEECOM_TA_ION_ALLOCATE_MAX_ATTEMP      20

#define ICE_KEY_SIZE 32
#define ICE_SALT_SIZE 32

/*
 * struct qseecom_register_listener_req -
 *      for register listener ioctl request
 * @listener_id - service id (shared between userspace and QSE)
 * @ifd_data_fd - ion handle
 * @virt_sb_base - shared buffer base in user space
 * @sb_size - shared buffer size
 */
struct qseecom_register_listener_req {
	__u32 listener_id; /* in */
	__s32 ifd_data_fd; /* in */
	void *virt_sb_base; /* in */
	__u32 sb_size; /* in */
};

/*
 * struct qseecom_send_cmd_req - for send command ioctl request
 * @cmd_req_len - command buffer length
 * @cmd_req_buf - command buffer
 * @resp_len - response buffer length
 * @resp_buf - response buffer
 */
struct qseecom_send_cmd_req {
	void *cmd_req_buf; /* in */
	unsigned int cmd_req_len; /* in */
	void *resp_buf; /* in/out */
	unsigned int resp_len; /* in/out */
};

/*
 * struct qseecom_ion_fd_info - ion fd handle data information
 * @fd - ion handle to some memory allocated in user space
 * @cmd_buf_offset - command buffer offset
 */
struct qseecom_ion_fd_info {
	__s32 fd;
	__u32 cmd_buf_offset;
};
/*
 * struct qseecom_send_modfd_cmd_req - for send command ioctl request
 * @cmd_req_len - command buffer length
 * @cmd_req_buf - command buffer
 * @resp_len - response buffer length
 * @resp_buf - response buffer
 * @ifd_data_fd - ion handle to memory allocated in user space
 * @cmd_buf_offset - command buffer offset
 */
struct qseecom_send_modfd_cmd_req {
	void *cmd_req_buf; /* in */
	unsigned int cmd_req_len; /* in */
	void *resp_buf; /* in/out */
	unsigned int resp_len; /* in/out */
	struct qseecom_ion_fd_info ifd_data[MAX_ION_FD];
};

/*
 * struct qseecom_listener_send_resp_req - signal to continue the send_cmd req.
 * Used as a trigger from HLOS service to notify QSEECOM that it's done with its
 * operation and provide the response for QSEECOM can continue the incomplete
 * command execution
 * @resp_len - Length of the response
 * @resp_buf - Response buffer where the response of the cmd should go.
 */
struct qseecom_send_resp_req {
	void *resp_buf; /* in */
	unsigned int resp_len; /* in */
};

/*
 * struct qseecom_load_img_data - for sending image length information and
 * ion file descriptor to the qseecom driver. ion file descriptor is used
 * for retrieving the ion file handle and in turn the physical address of
 * the image location.
 * @mdt_len - Length of the .mdt file in bytes.
 * @img_len - Length of the .mdt + .b00 +..+.bxx images files in bytes
 * @ion_fd - Ion file descriptor used when allocating memory.
 * @img_name - Name of the image.
 * @app_arch - Architecture of the image, i.e. 32bit or 64bit app
 */
struct qseecom_load_img_req {
	__u32 mdt_len; /* in */
	__u32 img_len; /* in */
	__s32  ifd_data_fd; /* in */
	char	 img_name[MAX_APP_NAME_SIZE]; /* in */
	__u32 app_arch; /* in */
	__u32 app_id; /* out*/
};

struct qseecom_set_sb_mem_param_req {
	__s32 ifd_data_fd; /* in */
	void *virt_sb_base; /* in */
	__u32 sb_len; /* in */
};

/*
 * struct qseecom_qseos_version_req - get qseos version
 * @qseos_version - version number
 */
struct qseecom_qseos_version_req {
	unsigned int qseos_version; /* in */
};

/*
 * struct qseecom_qseos_app_load_query - verify if app is loaded in qsee
 * @app_name[MAX_APP_NAME_SIZE]-  name of the app.
 * @app_id - app id.
 */
struct qseecom_qseos_app_load_query {
	char app_name[MAX_APP_NAME_SIZE]; /* in */
	__u32 app_id; /* out */
	__u32 app_arch;
};

struct qseecom_send_svc_cmd_req {
	__u32 cmd_id;
	void *cmd_req_buf; /* in */
	unsigned int cmd_req_len; /* in */
	void *resp_buf; /* in/out */
	unsigned int resp_len; /* in/out */
};

enum qseecom_key_management_usage_type {
	QSEOS_KM_USAGE_DISK_ENCRYPTION = 0x01,
	QSEOS_KM_USAGE_FILE_ENCRYPTION = 0x02,
	QSEOS_KM_USAGE_UFS_ICE_DISK_ENCRYPTION = 0x03,
	QSEOS_KM_USAGE_SDCC_ICE_DISK_ENCRYPTION = 0x04,
	QSEOS_KM_USAGE_MAX
};

struct qseecom_create_key_req {
	unsigned char hash32[QSEECOM_HASH_SIZE];
	enum qseecom_key_management_usage_type usage;
};

struct qseecom_wipe_key_req {
	enum qseecom_key_management_usage_type usage;
	int wipe_key_flag;/* 1->remove key from storage(alone with clear key) */
			  /* 0->do not remove from storage (clear key) */
};

struct qseecom_update_key_userinfo_req {
	unsigned char current_hash32[QSEECOM_HASH_SIZE];
	unsigned char new_hash32[QSEECOM_HASH_SIZE];
	enum qseecom_key_management_usage_type usage;
};

#define SHA256_DIGEST_LENGTH	(256/8)
/*
 * struct qseecom_save_partition_hash_req
 * @partition_id - partition id.
 * @hash[SHA256_DIGEST_LENGTH] -  sha256 digest.
 */
struct qseecom_save_partition_hash_req {
	int partition_id; /* in */
	char digest[SHA256_DIGEST_LENGTH]; /* in */
};

/*
 * struct qseecom_is_es_activated_req
 * @is_activated - 1=true , 0=false
 */
struct qseecom_is_es_activated_req {
	int is_activated; /* out */
};

/*
 * struct qseecom_mdtp_cipher_dip_req
 * @in_buf - input buffer
 * @in_buf_size - input buffer size
 * @out_buf - output buffer
 * @out_buf_size - output buffer size
 * @direction - 0=encrypt, 1=decrypt
 */
struct qseecom_mdtp_cipher_dip_req {
	__u8 *in_buf;
	__u32 in_buf_size;
	__u8 *out_buf;
	__u32 out_buf_size;
	__u32 direction;
};

enum qseecom_bandwidth_request_mode {
	INACTIVE = 0,
	LOW,
	MEDIUM,
	HIGH,
};

/*
 * struct qseecom_send_modfd_resp - for send command ioctl request
 * @req_len - command buffer length
 * @req_buf - command buffer
 * @ifd_data_fd - ion handle to memory allocated in user space
 * @cmd_buf_offset - command buffer offset
 */
struct qseecom_send_modfd_listener_resp {
	void *resp_buf_ptr; /* in */
	unsigned int resp_len; /* in */
	struct qseecom_ion_fd_info ifd_data[MAX_ION_FD]; /* in */
};

struct qseecom_qteec_req {
	void    *req_ptr;
	__u32    req_len;
	void    *resp_ptr;
	__u32    resp_len;
};

struct qseecom_qteec_modfd_req {
	void    *req_ptr;
	__u32    req_len;
	void    *resp_ptr;
	__u32    resp_len;
	struct qseecom_ion_fd_info ifd_data[MAX_ION_FD];
};

struct qseecom_sg_entry {
	__u32 phys_addr;
	__u32 len;
};

struct qseecom_sg_entry_64bit {
	__u64 phys_addr;
	__u32 len;
} __attribute__ ((packed));

/*
 * sg list buf format version
 * 1: Legacy format to support only 512 SG list entries
 * 2: new format to support > 512 entries
 */
#define QSEECOM_SG_LIST_BUF_FORMAT_VERSION_1	1
#define QSEECOM_SG_LIST_BUF_FORMAT_VERSION_2	2

struct qseecom_sg_list_buf_hdr_64bit {
	struct qseecom_sg_entry_64bit  blank_entry;	/* must be all 0 */
	__u32 version;		/* sg list buf format version */
	__u64 new_buf_phys_addr;	/* PA of new buffer */
	__u32 nents_total;		/* Total number of SG entries */
} __attribute__ ((packed));

#define QSEECOM_SG_LIST_BUF_HDR_SZ_64BIT	\
			sizeof(struct qseecom_sg_list_buf_hdr_64bit)

#define MAX_CE_PIPE_PAIR_PER_UNIT 3
#define INVALID_CE_INFO_UNIT_NUM 0xffffffff

#define CE_PIPE_PAIR_USE_TYPE_FDE 0
#define CE_PIPE_PAIR_USE_TYPE_PFE 1

struct qseecom_ce_pipe_entry {
	int valid;
	unsigned int ce_num;
	unsigned int ce_pipe_pair;
};

struct qseecom_ice_data_t {
	int flag;
};

#define MAX_CE_INFO_HANDLE_SIZE 32
struct qseecom_ce_info_req {
	unsigned char handle[MAX_CE_INFO_HANDLE_SIZE];
	unsigned int usage;
	unsigned int unit_num;
	unsigned int num_ce_pipe_entries;
	struct qseecom_ce_pipe_entry ce_pipe_entry[MAX_CE_PIPE_PAIR_PER_UNIT];
};

struct qseecom_ice_key_data_t {
	__u8 key[ICE_KEY_SIZE];
	__u32 key_len;
	__u8 salt[ICE_SALT_SIZE];
	__u32 salt_len;
};

#define SG_ENTRY_SZ		sizeof(struct qseecom_sg_entry)
#define SG_ENTRY_SZ_64BIT	sizeof(struct qseecom_sg_entry_64bit)

struct file;


#define QSEECOM_IOC_MAGIC    0x97


#define QSEECOM_IOCTL_REGISTER_LISTENER_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 1, struct qseecom_register_listener_req)

#define QSEECOM_IOCTL_UNREGISTER_LISTENER_REQ \
	_IO(QSEECOM_IOC_MAGIC, 2)

#define QSEECOM_IOCTL_SEND_CMD_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 3, struct qseecom_send_cmd_req)

#define QSEECOM_IOCTL_SEND_MODFD_CMD_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 4, struct qseecom_send_modfd_cmd_req)

#define QSEECOM_IOCTL_RECEIVE_REQ \
	_IO(QSEECOM_IOC_MAGIC, 5)

#define QSEECOM_IOCTL_SEND_RESP_REQ \
	_IO(QSEECOM_IOC_MAGIC, 6)

#define QSEECOM_IOCTL_LOAD_APP_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 7, struct qseecom_load_img_req)

#define QSEECOM_IOCTL_SET_MEM_PARAM_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 8, struct qseecom_set_sb_mem_param_req)

#define QSEECOM_IOCTL_UNLOAD_APP_REQ \
	_IO(QSEECOM_IOC_MAGIC, 9)

#define QSEECOM_IOCTL_GET_QSEOS_VERSION_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 10, struct qseecom_qseos_version_req)

#define QSEECOM_IOCTL_PERF_ENABLE_REQ \
	_IO(QSEECOM_IOC_MAGIC, 11)

#define QSEECOM_IOCTL_PERF_DISABLE_REQ \
	_IO(QSEECOM_IOC_MAGIC, 12)

#define QSEECOM_IOCTL_LOAD_EXTERNAL_ELF_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 13, struct qseecom_load_img_req)

#define QSEECOM_IOCTL_UNLOAD_EXTERNAL_ELF_REQ \
	_IO(QSEECOM_IOC_MAGIC, 14)

#define QSEECOM_IOCTL_APP_LOADED_QUERY_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 15, struct qseecom_qseos_app_load_query)

#define QSEECOM_IOCTL_SEND_CMD_SERVICE_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 16, struct qseecom_send_svc_cmd_req)

#define QSEECOM_IOCTL_CREATE_KEY_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 17, struct qseecom_create_key_req)

#define QSEECOM_IOCTL_WIPE_KEY_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 18, struct qseecom_wipe_key_req)

#define QSEECOM_IOCTL_SAVE_PARTITION_HASH_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 19, struct qseecom_save_partition_hash_req)

#define QSEECOM_IOCTL_IS_ES_ACTIVATED_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 20, struct qseecom_is_es_activated_req)

#define QSEECOM_IOCTL_SEND_MODFD_RESP \
	_IOWR(QSEECOM_IOC_MAGIC, 21, struct qseecom_send_modfd_listener_resp)

#define QSEECOM_IOCTL_SET_BUS_SCALING_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 23, int)

#define QSEECOM_IOCTL_UPDATE_KEY_USER_INFO_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 24, struct qseecom_update_key_userinfo_req)

#define QSEECOM_QTEEC_IOCTL_OPEN_SESSION_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 30, struct qseecom_qteec_modfd_req)

#define QSEECOM_QTEEC_IOCTL_CLOSE_SESSION_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 31, struct qseecom_qteec_req)

#define QSEECOM_QTEEC_IOCTL_INVOKE_MODFD_CMD_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 32, struct qseecom_qteec_modfd_req)

#define QSEECOM_QTEEC_IOCTL_REQUEST_CANCELLATION_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 33, struct qseecom_qteec_modfd_req)

#define QSEECOM_IOCTL_MDTP_CIPHER_DIP_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 34, struct qseecom_mdtp_cipher_dip_req)

#define QSEECOM_IOCTL_SEND_MODFD_CMD_64_REQ \
	_IOWR(QSEECOM_IOC_MAGIC, 35, struct qseecom_send_modfd_cmd_req)

#define QSEECOM_IOCTL_SEND_MODFD_RESP_64 \
	_IOWR(QSEECOM_IOC_MAGIC, 36, struct qseecom_send_modfd_listener_resp)

#define QSEECOM_IOCTL_GET_CE_PIPE_INFO \
	_IOWR(QSEECOM_IOC_MAGIC, 40, struct qseecom_ce_info_req)

#define QSEECOM_IOCTL_FREE_CE_PIPE_INFO \
	_IOWR(QSEECOM_IOC_MAGIC, 41, struct qseecom_ce_info_req)

#define QSEECOM_IOCTL_QUERY_CE_PIPE_INFO \
	_IOWR(QSEECOM_IOC_MAGIC, 42, struct qseecom_ce_info_req)

#define QSEECOM_IOCTL_SET_ICE_INFO \
	_IOWR(QSEECOM_IOC_MAGIC, 43, struct qseecom_ice_data_t)

#define QSEECOM_IOCTL_FBE_CLEAR_KEY \
	_IOWR(QSEECOM_IOC_MAGIC, 44, struct qseecom_ice_key_data_t)

#endif /* _QSEECOM_H_ */
