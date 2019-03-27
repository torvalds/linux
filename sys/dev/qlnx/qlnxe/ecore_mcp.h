/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */


#ifndef __ECORE_MCP_H__
#define __ECORE_MCP_H__

#include "bcm_osal.h"
#include "mcp_public.h"
#include "ecore.h"
#include "ecore_mcp_api.h"
#include "ecore_dev_api.h"

/* Using hwfn number (and not pf_num) is required since in CMT mode,
 * same pf_num may be used by two different hwfn
 * TODO - this shouldn't really be in .h file, but until all fields
 * required during hw-init will be placed in their correct place in shmem
 * we need it in ecore_dev.c [for readin the nvram reflection in shmem].
 */
#define MCP_PF_ID_BY_REL(p_hwfn, rel_pfid) (ECORE_IS_BB((p_hwfn)->p_dev) ? \
					    ((rel_pfid) | \
					     ((p_hwfn)->abs_pf_id & 1) << 3) : \
					     rel_pfid)
#define MCP_PF_ID(p_hwfn)	MCP_PF_ID_BY_REL(p_hwfn, (p_hwfn)->rel_pf_id)

struct ecore_mcp_info {
	/* List for mailbox commands which were sent and wait for a response */
	osal_list_t cmd_list;

	/* Spinlock used for protecting the access to the mailbox commands list
	 * and the sending of the commands.
	 */
	osal_spinlock_t cmd_lock;

	/* Flag to indicate whether sending a MFW mailbox command is blocked */
	bool b_block_cmd;

	/* Spinlock used for syncing SW link-changes and link-changes
	 * originating from attention context.
	 */
	osal_spinlock_t link_lock;

	/* Address of the MCP public area */
	u32 public_base;
	/* Address of the driver mailbox */
	u32 drv_mb_addr;
	/* Address of the MFW mailbox */
	u32 mfw_mb_addr;
	/* Address of the port configuration (link) */
	u32 port_addr;

	/* Current driver mailbox sequence */
	u16 drv_mb_seq;
	/* Current driver pulse sequence */
	u16 drv_pulse_seq;

	struct ecore_mcp_link_params       link_input;
	struct ecore_mcp_link_state	   link_output;
	struct ecore_mcp_link_capabilities link_capabilities;

	struct ecore_mcp_function_info	   func_info;

	u8 *mfw_mb_cur;
	u8 *mfw_mb_shadow;
	u16 mfw_mb_length;
	u32 mcp_hist;

	/* Capabilties negotiated with the MFW */
	u32 capabilities;
};

