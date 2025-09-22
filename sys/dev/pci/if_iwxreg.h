/*	$OpenBSD: if_iwxreg.h,v 1.57 2025/07/15 13:40:02 jsg Exp $	*/

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */


/* maximum number of DRAM map entries supported by FW */
#define IWX_MAX_DRAM_ENTRY	64
#define IWX_CSR_CTXT_INFO_BA	0x40

/**
 * enum iwx_context_info_flags - Context information control flags
 * @IWX_CTXT_INFO_AUTO_FUNC_INIT: If set, FW will not wait before interrupting
 *	the init done for driver command that configures several system modes
 * @IWX_CTXT_INFO_EARLY_DEBUG: enable early debug
 * @IWX_CTXT_INFO_ENABLE_CDMP: enable core dump
 * @IWX_CTXT_INFO_RB_CB_SIZE_POS: position of the RBD Cyclic Buffer Size
 *	exponent, the actual size is 2**value, valid sizes are 8-2048.
 *	The value is four bits long. Maximum valid exponent is 12
 * @IWX_CTXT_INFO_TFD_FORMAT_LONG: use long TFD Format (the
 *	default is short format - not supported by the driver)
 * @IWX_CTXT_INFO_RB_SIZE_POS: RB size position
 *	(values are IWX_CTXT_INFO_RB_SIZE_*K)
 * @IWX_CTXT_INFO_RB_SIZE_1K: Value for 1K RB size
 * @IWX_CTXT_INFO_RB_SIZE_2K: Value for 2K RB size
 * @IWX_CTXT_INFO_RB_SIZE_4K: Value for 4K RB size
 * @IWX_CTXT_INFO_RB_SIZE_8K: Value for 8K RB size
 * @IWX_CTXT_INFO_RB_SIZE_12K: Value for 12K RB size
 * @IWX_CTXT_INFO_RB_SIZE_16K: Value for 16K RB size
 * @IWX_CTXT_INFO_RB_SIZE_20K: Value for 20K RB size
 * @IWX_CTXT_INFO_RB_SIZE_24K: Value for 24K RB size
 * @IWX_CTXT_INFO_RB_SIZE_28K: Value for 28K RB size
 * @IWX_CTXT_INFO_RB_SIZE_32K: Value for 32K RB size
 */
enum iwx_context_info_flags {
	IWX_CTXT_INFO_AUTO_FUNC_INIT	= (1 << 0),
	IWX_CTXT_INFO_EARLY_DEBUG	= (1 << 1),
	IWX_CTXT_INFO_ENABLE_CDMP	= (1 << 2),
	IWX_CTXT_INFO_RB_CB_SIZE_POS	= 4,
	IWX_CTXT_INFO_TFD_FORMAT_LONG	= (1 << 8),
	IWX_CTXT_INFO_RB_SIZE_POS	= 9,
	IWX_CTXT_INFO_RB_SIZE_1K	= 0x1,
	IWX_CTXT_INFO_RB_SIZE_2K	= 0x2,
	IWX_CTXT_INFO_RB_SIZE_4K	= 0x4,
	IWX_CTXT_INFO_RB_SIZE_8K	= 0x8,
	IWX_CTXT_INFO_RB_SIZE_12K	= 0x9,
	IWX_CTXT_INFO_RB_SIZE_16K	= 0xa,
	IWX_CTXT_INFO_RB_SIZE_20K	= 0xb,
	IWX_CTXT_INFO_RB_SIZE_24K	= 0xc,
	IWX_CTXT_INFO_RB_SIZE_28K	= 0xd,
	IWX_CTXT_INFO_RB_SIZE_32K	= 0xe,
};

/*
 * struct iwx_context_info_version - version structure
 * @mac_id: SKU and revision id
 * @version: context information version id
 * @size: the size of the context information in DWs
 */
struct iwx_context_info_version {
	uint16_t mac_id;
	uint16_t version;
	uint16_t size;
	uint16_t reserved;
} __packed;

/*
 * struct iwx_context_info_control - version structure
 * @control_flags: context information flags see &enum iwx_context_info_flags
 */
struct iwx_context_info_control {
	uint32_t control_flags;
	uint32_t reserved;
} __packed;

/*
 * struct iwx_context_info_dram - images DRAM map
 * each entry in the map represents a DRAM chunk of up to 32 KB
 * @umac_img: UMAC image DRAM map
 * @lmac_img: LMAC image DRAM map
 * @virtual_img: paged image DRAM map
 */
struct iwx_context_info_dram {
	uint64_t umac_img[IWX_MAX_DRAM_ENTRY];
	uint64_t lmac_img[IWX_MAX_DRAM_ENTRY];
	uint64_t virtual_img[IWX_MAX_DRAM_ENTRY];
} __packed;

struct iwx_pnvm_info_dram {
	uint64_t pnvm_img[IWX_MAX_DRAM_ENTRY];
} __packed;

/*
 * struct iwx_context_info_rbd_cfg - RBDs configuration
 * @free_rbd_addr: default queue free RB CB base address
 * @used_rbd_addr: default queue used RB CB base address
 * @status_wr_ptr: default queue used RB status write pointer
 */
struct iwx_context_info_rbd_cfg {
	uint64_t free_rbd_addr;
	uint64_t used_rbd_addr;
	uint64_t status_wr_ptr;
} __packed;

/*
 * struct iwx_context_info_hcmd_cfg  - command queue configuration
 * @cmd_queue_addr: address of command queue
 * @cmd_queue_size: number of entries
 */
struct iwx_context_info_hcmd_cfg {
	uint64_t cmd_queue_addr;
	uint8_t cmd_queue_size;
	uint8_t reserved[7];
} __packed;

/*
 * struct iwx_context_info_dump_cfg - Core Dump configuration
 * @core_dump_addr: core dump (debug DRAM address) start address
 * @core_dump_size: size, in DWs
 */
struct iwx_context_info_dump_cfg {
	uint64_t core_dump_addr;
	uint32_t core_dump_size;
	uint32_t reserved;
} __packed;

/*
 * struct iwx_context_info_pnvm_cfg - platform NVM data configuration
 * @platform_nvm_addr: Platform NVM data start address
 * @platform_nvm_size: size in DWs
 */
struct iwx_context_info_pnvm_cfg {
	uint64_t platform_nvm_addr;
	uint32_t platform_nvm_size;
	uint32_t reserved;
} __packed;

/*
 * struct iwx_context_info_early_dbg_cfg - early debug configuration for
 *	dumping DRAM addresses
 * @early_debug_addr: early debug start address
 * @early_debug_size: size in DWs
 */
struct iwx_context_info_early_dbg_cfg {
	uint64_t early_debug_addr;
	uint32_t early_debug_size;
	uint32_t reserved;
} __packed;

/*
 * struct iwx_context_info - device INIT configuration
 * @version: version information of context info and HW
 * @control: control flags of FH configurations
 * @rbd_cfg: default RX queue configuration
 * @hcmd_cfg: command queue configuration
 * @dump_cfg: core dump data
 * @edbg_cfg: early debug configuration
 * @pnvm_cfg: platform nvm configuration
 * @dram: firmware image addresses in DRAM
 */
struct iwx_context_info {
	struct iwx_context_info_version version;
	struct iwx_context_info_control control;
	uint64_t reserved0;
	struct iwx_context_info_rbd_cfg rbd_cfg;
	struct iwx_context_info_hcmd_cfg hcmd_cfg;
	uint32_t reserved1[4];
	struct iwx_context_info_dump_cfg dump_cfg;
	struct iwx_context_info_early_dbg_cfg edbg_cfg;
	struct iwx_context_info_pnvm_cfg pnvm_cfg;
	uint32_t reserved2[16];
	struct iwx_context_info_dram dram;
	uint32_t reserved3[16];
} __packed;


/*
 * Context info definitions for AX210 devices.
 */

#define IWX_CSR_CTXT_INFO_BOOT_CTRL         0x0
#define IWX_CSR_CTXT_INFO_ADDR              0x118
#define IWX_CSR_IML_DATA_ADDR               0x120
#define IWX_CSR_IML_SIZE_ADDR               0x128
#define IWX_CSR_IML_RESP_ADDR               0x12c

/* Set bit for enabling automatic function boot */
#define IWX_CSR_AUTO_FUNC_BOOT_ENA          (1 << 1)
/* Set bit for initiating function boot */
#define IWX_CSR_AUTO_FUNC_INIT              (1 << 7)

/**
 * iwx_prph_scratch_mtr_format - tfd size configuration
 * @IWX_PRPH_MTR_FORMAT_16B: 16 bit tfd
 * @IWX_PRPH_MTR_FORMAT_32B: 32 bit tfd
 * @IWX_PRPH_MTR_FORMAT_64B: 64 bit tfd
 * @IWX_PRPH_MTR_FORMAT_256B: 256 bit tfd
 */
#define IWX_PRPH_MTR_FORMAT_16B		0x0
#define IWX_PRPH_MTR_FORMAT_32B		0x40000
#define IWX_PRPH_MTR_FORMAT_64B		0x80000
#define IWX_PRPH_MTR_FORMAT_256B	0xC0000

/**
 * iwx_prph_scratch_flags - PRPH scratch control flags
 * @IWX_PRPH_SCRATCH_IMR_DEBUG_EN: IMR support for debug
 * @IWX_PRPH_SCRATCH_EARLY_DEBUG_EN: enable early debug conf
 * @IWX_PRPH_SCRATCH_EDBG_DEST_DRAM: use DRAM, with size allocated
 *	in hwm config.
 * @IWX_PRPH_SCRATCH_EDBG_DEST_INTERNAL: use buffer on SRAM
 * @IWX_PRPH_SCRATCH_EDBG_DEST_ST_ARBITER: use st arbiter, mainly for
 *	multicomm.
 * @IWX_PRPH_SCRATCH_EDBG_DEST_TB22DTF: route debug data to SoC HW
 * @IWX_PRPH_SCTATCH_RB_SIZE_4K: Use 4K RB size (the default is 2K)
 * @IWX_PRPH_SCRATCH_MTR_MODE: format used for completion - 0: for
 *	completion descriptor, 1 for responses (legacy)
 * @IWX_PRPH_SCRATCH_MTR_FORMAT: a mask for the size of the tfd.
 *	There are 4 optional values: 0: 16 bit, 1: 32 bit, 2: 64 bit,
 *	3: 256 bit.
 * @IWX_PRPH_SCRATCH_RB_SIZE_EXT_MASK: RB size full information, ignored
 *	by older firmware versions, so set IWX_PRPH_SCRATCH_RB_SIZE_4K
 *	appropriately; use the below values for this.
 * @IWX_PRPH_SCRATCH_RB_SIZE_EXT_8K: 8kB RB size
 * @IWX_PRPH_SCRATCH_RB_SIZE_EXT_12K: 12kB RB size
 * @IWX_PRPH_SCRATCH_RB_SIZE_EXT_16K: 16kB RB size
 */
#define IWX_PRPH_SCRATCH_IMR_DEBUG_EN		(1 << 1)
#define IWX_PRPH_SCRATCH_EARLY_DEBUG_EN		(1 << 4)
#define IWX_PRPH_SCRATCH_EDBG_DEST_DRAM		(1 << 8)
#define IWX_PRPH_SCRATCH_EDBG_DEST_INTERNAL	(1 << 9)
#define IWX_PRPH_SCRATCH_EDBG_DEST_ST_ARBITER	(1 << 10)
#define IWX_PRPH_SCRATCH_EDBG_DEST_TB22DTF	(1 << 11)
#define IWX_PRPH_SCRATCH_RB_SIZE_4K		(1 << 16)
#define IWX_PRPH_SCRATCH_MTR_MODE		(1 << 17)
#define IWX_PRPH_SCRATCH_MTR_FORMAT		((1 << 18) | (1 << 19))
#define IWX_PRPH_SCRATCH_RB_SIZE_EXT_MASK	(0xf << 20)
#define IWX_PRPH_SCRATCH_RB_SIZE_EXT_8K		(8 << 20)
#define IWX_PRPH_SCRATCH_RB_SIZE_EXT_12K	(9 << 20)
#define IWX_PRPH_SCRATCH_RB_SIZE_EXT_16K	(10 << 20)

/*
 * struct iwx_prph_scratch_version - version structure
 * @mac_id: SKU and revision id
 * @version: prph scratch information version id
 * @size: the size of the context information in DWs
 * @reserved: reserved
 */
struct iwx_prph_scratch_version {
	uint16_t mac_id;
	uint16_t version;
	uint16_t size;
	uint16_t reserved;
} __packed; /* PERIPH_SCRATCH_VERSION_S */

/*
 * struct iwx_prph_scratch_control - control structure
 * @control_flags: context information flags see &iwx_prph_scratch_flags
 * @reserved: reserved
 */
struct iwx_prph_scratch_control {
	uint32_t control_flags;
	uint32_t reserved;
} __packed; /* PERIPH_SCRATCH_CONTROL_S */

/*
 * struct iwx_prph_scratch_pnvm_cfg - ror config
 * @pnvm_base_addr: PNVM start address
 * @pnvm_size: PNVM size in DWs
 * @reserved: reserved
 */
struct iwx_prph_scratch_pnvm_cfg {
	uint64_t pnvm_base_addr;
	uint32_t pnvm_size;
	uint32_t reserved;
} __packed; /* PERIPH_SCRATCH_PNVM_CFG_S */

struct iwx_pnvm_section {
	uint32_t offset;
	const uint8_t data[];
} __packed;

/*
 * struct iwx_prph_scratch_hwm_cfg - hwm config
 * @hwm_base_addr: hwm start address
 * @hwm_size: hwm size in DWs
 * @debug_token_config: debug preset
 */
struct iwx_prph_scratch_hwm_cfg {
	uint64_t hwm_base_addr;
	uint32_t hwm_size;
	uint32_t debug_token_config;
} __packed; /* PERIPH_SCRATCH_HWM_CFG_S */

/*
 * struct iwx_prph_scratch_rbd_cfg - RBDs configuration
 * @free_rbd_addr: default queue free RB CB base address
 * @reserved: reserved
 */
struct iwx_prph_scratch_rbd_cfg {
	uint64_t free_rbd_addr;
	uint32_t reserved;
} __packed; /* PERIPH_SCRATCH_RBD_CFG_S */

/*
 * struct iwx_prph_scratch_uefi_cfg - prph scratch reduce power table
 * @base_addr: reduce power table address
 * @size: table size in dwords
 */
struct iwx_prph_scratch_uefi_cfg {
	uint64_t base_addr;
	uint32_t size;
	uint32_t reserved;
} __packed; /* PERIPH_SCRATCH_UEFI_CFG_S */

/*
 * struct iwx_prph_scratch_ctrl_cfg - prph scratch ctrl and config
 * @version: version information of context info and HW
 * @control: control flags of FH configurations
 * @pnvm_cfg: ror configuration
 * @hwm_cfg: hwm configuration
 * @rbd_cfg: default RX queue configuration
 */
struct iwx_prph_scratch_ctrl_cfg {
	struct iwx_prph_scratch_version version;
	struct iwx_prph_scratch_control control;
	struct iwx_prph_scratch_pnvm_cfg pnvm_cfg;
	struct iwx_prph_scratch_hwm_cfg hwm_cfg;
	struct iwx_prph_scratch_rbd_cfg rbd_cfg;
	struct iwx_prph_scratch_uefi_cfg reduce_power_cfg;
} __packed; /* PERIPH_SCRATCH_CTRL_CFG_S */

/*
 * struct iwx_prph_scratch - peripheral scratch mapping
 * @ctrl_cfg: control and configuration of prph scratch
 * @dram: firmware images addresses in DRAM
 * @reserved: reserved
 */
struct iwx_prph_scratch {
	struct iwx_prph_scratch_ctrl_cfg ctrl_cfg;
	uint32_t reserved[12];
	struct iwx_context_info_dram dram;
} __packed; /* PERIPH_SCRATCH_S */

/*
 * struct iwx_prph_info - peripheral information
 * @boot_stage_mirror: reflects the value in the Boot Stage CSR register
 * @ipc_status_mirror: reflects the value in the IPC Status CSR register
 * @sleep_notif: indicates the peripheral sleep status
 * @reserved: reserved
 */
struct iwx_prph_info {
	uint32_t boot_stage_mirror;
	uint32_t ipc_status_mirror;
	uint32_t sleep_notif;
	uint32_t reserved;
} __packed; /* PERIPH_INFO_S */

/*
 * struct iwx_context_info_gen3 - device INIT configuration
 * @version: version of the context information
 * @size: size of context information in DWs
 * @config: context in which the peripheral would execute - a subset of
 *	capability csr register published by the peripheral
 * @prph_info_base_addr: the peripheral information structure start address
 * @cr_head_idx_arr_base_addr: the completion ring head index array
 *	start address
 * @tr_tail_idx_arr_base_addr: the transfer ring tail index array
 *	start address
 * @cr_tail_idx_arr_base_addr: the completion ring tail index array
 *	start address
 * @tr_head_idx_arr_base_addr: the transfer ring head index array
 *	start address
 * @cr_idx_arr_size: number of entries in the completion ring index array
 * @tr_idx_arr_size: number of entries in the transfer ring index array
 * @mtr_base_addr: the message transfer ring start address
 * @mcr_base_addr: the message completion ring start address
 * @mtr_size: number of entries which the message transfer ring can hold
 * @mcr_size: number of entries which the message completion ring can hold
 * @mtr_doorbell_vec: the doorbell vector associated with the message
 *	transfer ring
 * @mcr_doorbell_vec: the doorbell vector associated with the message
 *	completion ring
 * @mtr_msi_vec: the MSI which shall be generated by the peripheral after
 *	completing a transfer descriptor in the message transfer ring
 * @mcr_msi_vec: the MSI which shall be generated by the peripheral after
 *	completing a completion descriptor in the message completion ring
 * @mtr_opt_header_size: the size of the optional header in the transfer
 *	descriptor associated with the message transfer ring in DWs
 * @mtr_opt_footer_size: the size of the optional footer in the transfer
 *	descriptor associated with the message transfer ring in DWs
 * @mcr_opt_header_size: the size of the optional header in the completion
 *	descriptor associated with the message completion ring in DWs
 * @mcr_opt_footer_size: the size of the optional footer in the completion
 *	descriptor associated with the message completion ring in DWs
 * @msg_rings_ctrl_flags: message rings control flags
 * @prph_info_msi_vec: the MSI which shall be generated by the peripheral
 *	after updating the Peripheral Information structure
 * @prph_scratch_base_addr: the peripheral scratch structure start address
 * @prph_scratch_size: the size of the peripheral scratch structure in DWs
 * @reserved: reserved
 */
struct iwx_context_info_gen3 {
	uint16_t version;
	uint16_t size;
	uint32_t config;
	uint64_t prph_info_base_addr;
	uint64_t cr_head_idx_arr_base_addr;
	uint64_t tr_tail_idx_arr_base_addr;
	uint64_t cr_tail_idx_arr_base_addr;
	uint64_t tr_head_idx_arr_base_addr;
	uint16_t cr_idx_arr_size;
	uint16_t tr_idx_arr_size;
	uint64_t mtr_base_addr;
	uint64_t mcr_base_addr;
	uint16_t mtr_size;
	uint16_t mcr_size;
	uint16_t mtr_doorbell_vec;
	uint16_t mcr_doorbell_vec;
	uint16_t mtr_msi_vec;
	uint16_t mcr_msi_vec;
	uint8_t mtr_opt_header_size;
	uint8_t mtr_opt_footer_size;
	uint8_t mcr_opt_header_size;
	uint8_t mcr_opt_footer_size;
	uint16_t msg_rings_ctrl_flags;
	uint16_t prph_info_msi_vec;
	uint64_t prph_scratch_base_addr;
	uint32_t prph_scratch_size;
	uint32_t reserved;
} __packed; /* IPC_CONTEXT_INFO_S */

#define IWX_MGMT_TID		15

#define IWX_MQ_RX_TABLE_SIZE	512

/* cb size is the exponent */
#define IWX_RX_QUEUE_CB_SIZE(x)	((sizeof(x) <= 4) ? (fls(x) - 1) : (flsl(x) - 1))

/*
 * CSR (control and status registers)
 *
 * CSR registers are mapped directly into PCI bus space, and are accessible
 * whenever platform supplies power to device, even when device is in
 * low power states due to driver-invoked device resets
 * (e.g. IWX_CSR_RESET_REG_FLAG_SW_RESET) or uCode-driven power-saving modes.
 *
 * Use iwl_write32() and iwl_read32() family to access these registers;
 * these provide simple PCI bus access, without waking up the MAC.
 * Do not use iwl_write_direct32() family for these registers;
 * no need to "grab nic access" via IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ.
 * The MAC (uCode processor, etc.) does not need to be powered up for accessing
 * the CSR registers.
 *
 * NOTE:  Device does need to be awake in order to read this memory
 *        via IWX_CSR_EEPROM and IWX_CSR_OTP registers
 */
#define IWX_CSR_HW_IF_CONFIG_REG    (0x000) /* hardware interface config */
#define IWX_CSR_INT_COALESCING      (0x004) /* accum ints, 32-usec units */
#define IWX_CSR_INT                 (0x008) /* host interrupt status/ack */
#define IWX_CSR_INT_MASK            (0x00c) /* host interrupt enable */
#define IWX_CSR_FH_INT_STATUS       (0x010) /* busmaster int status/ack*/
#define IWX_CSR_GPIO_IN             (0x018) /* read external chip pins */
#define IWX_CSR_RESET               (0x020) /* busmaster enable, NMI, etc*/
#define IWX_CSR_GP_CNTRL            (0x024)

/* 2nd byte of IWX_CSR_INT_COALESCING, not accessible via iwl_write32()! */
#define IWX_CSR_INT_PERIODIC_REG	(0x005)

/*
 * Hardware revision info
 * Bit fields:
 * 31-16:  Reserved
 *  15-4:  Type of device:  see IWX_CSR_HW_REV_TYPE_xxx definitions
 *  3-2:  Revision step:  0 = A, 1 = B, 2 = C, 3 = D
 *  1-0:  "Dash" (-) value, as in A-1, etc.
 */
#define IWX_CSR_HW_REV              (0x028)

/*
 * RF ID revision info
 * Bit fields:
 * 31:24: Reserved (set to 0x0)
 * 23:12: Type
 * 11:8:  Step (A - 0x0, B - 0x1, etc)
 * 7:4:   Dash
 * 3:0:   Flavor
 */
#define IWX_CSR_HW_RF_ID		(0x09c)


#define IWX_CSR_GIO_REG		(0x03C)

/*
 * UCODE-DRIVER GP (general purpose) mailbox registers.
 * SET/CLR registers set/clear bit(s) if "1" is written.
 */
#define IWX_CSR_UCODE_DRV_GP1       (0x054)
#define IWX_CSR_UCODE_DRV_GP1_SET   (0x058)
#define IWX_CSR_UCODE_DRV_GP1_CLR   (0x05c)
#define IWX_CSR_UCODE_DRV_GP2       (0x060)

#define IWX_CSR_MBOX_SET_REG		(0x088)
#define IWX_CSR_MBOX_SET_REG_OS_ALIVE	0x20

#define IWX_CSR_DRAM_INT_TBL_REG	(0x0A0)
#define IWX_CSR_MAC_SHADOW_REG_CTRL	(0x0A8) /* 6000 and up */

/* LTR control */
#define IWX_CSR_LTR_LONG_VAL_AD				(0x0d4)
#define IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_REQ		0x80000000
#define IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE_MASK	0x1c000000
#define IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE_SHIFT	24
#define IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL_MASK	0x03ff0000
#define IWX_CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL_SHIFT	16
#define IWX_CSR_LTR_LONG_VAL_AD_SNOOP_REQ		0x00008000
#define IWX_CSR_LTR_LONG_VAL_AD_SNOOP_SCALE_MASK	0x00001c00
#define IWX_CSR_LTR_LONG_VAL_AD_SNOOP_SCALE_SHIFT	8
#define IWX_CSR_LTR_LONG_VAL_AD_SNOOP_VAL		0x000003ff
#define IWX_CSR_LTR_LONG_VAL_AD_SCALE_USEC		2

/* GIO Chicken Bits (PCI Express bus link power management) */
#define IWX_CSR_GIO_CHICKEN_BITS    (0x100)

#define IWX_CSR_DBG_HPET_MEM_REG	(0x240)
#define IWX_CSR_DBG_LINK_PWR_MGMT_REG	(0x250)

/* Bits for IWX_CSR_HW_IF_CONFIG_REG */
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH	(0x00000003)
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP	(0x0000000C)
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_BOARD_VER	(0x000000C0)
#define IWX_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI	(0x00000100)
#define IWX_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI	(0x00000200)
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE	(0x00000C00)
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH	(0x00003000)
#define IWX_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP	(0x0000C000)

#define IWX_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH	(0)
#define IWX_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP	(2)
#define IWX_CSR_HW_IF_CONFIG_REG_POS_BOARD_VER	(6)
#define IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE	(10)
#define IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH	(12)
#define IWX_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP	(14)

#define IWX_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A	(0x00080000)
#define IWX_CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM	(0x00200000)
#define IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY	(0x00400000) /* PCI_OWN_SEM */
#define IWX_CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE (0x02000000) /* ME_OWN */
#define IWX_CSR_HW_IF_CONFIG_REG_PREPARE	(0x08000000) /* WAKE_ME */
#define IWX_CSR_HW_IF_CONFIG_REG_ENABLE_PME	(0x10000000)
#define IWX_CSR_HW_IF_CONFIG_REG_PERSIST_MODE	(0x40000000) /* PERSISTENCE */

#define IWX_CSR_INT_PERIODIC_DIS		(0x00) /* disable periodic int*/
#define IWX_CSR_INT_PERIODIC_ENA		(0xFF) /* 255*32 usec ~ 8 msec*/

/* interrupt flags in INTA, set by uCode or hardware (e.g. dma),
 * acknowledged (reset) by host writing "1" to flagged bits. */
#define IWX_CSR_INT_BIT_FH_RX	(1U << 31) /* Rx DMA, cmd responses, FH_INT[17:16] */
#define IWX_CSR_INT_BIT_HW_ERR	(1 << 29) /* DMA hardware error FH_INT[31] */
#define IWX_CSR_INT_BIT_RX_PERIODIC	(1 << 28) /* Rx periodic */
#define IWX_CSR_INT_BIT_FH_TX	(1 << 27) /* Tx DMA FH_INT[1:0] */
#define IWX_CSR_INT_BIT_SCD	(1 << 26) /* TXQ pointer advanced */
#define IWX_CSR_INT_BIT_SW_ERR	(1 << 25) /* uCode error */
#define IWX_CSR_INT_BIT_RF_KILL	(1 << 7)  /* HW RFKILL switch GP_CNTRL[27] toggled */
#define IWX_CSR_INT_BIT_CT_KILL	(1 << 6)  /* Critical temp (chip too hot) rfkill */
#define IWX_CSR_INT_BIT_SW_RX	(1 << 3)  /* Rx, command responses */
#define IWX_CSR_INT_BIT_WAKEUP	(1 << 1)  /* NIC controller waking up (pwr mgmt) */
#define IWX_CSR_INT_BIT_ALIVE	(1 << 0)  /* uCode interrupts once it initializes */

#define IWX_CSR_INI_SET_MASK	(IWX_CSR_INT_BIT_FH_RX   | \
				 IWX_CSR_INT_BIT_HW_ERR  | \
				 IWX_CSR_INT_BIT_FH_TX   | \
				 IWX_CSR_INT_BIT_SW_ERR  | \
				 IWX_CSR_INT_BIT_RF_KILL | \
				 IWX_CSR_INT_BIT_SW_RX   | \
				 IWX_CSR_INT_BIT_WAKEUP  | \
				 IWX_CSR_INT_BIT_ALIVE   | \
				 IWX_CSR_INT_BIT_RX_PERIODIC)

/* interrupt flags in FH (flow handler) (PCI busmaster DMA) */
#define IWX_CSR_FH_INT_BIT_ERR       (1U << 31) /* Error */
#define IWX_CSR_FH_INT_BIT_HI_PRIOR  (1 << 30) /* High priority Rx, bypass coalescing */
#define IWX_CSR_FH_INT_BIT_RX_CHNL1  (1 << 17) /* Rx channel 1 */
#define IWX_CSR_FH_INT_BIT_RX_CHNL0  (1 << 16) /* Rx channel 0 */
#define IWX_CSR_FH_INT_BIT_TX_CHNL1  (1 << 1)  /* Tx channel 1 */
#define IWX_CSR_FH_INT_BIT_TX_CHNL0  (1 << 0)  /* Tx channel 0 */

#define IWX_CSR_FH_INT_RX_MASK	(IWX_CSR_FH_INT_BIT_HI_PRIOR | \
				IWX_CSR_FH_INT_BIT_RX_CHNL1 | \
				IWX_CSR_FH_INT_BIT_RX_CHNL0)

#define IWX_CSR_FH_INT_TX_MASK	(IWX_CSR_FH_INT_BIT_TX_CHNL1 | \
				IWX_CSR_FH_INT_BIT_TX_CHNL0)

/**
 * struct iwx_rx_transfer_desc - transfer descriptor AX210
 * @addr: ptr to free buffer start address
 * @rbid: unique tag of the buffer
 * @reserved: reserved
 */
struct iwx_rx_transfer_desc {
	uint16_t rbid;
	uint16_t reserved[3];
	uint64_t addr;
};

#define IWX_RX_CD_FLAGS_FRAGMENTED	(1 << 0)

/**
 * struct iwx_rx_completion_desc - completion descriptor AX210
 * @reserved1: reserved
 * @rbid: unique tag of the received buffer
 * @flags: flags (0: fragmented, all others: reserved)
 * @reserved2: reserved
 */
struct iwx_rx_completion_desc {
	uint32_t reserved1;
	uint16_t rbid;
	uint8_t flags;
	uint8_t reserved2[25];
};

/* RESET */
#define IWX_CSR_RESET_REG_FLAG_NEVO_RESET                (0x00000001)
#define IWX_CSR_RESET_REG_FLAG_FORCE_NMI                 (0x00000002)
#define IWX_CSR_RESET_REG_FLAG_SW_RESET                  (0x00000080)
#define IWX_CSR_RESET_REG_FLAG_MASTER_DISABLED           (0x00000100)
#define IWX_CSR_RESET_REG_FLAG_STOP_MASTER               (0x00000200)
#define IWX_CSR_RESET_LINK_PWR_MGMT_DISABLED             (0x80000000)

/*
 * GP (general purpose) CONTROL REGISTER
 * Bit fields:
 *    27:  HW_RF_KILL_SW
 *         Indicates state of (platform's) hardware RF-Kill switch
 * 26-24:  POWER_SAVE_TYPE
 *         Indicates current power-saving mode:
 *         000 -- No power saving
 *         001 -- MAC power-down
 *         010 -- PHY (radio) power-down
 *         011 -- Error
 *   9-6:  SYS_CONFIG
 *         Indicates current system configuration, reflecting pins on chip
 *         as forced high/low by device circuit board.
 *     4:  GOING_TO_SLEEP
 *         Indicates MAC is entering a power-saving sleep power-down.
 *         Not a good time to access device-internal resources.
 *     3:  MAC_ACCESS_REQ
 *         Host sets this to request and maintain MAC wakeup, to allow host
 *         access to device-internal resources.  Host must wait for
 *         MAC_CLOCK_READY (and !GOING_TO_SLEEP) before accessing non-CSR
 *         device registers.
 *     2:  INIT_DONE
 *         Host sets this to put device into fully operational D0 power mode.
 *         Host resets this after SW_RESET to put device into low power mode.
 *     0:  MAC_CLOCK_READY
 *         Indicates MAC (ucode processor, etc.) is powered up and can run.
 *         Internal resources are accessible.
 *         NOTE:  This does not indicate that the processor is actually running.
 *         NOTE:  This does not indicate that device has completed
 *                init or post-power-down restore of internal SRAM memory.
 *                Use IWX_CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP as indication that
 *                SRAM is restored and uCode is in normal operation mode.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 *         NOTE:  After device reset, this bit remains "0" until host sets
 *                INIT_DONE
 */
#define IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY        (0x00000001)
#define IWX_CSR_GP_CNTRL_REG_FLAG_INIT_DONE              (0x00000004)
#define IWX_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ         (0x00000008)
#define IWX_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP         (0x00000010)

#define IWX_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN           (0x00000001)

#define IWX_CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE         (0x07000000)
#define IWX_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN     (0x04000000)
#define IWX_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)


/* HW REV */
#define IWX_CSR_HW_REV_DASH(_val)          (((_val) & 0x0000003) >> 0)
#define IWX_CSR_HW_REV_STEP(_val)          (((_val) & 0x000000C) >> 2)
#define IWX_CSR_HW_REV_TYPE(_val)          (((_val) & 0x000FFF0) >> 4)

#define IWX_CSR_HW_REV_TYPE_MSK		(0x000FFF0)
#define IWX_CSR_HW_REV_TYPE_QU_B0	(0x0000334)
#define IWX_CSR_HW_REV_TYPE_QU_C0	(0x0000338)
#define IWX_CSR_HW_REV_TYPE_QUZ		(0x0000354)
#define IWX_CSR_HW_REV_TYPE_SO		(0x0000370)
#define IWX_CSR_HW_REV_TYPE_TY		(0x0000420)

/* HW RFID */
#define IWX_CSR_HW_RFID_FLAVOR(_val)       (((_val) & 0x000000F) >> 0)
#define IWX_CSR_HW_RFID_DASH(_val)         (((_val) & 0x00000F0) >> 4)
#define IWX_CSR_HW_RFID_STEP(_val)         (((_val) & 0x0000F00) >> 8)
#define IWX_CSR_HW_RFID_TYPE(_val)         (((_val) & 0x0FFF000) >> 12)
#define IWX_CSR_HW_RFID_IS_CDB(_val)       (((_val) & 0x10000000) >> 28)
#define IWX_CSR_HW_RFID_IS_JACKET(_val)    (((_val) & 0x20000000) >> 29)

/* CSR GIO */
#define IWX_CSR_GIO_REG_VAL_L0S_DISABLED	(0x00000002)

/*
 * UCODE-DRIVER GP (general purpose) mailbox register 1
 * Host driver and uCode write and/or read this register to communicate with
 * each other.
 * Bit fields:
 *     4:  UCODE_DISABLE
 *         Host sets this to request permanent halt of uCode, same as
 *         sending CARD_STATE command with "halt" bit set.
 *     3:  CT_KILL_EXIT
 *         Host sets this to request exit from CT_KILL state, i.e. host thinks
 *         device temperature is low enough to continue normal operation.
 *     2:  CMD_BLOCKED
 *         Host sets this during RF KILL power-down sequence (HW, SW, CT KILL)
 *         to release uCode to clear all Tx and command queues, enter
 *         unassociated mode, and power down.
 *         NOTE:  Some devices also use HBUS_TARG_MBX_C register for this bit.
 *     1:  SW_BIT_RFKILL
 *         Host sets this when issuing CARD_STATE command to request
 *         device sleep.
 *     0:  MAC_SLEEP
 *         uCode sets this when preparing a power-saving power-down.
 *         uCode resets this when power-up is complete and SRAM is sane.
 *         NOTE:  device saves internal SRAM data to host when powering down,
 *                and must restore this data after powering back up.
 *                MAC_SLEEP is the best indication that restore is complete.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 */
#define IWX_CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP             (0x00000001)
#define IWX_CSR_UCODE_SW_BIT_RFKILL                     (0x00000002)
#define IWX_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED           (0x00000004)
#define IWX_CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT      (0x00000008)
#define IWX_CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE       (0x00000020)

/* GIO Chicken Bits (PCI Express bus link power management) */
#define IWX_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX  (0x00800000)
#define IWX_CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER  (0x20000000)

/* HPET MEM debug */
#define IWX_CSR_DBG_HPET_MEM_REG_VAL	(0xFFFF0000)

/* DRAM INT TABLE */
#define IWX_CSR_DRAM_INT_TBL_ENABLE		(1U << 31)
#define IWX_CSR_DRAM_INIT_TBL_WRITE_POINTER	(1 << 28)
#define IWX_CSR_DRAM_INIT_TBL_WRAP_CHECK	(1 << 27)

/* 22000 configuration registers */

/*
 * TFH Configuration register.
 *
 * BIT fields:
 *
 * Bits 3:0:
 * Define the maximum number of pending read requests.
 * Maximum configuration value allowed is 0xC
 * Bits 9:8:
 * Define the maximum transfer size. (64 / 128 / 256)
 * Bit 10:
 * When bit is set and transfer size is set to 128B, the TFH will enable
 * reading chunks of more than 64B only if the read address is aligned to 128B.
 * In case of DRAM read address which is not aligned to 128B, the TFH will
 * enable transfer size which doesn't cross 64B DRAM address boundary.
*/
#define IWX_TFH_TRANSFER_MODE			(0x1F40)
#define IWX_TFH_TRANSFER_MAX_PENDING_REQ	0xc
#define IWX_TFH_CHUNK_SIZE_128			(1 << 8)
#define IWX_TFH_CHUNK_SPLIT_MODE		(1 << 10)

/*
 * Defines the offset address in dwords referring from the beginning of the
 * Tx CMD which will be updated in DRAM.
 * Note that the TFH offset address for Tx CMD update is always referring to
 * the start of the TFD first TB.
 * In case of a DRAM Tx CMD update the TFH will update PN and Key ID
 */
#define IWX_TFH_TXCMD_UPDATE_CFG		(0x1F48)

/*
 * Controls TX DMA operation
 *
 * BIT fields:
 *
 * Bits 31:30: Enable the SRAM DMA channel.
 * Turning on bit 31 will kick the SRAM2DRAM DMA.
 * Note that the sram2dram may be enabled only after configuring the DRAM and
 * SRAM addresses registers and the byte count register.
 * Bits 25:24: Defines the interrupt target upon dram2sram transfer done. When
 * set to 1 - interrupt is sent to the driver
 * Bit 0: Indicates the snoop configuration
*/
#define IWX_TFH_SRV_DMA_CHNL0_CTRL	(0x1F60)
#define IWX_TFH_SRV_DMA_SNOOP		(1 << 0)
#define IWX_TFH_SRV_DMA_TO_DRIVER	(1 << 24)
#define IWX_TFH_SRV_DMA_START		(1U << 31)

/* Defines the DMA SRAM write start address to transfer a data block */
#define IWX_TFH_SRV_DMA_CHNL0_SRAM_ADDR	(0x1F64)

/* Defines the 64bits DRAM start address to read the DMA data block from */
#define IWX_TFH_SRV_DMA_CHNL0_DRAM_ADDR	(0x1F68)

/*
 * Defines the number of bytes to transfer from DRAM to SRAM.
 * Note that this register may be configured with non-dword aligned size.
 */
#define IWX_TFH_SRV_DMA_CHNL0_BC	(0x1F70)

/* 9000 rx series registers */

#define IWX_RFH_Q0_FRBDCB_BA_LSB 0xA08000 /* 64 bit address */
#define IWX_RFH_Q_FRBDCB_BA_LSB(q) (IWX_RFH_Q0_FRBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define IWX_RFH_Q0_FRBDCB_WIDX 0xA08080
#define IWX_RFH_Q_FRBDCB_WIDX(q) (IWX_RFH_Q0_FRBDCB_WIDX + (q) * 4)
/* Write index table - shadow registers */
#define IWX_RFH_Q0_FRBDCB_WIDX_TRG 0x1C80
#define IWX_RFH_Q_FRBDCB_WIDX_TRG(q) (IWX_RFH_Q0_FRBDCB_WIDX_TRG + (q) * 4)
/* Read index table */
#define IWX_RFH_Q0_FRBDCB_RIDX 0xA080C0
#define IWX_RFH_Q_FRBDCB_RIDX(q) (IWX_RFH_Q0_FRBDCB_RIDX + (q) * 4)
/* Used list table */
#define IWX_RFH_Q0_URBDCB_BA_LSB 0xA08100 /* 64 bit address */
#define IWX_RFH_Q_URBDCB_BA_LSB(q) (IWX_RFH_Q0_URBDCB_BA_LSB + (q) * 8)
/* Write index table */
#define IWX_RFH_Q0_URBDCB_WIDX 0xA08180
#define IWX_RFH_Q_URBDCB_WIDX(q) (IWX_RFH_Q0_URBDCB_WIDX + (q) * 4)
#define IWX_RFH_Q0_URBDCB_VAID 0xA081C0
#define IWX_RFH_Q_URBDCB_VAID(q) (IWX_RFH_Q0_URBDCB_VAID + (q) * 4)
/* stts */
#define IWX_RFH_Q0_URBD_STTS_WPTR_LSB 0xA08200 /*64 bits address */
#define IWX_RFH_Q_URBD_STTS_WPTR_LSB(q) (IWX_RFH_Q0_URBD_STTS_WPTR_LSB + (q) * 8)

#define IWX_RFH_Q0_ORB_WPTR_LSB 0xA08280
#define IWX_RFH_Q_ORB_WPTR_LSB(q) (IWX_RFH_Q0_ORB_WPTR_LSB + (q) * 8)
#define IWX_RFH_RBDBUF_RBD0_LSB 0xA08300
#define IWX_RFH_RBDBUF_RBD_LSB(q) (IWX_RFH_RBDBUF_RBD0_LSB + (q) * 8)

/**
 * RFH Status Register
 *
 * Bit fields:
 *
 * Bit 29: RBD_FETCH_IDLE
 * This status flag is set by the RFH when there is no active RBD fetch from
 * DRAM.
 * Once the RFH RBD controller starts fetching (or when there is a pending
 * RBD read response from DRAM), this flag is immediately turned off.
 *
 * Bit 30: SRAM_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * SRAM to DRAM.
 * Once the SRAM to DRAM DMA is active, this flag is immediately turned off.
 *
 * Bit 31: RXF_DMA_IDLE
 * This status flag is set by the RFH when there is no active transaction from
 * RXF to DRAM.
 * Once the RXF-to-DRAM DMA is active, this flag is immediately turned off.
 */
#define IWX_RFH_GEN_STATUS          0xA09808
#define IWX_RFH_GEN_STATUS_GEN3     0xA07824
#define IWX_RBD_FETCH_IDLE  (1 << 29)
#define IWX_SRAM_DMA_IDLE   (1 << 30)
#define IWX_RXF_DMA_IDLE    (1U << 31)

/* DMA configuration */
#define IWX_RFH_RXF_DMA_CFG         0xA09820
#define IWX_RFH_RXF_DMA_CFG_GEN3    0xA07880
/* RB size */
#define IWX_RFH_RXF_DMA_RB_SIZE_MASK (0x000F0000) /* bits 16-19 */
#define IWX_RFH_RXF_DMA_RB_SIZE_POS 16
#define IWX_RFH_RXF_DMA_RB_SIZE_1K  (0x1 << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_2K  (0x2 << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_4K  (0x4 << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_8K  (0x8 << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_12K (0x9 << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_16K (0xA << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_20K (0xB << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_24K (0xC << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_28K (0xD << IWX_RFH_RXF_DMA_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RB_SIZE_32K (0xE << IWX_RFH_RXF_DMA_RB_SIZE_POS)
/* RB Circular Buffer size:defines the table sizes in RBD units */
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_MASK (0x00F00000) /* bits 20-23 */
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_POS 20
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_8        (0x3 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_16       (0x4 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_32       (0x5 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_64       (0x7 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_128      (0x7 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_256      (0x8 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_512      (0x9 << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_1024     (0xA << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_RBDCB_SIZE_2048     (0xB << IWX_RFH_RXF_DMA_RBDCB_SIZE_POS)
#define IWX_RFH_RXF_DMA_MIN_RB_SIZE_MASK    (0x03000000) /* bit 24-25 */
#define IWX_RFH_RXF_DMA_MIN_RB_SIZE_POS     24
#define IWX_RFH_RXF_DMA_MIN_RB_4_8          (3 << IWX_RFH_RXF_DMA_MIN_RB_SIZE_POS)
#define IWX_RFH_RXF_DMA_DROP_TOO_LARGE_MASK (0x04000000) /* bit 26 */
#define IWX_RFH_RXF_DMA_SINGLE_FRAME_MASK   (0x20000000) /* bit 29 */
#define IWX_RFH_DMA_EN_MASK                 (0xC0000000) /* bits 30-31*/
#define IWX_RFH_DMA_EN_ENABLE_VAL           (1U << 31)

#define IWX_RFH_RXF_RXQ_ACTIVE 0xA0980C

#define IWX_RFH_GEN_CFG     0xA09800
#define IWX_RFH_GEN_CFG_SERVICE_DMA_SNOOP   (1 << 0)
#define IWX_RFH_GEN_CFG_RFH_DMA_SNOOP       (1 << 1)
#define IWX_RFH_GEN_CFG_RB_CHUNK_SIZE_128   0x00000010
#define IWX_RFH_GEN_CFG_RB_CHUNK_SIZE_64    0x00000000
/* the driver assumes everywhere that the default RXQ is 0 */
#define IWX_RFH_GEN_CFG_DEFAULT_RXQ_NUM     0xF00

/* end of 9000 rx series registers */

/*
 * This register is written by driver and is read by uCode during boot flow.
 * Note this address is cleared after MAC reset.
 */
#define IWX_UREG_UCODE_LOAD_STATUS	(0xa05c40)
#define IWX_UREG_CPU_INIT_RUN		(0xa05c44)

/*
 * HBUS (Host-side Bus)
 *
 * HBUS registers are mapped directly into PCI bus space, but are used
 * to indirectly access device's internal memory or registers that
 * may be powered-down.
 *
 * Use iwl_write_direct32()/iwl_read_direct32() family for these registers;
 * host must "grab nic access" via CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ
 * to make sure the MAC (uCode processor, etc.) is powered up for accessing
 * internal resources.
 *
 * Do not use iwl_write32()/iwl_read32() family to access these registers;
 * these provide only simple PCI bus access, without waking up the MAC.
 */
#define IWX_HBUS_BASE	(0x400)

/*
 * Registers for accessing device's internal SRAM memory (e.g. SCD SRAM
 * structures, error log, event log, verifying uCode load).
 * First write to address register, then read from or write to data register
 * to complete the job.  Once the address register is set up, accesses to
 * data registers auto-increment the address by one dword.
 * Bit usage for address registers (read or write):
 *  0-31:  memory address within device
 */
#define IWX_HBUS_TARG_MEM_RADDR     (IWX_HBUS_BASE+0x00c)
#define IWX_HBUS_TARG_MEM_WADDR     (IWX_HBUS_BASE+0x010)
#define IWX_HBUS_TARG_MEM_WDAT      (IWX_HBUS_BASE+0x018)
#define IWX_HBUS_TARG_MEM_RDAT      (IWX_HBUS_BASE+0x01c)

/*
 * Registers for accessing device's internal peripheral registers
 * (e.g. SCD, BSM, etc.).  First write to address register,
 * then read from or write to data register to complete the job.
 * Bit usage for address registers (read or write):
 *  0-15:  register address (offset) within device
 * 24-25:  (# bytes - 1) to read or write (e.g. 3 for dword)
 */
#define IWX_HBUS_TARG_PRPH_WADDR    (IWX_HBUS_BASE+0x044)
#define IWX_HBUS_TARG_PRPH_RADDR    (IWX_HBUS_BASE+0x048)
#define IWX_HBUS_TARG_PRPH_WDAT     (IWX_HBUS_BASE+0x04c)
#define IWX_HBUS_TARG_PRPH_RDAT     (IWX_HBUS_BASE+0x050)

/* enable the ID buf for read */
#define IWX_WFPM_PS_CTL_CLR			0xa0300c
#define IWX_WFMP_MAC_ADDR_0			0xa03080
#define IWX_WFMP_MAC_ADDR_1			0xa03084
#define IWX_LMPM_PMG_EN				0xa01cec
#define IWX_RADIO_REG_SYS_MANUAL_DFT_0		0xad4078
#define IWX_RFIC_REG_RD				0xad0470
#define IWX_WFPM_CTRL_REG			0xa03030
#define IWX_WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK	0x08000000
#define IWX_ENABLE_WFPM				0x80000000

#define IWX_AUX_MISC_MASTER1_EN			0xa20818
#define IWX_AUX_MISC_MASTER1_EN_SBE_MSK		0x1
#define IWX_AUX_MISC_MASTER1_SMPHR_STATUS	0xa20800
#define IWX_RSA_ENABLE				0xa24b08
#define IWX_PREG_AUX_BUS_WPROT_0		0xa04cc0
#define IWX_PREG_PRPH_WPROT_9000		0xa04ce0
#define IWX_PREG_PRPH_WPROT_22000		0xa04d00
#define IWX_SB_CFG_OVERRIDE_ADDR		0xa26c78
#define IWX_SB_CFG_OVERRIDE_ENABLE		0x8000
#define IWX_SB_CFG_BASE_OVERRIDE		0xa20000
#define IWX_SB_MODIFY_CFG_FLAG			0xa03088
#define IWX_UMAG_SB_CPU_1_STATUS		0xa038c0
#define IWX_UMAG_SB_CPU_2_STATUS		0xa038c4

#define IWX_UREG_CHICK				0xa05c00
#define IWX_UREG_CHICK_MSI_ENABLE		(1 << 24)
#define IWX_UREG_CHICK_MSIX_ENABLE		(1 << 25)

#define IWX_HPM_DEBUG			0xa03440
#define IWX_PERSISTENCE_BIT		(1 << 12)
#define IWX_PREG_WFPM_ACCESS		(1 << 12)

#define IWX_HPM_HIPM_GEN_CFG			0xa03458
#define IWX_HPM_HIPM_GEN_CFG_CR_PG_EN		(1 << 0)
#define IWX_HPM_HIPM_GEN_CFG_CR_SLP_EN		(1 << 1)
#define IWX_HPM_HIPM_GEN_CFG_CR_FORCE_ACTIVE	(1 << 10)

#define IWX_UREG_DOORBELL_TO_ISR6		0xa05c04
#define IWX_UREG_DOORBELL_TO_ISR6_NMI_BIT	(1 << 0)
#define IWX_UREG_DOORBELL_TO_ISR6_RESET_HANDSHAKE ((1 << 0) | (1 << 1))
#define IWX_UREG_DOORBELL_TO_ISR6_SUSPEND	(1 << 18)
#define IWX_UREG_DOORBELL_TO_ISR6_RESUME	(1 << 19)
#define IWX_UREG_DOORBELL_TO_ISR6_PNVM		(1 << 20)

/* LTR control (Qu only) */
#define IWX_HPM_MAC_LTR_CSR			0xa0348c
#define IWX_HPM_MAC_LRT_ENABLE_ALL		0xf
/* also uses CSR_LTR_* for values */
#define IWX_HPM_UMAC_LTR			0xa03480

/*
 * Per-Tx-queue write pointer (index, really!)
 * Indicates index to next TFD that driver will fill (1 past latest filled).
 * Bit usage:
 *  0-7:  queue write index
 * 11-8:  queue selector
 */
#define IWX_HBUS_TARG_WRPTR         (IWX_HBUS_BASE+0x060)

/**********************************************************
 * CSR values
 **********************************************************/
 /*
 * host interrupt timeout value
 * used with setting interrupt coalescing timer
 * the CSR_INT_COALESCING is an 8 bit register in 32-usec unit
 *
 * default interrupt coalescing timer is 64 x 32 = 2048 usecs
 */
#define IWX_HOST_INT_TIMEOUT_MAX	(0xFF)
#define IWX_HOST_INT_TIMEOUT_DEF	(0x40)
#define IWX_HOST_INT_TIMEOUT_MIN	(0x0)
#define IWX_HOST_INT_OPER_MODE		(1U << 31)

/*****************************************************************************
 *                        MSIX related registers                             *
 *****************************************************************************/

#define IWX_CSR_MSIX_BASE			(0x2000)
#define IWX_CSR_MSIX_FH_INT_CAUSES_AD		(IWX_CSR_MSIX_BASE + 0x800)
#define IWX_CSR_MSIX_FH_INT_MASK_AD		(IWX_CSR_MSIX_BASE + 0x804)
#define IWX_CSR_MSIX_HW_INT_CAUSES_AD		(IWX_CSR_MSIX_BASE + 0x808)
#define IWX_CSR_MSIX_HW_INT_MASK_AD		(IWX_CSR_MSIX_BASE + 0x80C)
#define IWX_CSR_MSIX_AUTOMASK_ST_AD		(IWX_CSR_MSIX_BASE + 0x810)
#define IWX_CSR_MSIX_RX_IVAR_AD_REG		(IWX_CSR_MSIX_BASE + 0x880)
#define IWX_CSR_MSIX_IVAR_AD_REG		(IWX_CSR_MSIX_BASE + 0x890)
#define IWX_CSR_MSIX_PENDING_PBA_AD		(IWX_CSR_MSIX_BASE + 0x1000)
#define IWX_CSR_MSIX_RX_IVAR(cause)		(IWX_CSR_MSIX_RX_IVAR_AD_REG + (cause))
#define IWX_CSR_MSIX_IVAR(cause)		(IWX_CSR_MSIX_IVAR_AD_REG + (cause))

/*
 * Causes for the FH register interrupts
 */
enum msix_fh_int_causes {
	IWX_MSIX_FH_INT_CAUSES_Q0		= (1 << 0),
	IWX_MSIX_FH_INT_CAUSES_Q1		= (1 << 1),
	IWX_MSIX_FH_INT_CAUSES_D2S_CH0_NUM	= (1 << 16),
	IWX_MSIX_FH_INT_CAUSES_D2S_CH1_NUM	= (1 << 17),
	IWX_MSIX_FH_INT_CAUSES_S2D		= (1 << 19),
	IWX_MSIX_FH_INT_CAUSES_FH_ERR		= (1 << 21),
};

/*
 * Causes for the HW register interrupts
 */
enum mix_hw_int_causes {
	IWX_MSIX_HW_INT_CAUSES_REG_ALIVE	= (1 << 0),
	IWX_MSIX_HW_INT_CAUSES_REG_WAKEUP	= (1 << 1),
	IWX_MSIX_HW_INT_CAUSES_REG_RESET_DONE	= (1 << 2),
	IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR_V2	= (1 << 5),
	IWX_MSIX_HW_INT_CAUSES_REG_CT_KILL	= (1 << 6),
	IWX_MSIX_HW_INT_CAUSES_REG_RF_KILL	= (1 << 7),
	IWX_MSIX_HW_INT_CAUSES_REG_PERIODIC	= (1 << 8),
	IWX_MSIX_HW_INT_CAUSES_REG_SW_ERR	= (1 << 25),
	IWX_MSIX_HW_INT_CAUSES_REG_SCD		= (1 << 26),
	IWX_MSIX_HW_INT_CAUSES_REG_FH_TX	= (1 << 27),
	IWX_MSIX_HW_INT_CAUSES_REG_HW_ERR	= (1 << 29),
	IWX_MSIX_HW_INT_CAUSES_REG_HAP		= (1 << 30),
};

/*
 * Registers to map causes to vectors
 */
enum msix_ivar_for_cause {
	IWX_MSIX_IVAR_CAUSE_D2S_CH0_NUM		= 0x0,
	IWX_MSIX_IVAR_CAUSE_D2S_CH1_NUM		= 0x1,
	IWX_MSIX_IVAR_CAUSE_S2D			= 0x3,
	IWX_MSIX_IVAR_CAUSE_FH_ERR		= 0x5,
	IWX_MSIX_IVAR_CAUSE_REG_ALIVE		= 0x10,
	IWX_MSIX_IVAR_CAUSE_REG_WAKEUP		= 0x11,
	IWX_MSIX_IVAR_CAUSE_REG_RESET_DONE	= 0x12,
	IWX_MSIX_IVAR_CAUSE_REG_CT_KILL		= 0x16,
	IWX_MSIX_IVAR_CAUSE_REG_RF_KILL		= 0x17,
	IWX_MSIX_IVAR_CAUSE_REG_PERIODIC	= 0x18,
	IWX_MSIX_IVAR_CAUSE_REG_SW_ERR		= 0x29,
	IWX_MSIX_IVAR_CAUSE_REG_SCD		= 0x2a,
	IWX_MSIX_IVAR_CAUSE_REG_FH_TX		= 0x2b,
	IWX_MSIX_IVAR_CAUSE_REG_HW_ERR		= 0x2d,
	IWX_MSIX_IVAR_CAUSE_REG_HAP		= 0x2e,
};

#define IWX_MSIX_AUTO_CLEAR_CAUSE		(0 << 7)
#define IWX_MSIX_NON_AUTO_CLEAR_CAUSE		(1 << 7)

#define IWX_CSR_ADDR_BASE(sc)			((sc)->mac_addr_from_csr)
#define IWX_CSR_MAC_ADDR0_OTP(sc)		(IWX_CSR_ADDR_BASE(sc) + 0x00)
#define IWX_CSR_MAC_ADDR1_OTP(sc)		(IWX_CSR_ADDR_BASE(sc) + 0x04)
#define IWX_CSR_MAC_ADDR0_STRAP(sc)		(IWX_CSR_ADDR_BASE(sc) + 0x08)
#define IWX_CSR_MAC_ADDR1_STRAP(sc)		(IWX_CSR_ADDR_BASE(sc) + 0x0c)

/**
 * uCode API flags
 * @IWX_UCODE_TLV_FLAGS_PAN: This is PAN capable microcode; this previously
 *	was a separate TLV but moved here to save space.
 * @IWX_UCODE_TLV_FLAGS_NEWSCAN: new uCode scan behaviour on hidden SSID,
 *	treats good CRC threshold as a boolean
 * @IWX_UCODE_TLV_FLAGS_MFP: This uCode image supports MFP (802.11w).
 * @IWX_UCODE_TLV_FLAGS_P2P: This uCode image supports P2P.
 * @IWX_UCODE_TLV_FLAGS_DW_BC_TABLE: The SCD byte count table is in DWORDS
 * @IWX_UCODE_TLV_FLAGS_UAPSD: This uCode image supports uAPSD
 * @IWX_UCODE_TLV_FLAGS_SHORT_BL: 16 entries of black list instead of 64 in scan
 *	offload profile config command.
 * @IWX_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS: D3 image supports up to six
 *	(rather than two) IPv6 addresses
 * @IWX_UCODE_TLV_FLAGS_NO_BASIC_SSID: not sending a probe with the SSID element
 *	from the probe request template.
 * @IWX_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL: new NS offload (small version)
 * @IWX_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE: new NS offload (large version)
 * @IWX_UCODE_TLV_FLAGS_P2P_PS: P2P client power save is supported (only on a
 *	single bound interface).
 * @IWX_UCODE_TLV_FLAGS_UAPSD_SUPPORT: General support for uAPSD
 * @IWX_UCODE_TLV_FLAGS_EBS_SUPPORT: this uCode image supports EBS.
 * @IWX_UCODE_TLV_FLAGS_P2P_PS_UAPSD: P2P client supports uAPSD power save
 * @IWX_UCODE_TLV_FLAGS_BCAST_FILTERING: uCode supports broadcast filtering.
 * @IWX_UCODE_TLV_FLAGS_GO_UAPSD: AP/GO interfaces support uAPSD clients
 *
 */
#define IWX_UCODE_TLV_FLAGS_PAN			(1 << 0)
#define IWX_UCODE_TLV_FLAGS_NEWSCAN		(1 << 1)
#define IWX_UCODE_TLV_FLAGS_MFP			(1 << 2)
#define IWX_UCODE_TLV_FLAGS_P2P			(1 << 3)
#define IWX_UCODE_TLV_FLAGS_DW_BC_TABLE		(1 << 4)
#define IWX_UCODE_TLV_FLAGS_SHORT_BL		(1 << 7)
#define IWX_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS	(1 << 10)
#define IWX_UCODE_TLV_FLAGS_NO_BASIC_SSID	(1 << 12)
#define IWX_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL	(1 << 15)
#define IWX_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE	(1 << 16)
#define IWX_UCODE_TLV_FLAGS_P2P_PS		(1 << 21)
#define IWX_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM	(1 << 22)
#define IWX_UCODE_TLV_FLAGS_BSS_P2P_PS_SCM	(1 << 23)
#define IWX_UCODE_TLV_FLAGS_UAPSD_SUPPORT	(1 << 24)
#define IWX_UCODE_TLV_FLAGS_EBS_SUPPORT		(1 << 25)
#define IWX_UCODE_TLV_FLAGS_P2P_PS_UAPSD	(1 << 26)
#define IWX_UCODE_TLV_FLAGS_BCAST_FILTERING	(1 << 29)
#define IWX_UCODE_TLV_FLAGS_GO_UAPSD		(1 << 30)
#define IWX_UCODE_TLV_FLAGS_LTE_COEX		(1U << 31)

#define IWX_UCODE_TLV_FLAG_BITS \
	"\020\1PAN\2NEWSCAN\3MFP\4P2P\5DW_BC_TABLE\6NEWBT_COEX\7PM_CMD\10SHORT_BL\11RX_ENERGY\12TIME_EVENT_V2\13D3_6_IPV6\14BF_UPDATED\15NO_BASIC_SSID\17D3_CONTINUITY\20NEW_NSOFFL_S\21NEW_NSOFFL_L\22SCHED_SCAN\24STA_KEY_CMD\25DEVICE_PS_CMD\26P2P_PS\27P2P_PS_DCM\30P2P_PS_SCM\31UAPSD_SUPPORT\32EBS\33P2P_PS_UAPSD\36BCAST_FILTERING\37GO_UAPSD\40LTE_COEX"

/**
 * uCode TLV api
 * @IWX_UCODE_TLV_API_FRAGMENTED_SCAN: This ucode supports active dwell time
 *	longer than the passive one, which is essential for fragmented scan.
 * @IWX_UCODE_TLV_API_WIFI_MCC_UPDATE: ucode supports MCC updates with source.
 * @IWX_UCODE_TLV_API_WIDE_CMD_HDR: ucode supports wide command header
 * @IWX_UCODE_TLV_API_LQ_SS_PARAMS: Configure STBC/BFER via LQ CMD ss_params
 * @IWX_UCODE_TLV_API_NEW_VERSION: new versioning format
 * @IWX_UCODE_TLV_API_TX_POWER_CHAIN: TX power API has larger command size
 *	(command version 3) that supports per-chain limits
 * @IWX_UCODE_TLV_API_SCAN_TSF_REPORT: Scan start time reported in scan
 *	iteration complete notification, and the timestamp reported for RX
 *	received during scan, are reported in TSF of the mac specified in the
 *	scan request.
 * @IWX_UCODE_TLV_API_TKIP_MIC_KEYS: This ucode supports version 2 of
 *	ADD_MODIFY_STA_KEY_API_S_VER_2.
 * @IWX_UCODE_TLV_API_STA_TYPE: This ucode supports station type assignment.
 * @IWX_UCODE_TLV_API_EXT_SCAN_PRIORITY: scan APIs use 8-level priority
 *	instead of 3.
 * @IWX_UCODE_TLV_API_NEW_RX_STATS: should new RX STATISTICS API be used
 * @IWX_UCODE_TLV_API_REDUCED_SCAN_CONFIG: This ucode supports v3 of
 *  SCAN_CONFIG_DB_CMD_API_S.
 *
 * @IWX_NUM_UCODE_TLV_API: number of bits used
 */
#define IWX_UCODE_TLV_API_FRAGMENTED_SCAN	8
#define IWX_UCODE_TLV_API_WIFI_MCC_UPDATE	9
#define IWX_UCODE_TLV_API_WIDE_CMD_HDR		14
#define IWX_UCODE_TLV_API_LQ_SS_PARAMS		18
#define IWX_UCODE_TLV_API_NEW_VERSION		20
#define IWX_UCODE_TLV_API_EXT_SCAN_PRIORITY	24
#define IWX_UCODE_TLV_API_TX_POWER_CHAIN	27
#define IWX_UCODE_TLV_API_SCAN_TSF_REPORT	28
#define IWX_UCODE_TLV_API_TKIP_MIC_KEYS         29
#define IWX_UCODE_TLV_API_STA_TYPE		30
#define IWX_UCODE_TLV_API_NAN2_VER2		31
#define IWX_UCODE_TLV_API_ADAPTIVE_DWELL	32
#define IWX_UCODE_TLV_API_NEW_RX_STATS		35
#define IWX_UCODE_TLV_API_WOWLAN_KEY_MATERIAL	36
#define IWX_UCODE_TLV_API_QUOTA_LOW_LATENCY	38
#define IWX_UCODE_TLV_API_DEPRECATE_TTAK	41
#define IWX_UCODE_TLV_API_ADAPTIVE_DWELL_V2	42
#define IWX_UCODE_TLV_API_NAN_NOTIF_V2		43
#define IWX_UCODE_TLV_API_FRAG_EBS		44
#define IWX_UCODE_TLV_API_REDUCE_TX_POWER	45
#define IWX_UCODE_TLV_API_SHORT_BEACON_NOTIF	46
#define IWX_UCODE_TLV_API_BEACON_FILTER_V4	47
#define IWX_UCODE_TLV_API_REGULATORY_NVM_INFO	48
#define IWX_UCODE_TLV_API_FTM_NEW_RANGE_REQ	49
#define IWX_UCODE_TLV_API_REDUCED_SCAN_CONFIG	56
#define IWX_UCODE_TLV_API_SCAN_OFFLOAD_CHANS	50
#define IWX_UCODE_TLV_API_MBSSID_HE		52
#define IWX_UCODE_TLV_API_WOWLAN_TCP_SYN_WAKE	53
#define IWX_UCODE_TLV_API_FTM_RTT_ACCURACY	54
#define IWX_UCODE_TLV_API_SAR_TABLE_VER		55
#define IWX_UCODE_TLV_API_REDUCED_SCAN_CONFIG	56
#define IWX_UCODE_TLV_API_ADWELL_HB_DEF_N_AP	57
#define IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER	58
#define IWX_UCODE_TLV_API_BAND_IN_RX_DATA	59
#define IWX_NUM_UCODE_TLV_API			128

#define IWX_UCODE_TLV_API_BITS \
	"\020\10FRAGMENTED_SCAN\11WIFI_MCC_UPDATE\16WIDE_CMD_HDR\22LQ_SS_PARAMS\30EXT_SCAN_PRIO\33TX_POWER_CHAIN\35TKIP_MIC_KEYS"

/**
 * uCode capabilities
 * @IWX_UCODE_TLV_CAPA_D0I3_SUPPORT: supports D0i3
 * @IWX_UCODE_TLV_CAPA_LAR_SUPPORT: supports Location Aware Regulatory
 * @IWX_UCODE_TLV_CAPA_UMAC_SCAN: supports UMAC scan.
 * @IWX_UCODE_TLV_CAPA_BEAMFORMER: supports Beamformer
 * @IWX_UCODE_TLV_CAPA_TOF_SUPPORT: supports Time of Flight (802.11mc FTM)
 * @IWX_UCODE_TLV_CAPA_TDLS_SUPPORT: support basic TDLS functionality
 * @IWX_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT: supports insertion of current
 *	tx power value into TPC Report action frame and Link Measurement Report
 *	action frame
 * @IWX_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT: supports updating current
 *	channel in DS parameter set element in probe requests.
 * @IWX_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT: supports adding TPC Report IE in
 *	probe requests.
 * @IWX_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT: supports Quiet Period requests
 * @IWX_UCODE_TLV_CAPA_DQA_SUPPORT: supports dynamic queue allocation (DQA),
 *	which also implies support for the scheduler configuration command
 * @IWX_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH: supports TDLS channel switching
 * @IWX_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG: Consolidated D3-D0 image
 * @IWX_UCODE_TLV_CAPA_HOTSPOT_SUPPORT: supports Hot Spot Command
 * @IWX_UCODE_TLV_CAPA_DC2DC_SUPPORT: supports DC2DC Command
 * @IWX_UCODE_TLV_CAPA_2G_COEX_SUPPORT: supports 2G coex Command
 * @IWX_UCODE_TLV_CAPA_CSUM_SUPPORT: supports TCP Checksum Offload
 * @IWX_UCODE_TLV_CAPA_RADIO_BEACON_STATS: support radio and beacon statistics
 * @IWX_UCODE_TLV_CAPA_P2P_STANDALONE_UAPSD: support p2p standalone U-APSD
 * @IWX_UCODE_TLV_CAPA_BT_COEX_PLCR: enabled BT Coex packet level co-running
 * @IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC: ucode supports LAR updates with different
 *	sources for the MCC. This TLV bit is a future replacement to
 *	IWX_UCODE_TLV_API_WIFI_MCC_UPDATE. When either is set, multi-source LAR
 *	is supported.
 * @IWX_UCODE_TLV_CAPA_BT_COEX_RRC: supports BT Coex RRC
 * @IWX_UCODE_TLV_CAPA_GSCAN_SUPPORT: supports gscan
 * @IWX_UCODE_TLV_CAPA_NAN_SUPPORT: supports NAN
 * @IWX_UCODE_TLV_CAPA_UMAC_UPLOAD: supports upload mode in umac (1=supported,
 *	0=no support)
 * @IWx_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS: firmware supports ultra high band
 *	(6 GHz).
 * @IWX_UCODE_TLV_CAPA_EXTENDED_DTS_MEASURE: extended DTS measurement
 * @IWX_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS: supports short PM timeouts
 * @IWX_UCODE_TLV_CAPA_BT_MPLUT_SUPPORT: supports bt-coex Multi-priority LUT
 * @IWX_UCODE_TLV_CAPA_BEACON_ANT_SELECTION: firmware will decide on what
 *	antenna the beacon should be transmitted
 * @IWX_UCODE_TLV_CAPA_BEACON_STORING: firmware will store the latest beacon
 *	from AP and will send it upon d0i3 exit.
 * @IWX_UCODE_TLV_CAPA_LAR_SUPPORT_V2: support LAR API V2
 * @IWX_UCODE_TLV_CAPA_CT_KILL_BY_FW: firmware responsible for CT-kill
 * @IWX_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT: supports temperature
 *	thresholds reporting
 * @IWX_UCODE_TLV_CAPA_CTDP_SUPPORT: supports cTDP command
 * @IWX_UCODE_TLV_CAPA_USNIFFER_UNIFIED: supports usniffer enabled in
 *	regular image.
 * @IWX_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG: support getting more shared
 *	memory addresses from the firmware.
 * @IWX_UCODE_TLV_CAPA_LQM_SUPPORT: supports Link Quality Measurement
 * @IWX_UCODE_TLV_CAPA_LMAC_UPLOAD: supports upload mode in lmac (1=supported,
 *	0=no support)
 *
 * @IWX_NUM_UCODE_TLV_CAPA: number of bits used
 */
#define IWX_UCODE_TLV_CAPA_D0I3_SUPPORT			0
#define IWX_UCODE_TLV_CAPA_LAR_SUPPORT			1
#define IWX_UCODE_TLV_CAPA_UMAC_SCAN			2
#define IWX_UCODE_TLV_CAPA_BEAMFORMER			3
#define IWX_UCODE_TLV_CAPA_TOF_SUPPORT                  5
#define IWX_UCODE_TLV_CAPA_TDLS_SUPPORT			6
#define IWX_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT	8
#define IWX_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT	9
#define IWX_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT	10
#define IWX_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT		11
#define IWX_UCODE_TLV_CAPA_DQA_SUPPORT			12
#define IWX_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH		13
#define IWX_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG		17
#define IWX_UCODE_TLV_CAPA_HOTSPOT_SUPPORT		18
#define IWX_UCODE_TLV_CAPA_DC2DC_CONFIG_SUPPORT		19
#define IWX_UCODE_TLV_CAPA_2G_COEX_SUPPORT		20
#define IWX_UCODE_TLV_CAPA_CSUM_SUPPORT			21
#define IWX_UCODE_TLV_CAPA_RADIO_BEACON_STATS		22
#define IWX_UCODE_TLV_CAPA_P2P_STANDALONE_UAPSD		26
#define IWX_UCODE_TLV_CAPA_BT_COEX_PLCR			28
#define IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC		29
#define IWX_UCODE_TLV_CAPA_BT_COEX_RRC			30
#define IWX_UCODE_TLV_CAPA_GSCAN_SUPPORT		31
#define IWX_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG		32
#define IWX_UCODE_TLV_CAPA_NAN_SUPPORT			34
#define IWX_UCODE_TLV_CAPA_UMAC_UPLOAD			35
#define IWM_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT		37
#define IWX_UCODE_TLV_CAPA_STA_PM_NOTIF			38
#define IWX_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT		39
#define IWX_UCODE_TLV_CAPA_CDB_SUPPORT			40
#define IWX_UCODE_TLV_CAPA_D0I3_END_FIRST		41
#define IWX_UCODE_TLV_CAPA_TLC_OFFLOAD			43
#define IWX_UCODE_TLV_CAPA_DYNAMIC_QUOTA		44
#define IWX_UCODE_TLV_CAPA_COEX_SCHEMA_2		45
#define IWX_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD		46
#define IWX_UCODE_TLV_CAPA_FTM_CALIBRATED		47
#define IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS		48
#define IWX_UCODE_TLV_CAPA_CS_MODIFY			49
#define IWX_UCODE_TLV_CAPA_SET_LTR_GEN2			50
#define IWX_UCODE_TLV_CAPA_SET_PPAG			52
#define IWX_UCODE_TLV_CAPA_TAS_CFG			53
#define IWX_UCODE_TLV_CAPA_SESSION_PROT_CMD		54
#define IWX_UCODE_TLV_CAPA_PROTECTED_TWT		56
#define IWX_UCODE_TLV_CAPA_FW_RESET_HANDSHAKE		57
#define IWX_UCODE_TLV_CAPA_PASSIVE_6GHZ_SCAN		58
#define IWX_UCODE_TLV_CAPA_PROTECTED_TWT		56
#define IWX_UCODE_TLV_CAPA_FW_RESET_HANDSHAKE		57
#define IWX_UCODE_TLV_CAPA_PASSIVE_6GHZ_SCAN		58
#define IWX_UCODE_TLV_CAPA_BAID_ML_SUPPORT		63
#define IWX_UCODE_TLV_CAPA_EXTENDED_DTS_MEASURE		64
#define IWX_UCODE_TLV_CAPA_SHORT_PM_TIMEOUTS		65
#define IWX_UCODE_TLV_CAPA_BT_MPLUT_SUPPORT		67
#define IWX_UCODE_TLV_CAPA_MULTI_QUEUE_RX_SUPPORT	68
#define IWX_UCODE_TLV_CAPA_CSA_AND_TBTT_OFFLOAD		70
#define IWX_UCODE_TLV_CAPA_BEACON_ANT_SELECTION		71
#define IWX_UCODE_TLV_CAPA_BEACON_STORING		72
#define IWX_UCODE_TLV_CAPA_LAR_SUPPORT_V3		73
#define IWX_UCODE_TLV_CAPA_CT_KILL_BY_FW		74
#define IWX_UCODE_TLV_CAPA_TEMP_THS_REPORT_SUPPORT	75
#define IWX_UCODE_TLV_CAPA_CTDP_SUPPORT			76
#define IWX_UCODE_TLV_CAPA_USNIFFER_UNIFIED		77
#define IWX_UCODE_TLV_CAPA_LMAC_UPLOAD			79
#define IWX_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG	80
#define IWX_UCODE_TLV_CAPA_LQM_SUPPORT			81
#define IWX_UCODE_TLV_CAPA_TX_POWER_ACK			84
#define IWX_UCODE_TLV_CAPA_D3_DEBUG			87
#define IWX_UCODE_TLV_CAPA_LED_CMD_SUPPORT		88
#define IWX_UCODE_TLV_CAPA_MCC_UPDATE_11AX_SUPPORT	89
#define IWX_UCODE_TLV_CAPA_CSI_REPORTING		90
#define IWX_UCODE_TLV_CAPA_CSI_REPORTING_V2		91
#define IWX_UCODE_TLV_CAPA_DBG_SUSPEND_RESUME_CMD_SUPP	92
#define IWX_UCODE_TLV_CAPA_DBG_BUF_ALLOC_CMD_SUPP	93
#define IWX_UCODE_TLV_CAPA_MLME_OFFLOAD			96
#define IWX_UCODE_TLV_CAPA_BIGTK_SUPPORT		100
#define IWX_UCODE_TLV_CAPA_RFIM_SUPPORT			102
#define IWX_UCODE_TLV_CAPA_MLD_API_SUPPORT		110 

#define IWX_NUM_UCODE_TLV_CAPA 128

/*
 * For 16.0 uCode and above, there is no differentiation between sections,
 * just an offset to the HW address.
 */
#define IWX_CPU1_CPU2_SEPARATOR_SECTION		0xFFFFCCCC
#define IWX_PAGING_SEPARATOR_SECTION		0xAAAABBBB

/* uCode version contains 4 values: Major/Minor/API/Serial */
#define IWX_UCODE_MAJOR(ver)	(((ver) & 0xFF000000) >> 24)
#define IWX_UCODE_MINOR(ver)	(((ver) & 0x00FF0000) >> 16)
#define IWX_UCODE_API(ver)	(((ver) & 0x0000FF00) >> 8)
#define IWX_UCODE_SERIAL(ver)	((ver) & 0x000000FF)

/*
 * Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers.
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers.
 */
struct iwx_tlv_calib_ctrl {
	uint32_t flow_trigger;
	uint32_t event_trigger;
} __packed;

#define IWX_FW_PHY_CFG_RADIO_TYPE_POS	0
#define IWX_FW_PHY_CFG_RADIO_TYPE	(0x3 << IWX_FW_PHY_CFG_RADIO_TYPE_POS)
#define IWX_FW_PHY_CFG_RADIO_STEP_POS	2
#define IWX_FW_PHY_CFG_RADIO_STEP	(0x3 << IWX_FW_PHY_CFG_RADIO_STEP_POS)
#define IWX_FW_PHY_CFG_RADIO_DASH_POS	4
#define IWX_FW_PHY_CFG_RADIO_DASH	(0x3 << IWX_FW_PHY_CFG_RADIO_DASH_POS)
#define IWX_FW_PHY_CFG_TX_CHAIN_POS	16
#define IWX_FW_PHY_CFG_TX_CHAIN		(0xf << IWX_FW_PHY_CFG_TX_CHAIN_POS)
#define IWX_FW_PHY_CFG_RX_CHAIN_POS	20
#define IWX_FW_PHY_CFG_RX_CHAIN		(0xf << IWX_FW_PHY_CFG_RX_CHAIN_POS)

/**
 * struct iwx_fw_cipher_scheme - a cipher scheme supported by FW.
 * @cipher: a cipher suite selector
 * @flags: cipher scheme flags (currently reserved for a future use)
 * @hdr_len: a size of MPDU security header
 * @pn_len: a size of PN
 * @pn_off: an offset of pn from the beginning of the security header
 * @key_idx_off: an offset of key index byte in the security header
 * @key_idx_mask: a bit mask of key_idx bits
 * @key_idx_shift: bit shift needed to get key_idx
 * @mic_len: mic length in bytes
 * @hw_cipher: a HW cipher index used in host commands
 */
struct iwx_fw_cipher_scheme {
	uint32_t cipher;
	uint8_t flags;
	uint8_t hdr_len;
	uint8_t pn_len;
	uint8_t pn_off;
	uint8_t key_idx_off;
	uint8_t key_idx_mask;
	uint8_t key_idx_shift;
	uint8_t mic_len;
	uint8_t hw_cipher;
} __packed;

/**
 * struct iwx_fw_cscheme_list - a cipher scheme list
 * @size: a number of entries
 * @cs: cipher scheme entries
 */
struct iwx_fw_cscheme_list {
	uint8_t size;
	struct iwx_fw_cipher_scheme cs[];
} __packed;

/* v1/v2 uCode file layout */
struct iwx_ucode_header {
	uint32_t ver;	/* major/minor/API/serial */
	union {
		struct {
			uint32_t inst_size;	/* bytes of runtime code */
			uint32_t data_size;	/* bytes of runtime data */
			uint32_t init_size;	/* bytes of init code */
			uint32_t init_data_size;	/* bytes of init data */
			uint32_t boot_size;	/* bytes of bootstrap code */
			uint8_t data[0];		/* in same order as sizes */
		} v1;
		struct {
			uint32_t build;		/* build number */
			uint32_t inst_size;	/* bytes of runtime code */
			uint32_t data_size;	/* bytes of runtime data */
			uint32_t init_size;	/* bytes of init code */
			uint32_t init_data_size;	/* bytes of init data */
			uint32_t boot_size;	/* bytes of bootstrap code */
			uint8_t data[0];		/* in same order as sizes */
		} v2;
	} u;
};

/*
 * new TLV uCode file layout
 *
 * The new TLV file format contains TLVs, that each specify
 * some piece of data.
 */

#define IWX_UCODE_TLV_INVALID		0 /* unused */
#define IWX_UCODE_TLV_INST		1
#define IWX_UCODE_TLV_DATA		2
#define IWX_UCODE_TLV_INIT		3
#define IWX_UCODE_TLV_INIT_DATA		4
#define IWX_UCODE_TLV_BOOT		5
#define IWX_UCODE_TLV_PROBE_MAX_LEN	6 /* a uint32_t value */
#define IWX_UCODE_TLV_PAN		7
#define IWX_UCODE_TLV_RUNT_EVTLOG_PTR	8
#define IWX_UCODE_TLV_RUNT_EVTLOG_SIZE	9
#define IWX_UCODE_TLV_RUNT_ERRLOG_PTR	10
#define IWX_UCODE_TLV_INIT_EVTLOG_PTR	11
#define IWX_UCODE_TLV_INIT_EVTLOG_SIZE	12
#define IWX_UCODE_TLV_INIT_ERRLOG_PTR	13
#define IWX_UCODE_TLV_ENHANCE_SENS_TBL	14
#define IWX_UCODE_TLV_PHY_CALIBRATION_SIZE 15
#define IWX_UCODE_TLV_WOWLAN_INST	16
#define IWX_UCODE_TLV_WOWLAN_DATA	17
#define IWX_UCODE_TLV_FLAGS		18
#define IWX_UCODE_TLV_SEC_RT		19
#define IWX_UCODE_TLV_SEC_INIT		20
#define IWX_UCODE_TLV_SEC_WOWLAN	21
#define IWX_UCODE_TLV_DEF_CALIB		22
#define IWX_UCODE_TLV_PHY_SKU		23
#define IWX_UCODE_TLV_SECURE_SEC_RT	24
#define IWX_UCODE_TLV_SECURE_SEC_INIT	25
#define IWX_UCODE_TLV_SECURE_SEC_WOWLAN	26
#define IWX_UCODE_TLV_NUM_OF_CPU	27
#define IWX_UCODE_TLV_CSCHEME		28
#define IWX_UCODE_TLV_API_CHANGES_SET	29
#define IWX_UCODE_TLV_ENABLED_CAPABILITIES 30
#define IWX_UCODE_TLV_N_SCAN_CHANNELS	31
#define IWX_UCODE_TLV_PAGING		32
#define IWX_UCODE_TLV_SEC_RT_USNIFFER	34
#define IWX_UCODE_TLV_SDIO_ADMA_ADDR	35
#define IWX_UCODE_TLV_FW_VERSION	36
#define IWX_UCODE_TLV_FW_DBG_DEST	38
#define IWX_UCODE_TLV_FW_DBG_CONF	39
#define IWX_UCODE_TLV_FW_DBG_TRIGGER	40
#define IWX_UCODE_TLV_CMD_VERSIONS	48
#define IWX_UCODE_TLV_FW_GSCAN_CAPA	50
#define IWX_UCODE_TLV_FW_MEM_SEG	51
#define IWX_UCODE_TLV_IML		52
#define IWX_UCODE_TLV_FW_FMAC_API_VERSION 53
#define IWX_UCODE_TLV_UMAC_DEBUG_ADDRS	54
#define IWX_UCODE_TLV_LMAC_DEBUG_ADDRS	55
#define IWX_UCODE_TLV_FW_RECOVERY_INFO	57
#define IWX_UCODE_TLV_HW_TYPE		58
#define IWX_UCODE_TLV_FW_FMAC_RECOVERY_INFO 59
#define IWX_UCODE_TLV_FW_FSEQ_VERSION	60
#define IWX_UCODE_TLV_PHY_INTEGRATION_VERSION	61
#define IWX_UCODE_TLV_PNVM_VERSION		62
#define IWX_UCODE_TLV_PNVM_SKU			64

#define IWX_UCODE_TLV_SEC_TABLE_ADDR		66
#define IWX_UCODE_TLV_D3_KEK_KCK_ADDR		67
#define IWX_UCODE_TLV_CURRENT_PC		68

#define IWX_UCODE_TLV_CONST_BASE		0x100
#define IWX_UCODE_TLV_FW_NUM_STATIONS		(IWX_UCODE_TLV_CONST_BASE + 0)
#define IWX_UCODE_TLV_FW_NUM_BEACONS		(IWX_UCODE_TLV_CONST_BASE + 2)

#define IWX_UCODE_TLV_DEBUG_BASE	0x1000005
#define IWX_UCODE_TLV_TYPE_DEBUG_INFO		(IWX_UCODE_TLV_DEBUG_BASE + 0)
#define IWX_UCODE_TLV_TYPE_BUFFER_ALLOCATION	(IWX_UCODE_TLV_DEBUG_BASE + 1)
#define IWX_UCODE_TLV_TYPE_HCMD			(IWX_UCODE_TLV_DEBUG_BASE + 2)
#define IWX_UCODE_TLV_TYPE_REGIONS		(IWX_UCODE_TLV_DEBUG_BASE + 3)
#define IWX_UCODE_TLV_TYPE_TRIGGERS		(IWX_UCODE_TLV_DEBUG_BASE + 4)
#define IWX_UCODE_TLV_TYPE_CONF_SET		(IWX_UCODE_TLV_DEBUG_BASE + 5)
#define IWX_UCODE_TLV_DEBUG_MAX IWX_UCODE_TLV_TYPE_CONF_SET


struct iwx_ucode_tlv {
	uint32_t type;		/* see above */
	uint32_t length;		/* not including type/length fields */
	uint8_t data[0];
};

struct iwx_ucode_api {
	uint32_t api_index;
	uint32_t api_flags;
} __packed;

struct iwx_ucode_capa {
	uint32_t api_index;
	uint32_t api_capa;
} __packed;

#define IWX_TLV_UCODE_MAGIC	0x0a4c5749

struct iwx_tlv_ucode_header {
	/*
	 * The TLV style ucode header is distinguished from
	 * the v1/v2 style header by first four bytes being
	 * zero, as such is an invalid combination of
	 * major/minor/API/serial versions.
	 */
	uint32_t zero;
	uint32_t magic;
	uint8_t human_readable[64];
	uint32_t ver;		/* major/minor/API/serial */
	uint32_t build;
	uint64_t ignore;
	/*
	 * The data contained herein has a TLV layout,
	 * see above for the TLV header and types.
	 * Note that each TLV is padded to a length
	 * that is a multiple of 4 for alignment.
	 */
	uint8_t data[0];
};

/*
 * Registers in this file are internal, not PCI bus memory mapped.
 * Driver accesses these via IWX_HBUS_TARG_PRPH_* registers.
 */
#define IWX_PRPH_BASE	(0x00000)
#define IWX_PRPH_END	(0xFFFFF)

/****************************/
/* Flow Handler Definitions */
/****************************/

/**
 * struct iwx_rb_status - receive buffer status
 * 	host memory mapped FH registers
 * @closed_rb_num [0:11] - Indicates the index of the RB which was closed
 * @closed_fr_num [0:11] - Indicates the index of the RX Frame which was closed
 * @finished_rb_num [0:11] - Indicates the index of the current RB
 * 	in which the last frame was written to
 * @finished_fr_num [0:11] - Indicates the index of the RX Frame
 * 	which was transferred
 */
struct iwx_rb_status {
	uint16_t closed_rb_num;
	uint16_t closed_fr_num;
	uint16_t finished_rb_num;
	uint16_t finished_fr_nam;
	uint32_t unused;
} __packed;


#define IWX_TFD_QUEUE_SIZE_MAX		(256)
#define IWX_TFD_QUEUE_SIZE_MAX_GEN3	(65536)
/* cb size is the exponent - 3 */
#define IWX_TFD_QUEUE_CB_SIZE(x)	(IWX_RX_QUEUE_CB_SIZE(x) - 3)
#define IWX_TFD_QUEUE_SIZE_BC_DUP	(64)
#define IWX_TFD_QUEUE_BC_SIZE		(IWX_TFD_QUEUE_SIZE_MAX + \
					IWX_TFD_QUEUE_SIZE_BC_DUP)
#define IWX_TFD_QUEUE_BC_SIZE_GEN3_AX210	1024
#define IWX_TFD_QUEUE_BC_SIZE_GEN3_BZ		(1024 * 4)
#define IWX_TFH_NUM_TBS		25

/**
 * struct iwx_tfh_tb transmit buffer descriptor within transmit frame descriptor
 *
 * This structure contains dma address and length of transmission address
 *
 * @tb_len length of the tx buffer
 * @addr 64 bits dma address
 */
struct iwx_tfh_tb {
	uint16_t tb_len;
	uint64_t addr;
} __packed;

/**
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs.
 * For pre 22000 HW it is 256 x 128 bytes-per-TFD = 32 KBytes
 * For 22000 HW and on it is 256 x 256 bytes-per-TFD = 65 KBytes
 *
 * Each TFD contains pointer/size information for up to 25 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */

/**
 * struct iwx_tfh_tfd - Transmit Frame Descriptor (TFD)
 * @ num_tbs 0-4 number of active tbs
 *	     5 -15   reserved
 * @ tbs[25]	transmit frame buffer descriptors
 * @ __pad	padding
 */
struct iwx_tfh_tfd {
	uint16_t num_tbs;
	struct iwx_tfh_tb tbs[IWX_TFH_NUM_TBS];
	uint32_t __pad;
} __packed;

/* Fixed (non-configurable) rx data from phy */

/**
 * struct iwx_agn_schedq_bc_tbl scheduler byte count table
 *	base physical address provided by IWX_SCD_DRAM_BASE_ADDR
 * @tfd_offset  0-11 - tx command byte count
 *		12-13 - number of 64 byte chunks
 *		14-15 - reserved
 */
struct iwx_agn_scd_bc_tbl {
	uint16_t tfd_offset[IWX_TFD_QUEUE_BC_SIZE];
} __packed;

/**
 * struct iwx_gen3_bc_tbl_entry scheduler byte count table entry gen3
 * For AX210 and up, the table no longer needs to be contiguous in memory.
 * @tfd_offset: 0-13 - tx command byte count
 *		14-15 - number of 64 byte chunks
 */
struct iwx_gen3_bc_tbl_entry {
	uint16_t tfd_offset;
} __packed;

/**
 * DQA - Dynamic Queue Allocation -introduction
 *
 * Dynamic Queue Allocation (AKA "DQA") is a feature implemented in iwlwifi
 * to allow dynamic allocation of queues on-demand, rather than allocate them
 * statically ahead of time. Ideally, we would like to allocate one queue
 * per RA/TID, thus allowing an AP - for example - to send BE traffic to STA2
 * even if it also needs to send traffic to a sleeping STA1, without being
 * blocked by the sleeping station.
 *
 * Although the queues in DQA mode are dynamically allocated, there are still
 * some queues that are statically allocated:
 *	TXQ #0 - command queue
 *	TXQ #1 - aux frames
 */

/* static DQA Tx queue numbers */
#define IWX_DQA_CMD_QUEUE		0
#define IWX_DQA_AUX_QUEUE		1

#define IWX_DQA_INJECT_MONITOR_QUEUE	1 /* used in monitor mode only */
#define IWX_DQA_MGMT_QUEUE		1 /* default queue other modes */

/* Reserve 8 DQA Tx queues for QoS data frames. */
#define IWX_MAX_TID_COUNT	8
#define IWX_FIRST_AGG_TX_QUEUE	(IWX_DQA_MGMT_QUEUE + 1)
#define IWX_LAST_AGG_TX_QUEUE	(IWX_FIRST_AGG_TX_QUEUE + IWX_MAX_TID_COUNT - 1)
#define IWX_NUM_TX_QUEUES	(IWX_LAST_AGG_TX_QUEUE + 1)

/**
 * Max Tx window size is the max number of contiguous TFDs that the scheduler
 * can keep track of at one time when creating block-ack chains of frames.
 * Note that "64" matches the number of ack bits in a block-ack packet.
 */
#define IWX_FRAME_LIMIT	64

#define IWX_TX_FIFO_BK	0
#define IWX_TX_FIFO_BE	1
#define IWX_TX_FIFO_VI	2
#define IWX_TX_FIFO_VO	3
#define IWX_TX_FIFO_MCAST	5
#define IWX_TX_FIFO_CMD	7

enum iwx_gen2_tx_fifo {
	IWX_GEN2_TX_FIFO_CMD = 0,
	IWX_GEN2_EDCA_TX_FIFO_BK,
	IWX_GEN2_EDCA_TX_FIFO_BE,
	IWX_GEN2_EDCA_TX_FIFO_VI,
	IWX_GEN2_EDCA_TX_FIFO_VO,
	IWX_GEN2_TRIG_TX_FIFO_BK,
	IWX_GEN2_TRIG_TX_FIFO_BE,
	IWX_GEN2_TRIG_TX_FIFO_VI,
	IWX_GEN2_TRIG_TX_FIFO_VO,
};

/**
 * TXQ config options
 * @TX_QUEUE_CFG_ENABLE_QUEUE: enable a queue
 * @TX_QUEUE_CFG_TFD_SHORT_FORMAT: use short TFD format
 */
#define IWX_TX_QUEUE_CFG_ENABLE_QUEUE		(1 << 0)
#define IWX_TX_QUEUE_CFG_TFD_SHORT_FORMAT	(1 << 1)

#define IWX_DEFAULT_QUEUE_SIZE IWX_TFD_QUEUE_SIZE_MAX

/**
 * struct iwx_tx_queue_cfg_cmd - txq hw scheduler config command
 * @sta_id: station id
 * @tid: tid of the queue
 * @flags: see &enum iwl_tx_queue_cfg_actions
 * @cb_size: size of TFD cyclic buffer. Value is exponent - 3.
 *	Minimum value 0 (8 TFDs), maximum value 5 (256 TFDs)
 * @byte_cnt_addr: address of byte count table
 * @tfdq_addr: address of TFD circular buffer
 */
struct iwx_tx_queue_cfg_cmd {
	uint8_t sta_id;
	uint8_t tid;
	uint16_t flags;
	uint32_t cb_size;
	uint64_t byte_cnt_addr;
	uint64_t tfdq_addr;
} __packed; /* TX_QUEUE_CFG_CMD_API_S_VER_2 */

/**
 * struct iwx_tx_queue_cfg_rsp - response to txq hw scheduler config
 * @queue_number: queue number assigned to this RA -TID
 * @flags: set on failure
 * @write_pointer: initial value for write pointer
 * @reserved: reserved
 */
struct iwx_tx_queue_cfg_rsp {
	uint16_t queue_number;
	uint16_t flags;
	uint16_t write_pointer;
	uint16_t reserved;
} __packed; /* TX_QUEUE_CFG_RSP_API_S_VER_2 */


/*
 * Commands
 */
#define IWX_ALIVE		0x1
#define IWX_REPLY_ERROR		0x2
#define IWX_INIT_COMPLETE_NOTIF	0x4

/* PHY context commands */
#define IWX_PHY_CONTEXT_CMD	0x8
#define IWX_DBG_CFG		0x9

/* UMAC scan commands */
#define IWX_SCAN_ITERATION_COMPLETE_UMAC	0xb5
#define IWX_SCAN_CFG_CMD			0xc
#define IWX_SCAN_REQ_UMAC			0xd
#define IWX_SCAN_ABORT_UMAC			0xe
#define IWX_SCAN_COMPLETE_UMAC			0xf

/* station table */
#define IWX_ADD_STA_KEY	0x17
#define IWX_ADD_STA	0x18
#define IWX_REMOVE_STA	0x19

/* TX */
#define IWX_TX_CMD		0x1c
#define IWX_TXPATH_FLUSH	0x1e
#define IWX_MGMT_MCAST_KEY	0x1f

/* scheduler config */
#define IWX_SCD_QUEUE_CFG	0x1d

/* global key */
#define IWX_WEP_KEY	0x20

/* MAC and Binding commands */
#define IWX_MAC_CONTEXT_CMD		0x28
#define IWX_TIME_EVENT_CMD		0x29 /* both CMD and response */
#define IWX_TIME_EVENT_NOTIFICATION	0x2a
#define IWX_BINDING_CONTEXT_CMD		0x2b
#define IWX_TIME_QUOTA_CMD		0x2c
#define IWX_NON_QOS_TX_COUNTER_CMD	0x2d

/* Calibration */
#define IWX_TEMPERATURE_NOTIFICATION		0x62
#define IWX_CALIBRATION_CFG_CMD			0x65
#define IWX_CALIBRATION_RES_NOTIFICATION	0x66
#define IWX_CALIBRATION_COMPLETE_NOTIFICATION	0x67
#define IWX_RADIO_VERSION_NOTIFICATION		0x68

/* Phy */
#define IWX_PHY_CONFIGURATION_CMD		0x6a

/* Power - legacy power table command */
#define IWX_POWER_TABLE_CMD				0x77
#define IWX_PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION	0x78
#define IWX_LTR_CONFIG					0xee

/* NVM */
#define IWX_NVM_ACCESS_CMD	0x88

#define IWX_SET_CALIB_DEFAULT_CMD	0x8e

#define IWX_BEACON_NOTIFICATION		0x90
#define IWX_BEACON_TEMPLATE_CMD		0x91
#define IWX_TX_ANT_CONFIGURATION_CMD	0x98
#define IWX_BT_CONFIG			0x9b
#define IWX_STATISTICS_CMD		0x9c
#define IWX_STATISTICS_NOTIFICATION	0x9d
#define IWX_REDUCE_TX_POWER_CMD		0x9f

/* RF-KILL commands and notifications */
#define IWX_CARD_STATE_CMD		0xa0
#define IWX_CARD_STATE_NOTIFICATION	0xa1

#define IWX_MISSED_BEACONS_NOTIFICATION	0xa2

#define IWX_MFUART_LOAD_NOTIFICATION	0xb1

/* Power - new power table command */
#define IWX_MAC_PM_POWER_TABLE	0xa9

#define IWX_REPLY_RX_PHY_CMD	0xc0
#define IWX_REPLY_RX_MPDU_CMD	0xc1
#define IWX_BAR_FRAME_RELEASE	0xc2
#define IWX_FRAME_RELEASE	0xc3
#define IWX_BA_NOTIF		0xc5

/* Location Aware Regulatory */
#define IWX_MCC_UPDATE_CMD	0xc8
#define IWX_MCC_CHUB_UPDATE_CMD	0xc9

/* BT Coex */
#define IWX_BT_COEX_PRIO_TABLE	0xcc
#define IWX_BT_COEX_PROT_ENV	0xcd
#define IWX_BT_PROFILE_NOTIFICATION	0xce
#define IWX_BT_COEX_CI	0x5d

#define IWX_REPLY_SF_CFG_CMD		0xd1
#define IWX_REPLY_BEACON_FILTERING_CMD	0xd2

/* DTS measurements */
#define IWX_CMD_DTS_MEASUREMENT_TRIGGER		0xdc
#define IWX_DTS_MEASUREMENT_NOTIFICATION	0xdd

#define IWX_REPLY_DEBUG_CMD	0xf0
#define IWX_DEBUG_LOG_MSG	0xf7

#define IWX_MCAST_FILTER_CMD	0xd0

/* D3 commands/notifications */
#define IWX_D3_CONFIG_CMD		0xd3
#define IWX_PROT_OFFLOAD_CONFIG_CMD	0xd4
#define IWX_OFFLOADS_QUERY_CMD		0xd5
#define IWX_REMOTE_WAKE_CONFIG_CMD	0xd6

/* for WoWLAN in particular */
#define IWX_WOWLAN_PATTERNS		0xe0
#define IWX_WOWLAN_CONFIGURATION	0xe1
#define IWX_WOWLAN_TSC_RSC_PARAM	0xe2
#define IWX_WOWLAN_TKIP_PARAM		0xe3
#define IWX_WOWLAN_KEK_KCK_MATERIAL	0xe4
#define IWX_WOWLAN_GET_STATUSES		0xe5
#define IWX_WOWLAN_TX_POWER_PER_DB	0xe6

/* and for NetDetect */
#define IWX_NET_DETECT_CONFIG_CMD		0x54
#define IWX_NET_DETECT_PROFILES_QUERY_CMD	0x56
#define IWX_NET_DETECT_PROFILES_CMD		0x57
#define IWX_NET_DETECT_HOTSPOTS_CMD		0x58
#define IWX_NET_DETECT_HOTSPOTS_QUERY_CMD	0x59

/* system group command IDs */
#define IWX_FSEQ_VER_MISMATCH_NOTIFICATION	0xff

#define IWX_REPLY_MAX	0xff

/* PHY_OPS subcommand IDs */
#define IWX_CMD_DTS_MEASUREMENT_TRIGGER_WIDE	0x0
#define IWX_CTDP_CONFIG_CMD			0x03
#define IWX_TEMP_REPORTING_THRESHOLDS_CMD	0x04
#define IWX_CT_KILL_NOTIFICATION		0xFE
#define IWX_DTS_MEASUREMENT_NOTIF_WIDE		0xFF

/* command groups */
#define IWX_LEGACY_GROUP	0x0
#define IWX_LONG_GROUP		0x1
#define IWX_SYSTEM_GROUP	0x2
#define IWX_MAC_CONF_GROUP	0x3
#define IWX_PHY_OPS_GROUP	0x4
#define IWX_DATA_PATH_GROUP	0x5
#define IWX_PROT_OFFLOAD_GROUP	0xb
#define IWX_REGULATORY_AND_NVM_GROUP	0xc

/* SYSTEM_GROUP group subcommand IDs */

#define IWX_SHARED_MEM_CFG_CMD		0x00
#define IWX_SOC_CONFIGURATION_CMD	0x01
#define IWX_INIT_EXTENDED_CFG_CMD	0x03
#define IWX_FW_ERROR_RECOVERY_CMD	0x07

/* MAC_CONF group subcommand IDs */
#define IWX_SESSION_PROTECTION_CMD	0x05
#define IWX_MAC_CONFIG_CMD		0x08
#define IWX_LINK_CONFIG_CMD		0x09
#define IWX_STA_CONFIG_CMD		0x0a
#define IWX_STA_REMOVE_CMD		0x0c
#define IWX_SESSION_PROTECTION_NOTIF	0xfb

/* DATA_PATH group subcommand IDs */
#define IWX_DQA_ENABLE_CMD	0x00
#define IWX_RLC_CONFIG_CMD	0x08
#define IWX_TLC_MNG_CONFIG_CMD	0x0f
#define IWX_RX_BAID_ALLOCATION_CONFIG_CMD	0x16
#define IWX_SCD_QUEUE_CONFIG_CMD	0x17
#define IWX_SEC_KEY_CMD		0x18
#define IWX_RX_NO_DATA_NOTIF	0xf5
#define IWX_THERMAL_DUAL_CHAIN_REQUEST 0xf6
#define IWX_TLC_MNG_UPDATE_NOTIF 0xf7

/* REGULATORY_AND_NVM group subcommand IDs */
#define IWX_NVM_ACCESS_COMPLETE	0x00
#define IWX_NVM_GET_INFO	0x02
#define IWX_PNVM_INIT_COMPLETE	0xfe

/*
 * struct iwx_dqa_enable_cmd
 * @cmd_queue: the TXQ number of the command queue
 */
struct iwx_dqa_enable_cmd {
	uint32_t cmd_queue;
} __packed; /* DQA_CONTROL_CMD_API_S_VER_1 */

/**
 * struct iwx_cmd_response - generic response struct for most commands
 * @status: status of the command asked, changes for each one
 */
struct iwx_cmd_response {
	uint32_t status;
};

/*
 * struct iwx_tx_ant_cfg_cmd
 * @valid: valid antenna configuration
 */
struct iwx_tx_ant_cfg_cmd {
	uint32_t valid;
} __packed;

/*
 * Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers.
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers.
 */
struct iwx_calib_ctrl {
	uint32_t flow_trigger;
	uint32_t event_trigger;
} __packed;

/* This defines the bitmap of various calibrations to enable in both
 * init ucode and runtime ucode through IWX_CALIBRATION_CFG_CMD.
 */
#define IWX_CALIB_CFG_XTAL_IDX			(1 << 0)
#define IWX_CALIB_CFG_TEMPERATURE_IDX		(1 << 1)
#define IWX_CALIB_CFG_VOLTAGE_READ_IDX		(1 << 2)
#define IWX_CALIB_CFG_PAPD_IDX			(1 << 3)
#define IWX_CALIB_CFG_TX_PWR_IDX		(1 << 4)
#define IWX_CALIB_CFG_DC_IDX			(1 << 5)
#define IWX_CALIB_CFG_BB_FILTER_IDX		(1 << 6)
#define IWX_CALIB_CFG_LO_LEAKAGE_IDX		(1 << 7)
#define IWX_CALIB_CFG_TX_IQ_IDX			(1 << 8)
#define IWX_CALIB_CFG_TX_IQ_SKEW_IDX		(1 << 9)
#define IWX_CALIB_CFG_RX_IQ_IDX			(1 << 10)
#define IWX_CALIB_CFG_RX_IQ_SKEW_IDX		(1 << 11)
#define IWX_CALIB_CFG_SENSITIVITY_IDX		(1 << 12)
#define IWX_CALIB_CFG_CHAIN_NOISE_IDX		(1 << 13)
#define IWX_CALIB_CFG_DISCONNECTED_ANT_IDX	(1 << 14)
#define IWX_CALIB_CFG_ANT_COUPLING_IDX		(1 << 15)
#define IWX_CALIB_CFG_DAC_IDX			(1 << 16)
#define IWX_CALIB_CFG_ABS_IDX			(1 << 17)
#define IWX_CALIB_CFG_AGC_IDX			(1 << 18)

/*
 * Phy configuration command.
 */
struct iwx_phy_cfg_cmd {
	uint32_t	phy_cfg;
	struct iwx_calib_ctrl calib_control;
} __packed;

#define IWX_PHY_CFG_RADIO_TYPE	((1 << 0) | (1 << 1))
#define IWX_PHY_CFG_RADIO_STEP	((1 << 2) | (1 << 3))
#define IWX_PHY_CFG_RADIO_DASH	((1 << 4) | (1 << 5))
#define IWX_PHY_CFG_PRODUCT_NUMBER	((1 << 6) | (1 << 7))
#define IWX_PHY_CFG_TX_CHAIN_A	(1 << 8)
#define IWX_PHY_CFG_TX_CHAIN_B	(1 << 9)
#define IWX_PHY_CFG_TX_CHAIN_C	(1 << 10)
#define IWX_PHY_CFG_RX_CHAIN_A	(1 << 12)
#define IWX_PHY_CFG_RX_CHAIN_B	(1 << 13)
#define IWX_PHY_CFG_RX_CHAIN_C	(1 << 14)

#define IWX_MAX_DTS_TRIPS	8

/**
 * struct iwx_ct_kill_notif - CT-kill entry notification
 *
 * @temperature: the current temperature in celsius
 * @reserved: reserved
 */
struct iwx_ct_kill_notif {
	uint16_t temperature;
	uint16_t reserved;
} __packed; /* GRP_PHY_CT_KILL_NTF */

/**
 * struct iwx_temp_report_ths_cmd - set temperature thresholds
 * (IWX_TEMP_REPORTING_THRESHOLDS_CMD)
 *
 * @num_temps: number of temperature thresholds passed
 * @thresholds: array with the thresholds to be configured
 */
struct iwx_temp_report_ths_cmd {
	uint32_t num_temps;
	uint16_t thresholds[IWX_MAX_DTS_TRIPS];
} __packed; /* GRP_PHY_TEMP_REPORTING_THRESHOLDS_CMD */

/*
 * channel flags in NVM
 * @IWX_NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @IWX_NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @IWX_NVM_CHANNEL_ACTIVE: active scanning allowed
 * @IWX_NVM_CHANNEL_RADAR: radar detection required
 * @IWX_NVM_CHANNEL_INDOOR_ONLY: only indoor use is allowed
 * @IWX_NVM_CHANNEL_GO_CONCURRENT: GO operation is allowed when connected to BSS
 *	on same channel on 2.4 or same UNII band on 5.2
 * @IWX_NVM_CHANNEL_DFS: dynamic freq selection candidate
 * @IWX_NVM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @IWX_NVM_CHANNEL_40MHZ: 40 MHz channel okay (?)
 * @IWX_NVM_CHANNEL_80MHZ: 80 MHz channel okay (?)
 * @IWX_NVM_CHANNEL_160MHZ: 160 MHz channel okay (?)
 * @IWX_NVM_CHANNEL_DC_HIGH: DC HIGH required/allowed (?)
 */
#define IWX_NVM_CHANNEL_VALID	(1 << 0)
#define IWX_NVM_CHANNEL_IBSS	(1 << 1)
#define IWX_NVM_CHANNEL_ACTIVE	(1 << 3)
#define IWX_NVM_CHANNEL_RADAR	(1 << 4)
#define IWX_NVM_CHANNEL_INDOOR_ONLY	(1 << 5)
#define IWX_NVM_CHANNEL_GO_CONCURRENT	(1 << 6)
#define IWX_NVM_CHANNEL_DFS	(1 << 7)
#define IWX_NVM_CHANNEL_WIDE	(1 << 8)
#define IWX_NVM_CHANNEL_40MHZ	(1 << 9)
#define IWX_NVM_CHANNEL_80MHZ	(1 << 10)
#define IWX_NVM_CHANNEL_160MHZ	(1 << 11)
#define IWX_NVM_CHANNEL_DC_HIGH	(1 << 12)

/**
 * struct iwx_nvm_access_complete_cmd - NVM_ACCESS commands are completed
 * @reserved: reserved
 */
struct iwx_nvm_access_complete_cmd {
	uint32_t reserved;
} __packed; /* NVM_ACCESS_COMPLETE_CMD_API_S_VER_1 */

/*
 * struct iwx_nvm_get_info - request to get NVM data
 */
struct iwx_nvm_get_info {
	uint32_t reserved;
} __packed; /* REGULATORY_NVM_GET_INFO_CMD_API_S_VER_1 */

/**
 * enum iwx_nvm_info_general_flags - flags in NVM_GET_INFO resp
 * @NVM_GENERAL_FLAGS_EMPTY_OTP: 1 if OTP is empty
 */
#define IWX_NVM_GENERAL_FLAGS_EMPTY_OTP	(1 << 0)

/**
 * struct iwx_nvm_get_info_general - general NVM data
 * @flags: bit 0: 1 - empty, 0 - non-empty
 * @nvm_version: nvm version
 * @board_type: board type
 * @n_hw_addrs: number of reserved MAC addresses
 */
struct iwx_nvm_get_info_general {
	uint32_t flags;
	uint16_t nvm_version;
	uint8_t board_type;
	uint8_t n_hw_addrs;
} __packed; /* REGULATORY_NVM_GET_INFO_GENERAL_S_VER_2 */

/**
 * iwx_nvm_mac_sku_flags - flags in &iwl_nvm_get_info_sku
 * @NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED: true if 2.4 band enabled
 * @NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED: true if 5.2 band enabled
 * @NVM_MAC_SKU_FLAGS_802_11N_ENABLED: true if 11n enabled
 * @NVM_MAC_SKU_FLAGS_802_11AC_ENABLED: true if 11ac enabled
 * @NVM_MAC_SKU_FLAGS_802_11AX_ENABLED: true if 11ax enabled
 * @NVM_MAC_SKU_FLAGS_MIMO_DISABLED: true if MIMO disabled
 * @NVM_MAC_SKU_FLAGS_WAPI_ENABLED: true if WAPI enabled
 * @NVM_MAC_SKU_FLAGS_REG_CHECK_ENABLED: true if regulatory checker enabled
 * @NVM_MAC_SKU_FLAGS_API_LOCK_ENABLED: true if API lock enabled
 */
#define IWX_NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED	(1 << 0)
#define IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED	(1 << 1)
#define IWX_NVM_MAC_SKU_FLAGS_802_11N_ENABLED	(1 << 2)
#define IWX_NVM_MAC_SKU_FLAGS_802_11AC_ENABLED	(1 << 3)
#define IWX_NVM_MAC_SKU_FLAGS_802_11AX_ENABLED	(1 << 4)
#define IWX_NVM_MAC_SKU_FLAGS_MIMO_DISABLED	(1 << 5)
#define IWX_NVM_MAC_SKU_FLAGS_WAPI_ENABLED	(1 << 8)
#define IWX_NVM_MAC_SKU_FLAGS_REG_CHECK_ENABLED	(1 << 14)
#define IWX_NVM_MAC_SKU_FLAGS_API_LOCK_ENABLED	(1 << 15)

/**
 * struct iwx_nvm_get_info_sku - mac information
 * @mac_sku_flags: flags for SKU, see &enum iwl_nvm_mac_sku_flags
 */
struct iwx_nvm_get_info_sku {
	uint32_t mac_sku_flags;
} __packed; /* REGULATORY_NVM_GET_INFO_MAC_SKU_SECTION_S_VER_2 */

/**
 * struct iwx_nvm_get_info_phy - phy information
 * @tx_chains: BIT 0 chain A, BIT 1 chain B
 * @rx_chains: BIT 0 chain A, BIT 1 chain B
 */
struct iwx_nvm_get_info_phy {
	uint32_t tx_chains;
	uint32_t rx_chains;
} __packed; /* REGULATORY_NVM_GET_INFO_PHY_SKU_SECTION_S_VER_1 */

#define IWX_NUM_CHANNELS_V1	51
#define IWX_NUM_CHANNELS	110

/**
 * struct iwx_nvm_get_info_regulatory - regulatory information
 * @lar_enabled: is LAR enabled
 * @channel_profile: regulatory data of this channel
 * @reserved: reserved
 */
struct iwx_nvm_get_info_regulatory_v1 {
	uint32_t lar_enabled;
	uint16_t channel_profile[IWX_NUM_CHANNELS_V1];
	uint16_t reserved;
} __packed; /* REGULATORY_NVM_GET_INFO_REGULATORY_S_VER_1 */

/**
 * struct iwx_nvm_get_info_regulatory - regulatory information
 * @lar_enabled: is LAR enabled
 * @n_channels: number of valid channels in the array
 * @channel_profile: regulatory data of this channel
 */
struct iwx_nvm_get_info_regulatory {
	uint32_t lar_enabled;
	uint32_t n_channels;
	uint32_t channel_profile[IWX_NUM_CHANNELS];
} __packed; /* REGULATORY_NVM_GET_INFO_REGULATORY_S_VER_2 */

/**
 * struct iwx_nvm_get_info_rsp_v3 - response to get NVM data
 * @general: general NVM data
 * @mac_sku: data relating to MAC sku
 * @phy_sku: data relating to PHY sku
 * @regulatory: regulatory data
 */
struct iwx_nvm_get_info_rsp_v3 {
	struct iwx_nvm_get_info_general general;
	struct iwx_nvm_get_info_sku mac_sku;
	struct iwx_nvm_get_info_phy phy_sku;
	struct iwx_nvm_get_info_regulatory_v1 regulatory;
} __packed; /* REGULATORY_NVM_GET_INFO_RSP_API_S_VER_3 */

/**
 * struct iwx_nvm_get_info_rsp - response to get NVM data
 * @general: general NVM data
 * @mac_sku: data relating to MAC sku
 * @phy_sku: data relating to PHY sku
 * @regulatory: regulatory data
 */
struct iwx_nvm_get_info_rsp {
	struct iwx_nvm_get_info_general general;
	struct iwx_nvm_get_info_sku mac_sku;
	struct iwx_nvm_get_info_phy phy_sku;
	struct iwx_nvm_get_info_regulatory regulatory;
} __packed; /* REGULATORY_NVM_GET_INFO_RSP_API_S_VER_4 */


#define IWX_ALIVE_STATUS_ERR 0xDEAD
#define IWX_ALIVE_STATUS_OK 0xCAFE

struct iwx_lmac_debug_addrs {
	uint32_t error_event_table_ptr;	/* SRAM address for error log */
	uint32_t log_event_table_ptr;	/* SRAM address for LMAC event log */
	uint32_t cpu_register_ptr;
	uint32_t dbgm_config_ptr;
	uint32_t alive_counter_ptr;
	uint32_t scd_base_ptr;		/* SRAM address for SCD */
	uint32_t st_fwrd_addr;		/* pointer to Store and forward */
	uint32_t st_fwrd_size;
} __packed; /* UCODE_DEBUG_ADDRS_API_S_VER_2 */

struct iwx_lmac_alive {
	uint32_t ucode_major;
	uint32_t ucode_minor;
	uint8_t ver_subtype;
	uint8_t ver_type;
	uint8_t mac;
	uint8_t opt;
	uint32_t timestamp;
	struct iwx_lmac_debug_addrs dbg_ptrs;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_3 */

struct iwx_umac_debug_addrs {
	uint32_t error_info_addr;		/* SRAM address for UMAC error log */
	uint32_t dbg_print_buff_addr;
} __packed; /* UMAC_DEBUG_ADDRS_API_S_VER_1 */

struct iwx_umac_alive {
	uint32_t umac_major;		/* UMAC version: major */
	uint32_t umac_minor;		/* UMAC version: minor */
	struct iwx_umac_debug_addrs dbg_ptrs;
} __packed; /* UMAC_ALIVE_DATA_API_S_VER_2 */

struct iwx_alive_resp_v4 {
	uint16_t status;
	uint16_t flags;
	struct iwx_lmac_alive lmac_data[2];
	struct iwx_umac_alive umac_data;
} __packed; /* ALIVE_RES_API_S_VER_4 */

struct iwx_sku_id {
	uint32_t data[3];
} __packed; /* SKU_ID_API_S_VER_1 */

struct iwx_alive_resp_v5 {
	uint16_t status;
	uint16_t flags;
	struct iwx_lmac_alive lmac_data[2];
	struct iwx_umac_alive umac_data;
	struct iwx_sku_id sku_id;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_5 */

struct iwx_imr_alive_info {
	uint64_t base_addr;
	uint32_t size;
	uint32_t enabled;
} __packed; /* IMR_ALIVE_INFO_API_S_VER_1 */

struct iwx_alive_resp_v6 {
	uint16_t status;
	uint16_t flags;
	struct iwx_lmac_alive lmac_data[2];
	struct iwx_umac_alive umac_data;
	struct iwx_sku_id sku_id;
	struct iwx_imr_alive_info imr;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_6 */


#define IWX_SOC_CONFIG_CMD_FLAGS_DISCRETE	(1 << 0)
#define IWX_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY	(1 << 1)

#define IWX_SOC_FLAGS_LTR_APPLY_DELAY_MASK		0xc
#define IWX_SOC_FLAGS_LTR_APPLY_DELAY_NONE		0
#define IWX_SOC_FLAGS_LTR_APPLY_DELAY_200		1
#define IWX_SOC_FLAGS_LTR_APPLY_DELAY_2500		2
#define IWX_SOC_FLAGS_LTR_APPLY_DELAY_1820		3

/**
 * struct iwx_soc_configuration_cmd - Set device stabilization latency
 *
 * @flags: soc settings flags.  In VER_1, we can only set the DISCRETE
 *	flag, because the FW treats the whole value as an integer. In
 *	VER_2, we can set the bits independently.
 * @latency: time for SOC to ensure stable power & XTAL
 */
struct iwx_soc_configuration_cmd {
	uint32_t flags;
	uint32_t latency;
} __packed; /*
	     * SOC_CONFIGURATION_CMD_S_VER_1 (see description above)
	     * SOC_CONFIGURATION_CMD_S_VER_2
	     */

/**
 * commands driver may send before finishing init flow
 * @IWX_INIT_DEBUG_CFG: driver is going to send debug config command
 * @IWX_INIT_NVM: driver is going to send NVM_ACCESS commands
 */
#define IWX_INIT_DEBUG_CFG	(1 << 0)
#define IWX_INIT_NVM		(1 << 1)

/**
 * struct iwx_extended_cfg_cmd - mark what commands ucode should wait for
 * before finishing init flows
 * @init_flags: IWX_INIT_* flag bits
 */
struct iwx_init_extended_cfg_cmd {
	uint32_t init_flags;
} __packed; /* INIT_EXTENDED_CFG_CMD_API_S_VER_1 */

/* Error response/notification */
#define IWX_FW_ERR_UNKNOWN_CMD		0x0
#define IWX_FW_ERR_INVALID_CMD_PARAM	0x1
#define IWX_FW_ERR_SERVICE		0x2
#define IWX_FW_ERR_ARC_MEMORY		0x3
#define IWX_FW_ERR_ARC_CODE		0x4
#define IWX_FW_ERR_WATCH_DOG		0x5
#define IWX_FW_ERR_WEP_GRP_KEY_INDX	0x10
#define IWX_FW_ERR_WEP_KEY_SIZE		0x11
#define IWX_FW_ERR_OBSOLETE_FUNC	0x12
#define IWX_FW_ERR_UNEXPECTED		0xFE
#define IWX_FW_ERR_FATAL		0xFF

/**
 * struct iwx_error_resp - FW error indication
 * ( IWX_REPLY_ERROR = 0x2 )
 * @error_type: one of IWX_FW_ERR_*
 * @cmd_id: the command ID for which the error occurred
 * @bad_cmd_seq_num: sequence number of the erroneous command
 * @error_service: which service created the error, applicable only if
 *	error_type = 2, otherwise 0
 * @timestamp: TSF in usecs.
 */
struct iwx_error_resp {
	uint32_t error_type;
	uint8_t cmd_id;
	uint8_t reserved1;
	uint16_t bad_cmd_seq_num;
	uint32_t error_service;
	uint64_t timestamp;
} __packed;

enum iwx_fw_dbg_reg_operator {
	CSR_ASSIGN,
	CSR_SETBIT,
	CSR_CLEARBIT,

	PRPH_ASSIGN,
	PRPH_SETBIT,
	PRPH_CLEARBIT,

	INDIRECT_ASSIGN,
	INDIRECT_SETBIT,
	INDIRECT_CLEARBIT,

	PRPH_BLOCKBIT,
};

/**
 * struct iwx_fw_dbg_reg_op - an operation on a register
 *
 * @op: &enum iwx_fw_dbg_reg_operator
 * @addr: offset of the register
 * @val: value
 */
struct iwx_fw_dbg_reg_op {
	uint8_t op;
	uint8_t reserved[3];
	uint32_t addr;
	uint32_t val;
} __packed;

/**
 * enum iwx_fw_dbg_monitor_mode - available monitor recording modes
 *
 * @SMEM_MODE: monitor stores the data in SMEM
 * @EXTERNAL_MODE: monitor stores the data in allocated DRAM
 * @MARBH_MODE: monitor stores the data in MARBH buffer
 * @MIPI_MODE: monitor outputs the data through the MIPI interface
 */
enum iwx_fw_dbg_monitor_mode {
	SMEM_MODE = 0,
	EXTERNAL_MODE = 1,
	MARBH_MODE = 2,
	MIPI_MODE = 3,
};

/**
 * struct iwx_fw_dbg_mem_seg_tlv - configures the debug data memory segments
 *
 * @data_type: the memory segment type to record
 * @ofs: the memory segment offset
 * @len: the memory segment length, in bytes
 *
 * This parses IWX_UCODE_TLV_FW_MEM_SEG
 */
struct iwx_fw_dbg_mem_seg_tlv {
	uint32_t data_type;
	uint32_t ofs;
	uint32_t len;
} __packed;

/**
 * struct iwx_fw_dbg_dest_tlv_v1 - configures the destination of the debug data
 *
 * @version: version of the TLV - currently 0
 * @monitor_mode: &enum iwx_fw_dbg_monitor_mode
 * @size_power: buffer size will be 2^(size_power + 11)
 * @base_reg: addr of the base addr register (PRPH)
 * @end_reg:  addr of the end addr register (PRPH)
 * @write_ptr_reg: the addr of the reg of the write pointer
 * @wrap_count: the addr of the reg of the wrap_count
 * @base_shift: shift right of the base addr reg
 * @end_shift: shift right of the end addr reg
 * @reg_ops: array of registers operations
 *
 * This parses IWX_UCODE_TLV_FW_DBG_DEST
 */
struct iwx_fw_dbg_dest_tlv_v1 {
	uint8_t version;
	uint8_t monitor_mode;
	uint8_t size_power;
	uint8_t reserved;
	uint32_t base_reg;
	uint32_t end_reg;
	uint32_t write_ptr_reg;
	uint32_t wrap_count;
	uint8_t base_shift;
	uint8_t end_shift;
	struct iwx_fw_dbg_reg_op reg_ops[0];
} __packed;

/* Mask of the register for defining the LDBG MAC2SMEM buffer SMEM size */
#define IWX_LDBG_M2S_BUF_SIZE_MSK	0x0fff0000
/* Mask of the register for defining the LDBG MAC2SMEM SMEM base address */
#define IWX_LDBG_M2S_BUF_BA_MSK		0x00000fff
/* The smem buffer chunks are in units of 256 bits */
#define IWX_M2S_UNIT_SIZE			0x100

struct iwx_fw_dbg_dest_tlv {
	uint8_t version;
	uint8_t monitor_mode;
	uint8_t size_power;
	uint8_t reserved;
	uint32_t cfg_reg;
	uint32_t write_ptr_reg;
	uint32_t wrap_count;
	uint8_t base_shift;
	uint8_t size_shift;
	struct iwx_fw_dbg_reg_op reg_ops[0];
} __packed;

struct iwx_fw_dbg_conf_hcmd {
	uint8_t id;
	uint8_t reserved;
	uint16_t len;
	uint8_t data[0];
} __packed;

/**
 * enum iwx_fw_dbg_trigger_mode - triggers functionalities
 *
 * @IWX_FW_DBG_TRIGGER_START: when trigger occurs re-conf the dbg mechanism
 * @IWX_FW_DBG_TRIGGER_STOP: when trigger occurs pull the dbg data
 * @IWX_FW_DBG_TRIGGER_MONITOR_ONLY: when trigger occurs trigger is set to
 *	collect only monitor data
 */
enum iwx_fw_dbg_trigger_mode {
	IWX_FW_DBG_TRIGGER_START = (1 << 0),
	IWX_FW_DBG_TRIGGER_STOP = (1 << 1),
	IWX_FW_DBG_TRIGGER_MONITOR_ONLY = (1 << 2),
};

/**
 * enum iwx_fw_dbg_trigger_flags - the flags supported by wrt triggers
 * @IWX_FW_DBG_FORCE_RESTART: force a firmware restart
 */
enum iwx_fw_dbg_trigger_flags {
	IWX_FW_DBG_FORCE_RESTART = (1 << 0),
};

/**
 * enum iwx_fw_dbg_trigger_vif_type - define the VIF type for a trigger
 * @IWX_FW_DBG_CONF_VIF_ANY: any vif type
 * @IWX_FW_DBG_CONF_VIF_IBSS: IBSS mode
 * @IWX_FW_DBG_CONF_VIF_STATION: BSS mode
 * @IWX_FW_DBG_CONF_VIF_AP: AP mode
 * @IWX_FW_DBG_CONF_VIF_P2P_CLIENT: P2P Client mode
 * @IWX_FW_DBG_CONF_VIF_P2P_GO: P2P GO mode
 * @IWX_FW_DBG_CONF_VIF_P2P_DEVICE: P2P device
 * @IWX_FW_DBG_CONF_VIF_NAN: NAN device
 */
enum iwx_fw_dbg_trigger_vif_type {
	IWX_FW_DBG_CONF_VIF_ANY		= 0,
	IWX_FW_DBG_CONF_VIF_IBSS	= 1,
	IWX_FW_DBG_CONF_VIF_STATION	= 2,
	IWX_FW_DBG_CONF_VIF_AP		= 3,
	IWX_FW_DBG_CONF_VIF_P2P_CLIENT  = 8,
	IWX_FW_DBG_CONF_VIF_P2P_GO	= 9,
	IWX_FW_DBG_CONF_VIF_P2P_DEVICE	= 10,
	IWX_FW_DBG_CONF_VIF_NAN		= 12,
};

/**
 * enum iwl_fw_dbg_trigger - triggers available
 *
 * @FW_DBG_TRIGGER_USER: trigger log collection by user
 *	This should not be defined as a trigger to the driver, but a value the
 *	driver should set to indicate that the trigger was initiated by the
 *	user.
 * @FW_DBG_TRIGGER_FW_ASSERT: trigger log collection when the firmware asserts
 * @FW_DBG_TRIGGER_MISSED_BEACONS: trigger log collection when beacons are
 *	missed.
 * @FW_DBG_TRIGGER_CHANNEL_SWITCH: trigger log collection upon channel switch.
 * @FW_DBG_TRIGGER_FW_NOTIF: trigger log collection when the firmware sends a
 *	command response or a notification.
 * @FW_DBG_TRIGGER_MLME: trigger log collection upon MLME event.
 * @FW_DBG_TRIGGER_STATS: trigger log collection upon statistics threshold.
 * @FW_DBG_TRIGGER_RSSI: trigger log collection when the rssi of the beacon
 *	goes below a threshold.
 * @FW_DBG_TRIGGER_TXQ_TIMERS: configures the timers for the Tx queue hang
 *	detection.
 * @FW_DBG_TRIGGER_TIME_EVENT: trigger log collection upon time events related
 *	events.
 * @FW_DBG_TRIGGER_BA: trigger log collection upon BlockAck related events.
 * @FW_DBG_TX_LATENCY: trigger log collection when the tx latency goes above a
 *	threshold.
 * @FW_DBG_TDLS: trigger log collection upon TDLS related events.
 * @FW_DBG_TRIGGER_TX_STATUS: trigger log collection upon tx status when
 *  the firmware sends a tx reply.
 * @FW_DBG_TRIGGER_USER_EXTENDED: trigger log collection upon user space
 *  request.
 * @FW_DBG_TRIGGER_ALIVE_TIMEOUT: trigger log collection if alive flow timeouts
 * @FW_DBG_TRIGGER_DRIVER: trigger log collection upon a flow failure
 *	in the driver.
 */
enum iwx_fw_dbg_trigger {
	IWX_FW_DBG_TRIGGER_INVALID = 0,
	IWX_FW_DBG_TRIGGER_USER,
	IWX_FW_DBG_TRIGGER_FW_ASSERT,
	IWX_FW_DBG_TRIGGER_MISSED_BEACONS,
	IWX_FW_DBG_TRIGGER_CHANNEL_SWITCH,
	IWX_FW_DBG_TRIGGER_FW_NOTIF,
	IWX_FW_DBG_TRIGGER_MLME,
	IWX_FW_DBG_TRIGGER_STATS,
	IWX_FW_DBG_TRIGGER_RSSI,
	IWX_FW_DBG_TRIGGER_TXQ_TIMERS,
	IWX_FW_DBG_TRIGGER_TIME_EVENT,
	IWX_FW_DBG_TRIGGER_BA,
	IWX_FW_DBG_TRIGGER_TX_LATENCY,
	IWX_FW_DBG_TRIGGER_TDLS,
	IWX_FW_DBG_TRIGGER_TX_STATUS,
	IWX_FW_DBG_TRIGGER_USER_EXTENDED,
	IWX_FW_DBG_TRIGGER_ALIVE_TIMEOUT,
	IWX_FW_DBG_TRIGGER_DRIVER,

	/* must be last */
	IWX_FW_DBG_TRIGGER_MAX,
};


/**
 * struct iwx_fw_dbg_trigger_tlv - a TLV that describes the trigger
 * @id: &enum iwx_fw_dbg_trigger
 * @vif_type: &enum iwx_fw_dbg_trigger_vif_type
 * @stop_conf_ids: bitmap of configurations this trigger relates to.
 *	if the mode is %IWX_FW_DBG_TRIGGER_STOP, then if the bit corresponding
 *	to the currently running configuration is set, the data should be
 *	collected.
 * @stop_delay: how many milliseconds to wait before collecting the data
 *	after the STOP trigger fires.
 * @mode: &enum iwx_fw_dbg_trigger_mode - can be stop / start of both
 * @start_conf_id: if mode is %IWX_FW_DBG_TRIGGER_START, this defines what
 *	configuration should be applied when the triggers kicks in.
 * @occurrences: number of occurrences. 0 means the trigger will never fire.
 * @trig_dis_ms: the time, in milliseconds, after an occurrence of this
 *	trigger in which another occurrence should be ignored.
 * @flags: &enum iwx_fw_dbg_trigger_flags
 */
struct iwx_fw_dbg_trigger_tlv {
	uint32_t id;
	uint32_t vif_type;
	uint32_t stop_conf_ids;
	uint32_t stop_delay;
	uint8_t mode;
	uint8_t start_conf_id;
	uint16_t occurrences;
	uint16_t trig_dis_ms;
	uint8_t flags;
	uint8_t reserved[5];

	uint8_t data[0];
} __packed;

#define IWX_FW_DBG_START_FROM_ALIVE	0
#define IWX_FW_DBG_CONF_MAX		32
#define IWX_FW_DBG_INVALID		0xff

/**
 * struct iwx_fw_dbg_trigger_missed_bcon - configures trigger for missed beacons
 * @stop_consec_missed_bcon: stop recording if threshold is crossed.
 * @stop_consec_missed_bcon_since_rx: stop recording if threshold is crossed.
 * @start_consec_missed_bcon: start recording if threshold is crossed.
 * @start_consec_missed_bcon_since_rx: start recording if threshold is crossed.
 * @reserved1: reserved
 * @reserved2: reserved
 */
struct iwx_fw_dbg_trigger_missed_bcon {
	uint32_t stop_consec_missed_bcon;
	uint32_t stop_consec_missed_bcon_since_rx;
	uint32_t reserved2[2];
	uint32_t start_consec_missed_bcon;
	uint32_t start_consec_missed_bcon_since_rx;
	uint32_t reserved1[2];
} __packed;

/**
 * struct iwx_fw_dbg_trigger_cmd - configures trigger for messages from FW.
 * cmds: the list of commands to trigger the collection on
 */
struct iwx_fw_dbg_trigger_cmd {
	struct cmd {
		uint8_t cmd_id;
		uint8_t group_id;
	} __packed cmds[16];
} __packed;

/**
 * iwx_fw_dbg_trigger_stats - configures trigger for statistics
 * @stop_offset: the offset of the value to be monitored
 * @stop_threshold: the threshold above which to collect
 * @start_offset: the offset of the value to be monitored
 * @start_threshold: the threshold above which to start recording
 */
struct iwx_fw_dbg_trigger_stats {
	uint32_t stop_offset;
	uint32_t stop_threshold;
	uint32_t start_offset;
	uint32_t start_threshold;
} __packed;

/**
 * struct iwx_fw_dbg_trigger_low_rssi - trigger for low beacon RSSI
 * @rssi: RSSI value to trigger at
 */
struct iwx_fw_dbg_trigger_low_rssi {
	uint32_t rssi;
} __packed;

/**
 * struct iwx_fw_dbg_trigger_mlme - configures trigger for mlme events
 * @stop_auth_denied: number of denied authentication to collect
 * @stop_auth_timeout: number of authentication timeout to collect
 * @stop_rx_deauth: number of Rx deauth before to collect
 * @stop_tx_deauth: number of Tx deauth before to collect
 * @stop_assoc_denied: number of denied association to collect
 * @stop_assoc_timeout: number of association timeout to collect
 * @stop_connection_loss: number of connection loss to collect
 * @start_auth_denied: number of denied authentication to start recording
 * @start_auth_timeout: number of authentication timeout to start recording
 * @start_rx_deauth: number of Rx deauth to start recording
 * @start_tx_deauth: number of Tx deauth to start recording
 * @start_assoc_denied: number of denied association to start recording
 * @start_assoc_timeout: number of association timeout to start recording
 * @start_connection_loss: number of connection loss to start recording
 */
struct iwx_fw_dbg_trigger_mlme {
	uint8_t stop_auth_denied;
	uint8_t stop_auth_timeout;
	uint8_t stop_rx_deauth;
	uint8_t stop_tx_deauth;

	uint8_t stop_assoc_denied;
	uint8_t stop_assoc_timeout;
	uint8_t stop_connection_loss;
	uint8_t reserved;

	uint8_t start_auth_denied;
	uint8_t start_auth_timeout;
	uint8_t start_rx_deauth;
	uint8_t start_tx_deauth;

	uint8_t start_assoc_denied;
	uint8_t start_assoc_timeout;
	uint8_t start_connection_loss;
	uint8_t reserved2;
} __packed;

/**
 * struct iwx_fw_dbg_trigger_txq_timer - configures the Tx queue's timer
 * @command_queue: timeout for the command queue in ms
 * @bss: timeout for the queues of a BSS (except for TDLS queues) in ms
 * @softap: timeout for the queues of a softAP in ms
 * @p2p_go: timeout for the queues of a P2P GO in ms
 * @p2p_client: timeout for the queues of a P2P client in ms
 * @p2p_device: timeout for the queues of a P2P device in ms
 * @ibss: timeout for the queues of an IBSS in ms
 * @tdls: timeout for the queues of a TDLS station in ms
 */
struct iwx_fw_dbg_trigger_txq_timer {
	uint32_t command_queue;
	uint32_t bss;
	uint32_t softap;
	uint32_t p2p_go;
	uint32_t p2p_client;
	uint32_t p2p_device;
	uint32_t ibss;
	uint32_t tdls;
	uint32_t reserved[4];
} __packed;

/**
 * struct iwx_fw_dbg_trigger_time_event - configures a time event trigger
 * time_Events: a list of tuples <id, action_bitmap>. The driver will issue a
 *	trigger each time a time event notification that relates to time event
 *	id with one of the actions in the bitmap is received and
 *	BIT(notif->status) is set in status_bitmap.
 *
 */
struct iwx_fw_dbg_trigger_time_event {
	struct {
		uint32_t id;
		uint32_t action_bitmap;
		uint32_t status_bitmap;
	} __packed time_events[16];
} __packed;

/**
 * struct iwx_fw_dbg_trigger_ba - configures BlockAck related trigger
 * rx_ba_start: tid bitmap to configure on what tid the trigger should occur
 *	when an Rx BlockAck session is started.
 * rx_ba_stop: tid bitmap to configure on what tid the trigger should occur
 *	when an Rx BlockAck session is stopped.
 * tx_ba_start: tid bitmap to configure on what tid the trigger should occur
 *	when a Tx BlockAck session is started.
 * tx_ba_stop: tid bitmap to configure on what tid the trigger should occur
 *	when a Tx BlockAck session is stopped.
 * rx_bar: tid bitmap to configure on what tid the trigger should occur
 *	when a BAR is received (for a Tx BlockAck session).
 * tx_bar: tid bitmap to configure on what tid the trigger should occur
 *	when a BAR is send (for an Rx BlockAck session).
 * frame_timeout: tid bitmap to configure on what tid the trigger should occur
 *	when a frame times out in the reordering buffer.
 */
struct iwx_fw_dbg_trigger_ba {
	uint16_t rx_ba_start;
	uint16_t rx_ba_stop;
	uint16_t tx_ba_start;
	uint16_t tx_ba_stop;
	uint16_t rx_bar;
	uint16_t tx_bar;
	uint16_t frame_timeout;
} __packed;

/**
 * struct iwx_fw_dbg_trigger_tx_latency - configures tx latency related trigger
 * @thrshold: the wanted threshold.
 * @tid_bitmap: the tid to apply the threshold on
 * @mode: recording mode (Internal buffer or continues recording)
 * @window: the size of the window before collecting.
 * @reserved: reserved.
 */
struct iwx_fw_dbg_trigger_tx_latency {
	uint32_t thrshold;
	uint16_t tid_bitmap;
	uint16_t mode;
	uint32_t window;
	uint32_t reserved[4];
} __packed;

/**
 * struct iwx_fw_dbg_trigger_tdls - configures trigger for TDLS events.
 * @action_bitmap: the TDLS action to trigger the collection upon
 * @peer_mode: trigger on specific peer or all
 * @peer: the TDLS peer to trigger the collection on
 */
struct iwx_fw_dbg_trigger_tdls {
	uint8_t action_bitmap;
	uint8_t peer_mode;
	uint8_t peer[ETHER_ADDR_LEN];
	uint8_t reserved[4];
} __packed;

/**
 * struct iwx_fw_dbg_trigger_tx_status - configures trigger for tx response
 *  status.
 * @statuses: the list of statuses to trigger the collection on
 */
struct iwx_fw_dbg_trigger_tx_status {
	struct tx_status {
		uint8_t status;
		uint8_t reserved[3];
	} __packed statuses[16];
	uint32_t reserved[2];
} __packed;

/**
 * struct iwx_fw_dbg_conf_tlv - a TLV that describes a debug configuration.
 * @id: conf id
 * @usniffer: should the uSniffer image be used
 * @num_of_hcmds: how many HCMDs to send are present here
 * @hcmd: a variable length host command to be sent to apply the configuration.
 *	If there is more than one HCMD to send, they will appear one after the
 *	other and be sent in the order that they appear in.
 * This parses IWX_UCODE_TLV_FW_DBG_CONF. The user can add up-to
 * %IWX_FW_DBG_CONF_MAX configuration per run.
 */
struct iwx_fw_dbg_conf_tlv {
	uint8_t id;
	uint8_t usniffer;
	uint8_t reserved;
	uint8_t num_of_hcmds;
	struct iwx_fw_dbg_conf_hcmd hcmd;
} __packed;

#define IWX_FW_CMD_VER_UNKNOWN 99

/**
 * struct iwx_fw_cmd_version - firmware command version entry
 * @cmd: command ID
 * @group: group ID
 * @cmd_ver: command version
 * @notif_ver: notification version
 */
struct iwx_fw_cmd_version {
	uint8_t cmd;
	uint8_t group;
	uint8_t cmd_ver;
	uint8_t notif_ver;
} __packed;

/* Common PHY, MAC and Bindings definitions */

#define IWX_MAX_MACS_IN_BINDING	(3)
#define IWX_MAX_BINDINGS	(4)
#define IWX_AUX_BINDING_INDEX	(3)
#define IWX_MAX_PHYS		(4)

/* Used to extract ID and color from the context dword */
#define IWX_FW_CTXT_ID_POS	(0)
#define IWX_FW_CTXT_ID_MSK	(0xff << IWX_FW_CTXT_ID_POS)
#define IWX_FW_CTXT_COLOR_POS	(8)
#define IWX_FW_CTXT_COLOR_MSK	(0xff << IWX_FW_CTXT_COLOR_POS)
#define IWX_FW_CTXT_INVALID	(0xffffffff)

#define IWX_FW_CMD_ID_AND_COLOR(_id, _color) ((_id << IWX_FW_CTXT_ID_POS) |\
					  (_color << IWX_FW_CTXT_COLOR_POS))

/* Possible actions on PHYs, MACs and Bindings */
#define IWX_FW_CTXT_ACTION_STUB		0
#define IWX_FW_CTXT_ACTION_ADD		1
#define IWX_FW_CTXT_ACTION_MODIFY	2
#define IWX_FW_CTXT_ACTION_REMOVE	3
#define IWX_FW_CTXT_ACTION_NUM		4
/* COMMON_CONTEXT_ACTION_API_E_VER_1 */

/* Time Events */

/* Time Event types, according to MAC type */

/* BSS Station Events */
#define IWX_TE_BSS_STA_AGGRESSIVE_ASSOC	0
#define IWX_TE_BSS_STA_ASSOC		1
#define IWX_TE_BSS_EAP_DHCP_PROT	2
#define IWX_TE_BSS_QUIET_PERIOD		3

/* P2P Device Events */
#define IWX_TE_P2P_DEVICE_DISCOVERABLE	4
#define IWX_TE_P2P_DEVICE_LISTEN	5
#define IWX_TE_P2P_DEVICE_ACTION_SCAN	6
#define IWX_TE_P2P_DEVICE_FULL_SCAN	7

/* P2P Client Events */
#define IWX_TE_P2P_CLIENT_AGGRESSIVE_ASSOC	8
#define IWX_TE_P2P_CLIENT_ASSOC			9
#define IWX_TE_P2P_CLIENT_QUIET_PERIOD		10

/* P2P GO Events */
#define IWX_TE_P2P_GO_ASSOC_PROT	11
#define IWX_TE_P2P_GO_REPETITIVE_NOA	12
#define IWX_TE_P2P_GO_CT_WINDOW		13

/* WiDi Sync Events */
#define IWX_TE_WIDI_TX_SYNC	14

/* Time event - defines for command API */

/**
 * DOC: Time Events - what is it?
 *
 * Time Events are a fw feature that allows the driver to control the presence
 * of the device on the channel. Since the fw supports multiple channels
 * concurrently, the fw may choose to jump to another channel at any time.
 * In order to make sure that the fw is on a specific channel at a certain time
 * and for a certain duration, the driver needs to issue a time event.
 *
 * The simplest example is for BSS association. The driver issues a time event,
 * waits for it to start, and only then tells mac80211 that we can start the
 * association. This way, we make sure that the association will be done
 * smoothly and won't be interrupted by channel switch decided within the fw.
 */

 /**
 * DOC: The flow against the fw
 *
 * When the driver needs to make sure we are in a certain channel, at a certain
 * time and for a certain duration, it sends a Time Event. The flow against the
 * fw goes like this:
 *	1) Driver sends a TIME_EVENT_CMD to the fw
 *	2) Driver gets the response for that command. This response contains the
 *	   Unique ID (UID) of the event.
 *	3) The fw sends notification when the event starts.
 *
 * Of course the API provides various options that allow to cover parameters
 * of the flow.
 *	What is the duration of the event?
 *	What is the start time of the event?
 *	Is there an end-time for the event?
 *	How much can the event be delayed?
 *	Can the event be split?
 *	If yes what is the maximal number of chunks?
 *	etc...
 */

/*
 * @IWX_TE_V2_FRAG_NONE: fragmentation of the time event is NOT allowed.
 * @IWX_TE_V2_FRAG_SINGLE: fragmentation of the time event is allowed, but only
 *  the first fragment is scheduled.
 * @IWX_TE_V2_FRAG_DUAL: fragmentation of the time event is allowed, but only
 *  the first 2 fragments are scheduled.
 * @IWX_TE_V2_FRAG_ENDLESS: fragmentation of the time event is allowed, and any
 *  number of fragments are valid.
 *
 * Other than the constant defined above, specifying a fragmentation value 'x'
 * means that the event can be fragmented but only the first 'x' will be
 * scheduled.
 */
#define IWX_TE_V2_FRAG_NONE		0
#define IWX_TE_V2_FRAG_SINGLE		1
#define IWX_TE_V2_FRAG_DUAL		2
#define IWX_TE_V2_FRAG_MAX		0xfe
#define IWX_TE_V2_FRAG_ENDLESS		0xff

/* Repeat the time event endlessly (until removed) */
#define IWX_TE_V2_REPEAT_ENDLESS	0xff
/* If a Time Event has bounded repetitions, this is the maximal value */
#define IWX_TE_V2_REPEAT_MAX	0xfe

#define IWX_TE_V2_PLACEMENT_POS	12
#define IWX_TE_V2_ABSENCE_POS	15

/* Time event policy values
 * A notification (both event and fragment) includes a status indicating weather
 * the FW was able to schedule the event or not. For fragment start/end
 * notification the status is always success. There is no start/end fragment
 * notification for monolithic events.
 *
 * @IWX_TE_V2_DEFAULT_POLICY: independent, social, present, unoticable
 * @IWX_TE_V2_NOTIF_HOST_EVENT_START: request/receive notification on event start
 * @IWX_TE_V2_NOTIF_HOST_EVENT_END:request/receive notification on event end
 * @IWX_TE_V2_NOTIF_INTERNAL_EVENT_START: internal FW use
 * @IWX_TE_V2_NOTIF_INTERNAL_EVENT_END: internal FW use.
 * @IWX_TE_V2_NOTIF_HOST_FRAG_START: request/receive notification on frag start
 * @IWX_TE_V2_NOTIF_HOST_FRAG_END:request/receive notification on frag end
 * @IWX_TE_V2_NOTIF_INTERNAL_FRAG_START: internal FW use.
 * @IWX_TE_V2_NOTIF_INTERNAL_FRAG_END: internal FW use.
 * @IWX_TE_V2_DEP_OTHER: depends on another time event
 * @IWX_TE_V2_DEP_TSF: depends on a specific time
 * @IWX_TE_V2_EVENT_SOCIOPATHIC: can't co-exist with other events of the same MAC
 * @IWX_TE_V2_ABSENCE: are we present or absent during the Time Event.
 */
#define IWX_TE_V2_DEFAULT_POLICY		0x0

/* notifications (event start/stop, fragment start/stop) */
#define IWX_TE_V2_NOTIF_HOST_EVENT_START	(1 << 0)
#define IWX_TE_V2_NOTIF_HOST_EVENT_END		(1 << 1)
#define IWX_TE_V2_NOTIF_INTERNAL_EVENT_START	(1 << 2)
#define IWX_TE_V2_NOTIF_INTERNAL_EVENT_END	(1 << 3)

#define IWX_TE_V2_NOTIF_HOST_FRAG_START		(1 << 4)
#define IWX_TE_V2_NOTIF_HOST_FRAG_END		(1 << 5)
#define IWX_TE_V2_NOTIF_INTERNAL_FRAG_START	(1 << 6)
#define IWX_TE_V2_NOTIF_INTERNAL_FRAG_END	(1 << 7)
#define IWX_T2_V2_START_IMMEDIATELY		(1 << 11)

#define IWX_TE_V2_NOTIF_MSK	0xff

/* placement characteristics */
#define IWX_TE_V2_DEP_OTHER		(1 << IWX_TE_V2_PLACEMENT_POS)
#define IWX_TE_V2_DEP_TSF		(1 << (IWX_TE_V2_PLACEMENT_POS + 1))
#define IWX_TE_V2_EVENT_SOCIOPATHIC	(1 << (IWX_TE_V2_PLACEMENT_POS + 2))

/* are we present or absent during the Time Event. */
#define IWX_TE_V2_ABSENCE		(1 << IWX_TE_V2_ABSENCE_POS)

/**
 * struct iwx_time_event_cmd_api - configuring Time Events
 * with struct IWX_MAC_TIME_EVENT_DATA_API_S_VER_2 (see also
 * with version 1. determined by IWX_UCODE_TLV_FLAGS)
 * ( IWX_TIME_EVENT_CMD = 0x29 )
 * @id_and_color: ID and color of the relevant MAC
 * @action: action to perform, one of IWX_FW_CTXT_ACTION_*
 * @id: this field has two meanings, depending on the action:
 *	If the action is ADD, then it means the type of event to add.
 *	For all other actions it is the unique event ID assigned when the
 *	event was added by the FW.
 * @apply_time: When to start the Time Event (in GP2)
 * @max_delay: maximum delay to event's start (apply time), in TU
 * @depends_on: the unique ID of the event we depend on (if any)
 * @interval: interval between repetitions, in TU
 * @duration: duration of event in TU
 * @repeat: how many repetitions to do, can be IWX_TE_REPEAT_ENDLESS
 * @max_frags: maximal number of fragments the Time Event can be divided to
 * @policy: defines whether uCode shall notify the host or other uCode modules
 *	on event and/or fragment start and/or end
 *	using one of IWX_TE_INDEPENDENT, IWX_TE_DEP_OTHER, IWX_TE_DEP_TSF
 *	IWX_TE_EVENT_SOCIOPATHIC
 *	using IWX_TE_ABSENCE and using IWX_TE_NOTIF_*
 */
struct iwx_time_event_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	uint32_t id;
	/* IWX_MAC_TIME_EVENT_DATA_API_S_VER_2 */
	uint32_t apply_time;
	uint32_t max_delay;
	uint32_t depends_on;
	uint32_t interval;
	uint32_t duration;
	uint8_t repeat;
	uint8_t max_frags;
	uint16_t policy;
} __packed; /* IWX_MAC_TIME_EVENT_CMD_API_S_VER_2 */

/**
 * struct iwx_time_event_resp - response structure to iwx_time_event_cmd
 * @status: bit 0 indicates success, all others specify errors
 * @id: the Time Event type
 * @unique_id: the unique ID assigned (in ADD) or given (others) to the TE
 * @id_and_color: ID and color of the relevant MAC
 */
struct iwx_time_event_resp {
	uint32_t status;
	uint32_t id;
	uint32_t unique_id;
	uint32_t id_and_color;
} __packed; /* IWX_MAC_TIME_EVENT_RSP_API_S_VER_1 */

/**
 * struct iwx_time_event_notif - notifications of time event start/stop
 * ( IWX_TIME_EVENT_NOTIFICATION = 0x2a )
 * @timestamp: action timestamp in GP2
 * @session_id: session's unique id
 * @unique_id: unique id of the Time Event itself
 * @id_and_color: ID and color of the relevant MAC
 * @action: one of IWX_TE_NOTIF_START or IWX_TE_NOTIF_END
 * @status: true if scheduled, false otherwise (not executed)
 */
struct iwx_time_event_notif {
	uint32_t timestamp;
	uint32_t session_id;
	uint32_t unique_id;
	uint32_t id_and_color;
	uint32_t action;
	uint32_t status;
} __packed; /* IWX_MAC_TIME_EVENT_NTFY_API_S_VER_1 */

/**
 * enum iwx_session_prot_conf_id - session protection's configurations
 * @SESSION_PROTECT_CONF_ASSOC: Start a session protection for association.
 *	The firmware will allocate two events.
 *	Valid for BSS_STA and P2P_STA.
 *	* A rather short event that can't be fragmented and with a very
 *	high priority. If every goes well (99% of the cases) the
 *	association should complete within this first event. During
 *	that event, no other activity will happen in the firmware,
 *	which is why it can't be too long.
 *	The length of this event is hard-coded in the firmware: 300TUs.
 *	* Another event which can be much longer (its duration is
 *	configurable by the driver) which has a slightly lower
 *	priority and that can be fragmented allowing other activities
 *	to run while this event is running.
 *	The firmware will automatically remove both events once the driver sets
 *	the BSS MAC as associated. Neither of the events will be removed
 *	for the P2P_STA MAC.
 *	Only the duration is configurable for this protection.
 * @SESSION_PROTECT_CONF_GO_CLIENT_ASSOC: not used
 * @SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV: Schedule the P2P Device to be in
 *	listen mode. Will be fragmented. Valid only on the P2P Device MAC.
 *	Valid only on the P2P Device MAC. The firmware will take into account
 *	the duration, the interval and the repetition count.
 * @SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION: Schedule the P2P Device to be be
 *	able to run the GO Negotiation. Will not be fragmented and not
 *	repetitive. Valid only on the P2P Device MAC. Only the duration will
 *	be taken into account.
 * @SESSION_PROTECT_CONF_MAX_ID: not used
 */
enum iwx_session_prot_conf_id {
	IWX_SESSION_PROTECT_CONF_ASSOC,
	IWX_SESSION_PROTECT_CONF_GO_CLIENT_ASSOC,
	IWX_SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV,
	IWX_SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION,
	IWX_SESSION_PROTECT_CONF_MAX_ID,
}; /* SESSION_PROTECTION_CONF_ID_E_VER_1 */

/**
 * struct iwx_session_prot_cmd - configure a session protection
 * @id_and_color: the id and color of the mac for which this session protection
 *	is sent
 * @action: can be either FW_CTXT_ACTION_ADD or FW_CTXT_ACTION_REMOVE
 * @conf_id: see &enum iwx_mvm_session_prot_conf_id
 * @duration_tu: the duration of the whole protection in TUs.
 * @repetition_count: not used
 * @interval: not used
 *
 * Note: the session protection will always be scheduled to start as
 * early as possible, but the maximum delay is configuration dependent.
 * The firmware supports only one concurrent session protection per vif.
 * Adding a new session protection will remove any currently running session.
 */
struct iwx_session_prot_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 hdr */
	uint32_t id_and_color;
	uint32_t action;
	uint32_t conf_id;
	uint32_t duration_tu;
	uint32_t repetition_count;
	uint32_t interval;
} __packed; /* SESSION_PROTECTION_CMD_API_S_VER_1 */

/**
 * struct iwx_session_prot_notif - session protection started / ended
 * @mac_id: the mac id for which the session protection started / ended
 * @status: 1 means success, 0 means failure
 * @start: 1 means the session protection started, 0 means it ended
 * @conf_id: see &enum iwx_mvm_session_prot_conf_id
 *
 * Note that any session protection will always get two notifications: start
 * and end even the firmware could not schedule it.
 */
struct iwx_session_prot_notif {
	uint32_t mac_id;
	uint32_t status;
	uint32_t start;
	uint32_t conf_id;
} __packed; /* SESSION_PROTECTION_NOTIFICATION_API_S_VER_2 */


/* Bindings and Time Quota */

/**
 * struct iwx_binding_cmd - configuring bindings
 * ( IWX_BINDING_CONTEXT_CMD = 0x2b )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWX_FW_CTXT_ACTION_*
 * @macs: array of MAC id and colors which belong to the binding
 * @phy: PHY id and color which belongs to the binding
 * @lmac_id: the lmac id the binding belongs to
 */
struct iwx_binding_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWX_BINDING_DATA_API_S_VER_1 */
	uint32_t macs[IWX_MAX_MACS_IN_BINDING];
	uint32_t phy;
	uint32_t lmac_id;
} __packed; /* IWX_BINDING_CMD_API_S_VER_2 */

#define IWX_LMAC_24G_INDEX		0
#define IWX_LMAC_5G_INDEX		1

/* The maximal number of fragments in the FW's schedule session */
#define IWX_MAX_QUOTA 128

/**
 * struct iwx_time_quota_data - configuration of time quota per binding
 * @id_and_color: ID and color of the relevant Binding
 * @quota: absolute time quota in TU. The scheduler will try to divide the
 *	remaining quota (after Time Events) according to this quota.
 * @max_duration: max uninterrupted context duration in TU
 */
struct iwx_time_quota_data {
	uint32_t id_and_color;
	uint32_t quota;
	uint32_t max_duration;
} __packed; /* IWX_TIME_QUOTA_DATA_API_S_VER_1 */

/**
 * struct iwx_time_quota_cmd - configuration of time quota between bindings
 * ( IWX_TIME_QUOTA_CMD = 0x2c )
 * @quotas: allocations per binding
 */
struct iwx_time_quota_cmd {
	struct iwx_time_quota_data quotas[IWX_MAX_BINDINGS];
} __packed; /* IWX_TIME_QUOTA_ALLOCATION_CMD_API_S_VER_1 */


/* PHY context */

/* Supported bands */
#define IWX_PHY_BAND_5  (0)
#define IWX_PHY_BAND_24 (1)

/* Supported channel width, vary if there is VHT support */
#define IWX_PHY_VHT_CHANNEL_MODE20	(0x0)
#define IWX_PHY_VHT_CHANNEL_MODE40	(0x1)
#define IWX_PHY_VHT_CHANNEL_MODE80	(0x2)
#define IWX_PHY_VHT_CHANNEL_MODE160	(0x3)

/*
 * Control channel position:
 * For legacy set bit means upper channel, otherwise lower.
 * For VHT - bit-2 marks if the control is lower/upper relative to center-freq
 *   bits-1:0 mark the distance from the center freq. for 20Mhz, offset is 0.
 *                                   center_freq
 *                                        |
 * 40Mhz                          |_______|_______|
 * 80Mhz                  |_______|_______|_______|_______|
 * 160Mhz |_______|_______|_______|_______|_______|_______|_______|_______|
 * code      011     010     001     000  |  100     101     110    111
 */
#define IWX_PHY_VHT_CTRL_POS_1_BELOW  (0x0)
#define IWX_PHY_VHT_CTRL_POS_2_BELOW  (0x1)
#define IWX_PHY_VHT_CTRL_POS_3_BELOW  (0x2)
#define IWX_PHY_VHT_CTRL_POS_4_BELOW  (0x3)
#define IWX_PHY_VHT_CTRL_POS_1_ABOVE  (0x4)
#define IWX_PHY_VHT_CTRL_POS_2_ABOVE  (0x5)
#define IWX_PHY_VHT_CTRL_POS_3_ABOVE  (0x6)
#define IWX_PHY_VHT_CTRL_POS_4_ABOVE  (0x7)

/*
 * @band: IWX_PHY_BAND_*
 * @channel: channel number
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 */
struct iwx_fw_channel_info_v1 {
	uint8_t band;
	uint8_t channel;
	uint8_t width;
	uint8_t ctrl_pos;
} __packed; /* CHANNEL_CONFIG_API_S_VER_1 */

/*
 * struct iwx_fw_channel_info - channel information
 *
 * @channel: channel number
 * @band: PHY_BAND_*
 * @width: PHY_[VHT|LEGACY]_CHANNEL_*
 * @ctrl channel: PHY_[VHT|LEGACY]_CTRL_*
 * @reserved: for future use and alignment
 */
struct iwx_fw_channel_info {
	uint32_t channel;
	uint8_t band;
	uint8_t width;
	uint8_t ctrl_pos;
	uint8_t reserved;
} __packed; /*CHANNEL_CONFIG_API_S_VER_2 */

#define IWX_PHY_RX_CHAIN_DRIVER_FORCE_POS	(0)
#define IWX_PHY_RX_CHAIN_DRIVER_FORCE_MSK \
	(0x1 << IWX_PHY_RX_CHAIN_DRIVER_FORCE_POS)
#define IWX_PHY_RX_CHAIN_VALID_POS		(1)
#define IWX_PHY_RX_CHAIN_VALID_MSK \
	(0x7 << IWX_PHY_RX_CHAIN_VALID_POS)
#define IWX_PHY_RX_CHAIN_FORCE_SEL_POS	(4)
#define IWX_PHY_RX_CHAIN_FORCE_SEL_MSK \
	(0x7 << IWX_PHY_RX_CHAIN_FORCE_SEL_POS)
#define IWX_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS	(7)
#define IWX_PHY_RX_CHAIN_FORCE_MIMO_SEL_MSK \
	(0x7 << IWX_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS)
#define IWX_PHY_RX_CHAIN_CNT_POS		(10)
#define IWX_PHY_RX_CHAIN_CNT_MSK \
	(0x3 << IWX_PHY_RX_CHAIN_CNT_POS)
#define IWX_PHY_RX_CHAIN_MIMO_CNT_POS	(12)
#define IWX_PHY_RX_CHAIN_MIMO_CNT_MSK \
	(0x3 << IWX_PHY_RX_CHAIN_MIMO_CNT_POS)
#define IWX_PHY_RX_CHAIN_MIMO_FORCE_POS	(14)
#define IWX_PHY_RX_CHAIN_MIMO_FORCE_MSK \
	(0x1 << IWX_PHY_RX_CHAIN_MIMO_FORCE_POS)

/* TODO: fix the value, make it depend on firmware at runtime? */
#define IWX_NUM_PHY_CTX	3

/**
 * struct iwl_phy_context_cmd - config of the PHY context
 * ( IWX_PHY_CONTEXT_CMD = 0x8 )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWX_FW_CTXT_ACTION_*
 * @lmac_id: the lmac id the phy context belongs to
 * @ci: channel info
 * @rxchain_info: ???
 * @dsp_cfg_flags: set to 0
 * @reserved: reserved to align to 64 bit
 */
struct iwx_phy_context_cmd_uhb {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* PHY_CONTEXT_DATA_API_S_VER_3 */
	struct iwx_fw_channel_info ci;
	uint32_t lmac_id;
	uint32_t rxchain_info;
	uint32_t dsp_cfg_flags;
	uint32_t reserved;
} __packed; /* PHY_CONTEXT_CMD_API_VER_3 */
struct iwx_phy_context_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* PHY_CONTEXT_DATA_API_S_VER_3, PHY_CONTEXT_DATA_API_S_VER_4 */
	struct iwx_fw_channel_info_v1 ci;
	uint32_t lmac_id;
	uint32_t rxchain_info; /* reserved in _VER_4 */
	uint32_t dsp_cfg_flags;
	uint32_t reserved;
} __packed; /* PHY_CONTEXT_CMD_API_VER_3, PHY_CONTEXT_CMD_API_VER_4 */

/* TODO: complete missing documentation */
/**
 * struct iwx_phy_context_cmd - config of the PHY context
 * ( IWX_PHY_CONTEXT_CMD = 0x8 )
 * @id_and_color: ID and color of the relevant Binding
 * @action: action to perform, one of IWX_FW_CTXT_ACTION_*
 * @apply_time: 0 means immediate apply and context switch.
 *	other value means apply new params after X usecs
 * @tx_param_color: ???
 * @channel_info:
 * @txchain_info: ???
 * @rxchain_info: ???
 * @acquisition_data: ???
 * @dsp_cfg_flags: set to 0
 */
/*
 * XXX Intel forgot to bump the PHY_CONTEXT command API when they increased
 * the size of fw_channel_info from v1 to v2.
 * To keep things simple we define two versions of this struct, and both
 * are labeled as CMD_API_VER_1. (The Linux iwlwifi driver performs dark
 * magic with pointers to struct members instead.)
 */
/* This version must be used if IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS is set: */
struct iwx_phy_context_cmd_uhb_v1 {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWX_PHY_CONTEXT_DATA_API_S_VER_1 */
	uint32_t apply_time;
	uint32_t tx_param_color;
	struct iwx_fw_channel_info ci;
	uint32_t txchain_info;
	uint32_t rxchain_info;
	uint32_t acquisition_data;
	uint32_t dsp_cfg_flags;
} __packed; /* IWX_PHY_CONTEXT_CMD_API_VER_1 */
/* This version must be used otherwise: */
struct iwx_phy_context_cmd_v1 {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWX_PHY_CONTEXT_DATA_API_S_VER_1 */
	uint32_t apply_time;
	uint32_t tx_param_color;
	struct iwx_fw_channel_info_v1 ci;
	uint32_t txchain_info;
	uint32_t rxchain_info;
	uint32_t acquisition_data;
	uint32_t dsp_cfg_flags;
} __packed; /* IWX_PHY_CONTEXT_CMD_API_VER_1 */


#define IWX_RX_INFO_PHY_CNT 8
#define IWX_RX_INFO_ENERGY_ANT_ABC_IDX 1
#define IWX_RX_INFO_ENERGY_ANT_A_MSK 0x000000ff
#define IWX_RX_INFO_ENERGY_ANT_B_MSK 0x0000ff00
#define IWX_RX_INFO_ENERGY_ANT_C_MSK 0x00ff0000
#define IWX_RX_INFO_ENERGY_ANT_A_POS 0
#define IWX_RX_INFO_ENERGY_ANT_B_POS 8
#define IWX_RX_INFO_ENERGY_ANT_C_POS 16

#define IWX_RX_INFO_AGC_IDX 1
#define IWX_RX_INFO_RSSI_AB_IDX 2
#define IWX_OFDM_AGC_A_MSK 0x0000007f
#define IWX_OFDM_AGC_A_POS 0
#define IWX_OFDM_AGC_B_MSK 0x00003f80
#define IWX_OFDM_AGC_B_POS 7
#define IWX_OFDM_AGC_CODE_MSK 0x3fe00000
#define IWX_OFDM_AGC_CODE_POS 20
#define IWX_OFDM_RSSI_INBAND_A_MSK 0x00ff
#define IWX_OFDM_RSSI_A_POS 0
#define IWX_OFDM_RSSI_ALLBAND_A_MSK 0xff00
#define IWX_OFDM_RSSI_ALLBAND_A_POS 8
#define IWX_OFDM_RSSI_INBAND_B_MSK 0xff0000
#define IWX_OFDM_RSSI_B_POS 16
#define IWX_OFDM_RSSI_ALLBAND_B_MSK 0xff000000
#define IWX_OFDM_RSSI_ALLBAND_B_POS 24

/**
 * struct iwx_rx_phy_info - phy info
 * (IWX_REPLY_RX_PHY_CMD = 0xc0)
 * @non_cfg_phy_cnt: non configurable DSP phy data byte count
 * @cfg_phy_cnt: configurable DSP phy data byte count
 * @stat_id: configurable DSP phy data set ID
 * @reserved1:
 * @system_timestamp: GP2  at on air rise
 * @timestamp: TSF at on air rise
 * @beacon_time_stamp: beacon at on-air rise
 * @phy_flags: general phy flags: band, modulation, ...
 * @channel: channel number
 * @non_cfg_phy_buf: for various implementations of non_cfg_phy
 * @rate_n_flags: IWX_RATE_MCS_*
 * @byte_count: frame's byte-count
 * @frame_time: frame's time on the air, based on byte count and frame rate
 *	calculation
 * @mac_active_msk: what MACs were active when the frame was received
 *
 * Before each Rx, the device sends this data. It contains PHY information
 * about the reception of the packet.
 */
struct iwx_rx_phy_info {
	uint8_t non_cfg_phy_cnt;
	uint8_t cfg_phy_cnt;
	uint8_t stat_id;
	uint8_t reserved1;
	uint32_t system_timestamp;
	uint64_t timestamp;
	uint32_t beacon_time_stamp;
	uint16_t phy_flags;
#define IWX_PHY_INFO_FLAG_SHPREAMBLE	(1 << 2)
	uint16_t channel;
	uint32_t non_cfg_phy[IWX_RX_INFO_PHY_CNT];
	uint32_t rate_n_flags;
	uint32_t byte_count;
	uint16_t mac_active_msk;
	uint16_t frame_time;
} __packed;

struct iwx_rx_mpdu_res_start {
	uint16_t byte_count;
	uint16_t reserved;
} __packed;

/**
 * Values to parse %iwx_rx_phy_info phy_flags
 * @IWX_RX_RES_PHY_FLAGS_BAND_24: true if the packet was received on 2.4 band
 * @IWX_RX_RES_PHY_FLAGS_MOD_CCK:
 * @IWX_RX_RES_PHY_FLAGS_SHORT_PREAMBLE: true if packet's preamble was short
 * @IWX_RX_RES_PHY_FLAGS_NARROW_BAND:
 * @IWX_RX_RES_PHY_FLAGS_ANTENNA: antenna on which the packet was received
 * @IWX_RX_RES_PHY_FLAGS_AGG: set if the packet was part of an A-MPDU
 * @IWX_RX_RES_PHY_FLAGS_OFDM_HT: The frame was an HT frame
 * @IWX_RX_RES_PHY_FLAGS_OFDM_GF: The frame used GF preamble
 * @IWX_RX_RES_PHY_FLAGS_OFDM_VHT: The frame was a VHT frame
 */
#define IWX_RX_RES_PHY_FLAGS_BAND_24		(1 << 0)
#define IWX_RX_RES_PHY_FLAGS_MOD_CCK		(1 << 1)
#define IWX_RX_RES_PHY_FLAGS_SHORT_PREAMBLE	(1 << 2)
#define IWX_RX_RES_PHY_FLAGS_NARROW_BAND	(1 << 3)
#define IWX_RX_RES_PHY_FLAGS_ANTENNA		(0x7 << 4)
#define IWX_RX_RES_PHY_FLAGS_ANTENNA_POS	4
#define IWX_RX_RES_PHY_FLAGS_AGG		(1 << 7)
#define IWX_RX_RES_PHY_FLAGS_OFDM_HT		(1 << 8)
#define IWX_RX_RES_PHY_FLAGS_OFDM_GF		(1 << 9)
#define IWX_RX_RES_PHY_FLAGS_OFDM_VHT		(1 << 10)

/**
 * Values written by fw for each Rx packet
 * @IWX_RX_MPDU_RES_STATUS_CRC_OK: CRC is fine
 * @IWX_RX_MPDU_RES_STATUS_OVERRUN_OK: there was no RXE overflow
 * @IWX_RX_MPDU_RES_STATUS_SRC_STA_FOUND:
 * @IWX_RX_MPDU_RES_STATUS_KEY_VALID:
 * @IWX_RX_MPDU_RES_STATUS_KEY_PARAM_OK:
 * @IWX_RX_MPDU_RES_STATUS_ICV_OK: ICV is fine, if not, the packet is destroyed
 * @IWX_RX_MPDU_RES_STATUS_MIC_OK: used for CCM alg only. TKIP MIC is checked
 *	in the driver.
 * @IWX_RX_MPDU_RES_STATUS_TTAK_OK: TTAK is fine
 * @IWX_RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR:  valid for alg = CCM_CMAC or
 *	alg = CCM only. Checks replay attack for 11w frames. Relevant only if
 *	%IWX_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME is set.
 * @IWX_RX_MPDU_RES_STATUS_SEC_NO_ENC: this frame is not encrypted
 * @IWX_RX_MPDU_RES_STATUS_SEC_WEP_ENC: this frame is encrypted using WEP
 * @IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC: this frame is encrypted using CCM
 * @IWX_RX_MPDU_RES_STATUS_SEC_TKIP_ENC: this frame is encrypted using TKIP
 * @IWX_RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC: this frame is encrypted using CCM_CMAC
 * @IWX_RX_MPDU_RES_STATUS_SEC_ENC_ERR: this frame couldn't be decrypted
 * @IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK: bitmask of the encryption algorithm
 * @IWX_RX_MPDU_RES_STATUS_DEC_DONE: this frame has been successfully decrypted
 * @IWX_RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP:
 * @IWX_RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP:
 * @IWX_RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT:
 * @IWX_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME: this frame is an 11w management frame
 * @IWX_RX_MPDU_RES_STATUS_HASH_INDEX_MSK:
 * @IWX_RX_MPDU_RES_STATUS_STA_ID_MSK:
 * @IWX_RX_MPDU_RES_STATUS_RRF_KILL:
 * @IWX_RX_MPDU_RES_STATUS_FILTERING_MSK:
 * @IWX_RX_MPDU_RES_STATUS2_FILTERING_MSK:
 */
#define IWX_RX_MPDU_RES_STATUS_CRC_OK			(1 << 0)
#define IWX_RX_MPDU_RES_STATUS_OVERRUN_OK		(1 << 1)
#define IWX_RX_MPDU_RES_STATUS_SRC_STA_FOUND		(1 << 2)
#define IWX_RX_MPDU_RES_STATUS_KEY_VALID		(1 << 3)
#define IWX_RX_MPDU_RES_STATUS_KEY_PARAM_OK		(1 << 4)
#define IWX_RX_MPDU_RES_STATUS_ICV_OK			(1 << 5)
#define IWX_RX_MPDU_RES_STATUS_MIC_OK			(1 << 6)
#define IWX_RX_MPDU_RES_STATUS_TTAK_OK			(1 << 7)
#define IWX_RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR	(1 << 7)
#define IWX_RX_MPDU_RES_STATUS_SEC_NO_ENC		(0 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_WEP_ENC		(1 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_CCM_ENC		(2 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_TKIP_ENC		(3 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_EXT_ENC		(4 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC		(6 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_ENC_ERR		(7 << 8)
#define IWX_RX_MPDU_RES_STATUS_SEC_ENC_MSK		(7 << 8)
#define IWX_RX_MPDU_RES_STATUS_DEC_DONE			(1 << 11)
#define IWX_RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP	(1 << 12)
#define IWX_RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP		(1 << 13)
#define IWX_RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT		(1 << 14)
#define IWX_RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME		(1 << 15)
#define IWX_RX_MPDU_RES_STATUS_HASH_INDEX_MSK		(0x3F0000)
#define IWX_RX_MPDU_RES_STATUS_STA_ID_MSK		(0x1f000000)
#define IWX_RX_MPDU_RES_STATUS_RRF_KILL			(1 << 29)
#define IWX_RX_MPDU_RES_STATUS_FILTERING_MSK		(0xc00000)
#define IWX_RX_MPDU_RES_STATUS2_FILTERING_MSK		(0xc0000000)

#define IWX_RX_MPDU_MFLG1_ADDRTYPE_MASK		0x03
#define IWX_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK	0xf0
#define IWX_RX_MPDU_MFLG1_MIC_CRC_LEN_SHIFT	3

#define IWX_RX_MPDU_MFLG2_HDR_LEN_MASK		0x1f
#define	IWX_RX_MPDU_MFLG2_PAD			0x20
#define IWX_RX_MPDU_MFLG2_AMSDU			0x40

#define IWX_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK	0x7f
#define IWX_RX_MPDU_AMSDU_LAST_SUBFRAME		0x80

#define IWX_RX_MPDU_PHY_AMPDU			(1 << 5)
#define IWX_RX_MPDU_PHY_AMPDU_TOGGLE		(1 << 6)
#define IWX_RX_MPDU_PHY_SHORT_PREAMBLE		(1 << 7)
#define IWX_RX_MPDU_PHY_NCCK_ADDTL_NTFY		(1 << 7)
#define IWX_RX_MPDU_PHY_TSF_OVERLOAD		(1 << 8)

struct iwx_rx_mpdu_desc_v3 {
	union {
		uint32_t filter_match;
		uint32_t phy_data3;
	};
	union {
		uint32_t rss_hash;
		uint32_t phy_data2;
	};
	uint32_t partial_hash; /* ip/tcp header hash w/o some fields */
	uint16_t raw_xsum;
	uint16_t reserved_xsum;
	uint32_t rate_n_flags;
	uint8_t energy_a;
	uint8_t energy_b;
	uint8_t channel;
	uint8_t mac_context;
	uint32_t gp2_on_air_rise;
	union {
		/*
		 * TSF value on air rise (INA), only valid if
		 * IWX_RX_MPDU_PHY_TSF_OVERLOAD isn't set
		 */
		struct {
			uint32_t tsf_on_air_rise0;
			uint32_t tsf_on_air_rise1;
		};

		struct {
			uint32_t phy_data0;

			/* Only valid if IWX_RX_MPDU_PHY_TSF_OVERLOAD is set. */
			uint32_t phy_data1;
		};
	};
	uint32_t reserved[2];
} __packed; /* RX_MPDU_RES_START_API_S_VER_3,
	       RX_MPDU_RES_START_API_S_VER_5 */

struct iwx_rx_mpdu_desc_v1 {
	union {
		uint32_t rss_hash;
		uint32_t phy_data2;
	};
	union {
		uint32_t filter_match;
		uint32_t phy_data3;
	};
	uint32_t rate_n_flags;
	uint8_t energy_a;
	uint8_t energy_b;
	uint8_t channel;
	uint8_t mac_context;
	uint32_t gp2_on_air_rise;
	union {
		struct {
			uint32_t tsf_on_air_rise0;
			uint32_t tsf_on_air_rise1;
		};
		struct {
			uint32_t phy_data0;
			uint32_t phy_data1;
		};
	};
} __packed;

#define IWX_RX_REORDER_DATA_INVALID_BAID	0x7f

#define IWX_RX_MPDU_REORDER_NSSN_MASK		0x00000fff
#define IWX_RX_MPDU_REORDER_SN_MASK		0x00fff000
#define IWX_RX_MPDU_REORDER_SN_SHIFT		12
#define IWX_RX_MPDU_REORDER_BAID_MASK		0x7f000000
#define IWX_RX_MPDU_REORDER_BAID_SHIFT		24
#define IWX_RX_MPDU_REORDER_BA_OLD_SN		0x80000000

struct iwx_rx_mpdu_desc {
	uint16_t mpdu_len;
	uint8_t mac_flags1;
	uint8_t mac_flags2;
	uint8_t amsdu_info;
	uint16_t phy_info;
	uint8_t mac_phy_idx;
	uint16_t raw_csum;
	union {
		uint16_t l3l4_flags;
		uint16_t phy_data4;
	};
	uint16_t status;
	uint8_t hash_filter;
	uint8_t sta_id_flags;
	uint32_t reorder_data;
	union {
		struct iwx_rx_mpdu_desc_v1 v1;
		struct iwx_rx_mpdu_desc_v3 v3;
	};
} __packed; /* RX_MPDU_RES_START_API_S_VER_3,
	       RX_MPDU_RES_START_API_S_VER_4,
	       RX_MPDU_RES_START_API_S_VER_5 */

#define IWX_RX_DESC_SIZE_V1 ((sizeof(struct iwx_rx_mpdu_desc) - \
    sizeof(struct iwx_rx_mpdu_desc_v3)) + sizeof(struct iwx_rx_mpdu_desc_v1))

struct iwx_frame_release {
	uint8_t baid;
	uint8_t reserved;
	uint16_t nssn;
};

/**
 * enum iwx_bar_frame_release_sta_tid - STA/TID information for BAR release
 * @IWX_BAR_FRAME_RELEASE_TID_MASK: TID mask
 * @IWX_BAR_FRAME_RELEASE_STA_MASK: STA mask
 */
#define IWX_BAR_FRAME_RELEASE_TID_MASK	0x0000000f
#define IWX_BAR_FRAME_RELEASE_STA_MASK	0x000001f0
#define IWX_BAR_FRAME_RELEASE_STA_SHIFT	4

/**
 * enum iwx_bar_frame_release_ba_info - BA information for BAR release
 * @IWL_BAR_FRAME_RELEASE_NSSN_MASK: NSSN mask
 * @IWL_BAR_FRAME_RELEASE_SN_MASK: SN mask (ignored by driver)
 * @IWL_BAR_FRAME_RELEASE_BAID_MASK: BAID mask
 */
#define IWX_BAR_FRAME_RELEASE_NSSN_MASK		0x00000fff
#define IWX_BAR_FRAME_RELEASE_SN_MASK		0x00fff000
#define IWX_BAR_FRAME_RELEASE_SN_SHIFT		12
#define IWX_BAR_FRAME_RELEASE_BAID_MASK		0x3f000000
#define IWX_BAR_FRAME_RELEASE_BAID_SHIFT	24

/**
 * struct iwx_bar_frame_release - frame release from BAR info
 * @sta_tid: STA & TID information, see &enum iwx_bar_frame_release_sta_tid.
 * @ba_info: BA information, see &enum iwx_bar_frame_release_ba_info.
 */
struct iwx_bar_frame_release {
	uint32_t sta_tid;
	uint32_t ba_info;
} __packed; /* RX_BAR_TO_FRAME_RELEASE_API_S_VER_1 */

/**
 * struct iwx_radio_version_notif - information on the radio version
 * ( IWX_RADIO_VERSION_NOTIFICATION = 0x68 )
 * @radio_flavor:
 * @radio_step:
 * @radio_dash:
 */
struct iwx_radio_version_notif {
	uint32_t radio_flavor;
	uint32_t radio_step;
	uint32_t radio_dash;
} __packed; /* IWX_RADIO_VERSION_NOTOFICATION_S_VER_1 */

#define IWX_CARD_ENABLED		0x00
#define IWX_HW_CARD_DISABLED		0x01
#define IWX_SW_CARD_DISABLED		0x02
#define IWX_CT_KILL_CARD_DISABLED	0x04
#define IWX_HALT_CARD_DISABLED		0x08
#define IWX_CARD_DISABLED_MSK		0x0f
#define IWX_CARD_IS_RX_ON		0x10

/**
 * struct iwx_radio_version_notif - information on the radio version
 * (IWX_CARD_STATE_NOTIFICATION = 0xa1 )
 * @flags: %iwx_card_state_flags
 */
struct iwx_card_state_notif {
	uint32_t flags;
} __packed; /* CARD_STATE_NTFY_API_S_VER_1 */

/**
 * struct iwx_missed_beacons_notif - information on missed beacons
 * ( IWX_MISSED_BEACONS_NOTIFICATION = 0xa2 )
 * @mac_id: interface ID
 * @consec_missed_beacons_since_last_rx: number of consecutive missed
 *	beacons since last RX.
 * @consec_missed_beacons: number of consecutive missed beacons
 * @num_expected_beacons:
 * @num_recvd_beacons:
 */
struct iwx_missed_beacons_notif {
	uint32_t mac_id;
	uint32_t consec_missed_beacons_since_last_rx;
	uint32_t consec_missed_beacons;
	uint32_t num_expected_beacons;
	uint32_t num_recvd_beacons;
} __packed; /* IWX_MISSED_BEACON_NTFY_API_S_VER_3 */

/**
 * struct iwx_mfuart_load_notif - mfuart image version & status
 * ( IWX_MFUART_LOAD_NOTIFICATION = 0xb1 )
 * @installed_ver: installed image version
 * @external_ver: external image version
 * @status: MFUART loading status
 * @duration: MFUART loading time
*/
struct iwx_mfuart_load_notif {
	uint32_t installed_ver;
	uint32_t external_ver;
	uint32_t status;
	uint32_t duration;
} __packed; /*MFU_LOADER_NTFY_API_S_VER_1*/

/**
 * struct iwx_set_calib_default_cmd - set default value for calibration.
 * ( IWX_SET_CALIB_DEFAULT_CMD = 0x8e )
 * @calib_index: the calibration to set value for
 * @length: of data
 * @data: the value to set for the calibration result
 */
struct iwx_set_calib_default_cmd {
	uint16_t calib_index;
	uint16_t length;
	uint8_t data[0];
} __packed; /* IWX_PHY_CALIB_OVERRIDE_VALUES_S */

#define IWX_MAX_PORT_ID_NUM	2
#define IWX_MAX_MCAST_FILTERING_ADDRESSES 256

/**
 * struct iwx_mcast_filter_cmd - configure multicast filter.
 * @filter_own: Set 1 to filter out multicast packets sent by station itself
 * @port_id:	Multicast MAC addresses array specifier. This is a strange way
 *		to identify network interface adopted in host-device IF.
 *		It is used by FW as index in array of addresses. This array has
 *		IWX_MAX_PORT_ID_NUM members.
 * @count:	Number of MAC addresses in the array
 * @pass_all:	Set 1 to pass all multicast packets.
 * @bssid:	current association BSSID.
 * @addr_list:	Place holder for array of MAC addresses.
 *		IMPORTANT: add padding if necessary to ensure DWORD alignment.
 */
struct iwx_mcast_filter_cmd {
	uint8_t filter_own;
	uint8_t port_id;
	uint8_t count;
	uint8_t pass_all;
	uint8_t bssid[6];
	uint8_t reserved[2];
	uint8_t addr_list[0];
} __packed; /* IWX_MCAST_FILTERING_CMD_API_S_VER_1 */

struct iwx_statistics_dbg {
	uint32_t burst_check;
	uint32_t burst_count;
	uint32_t wait_for_silence_timeout_cnt;
	uint32_t reserved[3];
} __packed; /* IWX_STATISTICS_DEBUG_API_S_VER_2 */

struct iwx_statistics_div {
	uint32_t tx_on_a;
	uint32_t tx_on_b;
	uint32_t exec_time;
	uint32_t probe_time;
	uint32_t rssi_ant;
	uint32_t reserved2;
} __packed; /* IWX_STATISTICS_SLOW_DIV_API_S_VER_2 */

struct iwx_statistics_bt_activity {
	uint32_t hi_priority_tx_req_cnt;
	uint32_t hi_priority_tx_denied_cnt;
	uint32_t lo_priority_tx_req_cnt;
	uint32_t lo_priority_tx_denied_cnt;
	uint32_t hi_priority_rx_req_cnt;
	uint32_t hi_priority_rx_denied_cnt;
	uint32_t lo_priority_rx_req_cnt;
	uint32_t lo_priority_rx_denied_cnt;
} __packed;  /* IWX_STATISTICS_BT_ACTIVITY_API_S_VER_1 */

struct iwx_statistics_general_common {
	uint32_t radio_temperature;
	struct iwx_statistics_dbg dbg;
	uint32_t sleep_time;
	uint32_t slots_out;
	uint32_t slots_idle;
	uint32_t ttl_timestamp;
	struct iwx_statistics_div slow_div;
	uint32_t rx_enable_counter;
	/*
	 * num_of_sos_states:
	 *  count the number of times we have to re-tune
	 *  in order to get out of bad PHY status
	 */
	uint32_t num_of_sos_states;
	uint32_t beacon_filtered;
	uint32_t missed_beacons;
	uint8_t beacon_filter_average_energy;
	uint8_t beacon_filter_reason;
	uint8_t beacon_filter_current_energy;
	uint8_t beacon_filter_reserved;
	uint32_t beacon_filter_delta_time;
	struct iwx_statistics_bt_activity bt_activity;
	uint64_t rx_time;
	uint64_t on_time_rf;
	uint64_t on_time_scan;
	uint64_t tx_time;
} __packed; /* STATISTICS_GENERAL_API_S_VER_10 */

struct iwx_statistics_rx_non_phy {
	uint32_t bogus_cts;	/* CTS received when not expecting CTS */
	uint32_t bogus_ack;	/* ACK received when not expecting ACK */
	uint32_t non_bssid_frames;	/* number of frames with BSSID that
					 * doesn't belong to the STA BSSID */
	uint32_t filtered_frames;	/* count frames that were dumped in the
				 * filtering process */
	uint32_t non_channel_beacons;	/* beacons with our bss id but not on
					 * our serving channel */
	uint32_t channel_beacons;	/* beacons with our bss id and in our
				 * serving channel */
	uint32_t num_missed_bcon;	/* number of missed beacons */
	uint32_t adc_rx_saturation_time;	/* count in 0.8us units the time the
					 * ADC was in saturation */
	uint32_t ina_detection_search_time;/* total time (in 0.8us) searched
					  * for INA */
	uint32_t beacon_silence_rssi[3];/* RSSI silence after beacon frame */
	uint32_t interference_data_flag;	/* flag for interference data
					 * availability. 1 when data is
					 * available. */
	uint32_t channel_load;		/* counts RX Enable time in uSec */
	uint32_t dsp_false_alarms;	/* DSP false alarm (both OFDM
					 * and CCK) counter */
	uint32_t beacon_rssi_a;
	uint32_t beacon_rssi_b;
	uint32_t beacon_rssi_c;
	uint32_t beacon_energy_a;
	uint32_t beacon_energy_b;
	uint32_t beacon_energy_c;
	uint32_t num_bt_kills;
	uint32_t mac_id;
	uint32_t directed_data_mpdu;
} __packed; /* IWX_STATISTICS_RX_NON_PHY_API_S_VER_3 */

struct iwx_statistics_rx_phy {
	uint32_t ina_cnt;
	uint32_t fina_cnt;
	uint32_t plcp_err;
	uint32_t crc32_err;
	uint32_t overrun_err;
	uint32_t early_overrun_err;
	uint32_t crc32_good;
	uint32_t false_alarm_cnt;
	uint32_t fina_sync_err_cnt;
	uint32_t sfd_timeout;
	uint32_t fina_timeout;
	uint32_t unresponded_rts;
	uint32_t rxe_frame_limit_overrun;
	uint32_t sent_ack_cnt;
	uint32_t sent_cts_cnt;
	uint32_t sent_ba_rsp_cnt;
	uint32_t dsp_self_kill;
	uint32_t mh_format_err;
	uint32_t re_acq_main_rssi_sum;
	uint32_t reserved;
} __packed; /* IWX_STATISTICS_RX_PHY_API_S_VER_2 */

struct iwx_statistics_rx_ht_phy {
	uint32_t plcp_err;
	uint32_t overrun_err;
	uint32_t early_overrun_err;
	uint32_t crc32_good;
	uint32_t crc32_err;
	uint32_t mh_format_err;
	uint32_t agg_crc32_good;
	uint32_t agg_mpdu_cnt;
	uint32_t agg_cnt;
	uint32_t unsupport_mcs;
} __packed;  /* IWX_STATISTICS_HT_RX_PHY_API_S_VER_1 */

/*
 * The first MAC indices (starting from 0)
 * are available to the driver, AUX follows
 */
#define IWX_MAC_INDEX_AUX		4
#define IWX_MAC_INDEX_MIN_DRIVER	0
#define IWX_NUM_MAC_INDEX_DRIVER	IWX_MAC_INDEX_AUX

#define IWX_STATION_COUNT	16

#define IWX_MAX_CHAINS 3

struct iwx_statistics_tx_non_phy_agg {
	uint32_t ba_timeout;
	uint32_t ba_reschedule_frames;
	uint32_t scd_query_agg_frame_cnt;
	uint32_t scd_query_no_agg;
	uint32_t scd_query_agg;
	uint32_t scd_query_mismatch;
	uint32_t frame_not_ready;
	uint32_t underrun;
	uint32_t bt_prio_kill;
	uint32_t rx_ba_rsp_cnt;
	int8_t txpower[IWX_MAX_CHAINS];
	int8_t reserved;
	uint32_t reserved2;
} __packed; /* IWX_STATISTICS_TX_NON_PHY_AGG_API_S_VER_1 */

struct iwx_statistics_tx_channel_width {
	uint32_t ext_cca_narrow_ch20[1];
	uint32_t ext_cca_narrow_ch40[2];
	uint32_t ext_cca_narrow_ch80[3];
	uint32_t ext_cca_narrow_ch160[4];
	uint32_t last_tx_ch_width_indx;
	uint32_t rx_detected_per_ch_width[4];
	uint32_t success_per_ch_width[4];
	uint32_t fail_per_ch_width[4];
}; /* IWX_STATISTICS_TX_CHANNEL_WIDTH_API_S_VER_1 */

struct iwx_statistics_tx {
	uint32_t preamble_cnt;
	uint32_t rx_detected_cnt;
	uint32_t bt_prio_defer_cnt;
	uint32_t bt_prio_kill_cnt;
	uint32_t few_bytes_cnt;
	uint32_t cts_timeout;
	uint32_t ack_timeout;
	uint32_t expected_ack_cnt;
	uint32_t actual_ack_cnt;
	uint32_t dump_msdu_cnt;
	uint32_t burst_abort_next_frame_mismatch_cnt;
	uint32_t burst_abort_missing_next_frame_cnt;
	uint32_t cts_timeout_collision;
	uint32_t ack_or_ba_timeout_collision;
	struct iwx_statistics_tx_non_phy_agg agg;
	struct iwx_statistics_tx_channel_width channel_width;
} __packed; /* IWX_STATISTICS_TX_API_S_VER_4 */

struct iwx_statistics_general {
	struct iwx_statistics_general_common common;
	uint32_t beacon_counter[IWX_MAC_INDEX_AUX];
	uint8_t beacon_average_energy[IWX_MAC_INDEX_AUX];
	uint8_t reserved[8 - IWX_MAC_INDEX_AUX];
} __packed; /* STATISTICS_GENERAL_API_S_VER_10 */

struct iwx_statistics_rx {
	struct iwx_statistics_rx_phy ofdm;
	struct iwx_statistics_rx_phy cck;
	struct iwx_statistics_rx_non_phy general;
	struct iwx_statistics_rx_ht_phy ofdm_ht;
} __packed; /* IWX_STATISTICS_RX_API_S_VER_3 */

/*
 * IWX_STATISTICS_NOTIFICATION = 0x9d (notification only, not a command)
 *
 * By default, uCode issues this notification after receiving a beacon
 * while associated.  To disable this behavior, set DISABLE_NOTIF flag in the
 * IWX_REPLY_STATISTICS_CMD 0x9c, above.
 *
 * Statistics counters continue to increment beacon after beacon, but are
 * cleared when changing channels or when driver issues IWX_REPLY_STATISTICS_CMD
 * 0x9c with CLEAR_STATS bit set (see above).
 *
 * uCode also issues this notification during scans.  uCode clears statistics
 * appropriately so that each notification contains statistics for only the
 * one channel that has just been scanned.
 */

/**
 * struct iwx_statistics_load - RX statistics for multi-queue devices
 * @air_time: accumulated air time, per mac
 * @byte_count: accumulated byte count, per mac
 * @pkt_count: accumulated packet count, per mac
 * @avg_energy: average RSSI, per station
 */
struct iwx_statistics_load {
	uint32_t air_time[IWX_MAC_INDEX_AUX];
	uint32_t byte_count[IWX_MAC_INDEX_AUX];
	uint32_t pkt_count[IWX_MAC_INDEX_AUX];
	uint8_t avg_energy[IWX_STATION_COUNT];
} __packed; /* STATISTICS_RX_MAC_STATION_S_VER_3 */

struct iwx_notif_statistics {
	uint32_t flag;
	struct iwx_statistics_rx rx;
	struct iwx_statistics_tx tx;
	struct iwx_statistics_general general;
	struct iwx_statistics_load load_stats;
} __packed; /* STATISTICS_NTFY_API_S_VER_13 */


/**
 * flags used in statistics notification
 * @IWX_STATISTICS_REPLY_FLG_CLEAR: statistics were cleared after this report
 */
#define IWX_STATISTICS_REPLY_FLG_CLEAR	0x01

/**
 * flags used in statistics command
 * @IWX_STATISTICS_FLG_CLEAR: request to clear statistics after the report
 *	that's sent after this command
 * @IWX_STATISTICS_FLG_DISABLE_NOTIF: disable unilateral statistics
 *	notifications
 */
#define IWX_STATISTICS_FLG_CLEAR		0x01
#define IWX_STATISTICS_FLG_DISABLE_NOTIF	0x02

/**
 * struct iwx_statistics_cmd - statistics config command
 * @flags: IWX_STATISTICS_* flags
 */
struct iwx_statistics_cmd {
	uint32_t flags;
} __packed; /* STATISTICS_CMD_API_S_VER_1 */


/***********************************
 * Smart Fifo API
 ***********************************/
/* Smart Fifo state */
#define IWX_SF_LONG_DELAY_ON	0 /* should never be called by driver */
#define IWX_SF_FULL_ON		1
#define IWX_SF_UNINIT		2
#define IWX_SF_INIT_OFF		3
#define IWX_SF_HW_NUM_STATES	4

/* Smart Fifo possible scenario */
#define IWX_SF_SCENARIO_SINGLE_UNICAST	0
#define IWX_SF_SCENARIO_AGG_UNICAST	1
#define IWX_SF_SCENARIO_MULTICAST	2
#define IWX_SF_SCENARIO_BA_RESP		3
#define IWX_SF_SCENARIO_TX_RESP		4
#define IWX_SF_NUM_SCENARIO		5

#define IWX_SF_TRANSIENT_STATES_NUMBER 2 /* IWX_SF_LONG_DELAY_ON and IWX_SF_FULL_ON */
#define IWX_SF_NUM_TIMEOUT_TYPES 2	/* Aging timer and Idle timer */

/* smart FIFO default values */
#define IWX_SF_W_MARK_SISO 4096
#define IWX_SF_W_MARK_MIMO2 8192
#define IWX_SF_W_MARK_MIMO3 6144
#define IWX_SF_W_MARK_LEGACY 4096
#define IWX_SF_W_MARK_SCAN 4096

/* SF Scenarios timers for default configuration (aligned to 32 uSec) */
#define IWX_SF_SINGLE_UNICAST_IDLE_TIMER_DEF 160	/* 150 uSec  */
#define IWX_SF_SINGLE_UNICAST_AGING_TIMER_DEF 400	/* 0.4 mSec */
#define IWX_SF_AGG_UNICAST_IDLE_TIMER_DEF 160		/* 150 uSec */
#define IWX_SF_AGG_UNICAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define IWX_SF_MCAST_IDLE_TIMER_DEF 160			/* 150 mSec */
#define IWX_SF_MCAST_AGING_TIMER_DEF 400		/* 0.4 mSec */
#define IWX_SF_BA_IDLE_TIMER_DEF 160			/* 150 uSec */
#define IWX_SF_BA_AGING_TIMER_DEF 400			/* 0.4 mSec */
#define IWX_SF_TX_RE_IDLE_TIMER_DEF 160			/* 150 uSec */
#define IWX_SF_TX_RE_AGING_TIMER_DEF 400		/* 0.4 mSec */

/* SF Scenarios timers for FULL_ON state (aligned to 32 uSec) */
#define IWX_SF_SINGLE_UNICAST_IDLE_TIMER 320	/* 300 uSec  */
#define IWX_SF_SINGLE_UNICAST_AGING_TIMER 2016	/* 2 mSec */
#define IWX_SF_AGG_UNICAST_IDLE_TIMER 320	/* 300 uSec */
#define IWX_SF_AGG_UNICAST_AGING_TIMER 2016	/* 2 mSec */
#define IWX_SF_MCAST_IDLE_TIMER 2016		/* 2 mSec */
#define IWX_SF_MCAST_AGING_TIMER 10016		/* 10 mSec */
#define IWX_SF_BA_IDLE_TIMER 320		/* 300 uSec */
#define IWX_SF_BA_AGING_TIMER 2016		/* 2 mSec */
#define IWX_SF_TX_RE_IDLE_TIMER 320		/* 300 uSec */
#define IWX_SF_TX_RE_AGING_TIMER 2016		/* 2 mSec */

#define IWX_SF_LONG_DELAY_AGING_TIMER 1000000	/* 1 Sec */

#define IWX_SF_CFG_DUMMY_NOTIF_OFF	(1 << 16)

/**
 * Smart Fifo configuration command.
 * @state: smart fifo state, types listed in enum %iwx_sf_state.
 * @watermark: Minimum allowed available free space in RXF for transient state.
 * @long_delay_timeouts: aging and idle timer values for each scenario
 * in long delay state.
 * @full_on_timeouts: timer values for each scenario in full on state.
 */
struct iwx_sf_cfg_cmd {
	uint32_t state;
	uint32_t watermark[IWX_SF_TRANSIENT_STATES_NUMBER];
	uint32_t long_delay_timeouts[IWX_SF_NUM_SCENARIO][IWX_SF_NUM_TIMEOUT_TYPES];
	uint32_t full_on_timeouts[IWX_SF_NUM_SCENARIO][IWX_SF_NUM_TIMEOUT_TYPES];
} __packed; /* IWX_SF_CFG_API_S_VER_2 */

#define IWX_AC_BK	0
#define IWX_AC_BE	1
#define IWX_AC_VI	2
#define IWX_AC_VO	3
#define IWX_AC_NUM	4

/**
 * MAC context flags
 * @IWX_MAC_PROT_FLG_TGG_PROTECT: 11g protection when transmitting OFDM frames,
 *	this will require CCK RTS/CTS2self.
 *	RTS/CTS will protect full burst time.
 * @IWX_MAC_PROT_FLG_HT_PROT: enable HT protection
 * @IWX_MAC_PROT_FLG_FAT_PROT: protect 40 MHz transmissions
 * @IWX_MAC_PROT_FLG_SELF_CTS_EN: allow CTS2self
 */
#define IWX_MAC_PROT_FLG_TGG_PROTECT	(1 << 3)
#define IWX_MAC_PROT_FLG_HT_PROT	(1 << 23)
#define IWX_MAC_PROT_FLG_FAT_PROT	(1 << 24)
#define IWX_MAC_PROT_FLG_SELF_CTS_EN	(1 << 30)

#define IWX_MAC_FLG_SHORT_SLOT		(1 << 4)
#define IWX_MAC_FLG_SHORT_PREAMBLE	(1 << 5)

/**
 * Supported MAC types
 * @IWX_FW_MAC_TYPE_FIRST: lowest supported MAC type
 * @IWX_FW_MAC_TYPE_AUX: Auxiliary MAC (internal)
 * @IWX_FW_MAC_TYPE_LISTENER: monitor MAC type (?)
 * @IWX_FW_MAC_TYPE_PIBSS: Pseudo-IBSS
 * @IWX_FW_MAC_TYPE_IBSS: IBSS
 * @IWX_FW_MAC_TYPE_BSS_STA: BSS (managed) station
 * @IWX_FW_MAC_TYPE_P2P_DEVICE: P2P Device
 * @IWX_FW_MAC_TYPE_P2P_STA: P2P client
 * @IWX_FW_MAC_TYPE_GO: P2P GO
 * @IWX_FW_MAC_TYPE_TEST: ?
 * @IWX_FW_MAC_TYPE_MAX: highest support MAC type
 */
#define IWX_FW_MAC_TYPE_FIRST		1
#define IWX_FW_MAC_TYPE_AUX		IWX_FW_MAC_TYPE_FIRST
#define IWX_FW_MAC_TYPE_LISTENER	2
#define IWX_FW_MAC_TYPE_PIBSS		3
#define IWX_FW_MAC_TYPE_IBSS		4
#define IWX_FW_MAC_TYPE_BSS_STA		5
#define IWX_FW_MAC_TYPE_P2P_DEVICE	6
#define IWX_FW_MAC_TYPE_P2P_STA		7
#define IWX_FW_MAC_TYPE_GO		8
#define IWX_FW_MAC_TYPE_TEST		9
#define IWX_FW_MAC_TYPE_MAX		IWX_FW_MAC_TYPE_TEST
/* IWX_MAC_CONTEXT_TYPE_API_E_VER_1 */

/**
 * TSF hw timer ID
 * @IWX_TSF_ID_A: use TSF A
 * @IWX_TSF_ID_B: use TSF B
 * @IWX_TSF_ID_C: use TSF C
 * @IWX_TSF_ID_D: use TSF D
 * @IWX_NUM_TSF_IDS: number of TSF timers available
 */
#define IWX_TSF_ID_A	0
#define IWX_TSF_ID_B	1
#define IWX_TSF_ID_C	2
#define IWX_TSF_ID_D	3
#define IWX_NUM_TSF_IDS	4
/* IWX_TSF_ID_API_E_VER_1 */

/**
 * struct iwx_mac_data_ap - configuration data for AP MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @bi_reciprocal: 2^32 / bi
 * @dtim_interval: dtim transmit time in TU
 * @dtim_reciprocal: 2^32 / dtim_interval
 * @mcast_qid: queue ID for multicast traffic
 *	NOTE: obsolete from VER2 and on
 * @beacon_template: beacon template ID
 */
struct iwx_mac_data_ap {
	uint32_t beacon_time;
	uint64_t beacon_tsf;
	uint32_t bi;
	uint32_t bi_reciprocal;
	uint32_t dtim_interval;
	uint32_t dtim_reciprocal;
	uint32_t mcast_qid;
	uint32_t beacon_template;
} __packed; /* AP_MAC_DATA_API_S_VER_2 */

/**
 * struct iwx_mac_data_ibss - configuration data for IBSS MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @bi_reciprocal: 2^32 / bi
 * @beacon_template: beacon template ID
 */
struct iwx_mac_data_ibss {
	uint32_t beacon_time;
	uint64_t beacon_tsf;
	uint32_t bi;
	uint32_t bi_reciprocal;
	uint32_t beacon_template;
} __packed; /* IBSS_MAC_DATA_API_S_VER_1 */

/**
 * enum iwx_mac_data_policy - policy of the data path for this MAC
 * @TWT_SUPPORTED: twt is supported
 * @MORE_DATA_ACK_SUPPORTED: AP supports More Data Ack according to
 *	paragraph 9.4.1.17 in P802.11ax_D4 specification. Used for TWT
 *	early termination detection.
 * @FLEXIBLE_TWT_SUPPORTED: AP supports flexible TWT schedule
 * @PROTECTED_TWT_SUPPORTED: AP supports protected TWT frames (with 11w)
 * @BROADCAST_TWT_SUPPORTED: AP and STA support broadcast TWT
 * @COEX_HIGH_PRIORITY_ENABLE: high priority mode for BT coex, to be used
 *	during 802.1X negotiation (and allowed during 4-way-HS)
 */
#define IWX_TWT_SUPPORTED BIT		(1 << 0)
#define IWX_MORE_DATA_ACK_SUPPORTED	(1 << 1)
#define	IWX_FLEXIBLE_TWT_SUPPORTED	(1 << 2)
#define IWX_PROTECTED_TWT_SUPPORTED	(1 << 3)
#define IWX_BROADCAST_TWT_SUPPORTED	(1 << 4)
#define IWX_COEX_HIGH_PRIORITY_ENABLE	(1 << 5)

/**
 * struct iwx_mac_data_sta - configuration data for station MAC context
 * @is_assoc: 1 for associated state, 0 otherwise
 * @dtim_time: DTIM arrival time in system time
 * @dtim_tsf: DTIM arrival time in TSF
 * @bi: beacon interval in TU, applicable only when associated
 * @data_policy: see &enum iwl_mac_data_policy
 * @dtim_interval: DTIM interval in TU, applicable only when associated
 * @listen_interval: in beacon intervals, applicable only when associated
 * @assoc_id: unique ID assigned by the AP during association
 */
struct iwx_mac_data_sta {
	uint32_t is_assoc;
	uint32_t dtim_time;
	uint64_t dtim_tsf;
	uint32_t bi;
	uint32_t reserved1;
	uint32_t dtim_interval;
	uint32_t data_policy;
	uint32_t listen_interval;
	uint32_t assoc_id;
	uint32_t assoc_beacon_arrive_time;
} __packed; /* IWX_STA_MAC_DATA_API_S_VER_2 */

/**
 * struct iwx_mac_data_go - configuration data for P2P GO MAC context
 * @ap: iwx_mac_data_ap struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 * @opp_ps_enabled: indicate that opportunistic PS allowed
 */
struct iwx_mac_data_go {
	struct iwx_mac_data_ap ap;
	uint32_t ctwin;
	uint32_t opp_ps_enabled;
} __packed; /* GO_MAC_DATA_API_S_VER_1 */

/**
 * struct iwx_mac_data_p2p_sta - configuration data for P2P client MAC context
 * @sta: iwx_mac_data_sta struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 */
struct iwx_mac_data_p2p_sta {
	struct iwx_mac_data_sta sta;
	uint32_t ctwin;
} __packed; /* P2P_STA_MAC_DATA_API_S_VER_1 */

/**
 * struct iwx_mac_data_pibss - Pseudo IBSS config data
 * @stats_interval: interval in TU between statistics notifications to host.
 */
struct iwx_mac_data_pibss {
	uint32_t stats_interval;
} __packed; /* PIBSS_MAC_DATA_API_S_VER_1 */

/*
 * struct iwx_mac_data_p2p_dev - configuration data for the P2P Device MAC
 * context.
 * @is_disc_extended: if set to true, P2P Device discoverability is enabled on
 *	other channels as well. This should be to true only in case that the
 *	device is discoverable and there is an active GO. Note that setting this
 *	field when not needed, will increase the number of interrupts and have
 *	effect on the platform power, as this setting opens the Rx filters on
 *	all macs.
 */
struct iwx_mac_data_p2p_dev {
	uint32_t is_disc_extended;
} __packed; /* _P2P_DEV_MAC_DATA_API_S_VER_1 */

/**
 * MAC context filter flags
 * @IWX_MAC_FILTER_IN_PROMISC: accept all data frames
 * @IWX_MAC_FILTER_IN_CONTROL_AND_MGMT: pass all management and
 *	control frames to the host
 * @IWX_MAC_FILTER_ACCEPT_GRP: accept multicast frames
 * @IWX_MAC_FILTER_DIS_DECRYPT: don't decrypt unicast frames
 * @IWX_MAC_FILTER_DIS_GRP_DECRYPT: don't decrypt multicast frames
 * @IWX_MAC_FILTER_IN_BEACON: transfer foreign BSS's beacons to host
 *	(in station mode when associated)
 * @IWX_MAC_FILTER_OUT_BCAST: filter out all broadcast frames
 * @IWX_MAC_FILTER_IN_CRC32: extract FCS and append it to frames
 * @IWX_MAC_FILTER_IN_PROBE_REQUEST: pass probe requests to host
 */
#define IWX_MAC_FILTER_IN_PROMISC		(1 << 0)
#define IWX_MAC_FILTER_IN_CONTROL_AND_MGMT	(1 << 1)
#define IWX_MAC_FILTER_ACCEPT_GRP		(1 << 2)
#define IWX_MAC_FILTER_DIS_DECRYPT		(1 << 3)
#define IWX_MAC_FILTER_DIS_GRP_DECRYPT		(1 << 4)
#define IWX_MAC_FILTER_IN_BEACON		(1 << 6)
#define IWX_MAC_FILTER_OUT_BCAST		(1 << 8)
#define IWX_MAC_FILTER_IN_CRC32			(1 << 11)
#define IWX_MAC_FILTER_IN_PROBE_REQUEST		(1 << 12)

/**
 * QoS flags
 * @IWX_MAC_QOS_FLG_UPDATE_EDCA: ?
 * @IWX_MAC_QOS_FLG_TGN: HT is enabled
 * @IWX_MAC_QOS_FLG_TXOP_TYPE: ?
 *
 */
#define IWX_MAC_QOS_FLG_UPDATE_EDCA	(1 << 0)
#define IWX_MAC_QOS_FLG_TGN		(1 << 1)
#define IWX_MAC_QOS_FLG_TXOP_TYPE	(1 << 4)

/**
 * struct iwx_ac_qos - QOS timing params for IWX_MAC_CONTEXT_CMD
 * @cw_min: Contention window, start value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x0f.
 * @cw_max: Contention window, max value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x3f.
 * @aifsn:  Number of slots in Arbitration Interframe Space (before
 *	performing random backoff timing prior to Tx).  Device default 1.
 * @fifos_mask: FIFOs used by this MAC for this AC
 * @edca_txop:  Length of Tx opportunity, in uSecs.  Device default is 0.
 *
 * One instance of this config struct for each of 4 EDCA access categories
 * in struct iwx_qosparam_cmd.
 *
 * Device will automatically increase contention window by (2*CW) + 1 for each
 * transmission retry.  Device uses cw_max as a bit mask, ANDed with new CW
 * value, to cap the CW value.
 */
struct iwx_ac_qos {
	uint16_t cw_min;
	uint16_t cw_max;
	uint8_t aifsn;
	uint8_t fifos_mask;
	uint16_t edca_txop;
} __packed; /* IWX_AC_QOS_API_S_VER_2 */

/**
 * struct iwx_mac_ctx_cmd - command structure to configure MAC contexts
 * ( IWX_MAC_CONTEXT_CMD = 0x28 )
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, one of IWX_FW_CTXT_ACTION_*
 * @mac_type: one of IWX_FW_MAC_TYPE_*
 * @tsf_id: TSF HW timer, one of IWX_TSF_ID_*
 * @node_addr: MAC address
 * @bssid_addr: BSSID
 * @cck_rates: basic rates available for CCK
 * @ofdm_rates: basic rates available for OFDM
 * @protection_flags: combination of IWX_MAC_PROT_FLG_FLAG_*
 * @cck_short_preamble: 0x20 for enabling short preamble, 0 otherwise
 * @short_slot: 0x10 for enabling short slots, 0 otherwise
 * @filter_flags: combination of IWX_MAC_FILTER_*
 * @qos_flags: from IWX_MAC_QOS_FLG_*
 * @ac: one iwx_mac_qos configuration for each AC
 * @mac_specific: one of struct iwx_mac_data_*, according to mac_type
 */
struct iwx_mac_ctx_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* IWX_MAC_CONTEXT_COMMON_DATA_API_S_VER_1 */
	uint32_t mac_type;
	uint32_t tsf_id;
	uint8_t node_addr[6];
	uint16_t reserved_for_node_addr;
	uint8_t bssid_addr[6];
	uint16_t reserved_for_bssid_addr;
	uint32_t cck_rates;
	uint32_t ofdm_rates;
	uint32_t protection_flags;
	uint32_t cck_short_preamble;
	uint32_t short_slot;
	uint32_t filter_flags;
	/* IWX_MAC_QOS_PARAM_API_S_VER_1 */
	uint32_t qos_flags;
	struct iwx_ac_qos ac[IWX_AC_NUM+1];
	/* IWX_MAC_CONTEXT_COMMON_DATA_API_S */
	union {
		struct iwx_mac_data_ap ap;
		struct iwx_mac_data_go go;
		struct iwx_mac_data_sta sta;
		struct iwx_mac_data_p2p_sta p2p_sta;
		struct iwx_mac_data_p2p_dev p2p_dev;
		struct iwx_mac_data_pibss pibss;
		struct iwx_mac_data_ibss ibss;
	};
} __packed; /* IWX_MAC_CONTEXT_CMD_API_S_VER_1 */

/**
 * struct iwx_mac_client_data - configuration data for client MAC context
 *
 * @is_assoc: 1 for associated state, 0 otherwise
 * @esr_transition_timeout: the timeout required by the AP for the eSR transition.
 *	Available only from version 2 of the command.
 *	This values comes from the EMLSR transition delay in the EML Capabilities
 *	subfield.
 * @medium_sync_delay: the value as it appears in P802.11be_D2.2 Figure 9-1002j.
 * @assoc_id: unique ID assigned by the AP during association
 * @reserved1: alignment
 * @data_policy: see &enum iwx_mac_data_policy
 * @reserved2: alignment
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 */
struct iwx_mac_client_data {
	uint8_t is_assoc;
	uint8_t esr_transition_timeout;
	uint16_t medium_sync_delay;

	uint16_t assoc_id;
	uint16_t reserved1;
	uint16_t data_policy;
	uint16_t reserved2;
	uint32_t ctwin;
} __packed; /* MAC_CONTEXT_CONFIG_CLIENT_DATA_API_S_VER_2 */

/**
 * struct iwx_mac_p2p_dev_data  - configuration data for P2P device MAC context
 *
 * @is_disc_extended: if set to true, P2P Device discoverability is enabled on
 *	other channels as well. This should be to true only in case that the
 *	device is discoverable and there is an active GO. Note that setting this
 *	field when not needed, will increase the number of interrupts and have
 *	effect on the platform power, as this setting opens the Rx filters on
 *	all macs.
 */
struct iwx_mac_p2p_dev_data {
	uint32_t is_disc_extended;
} __packed; /* MAC_CONTEXT_CONFIG_P2P_DEV_DATA_API_S_VER_1 */

/**
 * enum iwx_mac_config_filter_flags - MAC context configuration filter flags
 *
 * @IWX_MAC_CFG_FILTER_PROMISC: accept all data frames
 * @IWX_MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT: pass all management and
 *	control frames to the host
 * @IWX_MAC_CFG_FILTER_ACCEPT_GRP: accept multicast frames
 * @IWX_MAC_CFG_FILTER_ACCEPT_BEACON: accept beacon frames
 * @IWX_MAC_CFG_FILTER_ACCEPT_BCAST_PROBE_RESP: accept broadcast probe response
 * @IWX_MAC_CFG_FILTER_ACCEPT_PROBE_REQ: accept probe requests
 */
enum iwx_mac_config_filter_flags {
	IWX_MAC_CFG_FILTER_PROMISC			= (1 << 0),
	IWX_MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT	= (1 << 1),
	IWX_MAC_CFG_FILTER_ACCEPT_GRP			= (1 << 2),
	IWX_MAC_CFG_FILTER_ACCEPT_BEACON		= (1 << 3),
	IWX_MAC_CFG_FILTER_ACCEPT_BCAST_PROBE_RESP	= (1 << 4),
	IWX_MAC_CFG_FILTER_ACCEPT_PROBE_REQ		= (1 << 5),
}; /* MAC_FILTER_FLAGS_MASK_E_VER_1 */

/**
 * struct iwx_mac_config_cmd - command structure to configure MAC contexts in
 *	MLD API
 * ( MAC_CONTEXT_CONFIG_CMD = 0x8 )
 *
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, see &enum iwx_ctxt_action
 * @mac_type: one of &enum iwx_mac_types
 * @local_mld_addr: mld address
 * @reserved_for_local_mld_addr: reserved
 * @filter_flags: combination of &enum iwx_mac_config_filter_flags
 * @he_support: does this MAC support HE
 * @he_ap_support: HE AP enabled, "pseudo HE", no trigger frame handling
 * @eht_support: does this MAC support EHT. Requires he_support
 * @nic_not_ack_enabled: mark that the NIC doesn't support receiving
 *	ACK-enabled AGG, (i.e. both BACK and non-BACK frames in single AGG).
 *	If the NIC is not ACK_ENABLED it may use the EOF-bit in first non-0
 *	len delim to determine if AGG or single.
 */
struct iwx_mac_config_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	uint32_t id_and_color;
	uint32_t action;
	/* MAC_CONTEXT_TYPE_API_E */
	uint32_t mac_type;
	uint8_t local_mld_addr[6];
	uint16_t reserved_for_local_mld_addr;
	uint32_t filter_flags;
	uint16_t he_support;
	uint16_t he_ap_support;
	uint32_t eht_support;
	uint32_t nic_not_ack_enabled;
	/* MAC_CONTEXT_CONFIG_SPECIFIC_DATA_API_U_VER_2 */
	union {
		struct iwx_mac_client_data client;
		struct iwx_mac_p2p_dev_data p2p_dev;
	};
} __packed; /* MAC_CONTEXT_CONFIG_CMD_API_S_VER_2 */

/**
 * struct iwx_he_backoff_conf - used for backoff configuration
 * Per each trigger-based AC, (set by MU EDCA Parameter set info-element)
 * used for backoff configuration of TXF5..TXF8 trigger based.
 * The MU-TIMER is reloaded w/ MU_TIME each time a frame from the AC is sent via
 * trigger-based TX.
 * @cwmin: CW min
 * @cwmax: CW max
 * @aifsn: AIFSN
 *	AIFSN=0, means that no backoff from the specified TRIG-BASED AC is
 *	allowed till the MU-TIMER is 0
 * @mu_time: MU time in 8TU units
 */
struct iwx_he_backoff_conf {
	uint16_t cwmin;
	uint16_t cwmax;
	uint16_t aifsn;
	uint16_t mu_time;
} __packed; /* AC_QOS_DOT11AX_API_S */

/**
 * enum iwx_link_ctx_modify_flags - indicate to the fw what fields are being
 *	modified in &iwx_link_ctx_cfg_cmd
 *
 * @IWX_LINK_CONTEXT_MODIFY_ACTIVE: covers iwx_link_ctx_cfg_cmd::active
 * @IWX_LINK_CONTEXT_MODIFY_RATES_INFO: covers iwx_link_ctx_cfg_cmd::cck_rates,
 *	iwx_link_ctx_cfg_cmd::ofdm_rates,
 *	iwx_link_ctx_cfg_cmd::cck_short_preamble,
 *	iwx_link_ctx_cfg_cmd::short_slot
 * @IWX_LINK_CONTEXT_MODIFY_PROTECT_FLAGS: covers
 *	iwx_link_ctx_cfg_cmd::protection_flags
 * @IWX_LINK_CONTEXT_MODIFY_QOS_PARAMS: covers iwx_link_ctx_cfg_cmd::qos_flags,
 *	iwx_link_ctx_cfg_cmd::ac,
 * @IWX_LINK_CONTEXT_MODIFY_BEACON_TIMING: covers iwx_link_ctx_cfg_cmd::bi,
 *	iwx_link_ctx_cfg_cmd::dtim_interval,
 *	iwx_link_ctx_cfg_cmd::dtim_time,
 *	iwx_link_ctx_cfg_cmd::dtim_tsf,
 *	iwx_link_ctx_cfg_cmd::assoc_beacon_arrive_time.
 *	This flag can be set only once after assoc.
 * @IWX_LINK_CONTEXT_MODIFY_HE_PARAMS: covers
 *	iwx_link_ctx_cfg_cmd::htc_trig_based_pkt_ext
 *	iwx_link_ctx_cfg_cmd::rand_alloc_ecwmin,
 *	iwx_link_ctx_cfg_cmd::rand_alloc_ecwmax,
 *	iwx_link_ctx_cfg_cmd::trig_based_txf,
 *	iwx_link_ctx_cfg_cmd::bss_color,
 *	iwx_link_ctx_cfg_cmd::ndp_fdbk_buff_th_exp,
 *	iwx_link_ctx_cfg_cmd::ref_bssid_addr
 *	iwx_link_ctx_cfg_cmd::bssid_index,
 *	iwx_link_ctx_cfg_cmd::frame_time_rts_th.
 *	This flag can be set any time.
 * @IWX_LINK_CONTEXT_MODIFY_BSS_COLOR_DISABLE: covers
 *	iwx_link_ctx_cfg_cmd::bss_color_disable
 * @IWX_LINK_CONTEXT_MODIFY_EHT_PARAMS: covers iwx_link_ctx_cfg_cmd::puncture_mask.
 *	This flag can be set only if the MAC that this link relates to has
 *	eht_support set to true.
 * @IWX_LINK_CONTEXT_MODIFY_ALL: set all above flags
 */
enum iwx_link_ctx_modify_flags {
	IWX_LINK_CONTEXT_MODIFY_ACTIVE		= (1 << 0),
	IWX_LINK_CONTEXT_MODIFY_RATES_INFO	= (1 << 1),
	IWX_LINK_CONTEXT_MODIFY_PROTECT_FLAGS	= (1 << 2),
	IWX_LINK_CONTEXT_MODIFY_QOS_PARAMS	= (1 << 3),
	IWX_LINK_CONTEXT_MODIFY_BEACON_TIMING	= (1 << 4),
	IWX_LINK_CONTEXT_MODIFY_HE_PARAMS	= (1 << 5),
	IWX_LINK_CONTEXT_MODIFY_BSS_COLOR_DISABLE	= (1 << 6),
	IWX_LINK_CONTEXT_MODIFY_EHT_PARAMS	= (1 << 7),
	IWX_LINK_CONTEXT_MODIFY_ALL		= 0xff,
}; /* LINK_CONTEXT_MODIFY_MASK_E_VER_1 */

/**
 * enum iwx_link_ctx_protection_flags - link protection flags
 * @IWX_LINK_PROT_FLG_TGG_PROTECT: 11g protection when transmitting OFDM frames,
 *	this will require CCK RTS/CTS2self.
 *	RTS/CTS will protect full burst time.
 * @IWX_LINK_PROT_FLG_HT_PROT: enable HT protection
 * @IWX_LINK_PROT_FLG_FAT_PROT: protect 40 MHz transmissions
 * @IWX_LINK_PROT_FLG_SELF_CTS_EN: allow CTS2self
 */
enum iwx_link_ctx_protection_flags {
	IWX_LINK_PROT_FLG_TGG_PROTECT	= (1 << 0),
	IWX_LINK_PROT_FLG_HT_PROT	= (1 << 1),
	IWX_LINK_PROT_FLG_FAT_PROT	= (1 << 2),
	IWX_LINK_PROT_FLG_SELF_CTS_EN	= (1 << 3),
}; /* LINK_PROTECT_FLAGS_E_VER_1 */

/**
 * struct iwx_link_config_cmd - command structure to configure the LINK context
 *	in MLD API
 * ( LINK_CONFIG_CMD =0x9 )
 *
 * @action: action to perform, see &enum iwx_ctxt_action
 * @link_id: the id of the link that this cmd configures
 * @mac_id: interface ID. Relevant only if action is FW_CTXT_ACTION_ADD
 * @phy_id: PHY index. Can be changed only if the link was inactive
 *	(and stays inactive). If the link is active (or becomes active),
 *	this field is ignored.
 * @local_link_addr: the links MAC address. Can be changed only if the link was
 *	inactive (and stays inactive). If the link is active
 *	(or becomes active), this field is ignored.
 * @reserved_for_local_link_addr: reserved
 * @modify_mask: from &enum iwx_link_ctx_modify_flags, selects what to change.
 *	Relevant only if action is FW_CTXT_ACTION_MODIFY
 * @active: indicates whether the link is active or not
 * @listen_lmac: indicates whether the link should be allocated on the Listen
 *	Lmac or on the Main Lmac. Cannot be changed on an active Link.
 *	Relevant only for eSR.
 * @cck_rates: basic rates available for CCK
 * @ofdm_rates: basic rates available for OFDM
 * @cck_short_preamble: 1 for enabling short preamble, 0 otherwise
 * @short_slot: 1 for enabling short slots, 0 otherwise
 * @protection_flags: combination of &enum iwx_link_ctx_protection_flags
 * @qos_flags: from &enum iwx_mac_qos_flags
 * @ac: one iwx_mac_qos configuration for each AC
 * @htc_trig_based_pkt_ext: default PE in 4us units
 * @rand_alloc_ecwmin: random CWmin = 2**ECWmin-1
 * @rand_alloc_ecwmax: random CWmax = 2**ECWmax-1
 * @ndp_fdbk_buff_th_exp: set exponent for the NDP feedback buffered threshold
 * @trig_based_txf: MU EDCA Parameter set for the trigger based traffic queues
 * @bi: beacon interval in TU, applicable only when associated
 * @dtim_interval: DTIM interval in TU.
 *	Relevant only for GO, otherwise this is offloaded.
 * @puncture_mask: puncture mask for EHT
 * @frame_time_rts_th: HE duration RTS threshold, in units of 32us
 * @flags: a combination from &enum iwx_link_ctx_flags
 * @flags_mask: what of %flags have changed. Also &enum iwx_link_ctx_flags
 * Below fields are for multi-bssid:
 * @ref_bssid_addr: reference BSSID used by the AP
 * @reserved_for_ref_bssid_addr: reserved
 * @bssid_index: index of the associated VAP
 * @bss_color: 11ax AP ID that is used in the HE SIG-A to mark inter BSS frame
 * @spec_link_id: link_id as the AP knows it
 * @reserved: alignment
 * @ibss_bssid_addr: bssid for ibss
 * @reserved_for_ibss_bssid_addr: reserved
 * @reserved1: reserved for future use
 */
struct iwx_link_config_cmd {
	uint32_t action;
	uint32_t link_id;
	uint32_t mac_id;
	uint32_t phy_id;
	uint8_t local_link_addr[6];
	uint16_t reserved_for_local_link_addr;
	uint32_t modify_mask;
	uint32_t active;
	uint32_t listen_lmac;
	uint32_t cck_rates;
	uint32_t ofdm_rates;
	uint32_t cck_short_preamble;
	uint32_t short_slot;
	uint32_t protection_flags;
	/* MAC_QOS_PARAM_API_S_VER_1 */
	uint32_t qos_flags;
	struct iwx_ac_qos ac[IWX_AC_NUM + 1];
	uint8_t htc_trig_based_pkt_ext;
	uint8_t rand_alloc_ecwmin;
	uint8_t rand_alloc_ecwmax;
	uint8_t ndp_fdbk_buff_th_exp;
	struct iwx_he_backoff_conf trig_based_txf[IWX_AC_NUM];
	uint32_t bi;
	uint32_t dtim_interval;
	uint16_t puncture_mask;
	uint16_t frame_time_rts_th;
	uint32_t flags;
	uint32_t flags_mask;
	/* The below fields are for multi-bssid */
	uint8_t ref_bssid_addr[6];
	uint16_t reserved_for_ref_bssid_addr;
	uint8_t bssid_index;
	uint8_t bss_color;
	uint8_t spec_link_id;
	uint8_t reserved;
	uint8_t ibss_bssid_addr[6];
	uint16_t reserved_for_ibss_bssid_addr;
	uint32_t reserved1[8];
} __packed; /* LINK_CONTEXT_CONFIG_CMD_API_S_VER_1 */

#define MAX_HE_SUPP_NSS	2
#define MAX_CHANNEL_BW_INDX_API_D_VER_2	4
#define MAX_CHANNEL_BW_INDX_API_D_VER_3	5

/**
 * struct iwx_he_pkt_ext_v1 - QAM thresholds
 * The required PPE is set via HE Capabilities IE, per Nss x BW x MCS
 * The IE is organized in the following way:
 * Support for Nss x BW (or RU) matrix:
 *	(0=SISO, 1=MIMO2) x (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz)
 * Each entry contains 2 QAM thresholds for 8us and 16us:
 *	0=BPSK, 1=QPSK, 2=16QAM, 3=64QAM, 4=256QAM, 5=1024QAM, 6=RES, 7=NONE
 * i.e. QAM_th1 < QAM_th2 such if TX uses QAM_tx:
 *	QAM_tx < QAM_th1            --> PPE=0us
 *	QAM_th1 <= QAM_tx < QAM_th2 --> PPE=8us
 *	QAM_th2 <= QAM_tx           --> PPE=16us
 * @pkt_ext_qam_th: QAM thresholds
 *	For each Nss/Bw define 2 QAM thrsholds (0..5)
 *	For rates below the low_th, no need for PPE
 *	For rates between low_th and high_th, need 8us PPE
 *	For rates equal or higher then the high_th, need 16us PPE
 *	Nss (0-siso, 1-mimo2) x BW (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz) x
 *	(0-low_th, 1-high_th)
 */
struct iwx_he_pkt_ext_v1 {
	uint8_t pkt_ext_qam_th[MAX_HE_SUPP_NSS][MAX_CHANNEL_BW_INDX_API_D_VER_2][2];
} __packed; /* PKT_EXT_DOT11AX_API_S_VER_1 */

/**
 * struct iwx_he_pkt_ext_v2 - QAM thresholds
 * The required PPE is set via HE Capabilities IE, per Nss x BW x MCS
 * The IE is organized in the following way:
 * Support for Nss x BW (or RU) matrix:
 *	(0=SISO, 1=MIMO2) x (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz)
 * Each entry contains 2 QAM thresholds for 8us and 16us:
 *	0=BPSK, 1=QPSK, 2=16QAM, 3=64QAM, 4=256QAM, 5=1024QAM, 6=RES, 7=NONE
 * i.e. QAM_th1 < QAM_th2 such if TX uses QAM_tx:
 *	QAM_tx < QAM_th1            --> PPE=0us
 *	QAM_th1 <= QAM_tx < QAM_th2 --> PPE=8us
 *	QAM_th2 <= QAM_tx           --> PPE=16us
 * @pkt_ext_qam_th: QAM thresholds
 *	For each Nss/Bw define 2 QAM thrsholds (0..5)
 *	For rates below the low_th, no need for PPE
 *	For rates between low_th and high_th, need 8us PPE
 *	For rates equal or higher then the high_th, need 16us PPE
 *	Nss (0-siso, 1-mimo2) x
 *	BW (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz, 4-320MHz) x
 *	(0-low_th, 1-high_th)
 */
struct iwx_he_pkt_ext_v2 {
	uint8_t pkt_ext_qam_th[MAX_HE_SUPP_NSS][MAX_CHANNEL_BW_INDX_API_D_VER_3][2];
} __packed; /* PKT_EXT_DOT11AX_API_S_VER_2 */

/**
 * struct iwx_mvm_sta_cfg_cmd - cmd structure to add a peer sta to the uCode's
 *	station table
 * ( STA_CONFIG_CMD = 0xA )
 *
 * @sta_id: index of station in uCode's station table
 * @link_id: the id of the link that is used to communicate with this sta
 * @peer_mld_address: the peers mld address
 * @reserved_for_peer_mld_address: reserved
 * @peer_link_address: the address of the link that is used to communicate
 *	with this sta
 * @reserved_for_peer_link_address: reserved
 * @station_type: type of this station. See &enum iwx_fw_sta_type
 * @assoc_id: for GO only
 * @beamform_flags: beam forming controls
 * @mfp: indicates whether the STA uses management frame protection or not.
 * @mimo: indicates whether the sta uses mimo or not
 * @mimo_protection: indicates whether the sta uses mimo protection or not
 * @ack_enabled: indicates that the AP supports receiving ACK-
 *	enabled AGG, i.e. both BACK and non-BACK frames in a single AGG
 * @trig_rnd_alloc: indicates that trigger based random allocation
 *	is enabled according to UORA element existence
 * @tx_ampdu_spacing: minimum A-MPDU spacing:
 *	4 - 2us density, 5 - 4us density, 6 - 8us density, 7 - 16us density
 * @tx_ampdu_max_size: maximum A-MPDU length: 0 - 8K, 1 - 16K, 2 - 32K,
 *	3 - 64K, 4 - 128K, 5 - 256K, 6 - 512K, 7 - 1024K.
 * @sp_length: the size of the SP in actual number of frames
 * @uapsd_acs:  4 LS bits are trigger enabled ACs, 4 MS bits are the deliver
 *	enabled ACs.
 * @pkt_ext: optional, exists according to PPE-present bit in the HE/EHT-PHY
 *	capa
 * @htc_flags: which features are supported in HTC
 */
struct iwx_mvm_sta_cfg_cmd {
	uint32_t sta_id;
	uint32_t link_id;
	uint8_t peer_mld_address[6];
	uint16_t reserved_for_peer_mld_address;
	uint8_t peer_link_address[6];
	uint16_t reserved_for_peer_link_address;
	uint32_t station_type;
	uint32_t assoc_id;
	uint32_t beamform_flags;
	uint32_t mfp;
	uint32_t mimo;
	uint32_t mimo_protection;
	uint32_t ack_enabled;
	uint32_t trig_rnd_alloc;
	uint32_t tx_ampdu_spacing;
	uint32_t tx_ampdu_max_size;
	uint32_t sp_length;
	uint32_t uapsd_acs;
	struct iwx_he_pkt_ext_v2 pkt_ext;
	uint32_t htc_flags;
} __packed; /* STA_CMD_API_S_VER_1 */

/**
 * struct iwx_mvm_remove_sta_cmd - a cmd structure to remove a sta added by
 *	STA_CONFIG_CMD or AUX_STA_CONFIG_CMD
 * ( STA_REMOVE_CMD = 0xC )
 *
 * @sta_id: index of station to remove
 */
struct iwx_mvm_remove_sta_cmd {
	uint32_t sta_id;
} __packed; /* REMOVE_STA_API_S_VER_1 */

static inline uint32_t iwx_reciprocal(uint32_t v)
{
	if (!v)
		return 0;
	return 0xFFFFFFFF / v;
}

/* Power Management Commands, Responses, Notifications */

/**
 * masks for LTR config command flags
 * @IWX_LTR_CFG_FLAG_FEATURE_ENABLE: Feature operational status
 * @IWX_LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS: allow LTR change on shadow
 *      memory access
 * @IWX_LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH: allow LTR msg send on ANY LTR
 *      reg change
 * @IWX_LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3: allow LTR msg send on transition from
 *      D0 to D3
 * @IWX_LTR_CFG_FLAG_SW_SET_SHORT: fixed static short LTR register
 * @IWX_LTR_CFG_FLAG_SW_SET_LONG: fixed static short LONG register
 * @IWX_LTR_CFG_FLAG_DENIE_C10_ON_PD: allow going into C10 on PD
 */
#define IWX_LTR_CFG_FLAG_FEATURE_ENABLE			0x00000001
#define IWX_LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS	0x00000002
#define IWX_LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH		0x00000004
#define IWX_LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3		0x00000008
#define IWX_LTR_CFG_FLAG_SW_SET_SHORT			0x00000010
#define IWX_LTR_CFG_FLAG_SW_SET_LONG			0x00000020
#define IWX_LTR_CFG_FLAG_DENIE_C10_ON_PD		0x00000040

#define IWX_LTR_VALID_STATES_NUM 4

/**
 * struct iwx_ltr_config_cmd - configures the LTR
 * @flags: See %enum iwx_ltr_config_flags
 * @static_long:
 * @static_short:
 * @ltr_cfg_values:
 * @ltr_short_idle_timeout:
 */
struct iwx_ltr_config_cmd {
	uint32_t flags;
	uint32_t static_long;
	uint32_t static_short;
	uint32_t ltr_cfg_values[IWX_LTR_VALID_STATES_NUM];
	uint32_t ltr_short_idle_timeout;
} __packed; /* LTR_CAPABLE_API_S_VER_2 */

/* Radio LP RX Energy Threshold measured in dBm */
#define IWX_POWER_LPRX_RSSI_THRESHOLD	75
#define IWX_POWER_LPRX_RSSI_THRESHOLD_MAX	94
#define IWX_POWER_LPRX_RSSI_THRESHOLD_MIN	30

/**
 * Masks for iwx_mac_power_cmd command flags
 * @IWX_POWER_FLAGS_POWER_SAVE_ENA_MSK: '1' Allow to save power by turning off
 *		receiver and transmitter. '0' - does not allow.
 * @IWX_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK: '0' Driver disables power management,
 *		'1' Driver enables PM (use rest of parameters)
 * @IWX_POWER_FLAGS_SKIP_OVER_DTIM_MSK: '0' PM have to walk up every DTIM,
 *		'1' PM could sleep over DTIM till listen Interval.
 * @IWX_POWER_FLAGS_SNOOZE_ENA_MSK: Enable snoozing only if uAPSD is enabled and all
 *		access categories are both delivery and trigger enabled.
 * @IWX_POWER_FLAGS_BT_SCO_ENA: Enable BT SCO coex only if uAPSD and
 *		PBW Snoozing enabled
 * @IWX_POWER_FLAGS_ADVANCE_PM_ENA_MSK: Advanced PM (uAPSD) enable mask
 * @IWX_POWER_FLAGS_LPRX_ENA_MSK: Low Power RX enable.
 * @IWX_POWER_FLAGS_AP_UAPSD_MISBEHAVING_ENA_MSK: AP/GO's uAPSD misbehaving
 *		detection enablement
*/
#define IWX_POWER_FLAGS_POWER_SAVE_ENA_MSK		(1 << 0)
#define IWX_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK	(1 << 1)
#define IWX_POWER_FLAGS_SKIP_OVER_DTIM_MSK		(1 << 2)
#define IWX_POWER_FLAGS_SNOOZE_ENA_MSK			(1 << 5)
#define IWX_POWER_FLAGS_BT_SCO_ENA			(1 << 8)
#define IWX_POWER_FLAGS_ADVANCE_PM_ENA_MSK		(1 << 9)
#define IWX_POWER_FLAGS_LPRX_ENA_MSK			(1 << 11)
#define IWX_POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK	(1 << 12)

#define IWX_POWER_VEC_SIZE 5

/**
 * Masks for device power command flags
 * @IWX_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK:
 *   '1' Allow to save power by turning off receiver and transmitter.
 *   '0' Do not allow. This flag should be always set to '1' unless
 *       one needs to disable actual power down for debug purposes.
 * @IWX_DEVICE_POWER_FLAGS_CAM_MSK:
 *   '1' CAM (Continuous Active Mode) is set, power management is disabled.
 *   '0' Power management is enabled, one of the power schemes is applied.
 */
#define IWX_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK	(1 << 0)
#define IWX_DEVICE_POWER_FLAGS_CAM_MSK			(1 << 13)

/**
 * struct iwx_device_power_cmd - device wide power command.
 * IWX_POWER_TABLE_CMD = 0x77 (command, has simple generic response)
 *
 * @flags:	Power table command flags from IWX_DEVICE_POWER_FLAGS_*
 */
struct iwx_device_power_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_6 */
	uint16_t flags;
	uint16_t reserved;
} __packed;

/**
 * struct iwx_mac_power_cmd - New power command containing uAPSD support
 * IWX_MAC_PM_POWER_TABLE = 0xA9 (command, has simple generic response)
 * @id_and_color:	MAC context identifier
 * @flags:		Power table command flags from POWER_FLAGS_*
 * @keep_alive_seconds:	Keep alive period in seconds. Default - 25 sec.
 *			Minimum allowed:- 3 * DTIM. Keep alive period must be
 *			set regardless of power scheme or current power state.
 *			FW use this value also when PM is disabled.
 * @rx_data_timeout:    Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - legacy PM
 * @tx_data_timeout:    Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - legacy PM
 * @sleep_interval:	not in use
 * @skip_dtim_periods:	Number of DTIM periods to skip if Skip over DTIM flag
 *			is set. For example, if it is required to skip over
 *			one DTIM, this value need to be set to 2 (DTIM periods).
 * @rx_data_timeout_uapsd: Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - uAPSD
 * @tx_data_timeout_uapsd: Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - uAPSD
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 * @num_skip_dtim:	Number of DTIMs to skip if Skip over DTIM flag is set
 * @snooze_interval:	Maximum time between attempts to retrieve buffered data
 *			from the AP [msec]
 * @snooze_window:	A window of time in which PBW snoozing insures that all
 *			packets received. It is also the minimum time from last
 *			received unicast RX packet, before client stops snoozing
 *			for data. [msec]
 * @snooze_step:	TBD
 * @qndp_tid:		TID client shall use for uAPSD QNDP triggers
 * @uapsd_ac_flags:	Set trigger-enabled and delivery-enabled indication for
 *			each corresponding AC.
 *			Use IEEE80211_WMM_IE_STA_QOSINFO_AC* for correct values.
 * @uapsd_max_sp:	Use IEEE80211_WMM_IE_STA_QOSINFO_SP_* for correct
 *			values.
 * @heavy_tx_thld_packets:	TX threshold measured in number of packets
 * @heavy_rx_thld_packets:	RX threshold measured in number of packets
 * @heavy_tx_thld_percentage:	TX threshold measured in load's percentage
 * @heavy_rx_thld_percentage:	RX threshold measured in load's percentage
 * @limited_ps_threshold:
*/
struct iwx_mac_power_cmd {
	/* CONTEXT_DESC_API_T_VER_1 */
	uint32_t id_and_color;

	/* CLIENT_PM_POWER_TABLE_S_VER_1 */
	uint16_t flags;
	uint16_t keep_alive_seconds;
	uint32_t rx_data_timeout;
	uint32_t tx_data_timeout;
	uint32_t rx_data_timeout_uapsd;
	uint32_t tx_data_timeout_uapsd;
	uint8_t lprx_rssi_threshold;
	uint8_t skip_dtim_periods;
	uint16_t snooze_interval;
	uint16_t snooze_window;
	uint8_t snooze_step;
	uint8_t qndp_tid;
	uint8_t uapsd_ac_flags;
	uint8_t uapsd_max_sp;
	uint8_t heavy_tx_thld_packets;
	uint8_t heavy_rx_thld_packets;
	uint8_t heavy_tx_thld_percentage;
	uint8_t heavy_rx_thld_percentage;
	uint8_t limited_ps_threshold;
	uint8_t reserved;
} __packed;

#define IWX_DEFAULT_PS_TX_DATA_TIMEOUT      (100 * 1000)
#define IWX_DEFAULT_PS_RX_DATA_TIMEOUT      (100 * 1000)

/*
 * struct iwx_uapsd_misbehaving_ap_notif - FW sends this notification when
 * associated AP is identified as improperly implementing uAPSD protocol.
 * IWX_PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION = 0x78
 * @sta_id: index of station in uCode's station table - associated AP ID in
 *	    this context.
 */
struct iwx_uapsd_misbehaving_ap_notif {
	uint32_t sta_id;
	uint8_t mac_id;
	uint8_t reserved[3];
} __packed;

/**
 * struct iwx_beacon_filter_cmd
 * IWX_REPLY_BEACON_FILTERING_CMD = 0xd2 (command)
 * @id_and_color: MAC context identifier
 * @bf_energy_delta: Used for RSSI filtering, if in 'normal' state. Send beacon
 *      to driver if delta in Energy values calculated for this and last
 *      passed beacon is greater than this threshold. Zero value means that
 *      the Energy change is ignored for beacon filtering, and beacon will
 *      not be forced to be sent to driver regardless of this delta. Typical
 *      energy delta 5dB.
 * @bf_roaming_energy_delta: Used for RSSI filtering, if in 'roaming' state.
 *      Send beacon to driver if delta in Energy values calculated for this
 *      and last passed beacon is greater than this threshold. Zero value
 *      means that the Energy change is ignored for beacon filtering while in
 *      Roaming state, typical energy delta 1dB.
 * @bf_roaming_state: Used for RSSI filtering. If absolute Energy values
 *      calculated for current beacon is less than the threshold, use
 *      Roaming Energy Delta Threshold, otherwise use normal Energy Delta
 *      Threshold. Typical energy threshold is -72dBm.
 * @bf_temp_threshold: This threshold determines the type of temperature
 *	filtering (Slow or Fast) that is selected (Units are in Celsius):
 *      If the current temperature is above this threshold - Fast filter
 *	will be used, If the current temperature is below this threshold -
 *	Slow filter will be used.
 * @bf_temp_fast_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be  forced to be sent to driver
 *      regardless of whether its temperature has been changed.
 * @bf_temp_slow_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be forced to be sent to driver
 *      regardless of whether its temperature has been changed.
 * @bf_enable_beacon_filter: 1, beacon filtering is enabled; 0, disabled.
 * @bf_escape_timer: Send beacons to driver if no beacons were passed
 *      for a specific period of time. Units: Beacons.
 * @ba_escape_timer: Fully receive and parse beacon if no beacons were passed
 *      for a longer period of time then this escape-timeout. Units: Beacons.
 * @ba_enable_beacon_abort: 1, beacon abort is enabled; 0, disabled.
 * @bf_threshold_absolute_low: See below.
 * @bf_threshold_absolute_high: Send Beacon to driver if Energy value calculated
 *      for this beacon crossed this absolute threshold. For the 'Increase'
 *      direction the bf_energy_absolute_low[i] is used. For the 'Decrease'
 *      direction the bf_energy_absolute_high[i] is used. Zero value means
 *      that this specific threshold is ignored for beacon filtering, and
 *      beacon will not be forced to be sent to driver due to this setting.
 */
struct iwx_beacon_filter_cmd {
	uint32_t bf_energy_delta;
	uint32_t bf_roaming_energy_delta;
	uint32_t bf_roaming_state;
	uint32_t bf_temp_threshold;
	uint32_t bf_temp_fast_filter;
	uint32_t bf_temp_slow_filter;
	uint32_t bf_enable_beacon_filter;
	uint32_t bf_debug_flag;
	uint32_t bf_escape_timer;
	uint32_t ba_escape_timer;
	uint32_t ba_enable_beacon_abort;
	uint32_t bf_threshold_absolute_low[2];
	uint32_t bf_threshold_absolute_high[2];
} __packed; /* BEACON_FILTER_CONFIG_API_S_VER_4 */

/* Beacon filtering and beacon abort */
#define IWX_BF_ENERGY_DELTA_DEFAULT 5
#define IWX_BF_ENERGY_DELTA_MAX 255
#define IWX_BF_ENERGY_DELTA_MIN 0

#define IWX_BF_ROAMING_ENERGY_DELTA_DEFAULT 1
#define IWX_BF_ROAMING_ENERGY_DELTA_MAX 255
#define IWX_BF_ROAMING_ENERGY_DELTA_MIN 0

#define IWX_BF_ROAMING_STATE_DEFAULT 72
#define IWX_BF_ROAMING_STATE_MAX 255
#define IWX_BF_ROAMING_STATE_MIN 0

#define IWX_BF_TEMP_THRESHOLD_DEFAULT 112
#define IWX_BF_TEMP_THRESHOLD_MAX 255
#define IWX_BF_TEMP_THRESHOLD_MIN 0

#define IWX_BF_TEMP_FAST_FILTER_DEFAULT 1
#define IWX_BF_TEMP_FAST_FILTER_MAX 255
#define IWX_BF_TEMP_FAST_FILTER_MIN 0

#define IWX_BF_TEMP_SLOW_FILTER_DEFAULT 5
#define IWX_BF_TEMP_SLOW_FILTER_MAX 255
#define IWX_BF_TEMP_SLOW_FILTER_MIN 0

#define IWX_BF_ENABLE_BEACON_FILTER_DEFAULT 1

#define IWX_BF_DEBUG_FLAG_DEFAULT 0

#define IWX_BF_ESCAPE_TIMER_DEFAULT 50
#define IWX_BF_ESCAPE_TIMER_MAX 1024
#define IWX_BF_ESCAPE_TIMER_MIN 0

#define IWX_BA_ESCAPE_TIMER_DEFAULT 6
#define IWX_BA_ESCAPE_TIMER_D3 9
#define IWX_BA_ESCAPE_TIMER_MAX 1024
#define IWX_BA_ESCAPE_TIMER_MIN 0

#define IWX_BA_ENABLE_BEACON_ABORT_DEFAULT 1

#define IWX_BF_CMD_CONFIG_DEFAULTS					     \
	.bf_energy_delta = htole32(IWX_BF_ENERGY_DELTA_DEFAULT),	     \
	.bf_roaming_energy_delta =					     \
		htole32(IWX_BF_ROAMING_ENERGY_DELTA_DEFAULT),	     \
	.bf_roaming_state = htole32(IWX_BF_ROAMING_STATE_DEFAULT),	     \
	.bf_temp_threshold = htole32(IWX_BF_TEMP_THRESHOLD_DEFAULT),     \
	.bf_temp_fast_filter = htole32(IWX_BF_TEMP_FAST_FILTER_DEFAULT), \
	.bf_temp_slow_filter = htole32(IWX_BF_TEMP_SLOW_FILTER_DEFAULT), \
	.bf_debug_flag = htole32(IWX_BF_DEBUG_FLAG_DEFAULT),	     \
	.bf_escape_timer = htole32(IWX_BF_ESCAPE_TIMER_DEFAULT),	     \
	.ba_escape_timer = htole32(IWX_BA_ESCAPE_TIMER_DEFAULT)

/* uCode API values for HT/VHT bit rates */
#define IWX_RATE_HT_SISO_MCS_0_PLCP	0
#define IWX_RATE_HT_SISO_MCS_1_PLCP	1
#define IWX_RATE_HT_SISO_MCS_2_PLCP	2
#define IWX_RATE_HT_SISO_MCS_3_PLCP	3
#define IWX_RATE_HT_SISO_MCS_4_PLCP	4
#define IWX_RATE_HT_SISO_MCS_5_PLCP	5
#define IWX_RATE_HT_SISO_MCS_6_PLCP	6
#define IWX_RATE_HT_SISO_MCS_7_PLCP	7
#define IWX_RATE_HT_MIMO2_MCS_8_PLCP	0x8
#define IWX_RATE_HT_MIMO2_MCS_9_PLCP	0x9
#define IWX_RATE_HT_MIMO2_MCS_10_PLCP	0xA
#define IWX_RATE_HT_MIMO2_MCS_11_PLCP	0xB
#define IWX_RATE_HT_MIMO2_MCS_12_PLCP	0xC
#define IWX_RATE_HT_MIMO2_MCS_13_PLCP	0xD
#define IWX_RATE_HT_MIMO2_MCS_14_PLCP	0xE
#define IWX_RATE_HT_MIMO2_MCS_15_PLCP	0xF
#define IWX_RATE_VHT_SISO_MCS_0_PLCP	0
#define IWX_RATE_VHT_SISO_MCS_1_PLCP	1
#define IWX_RATE_VHT_SISO_MCS_2_PLCP	2
#define IWX_RATE_VHT_SISO_MCS_3_PLCP	3
#define IWX_RATE_VHT_SISO_MCS_4_PLCP	4
#define IWX_RATE_VHT_SISO_MCS_5_PLCP	5
#define IWX_RATE_VHT_SISO_MCS_6_PLCP	6
#define IWX_RATE_VHT_SISO_MCS_7_PLCP	7
#define IWX_RATE_VHT_SISO_MCS_8_PLCP	8
#define IWX_RATE_VHT_SISO_MCS_9_PLCP	9
#define IWX_RATE_VHT_MIMO2_MCS_0_PLCP	0x10
#define IWX_RATE_VHT_MIMO2_MCS_1_PLCP	0x11
#define IWX_RATE_VHT_MIMO2_MCS_2_PLCP	0x12
#define IWX_RATE_VHT_MIMO2_MCS_3_PLCP	0x13
#define IWX_RATE_VHT_MIMO2_MCS_4_PLCP	0x14
#define IWX_RATE_VHT_MIMO2_MCS_5_PLCP	0x15
#define IWX_RATE_VHT_MIMO2_MCS_6_PLCP	0x16
#define IWX_RATE_VHT_MIMO2_MCS_7_PLCP	0x17
#define IWX_RATE_VHT_MIMO2_MCS_8_PLCP	0x18
#define IWX_RATE_VHT_MIMO2_MCS_9_PLCP	0x19
#define IWX_RATE_HT_SISO_MCS_INV_PLCP	0x20
#define IWX_RATE_HT_MIMO2_MCS_INV_PLCP	IWX_RATE_HT_SISO_MCS_INV_PLCP
#define IWX_RATE_VHT_SISO_MCS_INV_PLCP	IWX_RATE_HT_SISO_MCS_INV_PLCP
#define IWX_RATE_VHT_MIMO2_MCS_INV_PLCP	IWX_RATE_HT_SISO_MCS_INV_PLCP
#define IWX_RATE_HT_SISO_MCS_8_PLCP	IWX_RATE_HT_SISO_MCS_INV_PLCP
#define IWX_RATE_HT_SISO_MCS_9_PLCP	IWX_RATE_HT_SISO_MCS_INV_PLCP

/*
 * These serve as indexes into struct iwx_rate iwx_rates[IWX_RIDX_MAX].
 */
enum {
	IWX_RATE_1M_INDEX = 0,
	IWX_FIRST_CCK_RATE = IWX_RATE_1M_INDEX,
	IWX_RATE_2M_INDEX,
	IWX_RATE_5M_INDEX,
	IWX_RATE_11M_INDEX,
	IWX_LAST_CCK_RATE = IWX_RATE_11M_INDEX,
	IWX_RATE_6M_INDEX,
	IWX_FIRST_OFDM_RATE = IWX_RATE_6M_INDEX,
	IWX_RATE_MCS_0_INDEX = IWX_RATE_6M_INDEX,
	IWX_FIRST_HT_RATE = IWX_RATE_MCS_0_INDEX,
	IWX_FIRST_VHT_RATE = IWX_RATE_MCS_0_INDEX,
	IWX_RATE_9M_INDEX,
	IWX_RATE_12M_INDEX,
	IWX_RATE_MCS_1_INDEX = IWX_RATE_12M_INDEX,
	IWX_RATE_MCS_8_INDEX,
	IWX_FIRST_HT_MIMO2_RATE = IWX_RATE_MCS_8_INDEX,
	IWX_RATE_18M_INDEX,
	IWX_RATE_MCS_2_INDEX = IWX_RATE_18M_INDEX,
	IWX_RATE_24M_INDEX,
	IWX_RATE_MCS_3_INDEX = IWX_RATE_24M_INDEX,
	IWX_RATE_MCS_9_INDEX,
	IWX_RATE_36M_INDEX,
	IWX_RATE_MCS_4_INDEX = IWX_RATE_36M_INDEX,
	IWX_RATE_MCS_10_INDEX,
	IWX_RATE_48M_INDEX,
	IWX_RATE_MCS_5_INDEX = IWX_RATE_48M_INDEX,
	IWX_RATE_MCS_11_INDEX,
	IWX_RATE_54M_INDEX,
	IWX_RATE_MCS_6_INDEX = IWX_RATE_54M_INDEX,
	IWX_LAST_NON_HT_RATE = IWX_RATE_54M_INDEX,
	IWX_RATE_MCS_7_INDEX,
	IWX_LAST_HT_SISO_RATE = IWX_RATE_MCS_7_INDEX,
	IWX_RATE_MCS_12_INDEX,
	IWX_RATE_MCS_13_INDEX,
	IWX_RATE_MCS_14_INDEX,
	IWX_RATE_MCS_15_INDEX,
	IWX_LAST_HT_RATE = IWX_RATE_MCS_15_INDEX,
	IWX_LAST_VHT_RATE = IWX_RATE_MCS_9_INDEX,
	IWX_RATE_COUNT_LEGACY = IWX_LAST_NON_HT_RATE + 1,
	IWX_RATE_COUNT = IWX_LAST_HT_RATE + 1,
};

#define IWX_RATE_BIT_MSK(r) (1 << (IWX_RATE_##r##M_INDEX))

/* fw API values for legacy bit rates, both OFDM and CCK */
#define IWX_RATE_6M_PLCP 	13
#define IWX_RATE_9M_PLCP 	15
#define IWX_RATE_12M_PLCP	5
#define IWX_RATE_18M_PLCP	7
#define IWX_RATE_24M_PLCP	9
#define IWX_RATE_36M_PLCP	11
#define IWX_RATE_48M_PLCP	1
#define IWX_RATE_54M_PLCP	3
#define IWX_RATE_1M_PLCP 	10
#define IWX_RATE_2M_PLCP 	20
#define IWX_RATE_5M_PLCP 	55
#define IWX_RATE_11M_PLCP	110
#define IWX_RATE_INVM_PLCP	0xff

/*
 * rate_n_flags bit fields version 1
 *
 * The 32-bit value has different layouts in the low 8 bites depending on the
 * format. There are three formats, HT, VHT and legacy (11abg, with subformats
 * for CCK and OFDM).
 *
 * High-throughput (HT) rate format
 *	bit 8 is 1, bit 26 is 0, bit 9 is 0 (OFDM)
 * Very High-throughput (VHT) rate format
 *	bit 8 is 0, bit 26 is 1, bit 9 is 0 (OFDM)
 * Legacy OFDM rate format for bits 7:0
 *	bit 8 is 0, bit 26 is 0, bit 9 is 0 (OFDM)
 * Legacy CCK rate format for bits 7:0:
 *	bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK)
 */

/* Bit 8: (1) HT format, (0) legacy or VHT format */
#define IWX_RATE_MCS_HT_POS 8
#define IWX_RATE_MCS_HT_MSK_V1 (1 << IWX_RATE_MCS_HT_POS)

/* Bit 9: (1) CCK, (0) OFDM.  HT (bit 8) must be "0" for this bit to be valid */
#define IWX_RATE_MCS_CCK_POS_V1 9
#define IWX_RATE_MCS_CCK_MSK_V1 (1 << IWX_RATE_MCS_CCK_POS_V1)

/* Bit 26: (1) VHT format, (0) legacy format in bits 8:0 */
#define IWX_RATE_MCS_VHT_POS_V1 26
#define IWX_RATE_MCS_VHT_MSK_V1 (1 << IWX_RATE_MCS_VHT_POS_V1)


/*
 * High-throughput (HT) rate format for bits 7:0
 *
 *  2-0:  MCS rate base
 *        0)   6 Mbps
 *        1)  12 Mbps
 *        2)  18 Mbps
 *        3)  24 Mbps
 *        4)  36 Mbps
 *        5)  48 Mbps
 *        6)  54 Mbps
 *        7)  60 Mbps
 *  4-3:  0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 *    5:  Value of 0x20 in bits 7:0 indicates 6 Mbps HT40 duplicate data
 *  (bits 7-6 are zero)
 *
 * Together the low 5 bits work out to the MCS index because we don't
 * support MCSes above 15/23, and 0-7 have one stream, 8-15 have two
 * streams and 16-23 have three streams. We could also support MCS 32
 * which is the duplicate 20 MHz MCS (bit 5 set, all others zero.)
 */
#define IWX_RATE_HT_MCS_RATE_CODE_MSK_V1	0x7
#define IWX_RATE_HT_MCS_NSS_POS_V1             3
#define IWX_RATE_HT_MCS_NSS_MSK_V1             (3 << IWX_RATE_HT_MCS_NSS_POS_V1)
#define IWX_RATE_HT_MCS_MIMO2_MSK_V1           (1 << IWX_RATE_HT_MCS_NSS_POS_V1)

/* Bit 10: (1) Use Green Field preamble */
#define IWX_RATE_HT_MCS_GF_POS		10
#define IWX_RATE_HT_MCS_GF_MSK		(1 << IWX_RATE_HT_MCS_GF_POS)

#define IWX_RATE_HT_MCS_INDEX_MSK_V1		0x3f

/*
 * Very High-throughput (VHT) rate format for bits 7:0
 *
 *  3-0:  VHT MCS (0-9)
 *  5-4:  number of streams - 1:
 *        0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 */

/* Bit 4-5: (0) SISO, (1) MIMO2 (2) MIMO3 */
#define IWX_RATE_VHT_MCS_RATE_CODE_MSK	0xf
#define IWX_RATE_VHT_MCS_NSS_POS		4
#define IWX_RATE_VHT_MCS_NSS_MSK		(3 << IWX_RATE_VHT_MCS_NSS_POS)
#define IWX_RATE_VHT_MCS_MIMO2_MSK		(1 << IWX_RATE_VHT_MCS_NSS_POS)

/*
 * Legacy OFDM rate format for bits 7:0
 *
 *  3-0:  0xD)   6 Mbps
 *        0xF)   9 Mbps
 *        0x5)  12 Mbps
 *        0x7)  18 Mbps
 *        0x9)  24 Mbps
 *        0xB)  36 Mbps
 *        0x1)  48 Mbps
 *        0x3)  54 Mbps
 * (bits 7-4 are 0)
 *
 * Legacy CCK rate format for bits 7:0:
 * bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK):
 *
 *  6-0:   10)  1 Mbps
 *         20)  2 Mbps
 *         55)  5.5 Mbps
 *        110)  11 Mbps
 * (bit 7 is 0)
 */
#define IWX_RATE_LEGACY_RATE_MSK_V1 0xff

/* Bit 10 - OFDM HE */
#define IWX_RATE_MCS_HE_POS_V1	10
#define IWX_RATE_MCS_HE_MSK_V1	(1 << RATE_MCS_HE_POS_V1)

/*
 * Bit 11-12: (0) 20MHz, (1) 40MHz, (2) 80MHz, (3) 160MHz
 * 0 and 1 are valid for HT and VHT, 2 and 3 only for VHT
 */
#define IWX_RATE_MCS_CHAN_WIDTH_POS		11
#define IWX_RATE_MCS_CHAN_WIDTH_MSK_V1		(3 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_20_V1		(0 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_40_V1		(1 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_80_V1		(2 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_160_V1		(3 << IWX_RATE_MCS_CHAN_WIDTH_POS)

/* Bit 13: (1) Short guard interval (0.4 usec), (0) normal GI (0.8 usec) */
#define IWX_RATE_MCS_SGI_POS_V1		13
#define IWX_RATE_MCS_SGI_MSK_V1		(1 << IWX_RATE_MCS_SGI_POS_V1)

/* Bit 14-16: Antenna selection (1) Ant A, (2) Ant B, (4) Ant C (unused) */
#define IWX_RATE_MCS_ANT_POS		14
#define IWX_RATE_MCS_ANT_A_MSK		(1 << IWX_RATE_MCS_ANT_POS)
#define IWX_RATE_MCS_ANT_B_MSK		(2 << IWX_RATE_MCS_ANT_POS)
#define IWX_RATE_MCS_ANT_AB_MSK		(IWX_RATE_MCS_ANT_A_MSK | \
					 IWX_RATE_MCS_ANT_B_MSK)
#define IWX_RATE_MCS_ANT_MSK		IWX_RATE_MCS_ANT_ABC_MSK
#define IWX_RATE_MCS_ANT_NUM 2

/* Bit 17: (0) SS, (1) SS*2 */
#define IWX_RATE_MCS_STBC_POS		17
#define IWX_RATE_MCS_STBC_MSK		(1 << IWX_RATE_MCS_STBC_POS)

/* Bit 18: OFDM-HE dual carrier mode */
#define IWX_RATE_HE_DUAL_CARRIER_MODE	18
#define IWX_RATE_HE_DUAL_CARRIER_MODE_MSK (1 << IWX_RATE_HE_DUAL_CARRIER_MODE)

/* Bit 19: (0) Beamforming is off, (1) Beamforming is on */
#define IWX_RATE_MCS_BF_POS			19
#define IWX_RATE_MCS_BF_MSK			(1 << IWX_RATE_MCS_BF_POS)

/*
 * Bit 20-21: HE LTF type and guard interval
 * HE (ext) SU:
 *	0			1xLTF+0.8us
 *	1			2xLTF+0.8us
 *	2			2xLTF+1.6us
 *	3 & SGI (bit 13) clear	4xLTF+3.2us
 *	3 & SGI (bit 13) set	4xLTF+0.8us
 * HE MU:
 *	0			4xLTF+0.8us
 *	1			2xLTF+0.8us
 *	2			2xLTF+1.6us
 *	3			4xLTF+3.2us
 * HE TRIG:
 *	0			1xLTF+1.6us
 *	1			2xLTF+1.6us
 *	2			4xLTF+3.2us
 *	3			(does not occur)
 */
#define IWX_RATE_MCS_HE_GI_LTF_POS	20
#define IWX_RATE_MCS_HE_GI_LTF_MSK_V1	(3 << IWX_RATE_MCS_HE_GI_LTF_POS)

/* Bit 22-23: HE type. (0) SU, (1) SU_EXT, (2) MU, (3) trigger based */
#define IWX_RATE_MCS_HE_TYPE_POS_V1	22
#define IWX_RATE_MCS_HE_TYPE_SU_V1	(0 << IWX_RATE_MCS_HE_TYPE_POS_V1)
#define IWX_RATE_MCS_HE_TYPE_EXT_SU_V1	(1 << IWX_RATE_MCS_HE_TYPE_POS_V1)
#define IWX_RATE_MCS_HE_TYPE_MU_V1	(2 << IWX_RATE_MCS_HE_TYPE_POS_V1)
#define IWX_RATE_MCS_HE_TYPE_TRIG_V1	(3 << IWX_RATE_MCS_HE_TYPE_POS_V1)
#define IWX_RATE_MCS_HE_TYPE_MSK_V1	(3 << IWX_RATE_MCS_HE_TYPE_POS_V1)

/* Bit 24-25: (0) 20MHz (no dup), (1) 2x20MHz, (2) 4x20MHz, 3 8x20MHz */
#define IWX_RATE_MCS_DUP_POS_V1		24
#define IWX_RATE_MCS_DUP_MSK_V1		(3 << IWX_RATE_MCS_DUP_POS_V1)

/* Bit 27: (1) LDPC enabled, (0) LDPC disabled */
#define IWX_RATE_MCS_LDPC_POS_V1	27
#define IWX_RATE_MCS_LDPC_MSK_V1	(1 << IWX_RATE_MCS_LDPC_POS_V1)

/* Bit 28: (1) 106-tone RX (8 MHz RU), (0) normal bandwidth */
#define IWX_RATE_MCS_HE_106T_POS_V1	28
#define IWX_RATE_MCS_HE_106T_MSK_V1	(1 << IWX_RATE_MCS_HE_106T_POS_V1)

/* Bit 30-31: (1) RTS, (2) CTS */
#define IWX_RATE_MCS_RTS_REQUIRED_POS  (30)
#define IWX_RATE_MCS_RTS_REQUIRED_MSK  (1 << IWX_RATE_MCS_RTS_REQUIRED_POS)
#define IWX_RATE_MCS_CTS_REQUIRED_POS  (31)
#define IWX_RATE_MCS_CTS_REQUIRED_MSK  (1 << IWX_RATE_MCS_CTS_REQUIRED_POS)


/* rate_n_flags bit field version 2
 *
 * The 32-bit value has different layouts in the low 8 bits depending on the
 * format. There are three formats, HT, VHT and legacy (11abg, with subformats
 * for CCK and OFDM).
 *
 */

/* Bits 10-8: rate format
 * (0) Legacy CCK (1) Legacy OFDM (2) High-throughput (HT)
 * (3) Very High-throughput (VHT) (4) High-efficiency (HE)
 * (5) Extremely High-throughput (EHT)
 */
#define IWX_RATE_MCS_MOD_TYPE_POS	8
#define IWX_RATE_MCS_MOD_TYPE_MSK	(0x7 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_CCK_MSK		(0 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_LEGACY_OFDM_MSK	(1 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_HT_MSK		(2 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_VHT_MSK		(3 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_HE_MSK		(4 << IWX_RATE_MCS_MOD_TYPE_POS)
#define IWX_RATE_MCS_EHT_MSK		(5 << IWX_RATE_MCS_MOD_TYPE_POS)

/*
 * Legacy CCK rate format for bits 0:3:
 *
 * (0) 0xa - 1 Mbps
 * (1) 0x14 - 2 Mbps
 * (2) 0x37 - 5.5 Mbps
 * (3) 0x6e - 11 nbps
 *
 * Legacy OFDM rate format for bits 0:3:
 *
 * (0) 6 Mbps
 * (1) 9 Mbps
 * (2) 12 Mbps
 * (3) 18 Mbps
 * (4) 24 Mbps
 * (5) 36 Mbps
 * (6) 48 Mbps
 * (7) 54 Mbps
 *
 */
#define IWX_RATE_LEGACY_RATE_MSK	0x7

/*
 * HT, VHT, HE, EHT rate format for bits 3:0
 * 3-0: MCS
 *
 */
#define IWX_RATE_HT_MCS_CODE_MSK	0x7
#define IWX_RATE_MCS_NSS_POS		4
#define IWX_RATE_MCS_NSS_MSK		(1 << IWX_RATE_MCS_NSS_POS)
#define IWX_RATE_MCS_CODE_MSK		0xf
#define IWX_RATE_HT_MCS_INDEX(r)	((((r) & IWX_RATE_MCS_NSS_MSK) >> 1) | \
					 ((r) & IWX_RATE_HT_MCS_CODE_MSK))

/* Bits 7-5: reserved */

/*
 * Bits 13-11: (0) 20MHz, (1) 40MHz, (2) 80MHz, (3) 160MHz, (4) 320MHz
 */
#define IWX_RATE_MCS_CHAN_WIDTH_MSK	(0x7 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_20	(0 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_40	(1 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_80	(2 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_160	(3 << IWX_RATE_MCS_CHAN_WIDTH_POS)
#define IWX_RATE_MCS_CHAN_WIDTH_320	(4 << IWX_RATE_MCS_CHAN_WIDTH_POS)

/* Bit 15-14: Antenna selection:
 * Bit 14: Ant A active
 * Bit 15: Ant B active
 *
 * All relevant definitions are same as in v1
 */

/* Bit 16 (1) LDPC enables, (0) LDPC disabled */
#define IWX_RATE_MCS_LDPC_POS	16
#define IWX_RATE_MCS_LDPC_MSK	(1 << IWX_RATE_MCS_LDPC_POS)

/* Bit 17: (0) SS, (1) SS*2 (same as v1) */

/* Bit 18: OFDM-HE dual carrier mode (same as v1) */

/* Bit 19: (0) Beamforming is off, (1) Beamforming is on (same as v1) */

/*
 * Bit 22-20: HE LTF type and guard interval
 * CCK:
 *	0			long preamble
 *	1			short preamble
 * HT/VHT:
 *	0			0.8us
 *	1			0.4us
 * HE (ext) SU:
 *	0			1xLTF+0.8us
 *	1			2xLTF+0.8us
 *	2			2xLTF+1.6us
 *	3			4xLTF+3.2us
 *	4			4xLTF+0.8us
 * HE MU:
 *	0			4xLTF+0.8us
 *	1			2xLTF+0.8us
 *	2			2xLTF+1.6us
 *	3			4xLTF+3.2us
 * HE TRIG:
 *	0			1xLTF+1.6us
 *	1			2xLTF+1.6us
 *	2			4xLTF+3.2us
 * */
#define IWX_RATE_MCS_HE_GI_LTF_MSK	(0x7 << IWX_RATE_MCS_HE_GI_LTF_POS)
#define IWX_RATE_MCS_SGI_POS		IWX_RATE_MCS_HE_GI_LTF_POS
#define IWX_RATE_MCS_SGI_MSK		(1 << IWX_RATE_MCS_SGI_POS)
#define IWX_RATE_MCS_HE_SU_4_LTF	3
#define IWX_RATE_MCS_HE_SU_4_LTF_08_GI	4

/* Bit 24-23: HE type. (0) SU, (1) SU_EXT, (2) MU, (3) trigger based */
#define IWX_RATE_MCS_HE_TYPE_POS	23
#define IWX_RATE_MCS_HE_TYPE_SU		(0 << IWX_RATE_MCS_HE_TYPE_POS)
#define IWX_RATE_MCS_HE_TYPE_EXT_SU	(1 << IWX_RATE_MCS_HE_TYPE_POS)
#define IWX_RATE_MCS_HE_TYPE_MU		(2 << IWX_RATE_MCS_HE_TYPE_POS)
#define IWX_RATE_MCS_HE_TYPE_TRIG	(3 << IWX_RATE_MCS_HE_TYPE_POS)
#define IWX_RATE_MCS_HE_TYPE_MSK	(3 << IWX_RATE_MCS_HE_TYPE_POS)

/* Bit 25: duplicate channel enabled
 *
 * if this bit is set, duplicate is according to BW (bits 11-13):
 *
 * CCK:  2x 20MHz
 * OFDM Legacy: N x 20Mhz, (N = BW \ 2 , either 2, 4, 8, 16)
 * EHT: 2 x BW/2, (80 - 2x40, 160 - 2x80, 320 - 2x160)
 * */
#define IWX_RATE_MCS_DUP_POS		25
#define IWX_RATE_MCS_DUP_MSK		(1 << IWX_RATE_MCS_DUP_POS)

/* Bit 26: (1) 106-tone RX (8 MHz RU), (0) normal bandwidth */
#define IWX_RATE_MCS_HE_106T_POS	26
#define IWX_RATE_MCS_HE_106T_MSK	(1 << IWX_RATE_MCS_HE_106T_POS)

/* Bit 27: EHT extra LTF:
 * instead of 1 LTF for SISO use 2 LTFs,
 * instead of 2 LTFs for NSTS=2 use 4 LTFs*/
#define IWX_RATE_MCS_EHT_EXTRA_LTF_POS	27
#define IWX_RATE_MCS_EHT_EXTRA_LTF_MSK	(1 << IWX_RATE_MCS_EHT_EXTRA_LTF_POS)

/* Bit 31-28: reserved */


/* Link Quality definitions */

/* # entries in rate scale table to support Tx retries */
#define  IWX_LQ_MAX_RETRY_NUM 16

/* Link quality command flags bit fields */

/* Bit 0: (0) Don't use RTS (1) Use RTS */
#define IWX_LQ_FLAG_USE_RTS_POS             0
#define IWX_LQ_FLAG_USE_RTS_MSK	        (1 << IWX_LQ_FLAG_USE_RTS_POS)

/* Bit 1-3: LQ command color. Used to match responses to LQ commands */
#define IWX_LQ_FLAG_COLOR_POS               1
#define IWX_LQ_FLAG_COLOR_MSK               (7 << IWX_LQ_FLAG_COLOR_POS)

/* Bit 4-5: Tx RTS BW Signalling
 * (0) No RTS BW signalling
 * (1) Static BW signalling
 * (2) Dynamic BW signalling
 */
#define IWX_LQ_FLAG_RTS_BW_SIG_POS          4
#define IWX_LQ_FLAG_RTS_BW_SIG_NONE         (0 << IWX_LQ_FLAG_RTS_BW_SIG_POS)
#define IWX_LQ_FLAG_RTS_BW_SIG_STATIC       (1 << IWX_LQ_FLAG_RTS_BW_SIG_POS)
#define IWX_LQ_FLAG_RTS_BW_SIG_DYNAMIC      (2 << IWX_LQ_FLAG_RTS_BW_SIG_POS)

/* Bit 6: (0) No dynamic BW selection (1) Allow dynamic BW selection
 * Dynamic BW selection allows Tx with narrower BW then requested in rates
 */
#define IWX_LQ_FLAG_DYNAMIC_BW_POS          6
#define IWX_LQ_FLAG_DYNAMIC_BW_MSK          (1 << IWX_LQ_FLAG_DYNAMIC_BW_POS)

#define IWX_RLC_CHAIN_INFO_DRIVER_FORCE		(1 << 0)
#define IWL_RLC_CHAIN_INFO_VALID		0x000e
#define IWL_RLC_CHAIN_INFO_FORCE		0x0070
#define IWL_RLC_CHAIN_INFO_FORCE_MIMO		0x0380
#define IWL_RLC_CHAIN_INFO_COUNT		0x0c00
#define IWL_RLC_CHAIN_INFO_MIMO_COUNT		0x3000

/**
 * struct iwx_rlc_properties - RLC properties
 * @rx_chain_info: RX chain info, IWX_RLC_CHAIN_INFO_*
 * @reserved: reserved
 */
struct iwx_rlc_properties {
	uint32_t rx_chain_info;
	uint32_t reserved;
} __packed; /* RLC_PROPERTIES_S_VER_1 */

#define IWX_SAD_MODE_ENABLED		(1 << 0)
#define IWX_SAD_MODE_DEFAULT_ANT_MSK	0x6
#define IWX_SAD_MODE_DEFAULT_ANT_FW	0x0
#define IWX_SAD_MODE_DEFAULT_ANT_A	0x2
#define IWX_SAD_MODE_DEFAULT_ANT_B	0x4

/**
 * struct iwx_sad_properties - SAD properties
 * @chain_a_sad_mode: chain A SAD mode, IWX_SAD_MODE_*
 * @chain_b_sad_mode: chain B SAD mode, IWX_SAD_MODE_*
 * @mac_id: MAC index
 * @reserved: reserved
 */
struct iwx_sad_properties {
	uint32_t chain_a_sad_mode;
	uint32_t chain_b_sad_mode;
	uint32_t mac_id;
	uint32_t reserved;
} __packed;

/**
 * struct iwx_rlc_config_cmd - RLC configuration
 * @phy_id: PHY index
 * @rlc: RLC properties, &struct iwx_rlc_properties
 * @sad: SAD (single antenna diversity) options, &struct iwx_sad_properties
 * @flags: flags, IWX_RLC_FLAGS_*
 * @reserved: reserved
 */
struct iwx_rlc_config_cmd {
	uint32_t phy_id;
	struct iwx_rlc_properties rlc;
	struct iwx_sad_properties sad;
	uint8_t flags;
	uint8_t reserved[3];
} __packed; /* RLC_CONFIG_CMD_API_S_VER_2 */

#define IWX_MAX_BAID_OLD	16 /* MAX_IMMEDIATE_BA_API_D_VER_2 */
#define IWX_MAX_BAID		32 /* MAX_IMMEDIATE_BA_API_D_VER_3 */

/**
 * BAID allocation/config action
 * @IWX_RX_BAID_ACTION_ADD: add a new BAID session
 * @IWX_RX_BAID_ACTION_MODIFY: modify the BAID session
 * @IWX_RX_BAID_ACTION_REMOVE: remove the BAID session
 */
#define IWX_RX_BAID_ACTION_ADD		0
#define IWX_RX_BAID_ACTION_MODIFY	1
#define IWX_RX_BAID_ACTION_REMOVE	2
/*  RX_BAID_ALLOCATION_ACTION_E_VER_1 */

/**
 * struct iwx_rx_baid_cfg_cmd_alloc - BAID allocation data
 * @sta_id_mask: station ID mask
 * @tid: the TID for this session
 * @reserved: reserved
 * @ssn: the starting sequence number
 * @win_size: RX BA session window size
 */
struct iwx_rx_baid_cfg_cmd_alloc {
	uint32_t sta_id_mask;
	uint8_t tid;
	uint8_t reserved[3];
	uint16_t ssn;
	uint16_t win_size;
} __packed; /* RX_BAID_ALLOCATION_ADD_CMD_API_S_VER_1 */

/**
 * struct iwx_rx_baid_cfg_cmd_modify - BAID modification data
 * @old_sta_id_mask: old station ID mask
 * @new_sta_id_mask: new station ID mask
 * @tid: TID of the BAID
 */
struct iwx_rx_baid_cfg_cmd_modify {
	uint32_t old_sta_id_mask;
	uint32_t new_sta_id_mask;
	uint32_t tid;
} __packed; /* RX_BAID_ALLOCATION_MODIFY_CMD_API_S_VER_2 */

/**
 * struct iwx_rx_baid_cfg_cmd_remove_v1 - BAID removal data
 * @baid: the BAID to remove
 */
struct iwx_rx_baid_cfg_cmd_remove_v1 {
	uint32_t baid;
} __packed; /* RX_BAID_ALLOCATION_REMOVE_CMD_API_S_VER_1 */

/**
 * struct iwx_rx_baid_cfg_cmd_remove - BAID removal data
 * @sta_id_mask: the station mask of the BAID to remove
 * @tid: the TID of the BAID to remove
 */
struct iwx_rx_baid_cfg_cmd_remove {
	uint32_t sta_id_mask;
	uint32_t tid;
} __packed; /* RX_BAID_ALLOCATION_REMOVE_CMD_API_S_VER_2 */

/**
 * struct iwx_rx_baid_cfg_cmd - BAID allocation/config command
 * @action: the action, from &enum iwx_rx_baid_action
 */
struct iwx_rx_baid_cfg_cmd {
	uint32_t action;
	union {
		struct iwx_rx_baid_cfg_cmd_alloc alloc;
		struct iwx_rx_baid_cfg_cmd_modify modify;
		struct iwx_rx_baid_cfg_cmd_remove_v1 remove_v1;
		struct iwx_rx_baid_cfg_cmd_remove remove;
	}; /* RX_BAID_ALLOCATION_OPERATION_API_U_VER_2 */
} __packed; /* RX_BAID_ALLOCATION_CONFIG_CMD_API_S_VER_2 */

/**
 * struct iwx_rx_baid_cfg_resp - BAID allocation response
 * @baid: the allocated BAID
 */
struct iwx_rx_baid_cfg_resp {
	uint32_t baid;
}; /* RX_BAID_ALLOCATION_RESPONSE_API_S_VER_1 */

/**
 * scheduler queue operation
 * @IWX_SCD_QUEUE_ADD: allocate a new queue
 * @IWX_SCD_QUEUE_REMOVE: remove a queue
 * @IWX_SCD_QUEUE_MODIFY: modify a queue
 */
#define IWX_SCD_QUEUE_ADD	0
#define IWX_SCD_QUEUE_REMOVE	1
#define IWX_SCD_QUEUE_MODIFY	2

/**
 * struct iwx_scd_queue_cfg_cmd - scheduler queue allocation command
 * @operation: the operation, see &enum iwl_scd_queue_cfg_operation
 * @u.add.sta_mask: station mask
 * @u.add.tid: TID
 * @u.add.reserved: reserved
 * @u.add.flags: flags from &enum iwl_tx_queue_cfg_actions, except
 *	%TX_QUEUE_CFG_ENABLE_QUEUE is not valid
 * @u.add.cb_size: size code
 * @u.add.bc_dram_addr: byte-count table IOVA
 * @u.add.tfdq_dram_addr: TFD queue IOVA
 * @u.remove.sta_mask: station mask of queue to remove
 * @u.remove.tid: TID of queue to remove
 * @u.modify.old_sta_mask: old station mask for modify
 * @u.modify.tid: TID of queue to modify
 * @u.modify.new_sta_mask: new station mask for modify
 */
struct iwx_scd_queue_cfg_cmd {
	uint32_t operation;
	union {
		struct {
			uint32_t sta_mask;
			uint8_t tid;
			uint8_t reserved[3];
			uint32_t flags;
			uint32_t cb_size;
			uint64_t bc_dram_addr;
			uint64_t tfdq_dram_addr;
		} __packed add; /* TX_QUEUE_CFG_CMD_ADD_API_S_VER_1 */
		struct {
			uint32_t sta_mask;
			uint32_t tid;
		} __packed remove; /* TX_QUEUE_CFG_CMD_REMOVE_API_S_VER_1 */
		struct {
			uint32_t old_sta_mask;
			uint32_t tid;
			uint32_t new_sta_mask;
		} __packed modify; /* TX_QUEUE_CFG_CMD_MODIFY_API_S_VER_1 */
	} __packed u; /* TX_QUEUE_CFG_CMD_OPERATION_API_U_VER_1 */
} __packed; /* TX_QUEUE_CFG_CMD_API_S_VER_3 */

/**
 * enum iwx_sec_key_flags - security key command key flags
 * @IWX_SEC_KEY_FLAG_CIPHER_MASK: cipher mask
 * @IWX_SEC_KEY_FLAG_CIPHER_WEP: WEP cipher
 * @IWX_SEC_KEY_FLAG_CIPHER_CCMP: CCMP/CMAC cipher
 * @IWX_SEC_KEY_FLAG_CIPHER_TKIP: TKIP cipher
 * @IWX_SEC_KEY_FLAG_CIPHER_GCMP: GCMP/GMAC cipher
 * @IWX_SEC_KEY_FLAG_NO_TX: don't install for TX
 * @IWX_SEC_KEY_FLAG_KEY_SIZE: large key size (WEP-104, GCMP-256, GMAC-256)
 * @IWX_SEC_KEY_FLAG_MFP: MFP is in used for this key
 * @IWX_SEC_KEY_FLAG_MCAST_KEY: this is a multicast key
 * @IWX_SEC_KEY_FLAG_SPP_AMSDU: SPP A-MSDU should be used
 */
enum iwx_sec_key_flags {
	IWX_SEC_KEY_FLAG_CIPHER_MASK	= 0x07,
	IWX_SEC_KEY_FLAG_CIPHER_WEP	= 0x01,
	IWX_SEC_KEY_FLAG_CIPHER_CCMP	= 0x02,
	IWX_SEC_KEY_FLAG_CIPHER_TKIP	= 0x03,
	IWX_SEC_KEY_FLAG_CIPHER_GCMP	= 0x05,
	IWX_SEC_KEY_FLAG_NO_TX		= 0x08,
	IWX_SEC_KEY_FLAG_KEY_SIZE	= 0x10,
	IWX_SEC_KEY_FLAG_MFP		= 0x20,
	IWX_SEC_KEY_FLAG_MCAST_KEY	= 0x40,
	IWX_SEC_KEY_FLAG_SPP_AMSDU	= 0x80,
};

#define IWX_SEC_WEP_KEY_OFFSET	3

/**
 * struct iwx_sec_key_cmd - security key command
 * @action: action from &enum iwx_ctxt_action
 * @u.add.sta_mask: station mask for the new key
 * @u.add.key_id: key ID (0-7) for the new key
 * @u.add.key_flags: key flags per &enum iwx_sec_key_flags
 * @u.add.key: key material. WEP keys should start from &IWX_SEC_WEP_KEY_OFFSET.
 * @u.add.tkip_mic_rx_key: TKIP MIC RX key
 * @u.add.tkip_mic_tx_key: TKIP MIC TX key
 * @u.add.rx_seq: RX sequence counter value
 * @u.add.tx_seq: TX sequence counter value
 * @u.modify.old_sta_mask: old station mask
 * @u.modify.new_sta_mask: new station mask
 * @u.modify.key_id: key ID
 * @u.modify.key_flags: new key flags
 * @u.remove.sta_mask: station mask
 * @u.remove.key_id: key ID
 * @u.remove.key_flags: key flags
 */
struct iwx_sec_key_cmd {
	uint32_t action;
	union {
		struct {
			uint32_t sta_mask;
			uint32_t key_id;
			uint32_t key_flags;
			uint8_t key[32];
			uint8_t tkip_mic_rx_key[8];
			uint8_t tkip_mic_tx_key[8];
			uint64_t rx_seq;
			uint64_t tx_seq;
		} __packed add; /* SEC_KEY_ADD_CMD_API_S_VER_1 */
		struct {
			uint32_t old_sta_mask;
			uint32_t new_sta_mask;
			uint32_t key_id;
			uint32_t key_flags;
		} __packed modify; /* SEC_KEY_MODIFY_CMD_API_S_VER_1 */
		struct {
			uint32_t sta_mask;
			uint32_t key_id;
			uint32_t key_flags;
		} __packed remove; /* SEC_KEY_REMOVE_CMD_API_S_VER_1 */
	} __packed u; /* SEC_KEY_OPERATION_API_U_VER_1 */
} __packed; /* SEC_KEY_CMD_API_S_VER_1 */

/**
 * Options for TLC config flags
 * @IWX_TLC_MNG_CFG_FLAGS_STBC_MSK: enable STBC. For HE this enables STBC for
 *				    bandwidths <= 80MHz
 * @IWX_TLC_MNG_CFG_FLAGS_LDPC_MSK: enable LDPC
 * @IWX_TLC_MNG_CFG_FLAGS_HE_STBC_160MHZ_MSK: enable STBC in HE at 160MHz
 *					      bandwidth
 * @IWX_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_1_MSK: enable HE Dual Carrier Modulation
 *					    for BPSK (MCS 0) with 1 spatial
 *					    stream
 * @IWX_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_2_MSK: enable HE Dual Carrier Modulation
 *					    for BPSK (MCS 0) with 2 spatial
 *					    streams
 */
#define IWX_TLC_MNG_CFG_FLAGS_STBC_MSK			(1 << 0)
#define IWX_TLC_MNG_CFG_FLAGS_LDPC_MSK			(1 << 1)
#define IWX_TLC_MNG_CFG_FLAGS_HE_STBC_160MHZ_MSK	(1 << 2)
#define IWX_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_1_MSK		(1 << 3)
#define IWX_TLC_MNG_CFG_FLAGS_HE_DCM_NSS_2_MSK		(1 << 4)

/**
 * enum iwx_tlc_mng_cfg_cw - channel width options
 * @IWX_TLC_MNG_CH_WIDTH_20MHZ: 20MHZ channel
 * @IWX_TLC_MNG_CH_WIDTH_40MHZ: 40MHZ channel
 * @IWX_TLC_MNG_CH_WIDTH_80MHZ: 80MHZ channel
 * @IWX_TLC_MNG_CH_WIDTH_160MHZ: 160MHZ channel
 * @IWX_TLC_MNG_CH_WIDTH_320MHZ: 320MHZ channel
 */
enum iwx_tlc_mng_cfg_cw {
	IWX_TLC_MNG_CH_WIDTH_20MHZ,
	IWX_TLC_MNG_CH_WIDTH_40MHZ,
	IWX_TLC_MNG_CH_WIDTH_80MHZ,
	IWX_TLC_MNG_CH_WIDTH_160MHZ,
	IWX_TLC_MNG_CH_WIDTH_320MHZ,
};

/**
 * @IWX_TLC_MNG_CHAIN_A_MSK: chain A
 * @IWX_TLC_MNG_CHAIN_B_MSK: chain B
 */
#define IWX_TLC_MNG_CHAIN_A_MSK	(1 << 0)
#define IWX_TLC_MNG_CHAIN_B_MSK	(1 << 1)

/**
 * enum iwx_tlc_mng_cfg_mode - supported modes
 * @IWX_TLC_MNG_MODE_CCK: enable CCK
 * @IWX_TLC_MNG_MODE_OFDM_NON_HT: enable OFDM (non HT)
 * @IWX_TLC_MNG_MODE_NON_HT: enable non HT
 * @IWX_TLC_MNG_MODE_HT: enable HT
 * @IWX_TLC_MNG_MODE_VHT: enable VHT
 * @IWX_TLC_MNG_MODE_HE: enable HE
 * @IWX_TLC_MNG_MODE_EHT: enable EHT
 */
enum iwx_tlc_mng_cfg_mode {
	IWX_TLC_MNG_MODE_CCK = 0,
	IWX_TLC_MNG_MODE_OFDM_NON_HT = IWX_TLC_MNG_MODE_CCK,
	IWX_TLC_MNG_MODE_NON_HT = IWX_TLC_MNG_MODE_CCK,
	IWX_TLC_MNG_MODE_HT,
	IWX_TLC_MNG_MODE_VHT,
	IWX_TLC_MNG_MODE_HE,
	IWX_TLC_MNG_MODE_EHT,
};

/**
 * @IWX_TLC_MNG_HT_RATE_MCS0: index of MCS0
 * @IWX_TLC_MNG_HT_RATE_MCS1: index of MCS1
 * @IWX_TLC_MNG_HT_RATE_MCS2: index of MCS2
 * @IWX_TLC_MNG_HT_RATE_MCS3: index of MCS3
 * @IWX_TLC_MNG_HT_RATE_MCS4: index of MCS4
 * @IWX_TLC_MNG_HT_RATE_MCS5: index of MCS5
 * @IWX_TLC_MNG_HT_RATE_MCS6: index of MCS6
 * @IWX_TLC_MNG_HT_RATE_MCS7: index of MCS7
 * @IWX_TLC_MNG_HT_RATE_MCS8: index of MCS8
 * @IWX_TLC_MNG_HT_RATE_MCS9: index of MCS9
 * @IWX_TLC_MNG_HT_RATE_MCS10: index of MCS10
 * @IWX_TLC_MNG_HT_RATE_MCS11: index of MCS11
 * @IWX_TLC_MNG_HT_RATE_MAX: maximal rate for HT/VHT
 */
enum iwx_tlc_mng_ht_rates {
	IWX_TLC_MNG_HT_RATE_MCS0 = 0,
	IWX_TLC_MNG_HT_RATE_MCS1,
	IWX_TLC_MNG_HT_RATE_MCS2,
	IWX_TLC_MNG_HT_RATE_MCS3,
	IWX_TLC_MNG_HT_RATE_MCS4,
	IWX_TLC_MNG_HT_RATE_MCS5,
	IWX_TLC_MNG_HT_RATE_MCS6,
	IWX_TLC_MNG_HT_RATE_MCS7,
	IWX_TLC_MNG_HT_RATE_MCS8,
	IWX_TLC_MNG_HT_RATE_MCS9,
	IWX_TLC_MNG_HT_RATE_MCS10,
	IWX_TLC_MNG_HT_RATE_MCS11,
	IWX_TLC_MNG_HT_RATE_MAX = IWX_TLC_MNG_HT_RATE_MCS11,
};

#define IWX_TLC_NSS_1	0
#define IWX_TLC_NSS_2	1
#define IWX_TLC_NSS_MAX	2


/**
 * IWX_TLC_MCS_PER_BW - mcs index per BW
 * @IWX_TLC_MCS_PER_BW_80: mcs for bw - 20Hhz, 40Hhz, 80Hhz
 * @IWX_TLC_MCS_PER_BW_160: mcs for bw - 160Mhz
 * @IWX_TLC_MCS_PER_BW_320: mcs for bw - 320Mhz
 * @IWX_TLC_MCS_PER_BW_NUM_V3: number of entries up to version 3
 * @IWX_TLC_MCS_PER_BW_NUM_V4: number of entries from version 4
 */
#define IWX_TLC_MCS_PER_BW_80	0
#define IWX_TLC_MCS_PER_BW_160	1
#define IWX_TLC_MCS_PER_BW_320  2
#define IWX_TLC_MCS_PER_BW_NUM_V3	(IWX_TLC_MCS_PER_BW_160 + 1)
#define IWX_TLC_MCS_PER_BW_NUM_V4	(IWX_TLC_MCS_PER_BW_320 + 1)

/**
 * struct iwx_tlc_config_cmd_v3 - TLC configuration version 3
 * @sta_id: station id
 * @reserved1: reserved
 * @max_ch_width: max supported channel width from @enum iwx_tlc_mng_cfg_cw
 * @mode: &enum iwx_tlc_mng_cfg_mode
 * @chains: bitmask of IWX_TLC_MNG_CHAIN_*_MSK
 * @amsdu: 1 = TX amsdu is supported, 0 = not supported
 * @flags: bitmask of IWX_TLC_MNG_CFG_*
 * @non_ht_rates: bitmap of supported legacy rates
 * @ht_rates: MCS index 0 - 11, per <nss, channel-width>
 *	      pair (0 - 80mhz width and below, 1 - 160mhz).
 * @max_mpdu_len: max MPDU length, in bytes
 * @sgi_ch_width_supp: bitmap of SGI support per channel width
 *		       use (1 << IWX_TLC_MNG_CFG_CW_*)
 * @reserved2: reserved
 * @max_tx_op: max TXOP in uSecs for all AC (BK, BE, VO, VI),
 *	       set zero for no limit.
 */
struct iwx_tlc_config_cmd_v3 {
	uint8_t sta_id;
	uint8_t reserved1[3];
	uint8_t max_ch_width;
	uint8_t mode;
	uint8_t chains;
	uint8_t amsdu;
	uint16_t flags;
	uint16_t non_ht_rates;
	uint16_t ht_rates[IWX_TLC_NSS_MAX][IWX_TLC_MCS_PER_BW_NUM_V3];
	uint16_t max_mpdu_len;
	uint8_t sgi_ch_width_supp;
	uint8_t reserved2;
	uint32_t max_tx_op;
} __packed; /* TLC_MNG_CONFIG_CMD_API_S_VER_3 */

/**
 * struct iwx_tlc_config_cmd_v4 - TLC configuration
 * @sta_id: station id
 * @reserved1: reserved
 * @max_ch_width: max supported channel width from @enum iwx_tlc_mng_cfg_cw
 * @mode: &enum iwx_tlc_mng_cfg_mode
 * @chains: bitmask of IWX_TLC_MNG_CHAIN_*_MSK
 * @sgi_ch_width_supp: bitmap of SGI support per channel width
 *		       use (1 << IWX_TLC_MNG_CFG_CW_*)
 * @flags: bitmask of IWX_TLC_MNG_CFG_*
 * @non_ht_rates: bitmap of supported legacy rates
 * @ht_rates: MCS index 0 - 11, per <nss, channel-width>
 *	      pair (0 - 80mhz width and below, 1 - 160mhz, 2 - 320mhz).
 * @max_mpdu_len: max MPDU length, in bytes
 * @max_tx_op: max TXOP in uSecs for all AC (BK, BE, VO, VI),
 *	       set zero for no limit.
 */
struct iwx_tlc_config_cmd_v4 {
	uint8_t sta_id;
	uint8_t reserved1[3];
	uint8_t max_ch_width;
	uint8_t mode;
	uint8_t chains;
	uint8_t sgi_ch_width_supp;
	uint16_t flags;
	uint16_t non_ht_rates;
	uint16_t ht_rates[IWX_TLC_NSS_MAX][IWX_TLC_MCS_PER_BW_NUM_V4];
	uint16_t max_mpdu_len;
	uint16_t max_tx_op;
} __packed; /* TLC_MNG_CONFIG_CMD_API_S_VER_4 */

/**
 * @IWX_TLC_NOTIF_FLAG_RATE: last initial rate update
 * @IWX_TLC_NOTIF_FLAG_AMSDU: umsdu parameters update
 */
#define IWX_TLC_NOTIF_FLAG_RATE		(1 << 0)
#define IWX_TLC_NOTIF_FLAG_AMSDU	(1 << 1)

/**
 * struct iwx_tlc_update_notif - TLC notification from FW
 * @sta_id: station id
 * @reserved: reserved
 * @flags: bitmap of notifications reported
 * @rate: current initial rate; using rate_n_flags version 1 if notification
 *  version is < 3 at run-time, else rate_n_flags version 2
 * @amsdu_size: Max AMSDU size, in bytes
 * @amsdu_enabled: bitmap for per-TID AMSDU enablement
 */
struct iwx_tlc_update_notif {
	uint8_t sta_id;
	uint8_t reserved[3];
	uint32_t flags;
	uint32_t rate;
	uint32_t amsdu_size;
	uint32_t amsdu_enabled;
} __packed; /* TLC_MNG_UPDATE_NTFY_API_S_VER_2 */

/* Antenna flags. */
#define IWX_ANT_A	(1 << 0)
#define IWX_ANT_B	(1 << 1)
#define IWX_ANT_C	(1 << 2)
/* Shortcuts. */
#define IWX_ANT_AB	(IWX_ANT_A | IWX_ANT_B)
#define IWX_ANT_BC	(IWX_ANT_B | IWX_ANT_C)
#define IWX_ANT_ABC	(IWX_ANT_A | IWX_ANT_B | IWX_ANT_C)

/*
 * TX command security control
 */
#define IWX_TX_CMD_SEC_WEP		0x01
#define IWX_TX_CMD_SEC_CCM		0x02
#define IWX_TX_CMD_SEC_TKIP		0x03
#define IWX_TX_CMD_SEC_EXT		0x04
#define IWX_TX_CMD_SEC_MSK		0x07
#define IWX_TX_CMD_SEC_WEP_KEY_IDX_POS	6
#define IWX_TX_CMD_SEC_WEP_KEY_IDX_MSK	0xc0
#define IWX_TX_CMD_SEC_KEY128		0x08

/* TODO: how does these values are OK with only 16 bit variable??? */
/*
 * TX command next frame info
 *
 * bits 0:2 - security control (IWX_TX_CMD_SEC_*)
 * bit 3 - immediate ACK required
 * bit 4 - rate is taken from STA table
 * bit 5 - frame belongs to BA stream
 * bit 6 - immediate BA response expected
 * bit 7 - unused
 * bits 8:15 - Station ID
 * bits 16:31 - rate
 */
#define IWX_TX_CMD_NEXT_FRAME_ACK_MSK		(0x8)
#define IWX_TX_CMD_NEXT_FRAME_STA_RATE_MSK	(0x10)
#define IWX_TX_CMD_NEXT_FRAME_BA_MSK		(0x20)
#define IWX_TX_CMD_NEXT_FRAME_IMM_BA_RSP_MSK	(0x40)
#define IWX_TX_CMD_NEXT_FRAME_FLAGS_MSK		(0xf8)
#define IWX_TX_CMD_NEXT_FRAME_STA_ID_MSK	(0xff00)
#define IWX_TX_CMD_NEXT_FRAME_STA_ID_POS	(8)
#define IWX_TX_CMD_NEXT_FRAME_RATE_MSK		(0xffff0000)
#define IWX_TX_CMD_NEXT_FRAME_RATE_POS		(16)

/*
 * TX command Frame life time in us - to be written in pm_frame_timeout
 */
#define IWX_TX_CMD_LIFE_TIME_INFINITE	0xFFFFFFFF
#define IWX_TX_CMD_LIFE_TIME_DEFAULT	2000000 /* 2000 ms*/
#define IWX_TX_CMD_LIFE_TIME_PROBE_RESP	40000 /* 40 ms */
#define IWX_TX_CMD_LIFE_TIME_EXPIRED_FRAME	0

/*
 * TID for non QoS frames - to be written in tid_tspec
 */
#define IWX_TID_NON_QOS	0

/*
 * Limits on the retransmissions - to be written in {data,rts}_retry_limit
 */
#define IWX_DEFAULT_TX_RETRY			15
#define IWX_MGMT_DFAULT_RETRY_LIMIT		3
#define IWX_RTS_DFAULT_RETRY_LIMIT		3
#define IWX_BAR_DFAULT_RETRY_LIMIT		60
#define IWX_LOW_RETRY_LIMIT			7

/*
 * The FH will write back to the first TB only, so we need to copy some data
 * into the buffer regardless of whether it should be mapped or not.
 * This indicates how big the first TB must be to include the scratch buffer
 * and the assigned PN.
 * Since PN location is 8 bytes at offset 12, it's 20 now.
 * If we make it bigger then allocations will be bigger and copy slower, so
 * that's probably not useful.
 */
#define IWX_FIRST_TB_SIZE	20
#define IWX_FIRST_TB_SIZE_ALIGN ((IWX_FIRST_TB_SIZE + (64 - 1)) & ~(64 - 1))

/**
 * %iwx_tx_cmd offload_assist values
 * @TX_CMD_OFFLD_IP_HDR: offset to start of IP header (in words)
 *	from mac header end. For normal case it is 4 words for SNAP.
 *	note: tx_cmd, mac header and pad are not counted in the offset.
 *	This is used to help the offload in case there is tunneling such as
 *	IPv6 in IPv4, in such case the ip header offset should point to the
 *	inner ip header and IPv4 checksum of the external header should be
 *	calculated by driver.
 * @TX_CMD_OFFLD_L4_EN: enable TCP/UDP checksum
 * @TX_CMD_OFFLD_L3_EN: enable IP header checksum
 * @TX_CMD_OFFLD_MH_SIZE: size of the mac header in words. Includes the IV
 *	field. Doesn't include the pad.
 * @TX_CMD_OFFLD_PAD: mark 2-byte pad was inserted after the mac header for
 *	alignment
 * @TX_CMD_OFFLD_AMSDU: mark TX command is A-MSDU
 */
#define IWX_TX_CMD_OFFLD_IP_HDR(x)	((x) << 0)
#define IWX_TX_CMD_OFFLD_L4_EN		(1 << 6)
#define IWX_TX_CMD_OFFLD_L3_EN		(1 << 7)
#define IWX_TX_CMD_OFFLD_MH_SIZE(x)	((x) << 8)
#define IWX_TX_CMD_OFFLD_PAD		(1 << 13)
#define IWX_TX_CMD_OFFLD_AMSDU		(1 << 14)
#define IWX_TX_CMD_OFFLD_MH_MASK	0x1f
#define IWX_TX_CMD_OFFLD_IP_HDR_MASK	0x3f

struct iwx_dram_sec_info {
	uint32_t pn_low;
	uint16_t pn_high;
	uint16_t aux_info;
} __packed; /* DRAM_SEC_INFO_API_S_VER_1 */

/**
 * bitmasks for tx_flags in TX command for 22000
 * @IWX_TX_FLAGS_CMD_RATE: use rate from the TX command
 * @IWX_TX_FLAGS_ENCRYPT_DIS: frame should not be encrypted, even if it belongs
 *	to a secured STA
 * @IWX_TX_FLAGS_HIGH_PRI: high priority frame (like EAPOL) - can affect rate
 *	selection, retry limits and BT kill
 */
/* Valid for TX_FLAGS_BITS_API_S_VER_3: */
#define IWX_TX_FLAGS_CMD_RATE		(1 << 0)
#define IWX_TX_FLAGS_ENCRYPT_DIS	(1 << 1)
#define IWX_TX_FLAGS_HIGH_PRI		(1 << 2)
/* Valid for TX_FLAGS_BITS_API_S_VER_4 and above: */
#define IWX_TX_FLAGS_RTS		(1 << 3)
#define IWX_TX_FLAGS_CTS		(1 << 4)
/* TX_FLAGS_BITS_API_S_VER_4 */

/**
 * struct iwx_tx_cmd_gen2 - TX command struct to FW for 22000 devices
 * ( TX_CMD = 0x1c )
 * @len: in bytes of the payload, see below for details
 * @offload_assist: TX offload configuration
 * @flags: combination of TX_FLAGS_*
 * @dram_info: FW internal DRAM storage
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @hdr: 802.11 header
 */
struct iwx_tx_cmd_gen2 {
	uint16_t len;
	uint16_t offload_assist;
	uint32_t flags;
	struct iwx_dram_sec_info dram_info;
	uint32_t rate_n_flags;
	struct ieee80211_frame hdr[0];
} __packed; /* TX_CMD_API_S_VER_7,
	       TX_CMD_API_S_VER_9 */

/**
 * struct iwx_tx_cmd_gen3 - TX command struct to FW for AX210+ devices
 * ( TX_CMD = 0x1c )
 * @len: in bytes of the payload, see below for details
 * @flags: combination of TX_FLAGS_*
 * @offload_assist: TX offload configuration
 * @dram_info: FW internal DRAM storage
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @reserved: reserved
 * @hdr: 802.11 header
 */
struct iwx_tx_cmd_gen3 {
	uint16_t len;
	uint16_t flags;
	uint32_t offload_assist;
	struct iwx_dram_sec_info dram_info;
	uint32_t rate_n_flags;
	uint8_t reserved[8];
	struct ieee80211_frame hdr[];
} __packed; /* TX_CMD_API_S_VER_8,
	       TX_CMD_API_S_VER_10 */

/*
 * TX response related data
 */

/*
 * status that is returned by the fw after attempts to Tx
 * @IWX_TX_STATUS_FAIL_STA_COLOR_MISMATCH: mismatch between color of Tx cmd and
 *	STA table
 * Valid only if frame_count =1
 */
#define IWX_TX_STATUS_MSK		0x000000ff
#define IWX_TX_STATUS_SUCCESS		0x01
#define IWX_TX_STATUS_DIRECT_DONE	0x02
/* postpone TX */
#define IWX_TX_STATUS_POSTPONE_DELAY		0x40
#define IWX_TX_STATUS_POSTPONE_FEW_BYTES	0x41
#define IWX_TX_STATUS_POSTPONE_BT_PRIO		0x42
#define IWX_TX_STATUS_POSTPONE_QUIET_PERIOD	0x43
#define IWX_TX_STATUS_POSTPONE_CALC_TTAK	0x44
/* abort TX */
#define IWX_TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY	0x81
#define IWX_TX_STATUS_FAIL_SHORT_LIMIT			0x82
#define IWX_TX_STATUS_FAIL_LONG_LIMIT			0x83
#define IWX_TX_STATUS_FAIL_UNDERRUN			0x84
#define IWX_TX_STATUS_FAIL_DRAIN_FLOW			0x85
#define IWX_TX_STATUS_FAIL_RFKILL_FLUSH			0x86
#define IWX_TX_STATUS_FAIL_LIFE_EXPIRE			0x87
#define IWX_TX_STATUS_FAIL_DEST_PS			0x88
#define IWX_TX_STATUS_FAIL_HOST_ABORTED			0x89
#define IWX_TX_STATUS_FAIL_BT_RETRY			0x8a
#define IWX_TX_STATUS_FAIL_STA_INVALID			0x8b
#define IWX_TX_STATUS_FAIL_FRAG_DROPPED			0x8c
#define IWX_TX_STATUS_FAIL_TID_DISABLE			0x8d
#define IWX_TX_STATUS_FAIL_FIFO_FLUSHED			0x8e
#define IWX_TX_STATUS_FAIL_SMALL_CF_POLL		0x8f
#define IWX_TX_STATUS_FAIL_FW_DROP			0x90
#define IWX_TX_STATUS_FAIL_STA_COLOR_MISMATCH		0x91
#define IWX_TX_STATUS_INTERNAL_ABORT			0x92
#define IWX_TX_MODE_MSK			0x00000f00
#define IWX_TX_MODE_NO_BURST		0x00000000
#define IWX_TX_MODE_IN_BURST_SEQ	0x00000100
#define IWX_TX_MODE_FIRST_IN_BURST	0x00000200
#define IWX_TX_QUEUE_NUM_MSK		0x0001f000
#define IWX_TX_NARROW_BW_MSK		0x00060000
#define IWX_TX_NARROW_BW_1DIV2		0x00020000
#define IWX_TX_NARROW_BW_1DIV4		0x00040000
#define IWX_TX_NARROW_BW_1DIV8		0x00060000

/*
 * TX aggregation status
 * @IWX_AGG_TX_STATE_TRY_CNT_MSK: Retry count for 1st frame in aggregation (retries
 *	occur if tx failed for this frame when it was a member of a previous
 *	aggregation block). If rate scaling is used, retry count indicates the
 *	rate table entry used for all frames in the new agg.
 * @IWX_AGG_TX_STATE_SEQ_NUM_MSK: Command ID and sequence number of Tx command for
 *	this frame
 */
#define IWX_AGG_TX_STATE_STATUS_MSK		0x0fff
#define IWX_AGG_TX_STATE_TRANSMITTED		0x0000
#define IWX_AGG_TX_STATE_UNDERRUN		0x0001
#define IWX_AGG_TX_STATE_BT_PRIO		0x0002
#define IWX_AGG_TX_STATE_FEW_BYTES		0x0004
#define IWX_AGG_TX_STATE_ABORT			0x0008
#define IWX_AGG_TX_STATE_LAST_SENT_TTL		0x0010
#define IWX_AGG_TX_STATE_LAST_SENT_TRY_CNT	0x0020
#define IWX_AGG_TX_STATE_LAST_SENT_BT_KILL	0x0040
#define IWX_AGG_TX_STATE_SCD_QUERY		0x0080
#define IWX_AGG_TX_STATE_TEST_BAD_CRC32		0x0100
#define IWX_AGG_TX_STATE_RESPONSE		0x01ff
#define IWX_AGG_TX_STATE_DUMP_TX		0x0200
#define IWX_AGG_TX_STATE_DELAY_TX		0x0400
#define IWX_AGG_TX_STATE_TRY_CNT_POS	12
#define IWX_AGG_TX_STATE_TRY_CNT_MSK	(0xf << IWX_AGG_TX_STATE_TRY_CNT_POS)

#define IWX_AGG_TX_STATE_LAST_SENT_MSK  (IWX_AGG_TX_STATE_LAST_SENT_TTL| \
				     IWX_AGG_TX_STATE_LAST_SENT_TRY_CNT| \
				     IWX_AGG_TX_STATE_LAST_SENT_BT_KILL)

/*
 * The mask below describes a status where we are absolutely sure that the MPDU
 * wasn't sent. For BA/Underrun we cannot be that sure. All we know that we've
 * written the bytes to the TXE, but we know nothing about what the DSP did.
 */
#define IWX_AGG_TX_STAT_FRAME_NOT_SENT (IWX_AGG_TX_STATE_FEW_BYTES | \
				    IWX_AGG_TX_STATE_ABORT | \
				    IWX_AGG_TX_STATE_SCD_QUERY)

/*
 * IWX_REPLY_TX = 0x1c (response)
 *
 * This response may be in one of two slightly different formats, indicated
 * by the frame_count field:
 *
 * 1)	No aggregation (frame_count == 1).  This reports Tx results for a single
 *	frame. Multiple attempts, at various bit rates, may have been made for
 *	this frame.
 *
 * 2)	Aggregation (frame_count > 1).  This reports Tx results for two or more
 *	frames that used block-acknowledge.  All frames were transmitted at
 *	same rate. Rate scaling may have been used if first frame in this new
 *	agg block failed in previous agg block(s).
 *
 *	Note that, for aggregation, ACK (block-ack) status is not delivered
 *	here; block-ack has not been received by the time the device records
 *	this status.
 *	This status relates to reasons the tx might have been blocked or aborted
 *	within the device, rather than whether it was received successfully by
 *	the destination station.
 */

/**
 * struct iwx_agg_tx_status - per packet TX aggregation status
 * @status: enum iwx_tx_agg_status
 * @sequence: Sequence # for this frame's Tx cmd (not SSN!)
 */
struct iwx_agg_tx_status {
	uint16_t status;
	uint16_t sequence;
} __packed;

/*
 * definitions for initial rate index field
 * bits [3:0] initial rate index
 * bits [6:4] rate table color, used for the initial rate
 * bit-7 invalid rate indication
 */
#define IWX_TX_RES_INIT_RATE_INDEX_MSK 0x0f
#define IWX_TX_RES_RATE_TABLE_COLOR_MSK 0x70
#define IWX_TX_RES_INV_RATE_INDEX_MSK 0x80

#define IWX_TX_RES_GET_TID(_ra_tid) ((_ra_tid) & 0x0f)
#define IWX_TX_RES_GET_RA(_ra_tid) ((_ra_tid) >> 4)

/**
 * struct iwx_tx_resp_v3 - notifies that fw is TXing a packet
 * ( IWX_REPLY_TX = 0x1c )
 * @frame_count: 1 no aggregation, >1 aggregation
 * @bt_kill_count: num of times blocked by bluetooth (unused for agg)
 * @failure_rts: num of failures due to unsuccessful RTS
 * @failure_frame: num failures due to no ACK (unused for agg)
 * @initial_rate: for non-agg: rate of the successful Tx. For agg: rate of the
 *	Tx of all the batch. IWX_RATE_MCS_*
 * @wireless_media_time: for non-agg: RTS + CTS + frame tx attempts time + ACK.
 *	for agg: RTS + CTS + aggregation tx time + block-ack time.
 *	in usec.
 * @pa_status: tx power info
 * @pa_integ_res_a: tx power info
 * @pa_integ_res_b: tx power info
 * @pa_integ_res_c: tx power info
 * @measurement_req_id: tx power info
 * @tfd_info: TFD information set by the FH
 * @seq_ctl: sequence control from the Tx cmd
 * @byte_cnt: byte count from the Tx cmd
 * @tlc_info: TLC rate info
 * @ra_tid: bits [3:0] = ra, bits [7:4] = tid
 * @frame_ctrl: frame control
 * @status: for non-agg:  frame status IWX_TX_STATUS_*
 *	for agg: status of 1st frame, IWX_AGG_TX_STATE_*; other frame status fields
 *	follow this one, up to frame_count.
 *
 * After the array of statuses comes the SSN of the SCD. Look at
 * %iwx_get_scd_ssn for more details.
 */
struct iwx_tx_resp_v3 {
	uint8_t frame_count;
	uint8_t bt_kill_count;
	uint8_t failure_rts;
	uint8_t failure_frame;
	uint32_t initial_rate;
	uint16_t wireless_media_time;

	uint8_t pa_status;
	uint8_t pa_integ_res_a[3];
	uint8_t pa_integ_res_b[3];
	uint8_t pa_integ_res_c[3];
	uint16_t measurement_req_id;
	uint16_t reserved;

	uint32_t tfd_info;
	uint16_t seq_ctl;
	uint16_t byte_cnt;
	uint8_t tlc_info;
	uint8_t ra_tid;
	uint16_t frame_ctrl;

	struct iwx_agg_tx_status status;
} __packed; /* IWX_TX_RSP_API_S_VER_3 */

/**
 * struct iwx_tx_resp - notifies that fw is TXing a packet
 * ( REPLY_TX = 0x1c )
 * @frame_count: 1 no aggregation, >1 aggregation
 * @bt_kill_count: num of times blocked by bluetooth (unused for agg)
 * @failure_rts: num of failures due to unsuccessful RTS
 * @failure_frame: num failures due to no ACK (unused for agg)
 * @initial_rate: for non-agg: rate of the successful Tx. For agg: rate of the
 *	Tx of all the batch. RATE_MCS_*
 * @wireless_media_time: for non-agg: RTS + CTS + frame tx attempts time + ACK.
 *	for agg: RTS + CTS + aggregation tx time + block-ack time.
 *	in usec.
 * @pa_status: tx power info
 * @pa_integ_res_a: tx power info
 * @pa_integ_res_b: tx power info
 * @pa_integ_res_c: tx power info
 * @measurement_req_id: tx power info
 * @reduced_tpc: transmit power reduction used
 * @reserved: reserved
 * @tfd_info: TFD information set by the FH
 * @seq_ctl: sequence control from the Tx cmd
 * @byte_cnt: byte count from the Tx cmd
 * @tlc_info: TLC rate info
 * @ra_tid: bits [3:0] = ra, bits [7:4] = tid
 * @frame_ctrl: frame control
 * @tx_queue: TX queue for this response
 * @reserved2: reserved for padding/alignment
 * @status: for non-agg:  frame status TX_STATUS_*
 *	For version 6 TX response isn't received for aggregation at all.
 *
 * After the array of statuses comes the SSN of the SCD. Look at
 * %iwl_mvm_get_scd_ssn for more details.
 */
struct iwx_tx_resp {
	uint8_t frame_count;
	uint8_t bt_kill_count;
	uint8_t failure_rts;
	uint8_t failure_frame;
	uint32_t initial_rate;
	uint16_t wireless_media_time;

	uint8_t pa_status;
	uint8_t pa_integ_res_a[3];
	uint8_t pa_integ_res_b[3];
	uint8_t pa_integ_res_c[3];
	uint16_t measurement_req_id;
	uint8_t reduced_tpc;
	uint8_t reserved;

	uint32_t tfd_info;
	uint16_t seq_ctl;
	uint16_t byte_cnt;
	uint8_t tlc_info;
	uint8_t ra_tid;
	uint16_t frame_ctrl;
	uint16_t tx_queue;
	uint16_t reserved2;
	struct iwx_agg_tx_status status;
} __packed; /* TX_RSP_API_S_VER_6 */

/**
 * struct iwx_compressed_ba_tfd - progress of a TFD queue
 * @q_num: TFD queue number
 * @tfd_index: Index of first un-acked frame in the  TFD queue
 * @scd_queue: For debug only - the physical queue the TFD queue is bound to
 * @tid: TID of the queue (0-7)
 * @reserved: reserved for alignment
 */
struct iwx_compressed_ba_tfd {
	uint16_t q_num;
	uint16_t tfd_index;
	uint8_t scd_queue;
	uint8_t tid;
	uint8_t reserved[2];
} __packed; /* COMPRESSED_BA_TFD_API_S_VER_1 */

/**
 * struct iwx_compressed_ba_ratid - progress of a RA TID queue
 * @q_num: RA TID queue number
 * @tid: TID of the queue
 * @ssn: BA window current SSN
 */
struct iwx_compressed_ba_ratid {
	uint8_t q_num;
	uint8_t tid;
	uint16_t ssn;
} __packed; /* COMPRESSED_BA_RATID_API_S_VER_1 */

/*
 * enum iwx_ba_resp_flags - TX aggregation status
 * @IWX_MVM_BA_RESP_TX_AGG: generated due to BA
 * @IWX_MVM_BA_RESP_TX_BAR: generated due to BA after BAR
 * @IWX_MVM_BA_RESP_TX_AGG_FAIL: aggregation didn't receive BA
 * @IWX_MVM_BA_RESP_TX_UNDERRUN: aggregation got underrun
 * @IWX_MVM_BA_RESP_TX_BT_KILL: aggregation got BT-kill
 * @IWX_MVM_BA_RESP_TX_DSP_TIMEOUT: aggregation didn't finish within the
 *	expected time
 */
enum iwx_ba_resp_flags {
	IWX_MVM_BA_RESP_TX_AGG,
	IWX_MVM_BA_RESP_TX_BAR,
	IWX_MVM_BA_RESP_TX_AGG_FAIL,
	IWX_MVM_BA_RESP_TX_UNDERRUN,
	IWX_MVM_BA_RESP_TX_BT_KILL,
	IWX_MVM_BA_RESP_TX_DSP_TIMEOUT
};

/**
 * struct iwx_compressed_ba_notif - notifies about reception of BA
 * ( BA_NOTIF = 0xc5 )
 * @flags: status flag, see the &iwx_ba_resp_flags
 * @sta_id: Index of recipient (BA-sending) station in fw's station table
 * @reduced_txp: power reduced according to TPC. This is the actual value and
 *	not a copy from the LQ command. Thus, if not the first rate was used
 *	for Tx-ing then this value will be set to 0 by FW.
 * @tlc_rate_info: TLC rate info, initial rate index, TLC table color
 * @retry_cnt: retry count
 * @query_byte_cnt: SCD query byte count
 * @query_frame_cnt: SCD query frame count
 * @txed: number of frames sent in the aggregation (all-TIDs)
 * @done: number of frames that were Acked by the BA (all-TIDs)
 * @reserved: reserved (for alignment)
 * @wireless_time: Wireless-media time
 * @tx_rate: the rate the aggregation was sent at
 * @tfd_cnt: number of TFD-Q elements
 * @ra_tid_cnt: number of RATID-Q elements
 * @tfd: array of TFD queue status updates. See &iwx_compressed_ba_tfd
 *	for details. Length in @tfd_cnt.
 * @ra_tid: array of RA-TID queue status updates. For debug purposes only. See
 *	&iwx_compressed_ba_ratid for more details. Length in @ra_tid_cnt.
 */
struct iwx_compressed_ba_notif {
	uint32_t flags;
	uint8_t sta_id;
	uint8_t reduced_txp;
	uint8_t tlc_rate_info;
	uint8_t retry_cnt;
	uint32_t query_byte_cnt;
	uint16_t query_frame_cnt;
	uint16_t txed;
	uint16_t done;
	uint16_t reserved;
	uint32_t wireless_time;
	uint32_t tx_rate;
	uint16_t tfd_cnt;
	uint16_t ra_tid_cnt;
	struct iwx_compressed_ba_ratid ra_tid[0];
	struct iwx_compressed_ba_tfd tfd[];
} __packed; /* COMPRESSED_BA_RES_API_S_VER_4 */


struct iwx_beacon_notif {
	struct iwx_tx_resp_v3 beacon_notify_hdr;
	uint64_t tsf;
	uint32_t ibss_mgr_status;
} __packed;

/**
 * dump (flush) control flags
 * @IWX_DUMP_TX_FIFO_FLUSH: Dump MSDUs until the FIFO is empty
 *	and the TFD queues are empty.
 */
#define IWX_DUMP_TX_FIFO_FLUSH	(1 << 1)

/**
 * struct iwx_tx_path_flush_cmd -- queue/FIFO flush command
 * @queues_ctl: bitmap of queues to flush
 * @flush_ctl: control flags
 * @reserved: reserved
 */
struct iwx_tx_path_flush_cmd_v1 {
	uint32_t queues_ctl;
	uint16_t flush_ctl;
	uint16_t reserved;
} __packed; /* IWX_TX_PATH_FLUSH_CMD_API_S_VER_1 */

/**
 * struct iwl_tx_path_flush_cmd -- queue/FIFO flush command
 * @sta_id: station ID to flush
 * @tid_mask: TID mask to flush
 * @reserved: reserved
 */
struct iwx_tx_path_flush_cmd {
	uint32_t sta_id;
	uint16_t tid_mask;
	uint16_t reserved;
} __packed; /* TX_PATH_FLUSH_CMD_API_S_VER_2 */

#define IWX_TX_FLUSH_QUEUE_RSP 16

/**
 * struct iwx_flush_queue_info - virtual flush queue info
 * @queue_num: virtual queue id
 * @read_before_flush: read pointer before flush
 * @read_after_flush: read pointer after flush
 */
struct iwx_flush_queue_info {
	uint16_t tid;
	uint16_t queue_num;
	uint16_t read_before_flush;
	uint16_t read_after_flush;
} __packed; /* TFDQ_FLUSH_INFO_API_S_VER_1 */

/**
 * struct iwx_tx_path_flush_cmd_rsp -- queue/FIFO flush command response
 * @num_flushed_queues: number of queues in queues array
 * @queues: all flushed queues
 */
struct iwx_tx_path_flush_cmd_rsp {
	uint16_t sta_id;
	uint16_t num_flushed_queues;
	struct iwx_flush_queue_info queues[IWX_TX_FLUSH_QUEUE_RSP];
} __packed; /* TX_PATH_FLUSH_CMD_RSP_API_S_VER_1 */


/**
 * iwx_get_scd_ssn - returns the SSN of the SCD
 * @tx_resp: the Tx response from the fw (agg or non-agg)
 *
 * When the fw sends an AMPDU, it fetches the MPDUs one after the other. Since
 * it can't know that everything will go well until the end of the AMPDU, it
 * can't know in advance the number of MPDUs that will be sent in the current
 * batch. This is why it writes the agg Tx response while it fetches the MPDUs.
 * Hence, it can't know in advance what the SSN of the SCD will be at the end
 * of the batch. This is why the SSN of the SCD is written at the end of the
 * whole struct at a variable offset. This function knows how to cope with the
 * variable offset and returns the SSN of the SCD.
 */
static inline uint32_t iwx_get_scd_ssn(struct iwx_tx_resp *tx_resp)
{
	return le32_to_cpup((uint32_t *)&tx_resp->status +
			    tx_resp->frame_count) & 0xfff;
}

/**
 * struct iwx_scd_txq_cfg_cmd - New txq hw scheduler config command
 * @token:
 * @sta_id: station id
 * @tid:
 * @scd_queue: scheduler queue to config
 * @enable: 1 queue enable, 0 queue disable
 * @aggregate: 1 aggregated queue, 0 otherwise
 * @tx_fifo: %enum iwx_tx_fifo
 * @window: BA window size
 * @ssn: SSN for the BA agreement
 */
struct iwx_scd_txq_cfg_cmd {
	uint8_t token;
	uint8_t sta_id;
	uint8_t tid;
	uint8_t scd_queue;
	uint8_t enable;
	uint8_t aggregate;
	uint8_t tx_fifo;
	uint8_t window;
	uint16_t ssn;
	uint16_t reserved;
} __packed; /* SCD_QUEUE_CFG_CMD_API_S_VER_1 */

/**
 * struct iwx_scd_txq_cfg_rsp
 * @token: taken from the command
 * @sta_id: station id from the command
 * @tid: tid from the command
 * @scd_queue: scd_queue from the command
 */
struct iwx_scd_txq_cfg_rsp {
	uint8_t token;
	uint8_t sta_id;
	uint8_t tid;
	uint8_t scd_queue;
} __packed; /* SCD_QUEUE_CFG_RSP_API_S_VER_1 */


/* Scan Commands, Responses, Notifications */

/* Max number of IEs for direct SSID scans in a command */
#define IWX_PROBE_OPTION_MAX		20

/**
 * struct iwx_ssid_ie - directed scan network information element
 *
 * Up to 20 of these may appear in IWX_REPLY_SCAN_CMD,
 * selected by "type" bit field in struct iwx_scan_channel;
 * each channel may select different ssids from among the 20 entries.
 * SSID IEs get transmitted in reverse order of entry.
 */
struct iwx_ssid_ie {
	uint8_t id;
	uint8_t len;
	uint8_t ssid[IEEE80211_NWID_LEN];
} __packed; /* IWX_SCAN_DIRECT_SSID_IE_API_S_VER_1 */

/* scan offload */
#define IWX_SCAN_MAX_BLACKLIST_LEN	64
#define IWX_SCAN_SHORT_BLACKLIST_LEN	16
#define IWX_SCAN_MAX_PROFILES		11
#define IWX_SCAN_OFFLOAD_PROBE_REQ_SIZE	512

/* Default watchdog (in MS) for scheduled scan iteration */
#define IWX_SCHED_SCAN_WATCHDOG cpu_to_le16(15000)

#define IWX_GOOD_CRC_TH_DEFAULT cpu_to_le16(1)
#define IWX_CAN_ABORT_STATUS 1

#define IWX_FULL_SCAN_MULTIPLIER 5
#define IWX_FAST_SCHED_SCAN_ITERATIONS 3
#define IWX_MAX_SCHED_SCAN_PLANS 2

/**
 * iwx_scan_schedule_lmac - schedule of scan offload
 * @delay:		delay between iterations, in seconds.
 * @iterations:		num of scan iterations
 * @full_scan_mul:	number of partial scans before each full scan
 */
struct iwx_scan_schedule_lmac {
	uint16_t delay;
	uint8_t iterations;
	uint8_t full_scan_mul;
} __packed; /* SCAN_SCHEDULE_API_S */

/**
 * iwx_scan_req_tx_cmd - SCAN_REQ_TX_CMD_API_S
 * @tx_flags: combination of TX_CMD_FLG_*
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @sta_id: index of destination station in FW station table
 * @reserved: for alignment and future use
 */
struct iwx_scan_req_tx_cmd {
	uint32_t tx_flags;
	uint32_t rate_n_flags;
	uint8_t sta_id;
	uint8_t reserved[3];
} __packed;

#define IWX_UNIFIED_SCAN_CHANNEL_FULL		(1 << 27)
#define IWX_UNIFIED_SCAN_CHANNEL_PARTIAL	(1 << 28)

/**
 * iwx_scan_channel_cfg_lmac - SCAN_CHANNEL_CFG_S_VER2
 * @flags:		bits 1-20: directed scan to i'th ssid
 *			other bits &enum iwx_scan_channel_flags_lmac
 * @channel_number:	channel number 1-13 etc
 * @iter_count:		scan iteration on this channel
 * @iter_interval:	interval in seconds between iterations on one channel
 */
struct iwx_scan_channel_cfg_lmac {
	uint32_t flags;
	uint16_t channel_num;
	uint16_t iter_count;
	uint32_t iter_interval;
} __packed;

/*
 * iwx_scan_probe_segment - PROBE_SEGMENT_API_S_VER_1
 * @offset: offset in the data block
 * @len: length of the segment
 */
struct iwx_scan_probe_segment {
	uint16_t offset;
	uint16_t len;
} __packed;

/* iwx_scan_probe_req - PROBE_REQUEST_FRAME_API_S_VER_2
 * @mac_header: first (and common) part of the probe
 * @band_data: band specific data
 * @common_data: last (and common) part of the probe
 * @buf: raw data block
 */
struct iwx_scan_probe_req_v1 {
	struct iwx_scan_probe_segment mac_header;
	struct iwx_scan_probe_segment band_data[2];
	struct iwx_scan_probe_segment common_data;
	uint8_t buf[IWX_SCAN_OFFLOAD_PROBE_REQ_SIZE];
} __packed;

/* iwl_scan_probe_req - PROBE_REQUEST_FRAME_API_S_VER_v2
 * @mac_header: first (and common) part of the probe
 * @band_data: band specific data
 * @common_data: last (and common) part of the probe
 * @buf: raw data block
 */
struct iwx_scan_probe_req {
	struct iwx_scan_probe_segment mac_header;
	struct iwx_scan_probe_segment band_data[3];
	struct iwx_scan_probe_segment common_data;
	uint8_t buf[IWX_SCAN_OFFLOAD_PROBE_REQ_SIZE];
} __packed;


#define IWX_SCAN_CHANNEL_FLAG_EBS		(1 << 0)
#define IWX_SCAN_CHANNEL_FLAG_EBS_ACCURATE	(1 << 1)
#define IWX_SCAN_CHANNEL_FLAG_CACHE_ADD		(1 << 2)
#define IWX_SCAN_CHANNEL_FLAG_EBS_FRAG		(1 << 3)
#define IWX_SCAN_CHANNEL_FLAG_FORCE_EBS		(1 << 4)
#define IWX_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER	(1 << 5)
#define IWX_SCAN_CHANNEL_FLAG_6G_PSC_NO_FILTER	(1 << 6)

/* iwx_scan_channel_opt - CHANNEL_OPTIMIZATION_API_S
 * @flags: enum iwx_scan_channel_flags
 * @non_ebs_ratio: defines the ratio of number of scan iterations where EBS is
 *	involved.
 *	1 - EBS is disabled.
 *	2 - every second scan will be full scan(and so on).
 */
struct iwx_scan_channel_opt {
	uint16_t flags;
	uint16_t non_ebs_ratio;
} __packed;

#define IWX_SCAN_PRIORITY_LOW		0
#define IWX_SCAN_PRIORITY_MEDIUM	1
#define IWX_SCAN_PRIORITY_HIGH		2

enum iwx_scan_priority_ext {
	IWX_SCAN_PRIORITY_EXT_0_LOWEST,
	IWX_SCAN_PRIORITY_EXT_1,
	IWX_SCAN_PRIORITY_EXT_2,
	IWX_SCAN_PRIORITY_EXT_3,
	IWX_SCAN_PRIORITY_EXT_4,
	IWX_SCAN_PRIORITY_EXT_5,
	IWX_SCAN_PRIORITY_EXT_6,
	IWX_SCAN_PRIORITY_EXT_7_HIGHEST,
};

/**
 * iwx_scan_offload_complete - PERIODIC_SCAN_COMPLETE_NTF_API_S_VER_2
 * @last_schedule_line: last schedule line executed (fast or regular)
 * @last_schedule_iteration: last scan iteration executed before scan abort
 * @status: enum iwx_scan_offload_complete_status
 * @ebs_status: EBS success status &enum iwx_scan_ebs_status
 * @time_after_last_iter; time in seconds elapsed after last iteration
 */
struct iwx_periodic_scan_complete {
	uint8_t last_schedule_line;
	uint8_t last_schedule_iteration;
	uint8_t status;
	uint8_t ebs_status;
	uint32_t time_after_last_iter;
	uint32_t reserved;
} __packed;

/**
 * struct iwx_scan_results_notif - scan results for one channel -
 *      SCAN_RESULT_NTF_API_S_VER_3
 * @channel: which channel the results are from
 * @band: 0 for 5.2 GHz, 1 for 2.4 GHz
 * @probe_status: IWX_SCAN_PROBE_STATUS_*, indicates success of probe request
 * @num_probe_not_sent: # of request that weren't sent due to not enough time
 * @duration: duration spent in channel, in usecs
 */
struct iwx_scan_results_notif {
	uint8_t channel;
	uint8_t band;
	uint8_t probe_status;
	uint8_t num_probe_not_sent;
	uint32_t duration;
} __packed;

#define IWX_SCAN_CLIENT_SCHED_SCAN		(1 << 0)
#define IWX_SCAN_CLIENT_NETDETECT		(1 << 1)
#define IWX_SCAN_CLIENT_ASSET_TRACKING		(1 << 2)

/**
 * iwx_scan_offload_blacklist - IWX_SCAN_OFFLOAD_BLACKLIST_S
 * @ssid:		MAC address to filter out
 * @reported_rssi:	AP rssi reported to the host
 * @client_bitmap: clients ignore this entry  - enum scan_framework_client
 */
struct iwx_scan_offload_blacklist {
	uint8_t ssid[ETHER_ADDR_LEN];
	uint8_t reported_rssi;
	uint8_t client_bitmap;
} __packed;

#define IWX_NETWORK_TYPE_BSS	1
#define IWX_NETWORK_TYPE_IBSS	2
#define IWX_NETWORK_TYPE_ANY	3

#define IWX_SCAN_OFFLOAD_SELECT_2_4	0x4
#define IWX_SCAN_OFFLOAD_SELECT_5_2	0x8
#define IWX_SCAN_OFFLOAD_SELECT_ANY	0xc

/**
 * iwx_scan_offload_profile - IWX_SCAN_OFFLOAD_PROFILE_S
 * @ssid_index:		index to ssid list in fixed part
 * @unicast_cipher:	encryption algorithm to match - bitmap
 * @aut_alg:		authentication algorithm to match - bitmap
 * @network_type:	enum iwx_scan_offload_network_type
 * @band_selection:	enum iwx_scan_offload_band_selection
 * @client_bitmap:	clients waiting for match - enum scan_framework_client
 */
struct iwx_scan_offload_profile {
	uint8_t ssid_index;
	uint8_t unicast_cipher;
	uint8_t auth_alg;
	uint8_t network_type;
	uint8_t band_selection;
	uint8_t client_bitmap;
	uint8_t reserved[2];
} __packed;

/**
 * iwx_scan_offload_profile_cfg - IWX_SCAN_OFFLOAD_PROFILES_CFG_API_S_VER_1
 * @blacklist:		AP list to filter off from scan results
 * @profiles:		profiles to search for match
 * @blacklist_len:	length of blacklist
 * @num_profiles:	num of profiles in the list
 * @match_notify:	clients waiting for match found notification
 * @pass_match:		clients waiting for the results
 * @active_clients:	active clients bitmap - enum scan_framework_client
 * @any_beacon_notify:	clients waiting for match notification without match
 */
struct iwx_scan_offload_profile_cfg {
	struct iwx_scan_offload_profile profiles[IWX_SCAN_MAX_PROFILES];
	uint8_t blacklist_len;
	uint8_t num_profiles;
	uint8_t match_notify;
	uint8_t pass_match;
	uint8_t active_clients;
	uint8_t any_beacon_notify;
	uint8_t reserved[2];
} __packed;

#define IWX_SCAN_OFFLOAD_COMPLETED	1
#define IWX_SCAN_OFFLOAD_ABORTED	2

/* UMAC Scan API */

#define IWX_SCAN_CONFIG_FLAG_ACTIVATE			(1 << 0)
#define IWX_SCAN_CONFIG_FLAG_DEACTIVATE			(1 << 1)
#define IWX_SCAN_CONFIG_FLAG_FORBID_CHUB_REQS		(1 << 2)
#define IWX_SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS		(1 << 3)
#define IWX_SCAN_CONFIG_FLAG_SET_TX_CHAINS		(1 << 8)
#define IWX_SCAN_CONFIG_FLAG_SET_RX_CHAINS		(1 << 9)
#define IWX_SCAN_CONFIG_FLAG_SET_AUX_STA_ID		(1 << 10)
#define IWX_SCAN_CONFIG_FLAG_SET_ALL_TIMES		(1 << 11)
#define IWX_SCAN_CONFIG_FLAG_SET_EFFECTIVE_TIMES	(1 << 12)
#define IWX_SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS		(1 << 13)
#define IWX_SCAN_CONFIG_FLAG_SET_LEGACY_RATES		(1 << 14)
#define IWX_SCAN_CONFIG_FLAG_SET_MAC_ADDR		(1 << 15)
#define IWX_SCAN_CONFIG_FLAG_SET_FRAGMENTED		(1 << 16)
#define IWX_SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED		(1 << 17)
#define IWX_SCAN_CONFIG_FLAG_SET_CAM_MODE		(1 << 18)
#define IWX_SCAN_CONFIG_FLAG_CLEAR_CAM_MODE		(1 << 19)
#define IWX_SCAN_CONFIG_FLAG_SET_PROMISC_MODE		(1 << 20)
#define IWX_SCAN_CONFIG_FLAG_CLEAR_PROMISC_MODE		(1 << 21)

/* Bits 26-31 are for num of channels in channel_array */
#define IWX_SCAN_CONFIG_N_CHANNELS(n) ((n) << 26)

/* OFDM basic rates */
#define IWX_SCAN_CONFIG_RATE_6M		(1 << 0)
#define IWX_SCAN_CONFIG_RATE_9M		(1 << 1)
#define IWX_SCAN_CONFIG_RATE_12M	(1 << 2)
#define IWX_SCAN_CONFIG_RATE_18M	(1 << 3)
#define IWX_SCAN_CONFIG_RATE_24M	(1 << 4)
#define IWX_SCAN_CONFIG_RATE_36M	(1 << 5)
#define IWX_SCAN_CONFIG_RATE_48M	(1 << 6)
#define IWX_SCAN_CONFIG_RATE_54M	(1 << 7)
/* CCK basic rates */
#define IWX_SCAN_CONFIG_RATE_1M		(1 << 8)
#define IWX_SCAN_CONFIG_RATE_2M		(1 << 9)
#define IWX_SCAN_CONFIG_RATE_5M		(1 << 10)
#define IWX_SCAN_CONFIG_RATE_11M	(1 << 11)

/* Bits 16-27 are for supported rates */
#define IWX_SCAN_CONFIG_SUPPORTED_RATE(rate)	((rate) << 16)

#define IWX_CHANNEL_FLAG_EBS				(1 << 0)
#define IWX_CHANNEL_FLAG_ACCURATE_EBS			(1 << 1)
#define IWX_CHANNEL_FLAG_EBS_ADD			(1 << 2)
#define IWX_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE	(1 << 3)

/**
 * struct iwx_scan_dwell
 * @active:		default dwell time for active scan
 * @passive:		default dwell time for passive scan
 * @fragmented:		default dwell time for fragmented scan
 * @extended:		default dwell time for channels 1, 6 and 11
 */
struct iwx_scan_dwell {
	uint8_t active;
	uint8_t passive;
	uint8_t fragmented;
	uint8_t extended;
} __packed;


#define IWX_SCAN_TWO_LMACS 2
#define IWX_SCAN_LB_LMAC_IDX 0 /* low-band */
#define IWX_SCAN_HB_LMAC_IDX 1 /* high-band */

/**
 * struct iwl_scan_config
 * @enable_cam_mode: whether to enable CAM mode.
 * @enable_promiscuous_mode: whether to enable promiscuous mode
 * @bcast_sta_id: the index of the station in the fw. Deprecated starting with
 *     API version 5.
 * @reserved: reserved
 * @tx_chains: valid_tx antenna - ANT_* definitions
 * @rx_chains: valid_rx antenna - ANT_* definitions
 */
struct iwx_scan_config {
	uint8_t enable_cam_mode;
	uint8_t enable_promiscuous_mode;
	uint8_t bcast_sta_id;
	uint8_t reserved;
	uint32_t tx_chains;
	uint32_t rx_chains;
} __packed; /* SCAN_CONFIG_DB_CMD_API_S_5 */

/**
 * struct iwx_scan_config_v2
 * @flags:			enum scan_config_flags
 * @tx_chains:			valid_tx antenna - ANT_* definitions
 * @rx_chains:			valid_rx antenna - ANT_* definitions
 * @legacy_rates:		default legacy rates - enum scan_config_rates
 * @out_of_channel_time:	default max out of serving channel time
 * @suspend_time:		default max suspend time
 * @dwell_active:		default dwell time for active scan
 * @dwell_passive:		default dwell time for passive scan
 * @dwell_fragmented:		default dwell time for fragmented scan
 * @dwell_extended:		default dwell time for channels 1, 6 and 11
 * @mac_addr:			default mac address to be used in probes
 * @bcast_sta_id:		the index of the station in the fw
 * @channel_flags:		default channel flags - enum iwx_channel_flags
 *				scan_config_channel_flag
 * @channel_array:		default supported channels
 */
struct iwx_scan_config_v2 {
	uint32_t flags;
	uint32_t tx_chains;
	uint32_t rx_chains;
	uint32_t legacy_rates;
	uint32_t out_of_channel_time[IWX_SCAN_TWO_LMACS];
	uint32_t suspend_time[IWX_SCAN_TWO_LMACS];
	struct iwx_scan_dwell dwell;
	uint8_t mac_addr[ETHER_ADDR_LEN];
	uint8_t bcast_sta_id;
	uint8_t channel_flags;
	uint8_t channel_array[];
} __packed; /* SCAN_CONFIG_DB_CMD_API_S_2 */

/**
 * iwx_umac_scan_flags
 *@IWX_UMAC_SCAN_FLAG_PREEMPTIVE: scan process triggered by this scan request
 *	can be preempted by other scan requests with higher priority.
 *	The low priority scan will be resumed when the higher priority scan is
 *	completed.
 *@IWX_UMAC_SCAN_FLAG_START_NOTIF: notification will be sent to the driver
 *	when scan starts.
 */
#define IWX_UMAC_SCAN_FLAG_PREEMPTIVE		(1 << 0)
#define IWX_UMAC_SCAN_FLAG_START_NOTIF		(1 << 1)

#define IWX_UMAC_SCAN_UID_TYPE_OFFSET		0
#define IWX_UMAC_SCAN_UID_SEQ_OFFSET		8

#define IWX_UMAC_SCAN_GEN_FLAGS_PERIODIC	(1 << 0)
#define IWX_UMAC_SCAN_GEN_FLAGS_OVER_BT		(1 << 1)
#define IWX_UMAC_SCAN_GEN_FLAGS_PASS_ALL	(1 << 2)
#define IWX_UMAC_SCAN_GEN_FLAGS_PASSIVE		(1 << 3)
#define IWX_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT	(1 << 4)
#define IWX_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE	(1 << 5)
#define IWX_UMAC_SCAN_GEN_FLAGS_MULTIPLE_SSID	(1 << 6)
#define IWX_UMAC_SCAN_GEN_FLAGS_FRAGMENTED	(1 << 7)
#define IWX_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED	(1 << 8)
#define IWX_UMAC_SCAN_GEN_FLAGS_MATCH		(1 << 9)
#define IWX_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL	(1 << 10)
/* Extended dwell is obsolete when adaptive dwell is used, making this
 * bit reusable. Hence, probe request defer is used only when adaptive
 * dwell is supported. */
#define IWX_UMAC_SCAN_GEN_FLAGS_PROB_REQ_DEFER_SUPP	(1 << 10)
#define IWX_UMAC_SCAN_GEN_FLAGS_LMAC2_FRAGMENTED	(1 << 11)
#define IWX_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL		(1 << 13)
#define IWX_UMAC_SCAN_GEN_FLAGS_MAX_CHNL_TIME		(1 << 14)
#define IWX_UMAC_SCAN_GEN_FLAGS_PROB_REQ_HIGH_TX_RATE	(1 << 15)

/**
 * UMAC scan general flags #2
 * @IWX_UMAC_SCAN_GEN_FLAGS2_NOTIF_PER_CHNL: Whether to send a complete
 *	notification per channel or not.
 * @IWX_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER: Whether to allow channel
 *	reorder optimization or not.
 */
#define IWX_UMAC_SCAN_GEN_FLAGS2_NOTIF_PER_CHNL		(1 << 0)
#define IWX_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER	(1 << 1)

/**
 * UMAC scan general flags version 2
 *
 * The FW flags were reordered and hence the driver introduce version 2
 *
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_PERIODIC: periodic or scheduled
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL: pass all probe responses and beacons
 *                                       during scan iterations
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE: send complete notification
 *      on every iteration instead of only once after the last iteration
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1: fragmented scan LMAC1
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC2: fragmented scan LMAC2
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_MATCH: does this scan check for profile matching
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_USE_ALL_RX_CHAINS: use all valid chains for RX
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL: works with adaptive dwell
 *                                             for active channel
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_PREEMPTIVE: can be preempted by other requests
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_NTF_START: send notification of scan start
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_MULTI_SSID: matching on multiple SSIDs
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE: all the channels scanned
 *                                           as passive
 * @IWX_UMAC_SCAN_GEN_FLAGS_V2_TRIGGER_UHB_SCAN: at the end of 2.4GHz and
 *		5.2Ghz bands scan, trigger scan on 6GHz band to discover
 *		the reported collocated APs
 */
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_PERIODIC             (1 << 0)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL             (1 << 1)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE   (1 << 2)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1     (1 << 3)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC2     (1 << 4)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_MATCH                (1 << 5)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_USE_ALL_RX_CHAINS    (1 << 6)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL       (1 << 7)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_PREEMPTIVE           (1 << 8)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_NTF_START            (1 << 9)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_MULTI_SSID           (1 << 10)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE        (1 << 11)
#define IWX_UMAC_SCAN_GEN_FLAGS_V2_TRIGGER_UHB_SCAN     (1 << 12)

/**
 * struct iwx_scan_channel_cfg_umac
 * @flags:		bitmap - 0-19:	directed scan to i'th ssid.
 * @channel_num:	channel number 1-13 etc.
 * @iter_count:		repetition count for the channel.
 * @iter_interval:	interval between two scan iterations on one channel.
 */
struct iwx_scan_channel_cfg_umac {
	uint32_t flags;
	union {
		struct {
			uint8_t channel_num;
			uint8_t iter_count;
			uint16_t iter_interval;
		} v1; /* SCAN_CHANNEL_CFG_S_VER1 */
		struct {
			uint8_t channel_num;
			uint8_t band;
			uint8_t iter_count;
			uint8_t iter_interval;
		} v2; /* SCAN_CHANNEL_CFG_S_VER{2,3,4} */
	};
} __packed;

/**
 * struct iwx_scan_umac_schedule
 * @interval: interval in seconds between scan iterations
 * @iter_count: num of scan iterations for schedule plan, 0xff for infinite loop
 * @reserved: for alignment and future use
 */
struct iwx_scan_umac_schedule {
	uint16_t interval;
	uint8_t iter_count;
	uint8_t reserved;
} __packed; /* SCAN_SCHED_PARAM_API_S_VER_1 */

/**
 * struct iwx_scan_req_umac_tail - the rest of the UMAC scan request command
 *      parameters following channels configuration array.
 * @schedule: two scheduling plans.
 * @delay: delay in TUs before starting the first scan iteration
 * @reserved: for future use and alignment
 * @preq: probe request with IEs blocks
 * @direct_scan: list of SSIDs for directed active scan
 */
struct iwx_scan_req_umac_tail_v1 {
	/* SCAN_PERIODIC_PARAMS_API_S_VER_1 */
	struct iwx_scan_umac_schedule schedule[IWX_MAX_SCHED_SCAN_PLANS];
	uint16_t delay;
	uint16_t reserved;
	/* SCAN_PROBE_PARAMS_API_S_VER_1 */
	struct iwx_scan_probe_req_v1 preq;
	struct iwx_ssid_ie direct_scan[IWX_PROBE_OPTION_MAX];
} __packed;

/**
 * struct iwx_scan_req_umac_tail - the rest of the UMAC scan request command
 *      parameters following channels configuration array.
 * @schedule: two scheduling plans.
 * @delay: delay in TUs before starting the first scan iteration
 * @reserved: for future use and alignment
 * @preq: probe request with IEs blocks
 * @direct_scan: list of SSIDs for directed active scan
 */
struct iwx_scan_req_umac_tail_v2 {
	/* SCAN_PERIODIC_PARAMS_API_S_VER_1 */
	struct iwx_scan_umac_schedule schedule[IWX_MAX_SCHED_SCAN_PLANS];
	uint16_t delay;
	uint16_t reserved;
	/* SCAN_PROBE_PARAMS_API_S_VER_2 */
	struct iwx_scan_probe_req preq;
	struct iwx_ssid_ie direct_scan[IWX_PROBE_OPTION_MAX];
} __packed;

/**
 * struct iwx_scan_umac_chan_param
 * @flags: channel flags &enum iwl_scan_channel_flags
 * @count: num of channels in scan request
 * @reserved: for future use and alignment
 */
struct iwx_scan_umac_chan_param {
	uint8_t flags;
	uint8_t count;
	uint16_t reserved;
} __packed; /* SCAN_CHANNEL_PARAMS_API_S_VER_1 */

#define IWX_SCAN_LB_LMAC_IDX 0
#define IWX_SCAN_HB_LMAC_IDX 1

/**
 * struct iwx_scan_req_umac
 * @flags: &enum iwl_umac_scan_flags
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @ooc_priority: out of channel priority - &enum iwl_scan_priority
 * @general_flags: &enum iwl_umac_scan_general_flags
 * @scan_start_mac_id: report the scan start TSF time according to this mac TSF
 * @extended_dwell: dwell time for channels 1, 6 and 11
 * @active_dwell: dwell time for active scan per LMAC
 * @passive_dwell: dwell time for passive scan per LMAC
 * @fragmented_dwell: dwell time for fragmented passive scan
 * @adwell_default_n_aps: for adaptive dwell the default number of APs
 *	per channel
 * @adwell_default_n_aps_social: for adaptive dwell the default
 *	number of APs per social (1,6,11) channel
 * @general_flags2: &enum iwl_umac_scan_general_flags2
 * @adwell_max_budget: for adaptive dwell the maximal budget of TU to be added
 *	to total scan time
 * @max_out_time: max out of serving channel time, per LMAC - for CDB there
 *	are 2 LMACs (high band and low band)
 * @suspend_time: max suspend time, per LMAC - for CDB there are 2 LMACs
 * @scan_priority: scan internal prioritization &enum iwl_scan_priority
 * @num_of_fragments: Number of fragments needed for full coverage per band.
 *	Relevant only for fragmented scan.
 * @channel: &struct iwx_scan_umac_chan_param
 * @reserved: for future use and alignment
 * @reserved3: for future use and alignment
 * @data: &struct iwx_scan_channel_cfg_umac and
 *	&struct iwx_scan_req_umac_tail
 */
struct iwx_scan_req_umac {
	uint32_t flags;
	uint32_t uid;
	uint32_t ooc_priority;
	/* SCAN_GENERAL_PARAMS_API_S_VER_1 */
	uint16_t general_flags;
	uint8_t reserved;
	uint8_t scan_start_mac_id;
	union {
		struct {
			uint8_t extended_dwell;
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint32_t max_out_time;
			uint32_t suspend_time;
			uint32_t scan_priority;
			struct iwx_scan_umac_chan_param channel;
			uint8_t data[];
		} v1; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_1 */
		struct {
			uint8_t extended_dwell;
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			struct iwx_scan_umac_chan_param channel;
			uint8_t data[];
		} v6; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_6 */
		struct {
			uint8_t active_dwell;
			uint8_t passive_dwell;
			uint8_t fragmented_dwell;
			uint8_t adwell_default_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t reserved3;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			struct iwx_scan_umac_chan_param channel;
			uint8_t data[];
		} v7; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_7 */
		struct {
			uint8_t active_dwell[2];
			uint8_t reserved2;
			uint8_t adwell_default_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t general_flags2;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			uint8_t passive_dwell[2];
			uint8_t num_of_fragments[2];
			struct iwx_scan_umac_chan_param channel;
			uint8_t data[];
		} v8; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_8 */
		struct {
			uint8_t active_dwell[2];
			uint8_t adwell_default_hb_n_aps;
			uint8_t adwell_default_lb_n_aps;
			uint8_t adwell_default_n_aps_social;
			uint8_t general_flags2;
			uint16_t adwell_max_budget;
			uint32_t max_out_time[2];
			uint32_t suspend_time[2];
			uint32_t scan_priority;
			uint8_t passive_dwell[2];
			uint8_t num_of_fragments[2];
			struct iwx_scan_umac_chan_param channel;
			uint8_t data[];
		} v9; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_9 */
	};
} __packed;

#define IWX_SCAN_REQ_UMAC_SIZE_V8 sizeof(struct iwx_scan_req_umac)
#define IWX_SCAN_REQ_UMAC_SIZE_V7 48
#define IWX_SCAN_REQ_UMAC_SIZE_V6 44
#define IWX_SCAN_REQ_UMAC_SIZE_V1 36

/**
 * struct iwx_scan_general_params_v10
 * @flags: &enum iwx_umac_scan_flags
 * @reserved: reserved for future
 * @scan_start_mac_id: report the scan start TSF time according to this mac TSF
 * @active_dwell: dwell time for active scan per LMAC
 * @adwell_default_2g: adaptive dwell default number of APs
 *                        for 2.4GHz channel
 * @adwell_default_5g: adaptive dwell default number of APs
 *                        for 5GHz channels
 * @adwell_default_social_chn: adaptive dwell default number of
 *                             APs per social channel
 * @reserved1: reserved for future
 * @adwell_max_budget: the maximal number of TUs that adaptive dwell
 *                     can add to the total scan time
 * @max_out_of_time: max out of serving channel time, per LMAC
 * @suspend_time: max suspend time, per LMAC
 * @scan_priority: priority of the request
 * @passive_dwell: continues dwell time for passive channel
 *                 (without adaptive dwell)
 * @num_of_fragments: number of fragments needed for full fragmented
 *                    scan coverage.
 */
struct iwx_scan_general_params_v10 {
	uint16_t flags;
	uint8_t reserved;
	uint8_t scan_start_mac_id;
	uint8_t active_dwell[IWX_SCAN_TWO_LMACS];
	uint8_t adwell_default_2g;
	uint8_t adwell_default_5g;
	uint8_t adwell_default_social_chn;
	uint8_t reserved1;
	uint16_t adwell_max_budget;
	uint32_t max_out_of_time[IWX_SCAN_TWO_LMACS];
	uint32_t suspend_time[IWX_SCAN_TWO_LMACS];
	uint32_t scan_priority;
	uint8_t passive_dwell[IWX_SCAN_TWO_LMACS];
	uint8_t num_of_fragments[IWX_SCAN_TWO_LMACS];
} __packed; /* SCAN_GENERAL_PARAMS_API_S_VER_10 */

/**
 * struct iwx_scan_channel_params_v6
 * @flags: channel flags &enum iwl_scan_channel_flags
 * @count: num of channels in scan request
 * @n_aps_override: override the number of APs the FW uses to calculate dwell
 *	time when adaptive dwell is used.
 *	Channel k will use n_aps_override[i] when BIT(20 + i) is set in
 *	channel_config[k].flags
 * @channel_config: array of explicit channel configurations
 *                  for 2.4Ghz and 5.2Ghz bands
 */
struct iwx_scan_channel_params_v6 {
	uint8_t flags;
	uint8_t count;
	uint8_t n_aps_override[2];
	struct iwx_scan_channel_cfg_umac channel_config[67];
} __packed; /* SCAN_CHANNEL_PARAMS_API_S_VER_6 */

/**
 * struct iwx_scan_periodic_parms_v1
 * @schedule: can scheduling parameter
 * @delay: initial delay of the periodic scan in seconds
 * @reserved: reserved for future
 */
struct iwx_scan_periodic_parms_v1 {
	struct iwx_scan_umac_schedule schedule[IWX_MAX_SCHED_SCAN_PLANS];
	uint16_t delay;
	uint16_t reserved;
} __packed; /* SCAN_PERIODIC_PARAMS_API_S_VER_1 */

#define IWX_SCAN_SHORT_SSID_MAX_SIZE        8
#define IWX_SCAN_BSSID_MAX_SIZE             16

/**
 * struct iwx_scan_probe_params_v4
 * @preq: scan probe request params
 * @short_ssid_num: number of valid short SSIDs in short ssid array
 * @bssid_num: number of valid bssid in bssids array
 * @reserved: reserved
 * @direct_scan: list of ssids
 * @short_ssid: array of short ssids
 * @bssid_array: array of bssids
 */
struct iwx_scan_probe_params_v4 {
	struct iwx_scan_probe_req preq;
	uint8_t short_ssid_num;
	uint8_t bssid_num;
	uint16_t reserved;
	struct iwx_ssid_ie direct_scan[IWX_PROBE_OPTION_MAX];
	uint32_t short_ssid[IWX_SCAN_SHORT_SSID_MAX_SIZE];
	uint8_t bssid_array[IWX_SCAN_BSSID_MAX_SIZE][ETHER_ADDR_LEN];
} __packed; /* SCAN_PROBE_PARAMS_API_S_VER_4 */

/**
 * struct iwx_scan_req_params_v14
 * @general_params: &struct iwx_scan_general_params_v10
 * @channel_params: &struct iwx_scan_channel_params_v6
 * @periodic_params: &struct iwx_scan_periodic_parms_v1
 * @probe_params: &struct iwx_scan_probe_params_v4
 */
struct iwx_scan_req_params_v14 {
	struct iwx_scan_general_params_v10 general_params;
	struct iwx_scan_channel_params_v6 channel_params;
	struct iwx_scan_periodic_parms_v1 periodic_params;
	struct iwx_scan_probe_params_v4 probe_params;
} __packed; /* SCAN_REQUEST_PARAMS_API_S_VER_14 */

/**
 * struct iwx_scan_req_umac_v14
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @ooc_priority: out of channel priority - &enum iwx_scan_priority
 * @scan_params: scan parameters
 */
struct iwx_scan_req_umac_v14 {
	uint32_t uid;
	uint32_t ooc_priority;
	struct iwx_scan_req_params_v14 scan_params;
} __packed; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_14 */

/**
 * struct iwx_umac_scan_abort
 * @uid: scan id, &enum iwx_umac_scan_uid_offsets
 * @flags: reserved
 */
struct iwx_umac_scan_abort {
	uint32_t uid;
	uint32_t flags;
} __packed; /* SCAN_ABORT_CMD_UMAC_API_S_VER_1 */

/**
 * struct iwx_umac_scan_complete
 * @uid: scan id, &enum iwx_umac_scan_uid_offsets
 * @last_schedule: last scheduling line
 * @last_iter:	last scan iteration number
 * @scan status: &enum iwx_scan_offload_complete_status
 * @ebs_status: &enum iwx_scan_ebs_status
 * @time_from_last_iter: time elapsed from last iteration
 * @reserved: for future use
 */
struct iwx_umac_scan_complete {
	uint32_t uid;
	uint8_t last_schedule;
	uint8_t last_iter;
	uint8_t status;
	uint8_t ebs_status;
	uint32_t time_from_last_iter;
	uint32_t reserved;
} __packed; /* SCAN_COMPLETE_NTF_UMAC_API_S_VER_1 */

#define IWX_SCAN_OFFLOAD_MATCHING_CHANNELS_LEN 5
/**
 * struct iwx_scan_offload_profile_match - match information
 * @bssid: matched bssid
 * @channel: channel where the match occurred
 * @energy:
 * @matching_feature:
 * @matching_channels: bitmap of channels that matched, referencing
 *	the channels passed in tue scan offload request
 */
struct iwx_scan_offload_profile_match {
	uint8_t bssid[ETHER_ADDR_LEN];
	uint16_t reserved;
	uint8_t channel;
	uint8_t energy;
	uint8_t matching_feature;
	uint8_t matching_channels[IWX_SCAN_OFFLOAD_MATCHING_CHANNELS_LEN];
} __packed; /* SCAN_OFFLOAD_PROFILE_MATCH_RESULTS_S_VER_1 */

/**
 * struct iwx_scan_offload_profiles_query - match results query response
 * @matched_profiles: bitmap of matched profiles, referencing the
 *	matches passed in the scan offload request
 * @last_scan_age: age of the last offloaded scan
 * @n_scans_done: number of offloaded scans done
 * @gp2_d0u: GP2 when D0U occurred
 * @gp2_invoked: GP2 when scan offload was invoked
 * @resume_while_scanning: not used
 * @self_recovery: obsolete
 * @reserved: reserved
 * @matches: array of match information, one for each match
 */
struct iwx_scan_offload_profiles_query {
	uint32_t matched_profiles;
	uint32_t last_scan_age;
	uint32_t n_scans_done;
	uint32_t gp2_d0u;
	uint32_t gp2_invoked;
	uint8_t resume_while_scanning;
	uint8_t self_recovery;
	uint16_t reserved;
	struct iwx_scan_offload_profile_match matches[IWX_SCAN_MAX_PROFILES];
} __packed; /* SCAN_OFFLOAD_PROFILES_QUERY_RSP_S_VER_2 */

/**
 * struct iwx_umac_scan_iter_complete_notif - notifies end of scanning iteration
 * @uid: scan id, &enum iwx_umac_scan_uid_offsets
 * @scanned_channels: number of channels scanned and number of valid elements in
 *	results array
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: array of scan results, only "scanned_channels" of them are valid
 */
struct iwx_umac_scan_iter_complete_notif {
	uint32_t uid;
	uint8_t scanned_channels;
	uint8_t status;
	uint8_t bt_status;
	uint8_t last_channel;
	uint32_t tsf_low;
	uint32_t tsf_high;
	struct iwx_scan_results_notif results[];
} __packed; /* SCAN_ITER_COMPLETE_NTF_UMAC_API_S_VER_1 */

#define IWX_GSCAN_START_CMD			0x0
#define IWX_GSCAN_STOP_CMD			0x1
#define IWX_GSCAN_SET_HOTLIST_CMD		0x2
#define IWX_GSCAN_RESET_HOTLIST_CMD		0x3
#define IWX_GSCAN_SET_SIGNIFICANT_CHANGE_CMD	0x4
#define IWX_GSCAN_RESET_SIGNIFICANT_CHANGE_CMD	0x5
#define IWX_GSCAN_SIGNIFICANT_CHANGE_EVENT	0xFD
#define IWX_GSCAN_HOTLIST_CHANGE_EVENT		0xFE
#define IWX_GSCAN_RESULTS_AVAILABLE_EVENT	0xFF

/* STA API */

/**
 * flags for the ADD_STA host command
 * @IWX_STA_FLG_REDUCED_TX_PWR_CTRL:
 * @IWX_STA_FLG_REDUCED_TX_PWR_DATA:
 * @IWX_STA_FLG_DISABLE_TX: set if TX should be disabled
 * @IWX_STA_FLG_PS: set if STA is in Power Save
 * @IWX_STA_FLG_INVALID: set if STA is invalid
 * @IWX_STA_FLG_DLP_EN: Direct Link Protocol is enabled
 * @IWX_STA_FLG_SET_ALL_KEYS: the current key applies to all key IDs
 * @IWX_STA_FLG_DRAIN_FLOW: drain flow
 * @IWX_STA_FLG_PAN: STA is for PAN interface
 * @IWX_STA_FLG_CLASS_AUTH:
 * @IWX_STA_FLG_CLASS_ASSOC:
 * @IWX_STA_FLG_CLASS_MIMO_PROT:
 * @IWX_STA_FLG_MAX_AGG_SIZE_MSK: maximal size for A-MPDU
 * @IWX_STA_FLG_AGG_MPDU_DENS_MSK: maximal MPDU density for Tx aggregation
 * @IWX_STA_FLG_FAT_EN_MSK: support for channel width (for Tx). This flag is
 *	initialised by driver and can be updated by fw upon reception of
 *	action frames that can change the channel width. When cleared the fw
 *	will send all the frames in 20MHz even when FAT channel is requested.
 * @IWX_STA_FLG_MIMO_EN_MSK: support for MIMO. This flag is initialised by the
 *	driver and can be updated by fw upon reception of action frames.
 * @IWX_STA_FLG_MFP_EN: Management Frame Protection
 */
#define IWX_STA_FLG_REDUCED_TX_PWR_CTRL	(1 << 3)
#define IWX_STA_FLG_REDUCED_TX_PWR_DATA	(1 << 6)

#define IWX_STA_FLG_DISABLE_TX		(1 << 4)

#define IWX_STA_FLG_PS			(1 << 8)
#define IWX_STA_FLG_DRAIN_FLOW		(1 << 12)
#define IWX_STA_FLG_PAN			(1 << 13)
#define IWX_STA_FLG_CLASS_AUTH		(1 << 14)
#define IWX_STA_FLG_CLASS_ASSOC		(1 << 15)
#define IWX_STA_FLG_RTS_MIMO_PROT	(1 << 17)

#define IWX_STA_FLG_MAX_AGG_SIZE_SHIFT	19
#define IWX_STA_FLG_MAX_AGG_SIZE_8K	(0 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_16K	(1 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_32K	(2 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_64K	(3 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_128K	(4 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_256K	(5 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_512K	(6 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_1024K	(7 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_2M	(8 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_4M	(9 << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)
#define IWX_STA_FLG_MAX_AGG_SIZE_MSK	(0xf << IWX_STA_FLG_MAX_AGG_SIZE_SHIFT)

#define IWX_STA_FLG_AGG_MPDU_DENS_SHIFT	23
#define IWX_STA_FLG_AGG_MPDU_DENS_2US	(4 << IWX_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWX_STA_FLG_AGG_MPDU_DENS_4US	(5 << IWX_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWX_STA_FLG_AGG_MPDU_DENS_8US	(6 << IWX_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWX_STA_FLG_AGG_MPDU_DENS_16US	(7 << IWX_STA_FLG_AGG_MPDU_DENS_SHIFT)
#define IWX_STA_FLG_AGG_MPDU_DENS_MSK	(7 << IWX_STA_FLG_AGG_MPDU_DENS_SHIFT)

#define IWX_STA_FLG_FAT_EN_20MHZ	(0 << 26)
#define IWX_STA_FLG_FAT_EN_40MHZ	(1 << 26)
#define IWX_STA_FLG_FAT_EN_80MHZ	(2 << 26)
#define IWX_STA_FLG_FAT_EN_160MHZ	(3 << 26)
#define IWX_STA_FLG_FAT_EN_MSK		(3 << 26)

#define IWX_STA_FLG_MIMO_EN_SISO	(0 << 28)
#define IWX_STA_FLG_MIMO_EN_MIMO2	(1 << 28)
#define IWX_STA_FLG_MIMO_EN_MIMO3	(2 << 28)
#define IWX_STA_FLG_MIMO_EN_MSK		(3 << 28)

/**
 * key flags for the ADD_STA host command
 * @IWX_STA_KEY_FLG_NO_ENC: no encryption
 * @IWX_STA_KEY_FLG_WEP: WEP encryption algorithm
 * @IWX_STA_KEY_FLG_CCM: CCMP encryption algorithm
 * @IWX_STA_KEY_FLG_TKIP: TKIP encryption algorithm
 * @IWX_STA_KEY_FLG_EXT: extended cipher algorithm (depends on the FW support)
 * @IWX_STA_KEY_FLG_CMAC: CMAC encryption algorithm
 * @IWX_STA_KEY_FLG_ENC_UNKNOWN: unknown encryption algorithm
 * @IWX_STA_KEY_FLG_EN_MSK: mask for encryption algorithm value
 * @IWX_STA_KEY_FLG_WEP_KEY_MAP: wep is either a group key (0 - legacy WEP) or from
 *	station info array (1 - n 1X mode)
 * @IWX_STA_KEY_FLG_KEYID_MSK: the index of the key
 * @IWX_STA_KEY_NOT_VALID: key is invalid
 * @IWX_STA_KEY_FLG_WEP_13BYTES: set for 13 bytes WEP key
 * @IWX_STA_KEY_MULTICAST: set for multicast key
 * @IWX_STA_KEY_MFP: key is used for Management Frame Protection
 */
#define IWX_STA_KEY_FLG_NO_ENC		(0 << 0)
#define IWX_STA_KEY_FLG_WEP		(1 << 0)
#define IWX_STA_KEY_FLG_CCM		(2 << 0)
#define IWX_STA_KEY_FLG_TKIP		(3 << 0)
#define IWX_STA_KEY_FLG_EXT		(4 << 0)
#define IWX_STA_KEY_FLG_CMAC		(6 << 0)
#define IWX_STA_KEY_FLG_ENC_UNKNOWN	(7 << 0)
#define IWX_STA_KEY_FLG_EN_MSK		(7 << 0)
#define IWX_STA_KEY_FLG_WEP_KEY_MAP	(1 << 3)
#define IWX_STA_KEY_FLG_KEYID_POS	8
#define IWX_STA_KEY_FLG_KEYID_MSK	(3 << IWX_STA_KEY_FLG_KEYID_POS)
#define IWX_STA_KEY_NOT_VALID		(1 << 11)
#define IWX_STA_KEY_FLG_WEP_13BYTES	(1 << 12)
#define IWX_STA_KEY_MULTICAST		(1 << 14)
#define IWX_STA_KEY_MFP			(1 << 15)

/**
 * indicate to the fw what flag are being changed
 * @IWX_STA_MODIFY_QUEUE_REMOVAL: this command removes a queue
 * @IWX_STA_MODIFY_TID_DISABLE_TX: this command modifies %tid_disable_tx
 * @IWX_STA_MODIFY_TX_RATE: unused
 * @IWX_STA_MODIFY_ADD_BA_TID: this command modifies %add_immediate_ba_tid
 * @IWX_STA_MODIFY_REMOVE_BA_TID: this command modifies %remove_immediate_ba_tid
 * @IWX_STA_MODIFY_SLEEPING_STA_TX_COUNT: this command modifies %sleep_tx_count
 * @IWX_STA_MODIFY_PROT_TH:
 * @IWX_STA_MODIFY_QUEUES: modify the queues used by this station
 */
#define IWX_STA_MODIFY_QUEUE_REMOVAL		(1 << 0)
#define IWX_STA_MODIFY_TID_DISABLE_TX		(1 << 1)
#define IWX_STA_MODIFY_TX_RATE			(1 << 2)
#define IWX_STA_MODIFY_ADD_BA_TID		(1 << 3)
#define IWX_STA_MODIFY_REMOVE_BA_TID		(1 << 4)
#define IWX_STA_MODIFY_SLEEPING_STA_TX_COUNT	(1 << 5)
#define IWX_STA_MODIFY_PROT_TH			(1 << 6)
#define IWX_STA_MODIFY_QUEUES			(1 << 7)

#define IWX_STA_MODE_MODIFY	1

/**
 * type of sleep of the station
 * @IWX_STA_SLEEP_STATE_AWAKE:
 * @IWX_STA_SLEEP_STATE_PS_POLL:
 * @IWX_STA_SLEEP_STATE_UAPSD:
 * @IWX_STA_SLEEP_STATE_MOREDATA: set more-data bit on
 *	(last) released frame
 */
#define IWX_STA_SLEEP_STATE_AWAKE	0
#define IWX_STA_SLEEP_STATE_PS_POLL	(1 << 0)
#define IWX_STA_SLEEP_STATE_UAPSD	(1 << 1)
#define IWX_STA_SLEEP_STATE_MOREDATA	(1 << 2)

/* STA ID and color bits definitions */
#define IWX_STA_ID_SEED		(0x0f)
#define IWX_STA_ID_POS		(0)
#define IWX_STA_ID_MSK		(IWX_STA_ID_SEED << IWX_STA_ID_POS)

#define IWX_STA_COLOR_SEED	(0x7)
#define IWX_STA_COLOR_POS	(4)
#define IWX_STA_COLOR_MSK	(IWX_STA_COLOR_SEED << IWX_STA_COLOR_POS)

#define IWX_STA_ID_N_COLOR_GET_COLOR(id_n_color) \
	(((id_n_color) & IWX_STA_COLOR_MSK) >> IWX_STA_COLOR_POS)
#define IWX_STA_ID_N_COLOR_GET_ID(id_n_color)    \
	(((id_n_color) & IWX_STA_ID_MSK) >> IWX_STA_ID_POS)

#define IWX_STA_KEY_MAX_NUM (16)
#define IWX_STA_KEY_IDX_INVALID (0xff)
#define IWX_STA_KEY_MAX_DATA_KEY_NUM (4)
#define IWX_MAX_GLOBAL_KEYS (4)
#define IWX_STA_KEY_LEN_WEP40 (5)
#define IWX_STA_KEY_LEN_WEP104 (13)

/**
 * struct iwx_keyinfo - key information
 * @key_flags: type %iwx_sta_key_flag
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 * @key_offset: key offset in the fw's key table
 * @key: 16-byte unicast decryption key
 * @tx_secur_seq_cnt: initial RSC / PN needed for replay check
 * @hw_tkip_mic_rx_key: byte: MIC Rx Key - used for TKIP only
 * @hw_tkip_mic_tx_key: byte: MIC Tx Key - used for TKIP only
 */
struct iwx_keyinfo {
	uint16_t key_flags;
	uint8_t tkip_rx_tsc_byte2;
	uint8_t reserved1;
	uint16_t tkip_rx_ttak[5];
	uint8_t key_offset;
	uint8_t reserved2;
	uint8_t key[16];
	uint64_t tx_secur_seq_cnt;
	uint64_t hw_tkip_mic_rx_key;
	uint64_t hw_tkip_mic_tx_key;
} __packed;

#define IWX_ADD_STA_STATUS_MASK		0xFF
#define IWX_ADD_STA_BAID_VALID_MASK	0x8000
#define IWX_ADD_STA_BAID_MASK		0x7F00
#define IWX_ADD_STA_BAID_SHIFT		8

/**
 * struct iwx_add_sta_cmd - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: see &enum iwl_sta_mode
 * @awake_acs: ACs to transmit data on while station is sleeping (for U-APSD)
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %STA_MODIFY_TID_DISABLE_TX to change this field.
 * @mac_id_n_color: the Mac context this station belongs to,
 *	see &enum iwl_ctxt_id_and_color
 * @addr: station's MAC address
 * @reserved2: reserved
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @reserved3: reserved
 * @station_flags: look at &enum iwl_sta_flags
 * @station_flags_msk: what of %station_flags have changed,
 *	also &enum iwl_sta_flags
 * @add_immediate_ba_tid: tid for which to add block-ack support (Rx)
 *	Set %STA_MODIFY_ADD_BA_TID to use this field, and also set
 *	add_immediate_ba_ssn.
 * @remove_immediate_ba_tid: tid for which to remove block-ack support (Rx)
 *	Set %STA_MODIFY_REMOVE_BA_TID to use this field
 * @add_immediate_ba_ssn: ssn for the Rx block-ack session. Used together with
 *	add_immediate_ba_tid.
 * @sleep_tx_count: number of packets to transmit to station even though it is
 *	asleep. Used to synchronise PS-poll and u-APSD responses while ucode
 *	keeps track of STA sleep state.
 * @station_type: type of this station. See &enum iwl_sta_type.
 * @sleep_state_flags: Look at &enum iwl_sta_sleep_flag.
 * @assoc_id: assoc_id to be sent in VHT PLCP (9-bit), for grp use 0, for AP
 *	mac-addr.
 * @beamform_flags: beam forming controls
 * @tfd_queue_msk: tfd queues used by this station.
 *	Obsolete for new TX API (9 and above).
 * @rx_ba_window: aggregation window size
 * @sp_length: the size of the SP in actual number of frames
 * @uapsd_acs:  4 LS bits are trigger enabled ACs, 4 MS bits are the deliver
 *	enabled ACs.
 *
 * The device contains an internal table of per-station information, with info
 * on security keys, aggregation parameters, and Tx rates for initial Tx
 * attempt and any retries (set by REPLY_TX_LINK_QUALITY_CMD).
 *
 * ADD_STA sets up the table entry for one station, either creating a new
 * entry, or modifying a pre-existing one.
 */
struct iwx_add_sta_cmd {
	uint8_t add_modify;
	uint8_t awake_acs;
	uint16_t tid_disable_tx;
	uint32_t mac_id_n_color;
	uint8_t addr[ETHER_ADDR_LEN];	/* _STA_ID_MODIFY_INFO_API_S_VER_1 */
	uint16_t reserved2;
	uint8_t sta_id;
	uint8_t modify_mask;
	uint16_t reserved3;
	uint32_t station_flags;
	uint32_t station_flags_msk;
	uint8_t add_immediate_ba_tid;
	uint8_t remove_immediate_ba_tid;
	uint16_t add_immediate_ba_ssn;
	uint16_t sleep_tx_count;
	uint8_t sleep_state_flags;
	uint8_t station_type;
	uint16_t assoc_id;
	uint16_t beamform_flags;
	uint32_t tfd_queue_msk;
	uint16_t rx_ba_window;
	uint8_t sp_length;
	uint8_t uapsd_acs;
} __packed; /* ADD_STA_CMD_API_S_VER_10 */

/**
 * FW station types
 * ( REPLY_ADD_STA = 0x18 )
 * @IWX_STA_LINK: Link station - normal RX and TX traffic.
 * @IWX_STA_GENERAL_PURPOSE: General purpose. In AP mode used for beacons
 *	and probe responses.
 * @IWX_STA_MULTICAST: multicast traffic,
 * @IWX_STA_TDLS_LINK: TDLS link station
 * @IWX_STA_AUX_ACTIVITY: auxiliary station (scan, ROC and so on).
 */
#define IWX_STA_LINK		0
#define IWX_STA_GENERAL_PURPOSE	1
#define IWX_STA_MULTICAST	2
#define IWX_STA_TDLS_LINK	3
#define IWX_STA_AUX_ACTIVITY	4

/**
 * struct iwx_add_sta_key_common - add/modify sta key common part
 * ( REPLY_ADD_STA_KEY = 0x17 )
 * @sta_id: index of station in uCode's station table
 * @key_offset: key offset in key storage
 * @key_flags: IWX_STA_KEY_FLG_* 
 * @key: key material data
 * @rx_secur_seq_cnt: RX security sequence counter for the key
 */
struct iwx_add_sta_key_common {
	uint8_t sta_id;
	uint8_t key_offset;
	uint16_t key_flags;
	uint8_t key[32];
	uint8_t rx_secur_seq_cnt[16];
} __packed;

/**
 * struct iwx_add_sta_key_cmd_v1 - add/modify sta key
 * @common: see &struct iwx_add_sta_key_common
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @reserved: reserved
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 */
struct iwx_add_sta_key_cmd_v1 {
	struct iwx_add_sta_key_common common;
	uint8_t tkip_rx_tsc_byte2;
	uint8_t reserved;
	uint16_t tkip_rx_ttak[5];
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_1 */

/**
 * struct iwx_add_sta_key_cmd - add/modify sta key
 * @common: see &struct iwx_add_sta_key_common
 * @rx_mic_key: TKIP RX unicast or multicast key
 * @tx_mic_key: TKIP TX key
 * @transmit_seq_cnt: TSC, transmit packet number
 */
struct iwx_add_sta_key_cmd {
	struct iwx_add_sta_key_common common;
	uint64_t rx_mic_key;
	uint64_t tx_mic_key;
	uint64_t transmit_seq_cnt;
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_2 */

/**
 * status in the response to ADD_STA command
 * @IWX_ADD_STA_SUCCESS: operation was executed successfully
 * @IWX_ADD_STA_STATIONS_OVERLOAD: no room left in the fw's station table
 * @IWX_ADD_STA_IMMEDIATE_BA_FAILURE: can't add Rx block ack session
 * @IWX_ADD_STA_MODIFY_NON_EXISTING_STA: driver requested to modify a station
 *	that doesn't exist.
 */
#define IWX_ADD_STA_SUCCESS			0x1
#define IWX_ADD_STA_STATIONS_OVERLOAD		0x2
#define IWX_ADD_STA_IMMEDIATE_BA_FAILURE	0x4
#define IWX_ADD_STA_MODIFY_NON_EXISTING_STA	0x8

/**
 * struct iwx_rm_sta_cmd - Add / modify a station in the fw's station table
 * ( IWX_REMOVE_STA = 0x19 )
 * @sta_id: the station id of the station to be removed
 */
struct iwx_rm_sta_cmd {
	uint8_t sta_id;
	uint8_t reserved[3];
} __packed; /* IWX_REMOVE_STA_CMD_API_S_VER_2 */

/**
 * struct iwx_mgmt_mcast_key_cmd
 * ( IWX_MGMT_MCAST_KEY = 0x1f )
 * @ctrl_flags: %iwx_sta_key_flag
 * @IGTK:
 * @K1: IGTK master key
 * @K2: IGTK sub key
 * @sta_id: station ID that support IGTK
 * @key_id:
 * @receive_seq_cnt: initial RSC/PN needed for replay check
 */
struct iwx_mgmt_mcast_key_cmd {
	uint32_t ctrl_flags;
	uint8_t IGTK[16];
	uint8_t K1[16];
	uint8_t K2[16];
	uint32_t key_id;
	uint32_t sta_id;
	uint64_t receive_seq_cnt;
} __packed; /* SEC_MGMT_MULTICAST_KEY_CMD_API_S_VER_1 */

struct iwx_wep_key {
	uint8_t key_index;
	uint8_t key_offset;
	uint16_t reserved1;
	uint8_t key_size;
	uint8_t reserved2[3];
	uint8_t key[16];
} __packed;

struct iwx_wep_key_cmd {
	uint32_t mac_id_n_color;
	uint8_t num_keys;
	uint8_t decryption_type;
	uint8_t flags;
	uint8_t reserved;
	struct iwx_wep_key wep_key[0];
} __packed; /* SEC_CURR_WEP_KEY_CMD_API_S_VER_2 */

/* 
 * BT coex
 */

#define IWX_BT_COEX_DISABLE		0x0
#define IWX_BT_COEX_NW			0x1
#define IWX_BT_COEX_BT			0x2
#define IWX_BT_COEX_WIFI		0x3
/* BT_COEX_MODES_E */

#define IWX_BT_COEX_MPLUT_ENABLED	(1 << 0)
#define IWX_BT_COEX_MPLUT_BOOST_ENABLED	(1 << 1)
#define IWX_BT_COEX_SYNC2SCO_ENABLED	(1 << 2)
#define IWX_BT_COEX_CORUN_ENABLED	(1 << 3)
#define IWX_BT_COEX_HIGH_BAND_RET	(1 << 4)
/* BT_COEX_MODULES_ENABLE_E_VER_1 */

/**
 * struct iwx_bt_coex_cmd - bt coex configuration command
 * @mode: enum %iwx_bt_coex_mode
 * @enabled_modules: enum %iwx_bt_coex_enabled_modules
 *
 * The structure is used for the BT_COEX command.
 */
struct iwx_bt_coex_cmd {
	uint32_t mode;
	uint32_t enabled_modules;
} __packed; /* BT_COEX_CMD_API_S_VER_6 */


/*
 * Location Aware Regulatory (LAR) API - MCC updates
 */

/**
 * struct iwx_mcc_update_cmd - Request the device to update geographic
 * regulatory profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: the source from where we got the MCC, see IWX_MCC_SOURCE_*
 * @reserved: reserved for alignment
 * @key: integrity key for MCC API OEM testing
 * @reserved2: reserved
 */
struct iwx_mcc_update_cmd {
	uint16_t mcc;
	uint8_t source_id;
	uint8_t reserved;
	uint32_t key;
	uint32_t reserved2[5];
} __packed; /* LAR_UPDATE_MCC_CMD_API_S_VER_2 */

/**
 * iwx_mcc_update_resp_v3 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwx_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @source_id: the MCC source, see IWX_MCC_SOURCE_*
 * @time: time elapsed from the MCC test start (in 30 seconds TU)
 * @reserved: reserved.
 * @n_channels: number of channels in @channels_data (may be 14, 39, 50 or 51
 *		channels, depending on platform)
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwx_mcc_update_resp_v3 {
	uint32_t status;
	uint16_t mcc;
	uint8_t cap;
	uint8_t source_id;
	uint16_t time;
	uint16_t geo_info;
	uint32_t n_channels;
	uint32_t channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_3 */

/**
 * geographic information.
 * @GEO_NO_INFO: no special info for this geo profile.
 * @GEO_WMM_ETSI_5GHZ_INFO: this geo profile limits the WMM params
 *	for the 5 GHz band.
 */
#define IWX_GEO_NO_INFO			0
#define IWX_GEO_WMM_ETSI_5GHZ_INFO (1 << 0)

/**
 * struct iwx_mcc_update_resp - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwl_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @time: time elapsed from the MCC test start (in units of 30 seconds)
 * @geo_info: geographic specific profile information
 *	see IWX_GEO_*
 * @source_id: the MCC source, see IWX_MCC_SOURCE_*
 * @reserved: for four bytes alignment.
 * @n_channels: number of channels in @channels_data.
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwx_mcc_update_resp {
	uint32_t status;
	uint16_t mcc;
	uint16_t cap;
	uint16_t time;
	uint16_t geo_info;
	uint8_t source_id;
	uint8_t reserved[3];
	uint32_t n_channels;
	uint32_t channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_4 */

/**
 * struct iwx_mcc_chub_notif - chub notifies of mcc change
 * (MCC_CHUB_UPDATE_CMD = 0xc9)
 * The Chub (Communication Hub, CommsHUB) is a HW component that connects to
 * the cellular and connectivity cores that gets updates of the mcc, and
 * notifies the ucode directly of any mcc change.
 * The ucode requests the driver to request the device to update geographic
 * regulatory  profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: identity of the change originator, see IWX_MCC_SOURCE_*
 * @reserved1: reserved for alignment
 */
struct iwx_mcc_chub_notif {
	uint16_t mcc;
	uint8_t source_id;
	uint8_t reserved1;
} __packed; /* LAR_MCC_NOTIFY_S */

enum iwx_mcc_update_status {
	IWX_MCC_RESP_NEW_CHAN_PROFILE,
	IWX_MCC_RESP_SAME_CHAN_PROFILE,
	IWX_MCC_RESP_INVALID,
	IWX_MCC_RESP_NVM_DISABLED,
	IWX_MCC_RESP_ILLEGAL,
	IWX_MCC_RESP_LOW_PRIORITY,
	IWX_MCC_RESP_TEST_MODE_ACTIVE,
	IWX_MCC_RESP_TEST_MODE_NOT_ACTIVE,
	IWX_MCC_RESP_TEST_MODE_DENIAL_OF_SERVICE,
};

#define IWX_MCC_SOURCE_OLD_FW			0
#define IWX_MCC_SOURCE_ME			1
#define IWX_MCC_SOURCE_BIOS			2
#define IWX_MCC_SOURCE_3G_LTE_HOST		3
#define IWX_MCC_SOURCE_3G_LTE_DEVICE		4
#define IWX_MCC_SOURCE_WIFI			5
#define IWX_MCC_SOURCE_RESERVED			6
#define IWX_MCC_SOURCE_DEFAULT			7
#define IWX_MCC_SOURCE_UNINITIALIZED		8
#define IWX_MCC_SOURCE_MCC_API			9
#define IWX_MCC_SOURCE_GET_CURRENT		0x10
#define IWX_MCC_SOURCE_GETTING_MCC_TEST_MODE	0x11

/*
 * From Linux commit ab02165ccec4c78162501acedeef1a768acdb811:
 *   As the firmware is slowly running out of command IDs and grouping of
 *   commands is desirable anyway, the firmware is extending the command
 *   header from 4 bytes to 8 bytes to introduce a group (in place of the
 *   former flags field, since that's always 0 on commands and thus can
 *   be easily used to distinguish between the two).
 *
 * These functions retrieve specific information from the id field in
 * the iwx_host_cmd struct which contains the command id, the group id,
 * and the version of the command.
*/
static inline uint8_t
iwx_cmd_opcode(uint32_t cmdid)
{
	return cmdid & 0xff;
}

static inline uint8_t
iwx_cmd_groupid(uint32_t cmdid)
{
	return ((cmdid & 0Xff00) >> 8);
}

static inline uint8_t
iwx_cmd_version(uint32_t cmdid)
{
	return ((cmdid & 0xff0000) >> 16);
}

static inline uint32_t
iwx_cmd_id(uint8_t opcode, uint8_t groupid, uint8_t version)
{
	return opcode + (groupid << 8) + (version << 16);
}

/* make uint16_t wide id out of uint8_t group and opcode */
#define IWX_WIDE_ID(grp, opcode) ((grp << 8) | opcode)

struct iwx_cmd_header {
	uint8_t code;
	uint8_t flags;
	uint8_t idx;
	uint8_t qid;
} __packed;

struct iwx_cmd_header_wide {
	uint8_t opcode;
	uint8_t group_id;
	uint8_t idx;
	uint8_t qid;
	uint16_t length;
	uint8_t reserved;
	uint8_t version;
} __packed;

#define IWX_POWER_SCHEME_CAM	1
#define IWX_POWER_SCHEME_BPS	2
#define IWX_POWER_SCHEME_LP	3

#define IWX_DEF_CMD_PAYLOAD_SIZE 320
#define IWX_MAX_CMD_PAYLOAD_SIZE (4096 - sizeof(struct iwx_cmd_header_wide))
#define IWX_CMD_FAILED_MSK 0x40

/**
 * struct iwx_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
 */
struct iwx_device_cmd {
	union {
		struct {
			struct iwx_cmd_header hdr;
			uint8_t data[IWX_DEF_CMD_PAYLOAD_SIZE];
		};
		struct {
			struct iwx_cmd_header_wide hdr_wide;
			uint8_t data_wide[IWX_DEF_CMD_PAYLOAD_SIZE -
					sizeof(struct iwx_cmd_header_wide) +
					sizeof(struct iwx_cmd_header)];
		};
	};
} __packed;

struct iwx_rx_packet {
	/*
	 * The first 4 bytes of the RX frame header contain both the RX frame
	 * size and some flags.
	 * Bit fields:
	 * 31:    flag flush RB request
	 * 30:    flag ignore TC (terminal counter) request
	 * 29:    flag fast IRQ request
	 * 28-26: Reserved
	 * 25:    Offload enabled
	 * 24:    RPF enabled
	 * 23:    RSS enabled
	 * 22:    Checksum enabled
	 * 21-16: RX queue
	 * 15-14: Reserved
	 * 13-00: RX frame size
	 */
	uint32_t len_n_flags;
	struct iwx_cmd_header hdr;
	uint8_t data[];
} __packed;

#define	IWX_FH_RSCSR_FRAME_SIZE_MSK	0x00003fff
#define	IWX_FH_RSCSR_FRAME_INVALID	0x55550000
#define	IWX_FH_RSCSR_FRAME_ALIGN	0x40
#define	IWX_FH_RSCSR_RPA_EN		(1 << 25)
#define	IWX_FH_RSCSR_RADA_EN		(1 << 26)
#define	IWX_FH_RSCSR_RXQ_POS		16
#define	IWX_FH_RSCSR_RXQ_MASK		0x3F0000

static uint32_t
iwx_rx_packet_len(const struct iwx_rx_packet *pkt)
{

	return le32toh(pkt->len_n_flags) & IWX_FH_RSCSR_FRAME_SIZE_MSK;
}

static uint32_t
iwx_rx_packet_payload_len(const struct iwx_rx_packet *pkt)
{

	return iwx_rx_packet_len(pkt) - sizeof(pkt->hdr);
}


#define IWX_MIN_DBM	-100
#define IWX_MAX_DBM	-33	/* realistic guess */

#define IWX_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define IWX_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define IWX_WRITE_1(sc, reg, val)					\
	bus_space_write_1((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define IWX_SETBITS(sc, reg, mask)					\
	IWX_WRITE(sc, reg, IWX_READ(sc, reg) | (mask))

#define IWX_CLRBITS(sc, reg, mask)					\
	IWX_WRITE(sc, reg, IWX_READ(sc, reg) & ~(mask))

#define IWX_BARRIER_WRITE(sc)						\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, (sc)->sc_sz,	\
	    BUS_SPACE_BARRIER_WRITE)

#define IWX_BARRIER_READ_WRITE(sc)					\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, (sc)->sc_sz,	\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)