struct ecore_mcp_mb_params {
	u32 cmd;
	u32 param;
	void *p_data_src;
	void *p_data_dst;
	u32 mcp_resp;
	u32 mcp_param;
	u8 data_src_size;
	u8 data_dst_size;
	u32 flags;
#define ECORE_MB_FLAG_CAN_SLEEP		(0x1 << 0)
#define ECORE_MB_FLAG_AVOID_BLOCK	(0x1 << 1)
#define ECORE_MB_FLAGS_IS_SET(params, flag)	\
	((params) != OSAL_NULL && ((params)->flags & ECORE_MB_FLAG_##flag))
};

enum ecore_ov_eswitch {
	ECORE_OV_ESWITCH_NONE,
	ECORE_OV_ESWITCH_VEB,
	ECORE_OV_ESWITCH_VEPA
};

struct ecore_drv_tlv_hdr {
	u8 tlv_type;	/* According to the enum below */
	u8 tlv_length;	/* In dwords - not including this header */
	u8 tlv_reserved;
#define ECORE_DRV_TLV_FLAGS_CHANGED 0x01
	u8 tlv_flags;
};

/**
 * @brief Initialize the interface with the MCP
 *
 * @param p_hwfn - HW func
 * @param p_ptt - PTT required for register access
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_mcp_cmd_init(struct ecore_hwfn *p_hwfn,
					struct ecore_ptt *p_ptt);

/**
 * @brief Initialize the port interface with the MCP
 *
 * @param p_hwfn
 * @param p_ptt
 * Can only be called after `num_ports_in_engine' is set
 */
void ecore_mcp_cmd_port_init(struct ecore_hwfn *p_hwfn,
			     struct ecore_ptt *p_ptt);
/**
 * @brief Releases resources allocated during the init process.
 *
 * @param p_hwfn - HW func
 * @param p_ptt - PTT required for register access
 *
 * @return enum _ecore_status_t
 */

enum _ecore_status_t ecore_mcp_free(struct ecore_hwfn *p_hwfn);

/**
 * @brief This function is called from the DPC context. After
 * pointing PTT to the mfw mb, check for events sent by the MCP
 * to the driver and ack them. In case a critical event
 * detected, it will be handled here, otherwise the work will be
 * queued to a sleepable work-queue.
 *
 * @param p_hwfn - HW function
 * @param p_ptt - PTT required for register access
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation
 * was successul.
 */
enum _ecore_status_t ecore_mcp_handle_events(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt);

/**
 * @brief When MFW doesn't get driver pulse for couple of seconds, at some
 * threshold before timeout expires, it will generate interrupt
 * through a dedicated status block (DPSB - Driver Pulse Status
 * Block), which the driver should respond immediately, by
 * providing keepalive indication after setting the PTT to the
 * driver-MFW mailbox. This function is called directly from the
 * DPC upon receiving the DPSB attention.
 *
 * @param p_hwfn - hw function
 * @param p_ptt - PTT required for register access
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation
 * was successful.
 */
enum _ecore_status_t ecore_issue_pulse(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt);

enum ecore_drv_role {
	ECORE_DRV_ROLE_OS,
	ECORE_DRV_ROLE_KDUMP,
};

struct ecore_load_req_params {
	/* Input params */
	enum ecore_drv_role drv_role;
	u8 timeout_val; /* 1..254, '0' - default value, '255' - no timeout */
	bool avoid_eng_reset;
	enum ecore_override_force_load override_force_load;

	/* Output params */
	u32 load_code;
};

/**
 * @brief Sends a LOAD_REQ to the MFW, and in case the operation succeeds,
 *        returns whether this PF is the first on the engine/port or function.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_params
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - Operation was successful.
 */
enum _ecore_status_t ecore_mcp_load_req(struct ecore_hwfn *p_hwfn,
					struct ecore_ptt *p_ptt,
					struct ecore_load_req_params *p_params);

/**
 * @brief Sends a LOAD_DONE message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - Operation was successful.
 */
enum _ecore_status_t ecore_mcp_load_done(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt);

/**
 * @brief Sends a CANCEL_LOAD_REQ message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - Operation was successful.
 */
enum _ecore_status_t ecore_mcp_cancel_load_req(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt);

/**
 * @brief Sends a UNLOAD_REQ message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - Operation was successful.
 */
enum _ecore_status_t ecore_mcp_unload_req(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt);

/**
 * @brief Sends a UNLOAD_DONE message to the MFW
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - Operation was successful.
 */
enum _ecore_status_t ecore_mcp_unload_done(struct ecore_hwfn *p_hwfn,
					   struct ecore_ptt *p_ptt);

/**
 * @brief Read the MFW mailbox into Current buffer.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void ecore_mcp_read_mb(struct ecore_hwfn *p_hwfn,
		       struct ecore_ptt *p_ptt);

/**
 * @brief Ack to mfw that driver finished FLR process for VFs
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vfs_to_ack - bit mask of all engine VFs for which the PF acks.
 *
 * @param return enum _ecore_status_t - ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_ack_vf_flr(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u32 *vfs_to_ack);

/**
 * @brief - calls during init to read shmem of all function-related info.
 *
 * @param p_hwfn
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_fill_shmem_func_info(struct ecore_hwfn *p_hwfn,
						    struct ecore_ptt *p_ptt);

/**
 * @brief - Reset the MCP using mailbox command.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_reset(struct ecore_hwfn *p_hwfn,
				     struct ecore_ptt *p_ptt);

/**
 * @brief indicates whether the MFW objects [under mcp_info] are accessible
 *
 * @param p_hwfn
 *
 * @return true iff MFW is running and mcp_info is initialized
 */
bool ecore_mcp_is_init(struct ecore_hwfn *p_hwfn);

/**
 * @brief request MFW to configure MSI-X for a VF
 *
 * @param p_hwfn
 * @param p_ptt
 * @param vf_id - absolute inside engine
 * @param num_sbs - number of entries to request
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t ecore_mcp_config_vf_msix(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      u8 vf_id, u8 num);

/**
 * @brief - Halt the MCP.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_halt(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt);

/**
 * @brief - Wake up the MCP.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_resume(struct ecore_hwfn *p_hwfn,
				      struct ecore_ptt *p_ptt);
int __ecore_configure_pf_max_bandwidth(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_mcp_link_state *p_link,
				       u8 max_bw);
int __ecore_configure_pf_min_bandwidth(struct ecore_hwfn *p_hwfn,
				       struct ecore_ptt *p_ptt,
				       struct ecore_mcp_link_state *p_link,
				       u8 min_bw);
enum _ecore_status_t ecore_mcp_mask_parities(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     u32 mask_parities);
#if 0
enum _ecore_status_t ecore_hw_init_first_eth(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt,
					     u8 *p_pf);
#endif

/**
 * @brief - Sends crash mdump related info to the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param epoch
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_mdump_set_values(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt,
						u32 epoch);

/**
 * @brief - Triggers a MFW crash dump procedure.
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t ecore_mcp_mdump_trigger(struct ecore_hwfn *p_hwfn,
					     struct ecore_ptt *p_ptt);

struct ecore_mdump_retain_data {
	u32 valid;
	u32 epoch;
	u32 pf;
	u32 status;
};

/**
 * @brief - Gets the mdump retained data from the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_mdump_retain
 *
 * @param return ECORE_SUCCESS upon success.
 */
enum _ecore_status_t
ecore_mcp_mdump_get_retain(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   struct ecore_mdump_retain_data *p_mdump_retain);

/**
 * @brief - Sets the MFW's max value for the given resource
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param res_id
 *  @param resc_max_val
 *  @param p_mcp_resp
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_set_resc_max_val(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			   enum ecore_resources res_id, u32 resc_max_val,
			   u32 *p_mcp_resp);

/**
 * @brief - Gets the MFW allocation info for the given resource
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param res_id
 *  @param p_mcp_resp
 *  @param p_resc_num
 *  @param p_resc_start
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_get_resc_info(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			enum ecore_resources res_id, u32 *p_mcp_resp,
			u32 *p_resc_num, u32 *p_resc_start);

/**
 * @brief - Initiates PF FLR
 *
 *  @param p_hwfn
 *  @param p_ptt
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t ecore_mcp_initiate_pf_flr(struct ecore_hwfn *p_hwfn,
					       struct ecore_ptt *p_ptt);

/**
 * @brief Send eswitch mode to MFW
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param eswitch - eswitch mode
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_ov_update_eswitch(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			    enum ecore_ov_eswitch eswitch);

#define ECORE_MCP_RESC_LOCK_MIN_VAL	RESOURCE_DUMP /* 0 */
#define ECORE_MCP_RESC_LOCK_MAX_VAL	31

enum ecore_resc_lock {
	ECORE_RESC_LOCK_DBG_DUMP = ECORE_MCP_RESC_LOCK_MIN_VAL,
	/* Locks that the MFW is aware of should be added here downwards */

	/* Ecore only locks should be added here upwards */
	ECORE_RESC_LOCK_IND_TABLE = 26,
	ECORE_RESC_LOCK_PTP_PORT0 = 27,
	ECORE_RESC_LOCK_PTP_PORT1 = 28,
	ECORE_RESC_LOCK_PTP_PORT2 = 29,
	ECORE_RESC_LOCK_PTP_PORT3 = 30,
	ECORE_RESC_LOCK_RESC_ALLOC = ECORE_MCP_RESC_LOCK_MAX_VAL,

	/* A dummy value to be used for auxillary functions in need of
	 * returning an 'error' value.
	 */
	ECORE_RESC_LOCK_RESC_INVALID,
};

struct ecore_resc_lock_params {
	/* Resource number [valid values are 0..31] */
	u8 resource;

	/* Lock timeout value in seconds [default, none or 1..254] */
	u8 timeout;
#define ECORE_MCP_RESC_LOCK_TO_DEFAULT	0
#define ECORE_MCP_RESC_LOCK_TO_NONE	255

	/* Number of times to retry locking */
	u8 retry_num;
#define ECORE_MCP_RESC_LOCK_RETRY_CNT_DFLT	10

	/* The interval in usec between retries */
	u32 retry_interval;
#define ECORE_MCP_RESC_LOCK_RETRY_VAL_DFLT	10000

	/* Use sleep or delay between retries */
	bool sleep_b4_retry;

	/* Will be set as true if the resource is free and granted */
	bool b_granted;

	/* Will be filled with the resource owner.
	 * [0..15 = PF0-15, 16 = MFW, 17 = diag over serial]
	 */
	u8 owner;
};

/**
 * @brief Acquires MFW generic resource lock
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param p_params
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_resc_lock(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		    struct ecore_resc_lock_params *p_params);

struct ecore_resc_unlock_params {
	/* Resource number [valid values are 0..31] */
	u8 resource;

	/* Allow to release a resource even if belongs to another PF */
	bool b_force;

	/* Will be set as true if the resource is released */
	bool b_released;
};

/**
 * @brief Releases MFW generic resource lock
 *
 *  @param p_hwfn
 *  @param p_ptt
 *  @param p_params
 *
 * @return enum _ecore_status_t - ECORE_SUCCESS - operation was successful.
 */
enum _ecore_status_t
ecore_mcp_resc_unlock(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		      struct ecore_resc_unlock_params *p_params);

/**
 * @brief - default initialization for lock/unlock resource structs
 *
 * @param p_lock - lock params struct to be initialized; Can be OSAL_NULL
 * @param p_unlock - unlock params struct to be initialized; Can be OSAL_NULL
 * @param resource - the requested resource
 * @paral b_is_permanent - disable retries & aging when set
 */
void ecore_mcp_resc_lock_default_init(struct ecore_resc_lock_params *p_lock,
				      struct ecore_resc_unlock_params *p_unlock,
				      enum ecore_resc_lock resource,
				      bool b_is_permanent);

void ecore_mcp_wol_wr(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
		      u32 offset, u32 val);

/**
 * @brief Learn of supported MFW features; To be done during early init
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_mcp_get_capabilities(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief Inform MFW of set of features supported by driver. Should be done
 * inside the contet of the LOAD_REQ.
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_mcp_set_capabilities(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief Initialize MFW mailbox and sequence values for driver interaction.
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_load_mcp_offsets(struct ecore_hwfn *p_hwfn,
					    struct ecore_ptt *p_ptt);

enum ecore_mcp_drv_attr_cmd {
	ECORE_MCP_DRV_ATTR_CMD_READ,
	ECORE_MCP_DRV_ATTR_CMD_WRITE,
	ECORE_MCP_DRV_ATTR_CMD_READ_CLEAR,
	ECORE_MCP_DRV_ATTR_CMD_CLEAR,
};

struct ecore_mcp_drv_attr {
	enum ecore_mcp_drv_attr_cmd attr_cmd;
	u32 attr_num;

	/* R/RC - will be set with the read value
	 * W - should hold the required value to be written
	 * C - DC
	 */
	u32 val;

	/* W - mask/offset to be applied on the given value
	 * R/RC/C - DC
	 */
	u32 mask;
	u32 offset;
};

/**
 * @brief Handle the drivers' attributes that are kept by the MFW.
 *
 * @param p_hwfn
 * @param p_ptt
 * @param p_drv_attr
 */
enum _ecore_status_t
ecore_mcp_drv_attribute(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt,
			struct ecore_mcp_drv_attr *p_drv_attr);

/**
 * @brief Read ufp config from the shared memory.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void
ecore_mcp_read_ufp_config(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

/**
 * @brief Get the engine affinity configuration.
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_mcp_get_engine_config(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt);

/**
 * @brief Get the PPFID bitmap.
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t ecore_mcp_get_ppfid_bitmap(struct ecore_hwfn *p_hwfn,
						struct ecore_ptt *p_ptt);

/**
 * @brief Acquire MCP lock to access to HW indirection table entries
 *
 * @param p_hwfn
 * @param p_ptt
 * @param retry_num
 * @param retry_interval
 */
enum _ecore_status_t
ecore_mcp_ind_table_lock(struct ecore_hwfn *p_hwfn,
			 struct ecore_ptt *p_ptt,
			 u8 retry_num,
			 u32 retry_interval);

/**
 * @brief Release MCP lock of access to HW indirection table entries
 *
 * @param p_hwfn
 * @param p_ptt
 */
enum _ecore_status_t
ecore_mcp_ind_table_unlock(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);

#endif /* __ECORE_MCP_H__ */
