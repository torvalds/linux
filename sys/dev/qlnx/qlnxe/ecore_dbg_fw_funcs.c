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
 */

/*
 * File : ecore_dbg_fw_funcs.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "bcm_osal.h"
#include "ecore.h"
#include "ecore_hw.h"
#include "ecore_mcp.h"
#include "spad_layout.h"
#include "nvm_map.h"
#include "reg_addr.h"
#include "ecore_hsi_common.h"
#include "ecore_hsi_debug_tools.h"
#include "mcp_public.h"
#include "nvm_map.h"
#ifndef USE_DBG_BIN_FILE
#include "ecore_dbg_values.h"
#endif
#include "ecore_dbg_fw_funcs.h"

/* Memory groups enum */
enum mem_groups {
	MEM_GROUP_PXP_MEM,
	MEM_GROUP_DMAE_MEM,
	MEM_GROUP_CM_MEM,
	MEM_GROUP_QM_MEM,
	MEM_GROUP_DORQ_MEM,
	MEM_GROUP_BRB_RAM,
	MEM_GROUP_BRB_MEM,
	MEM_GROUP_PRS_MEM,
	MEM_GROUP_IOR,
	MEM_GROUP_BTB_RAM,
	MEM_GROUP_CONN_CFC_MEM,
	MEM_GROUP_TASK_CFC_MEM,
	MEM_GROUP_CAU_PI,
	MEM_GROUP_CAU_MEM,
	MEM_GROUP_PXP_ILT,
	MEM_GROUP_TM_MEM,
	MEM_GROUP_SDM_MEM,
	MEM_GROUP_PBUF,
	MEM_GROUP_RAM,
	MEM_GROUP_MULD_MEM,
	MEM_GROUP_BTB_MEM,
	MEM_GROUP_RDIF_CTX,
	MEM_GROUP_TDIF_CTX,
	MEM_GROUP_CFC_MEM,
	MEM_GROUP_IGU_MEM,
	MEM_GROUP_IGU_MSIX,
	MEM_GROUP_CAU_SB,
	MEM_GROUP_BMB_RAM,
	MEM_GROUP_BMB_MEM,
	MEM_GROUPS_NUM
};

/* Memory groups names */
static const char* s_mem_group_names[] = {
	"PXP_MEM",
	"DMAE_MEM",
	"CM_MEM",
	"QM_MEM",
	"DORQ_MEM",
	"BRB_RAM",
	"BRB_MEM",
	"PRS_MEM",
	"IOR",
	"BTB_RAM",
	"CONN_CFC_MEM",
	"TASK_CFC_MEM",
	"CAU_PI",
	"CAU_MEM",
	"PXP_ILT",
	"TM_MEM",
	"SDM_MEM",
	"PBUF",
	"RAM",
	"MULD_MEM",
	"BTB_MEM",
	"RDIF_CTX",
	"TDIF_CTX",
	"CFC_MEM",
	"IGU_MEM",
	"IGU_MSIX",
	"CAU_SB",
	"BMB_RAM",
	"BMB_MEM",
};

/* Idle check conditions */

#ifndef __PREVENT_COND_ARR__

static u32 cond5(const u32 *r, const u32 *imm) {
	return (((r[0] & imm[0]) != imm[1]) && ((r[1] & imm[2]) != imm[3]));
}

static u32 cond7(const u32 *r, const u32 *imm) {
	return (((r[0] >> imm[0]) & imm[1]) != imm[2]);
}

static u32 cond6(const u32 *r, const u32 *imm) {
	return ((r[0] & imm[0]) != imm[1]);
}

static u32 cond9(const u32 *r, const u32 *imm) {
	return ((r[0] & imm[0]) >> imm[1]) != (((r[0] & imm[2]) >> imm[3]) | ((r[1] & imm[4]) << imm[5]));
}

static u32 cond10(const u32 *r, const u32 *imm) {
	return ((r[0] & imm[0]) >> imm[1]) != (r[0] & imm[2]);
}

static u32 cond4(const u32 *r, const u32 *imm) {
	return ((r[0] & ~imm[0]) != imm[1]);
}

static u32 cond0(const u32 *r, const u32 *imm) {
	return ((r[0] & ~r[1]) != imm[0]);
}

static u32 cond1(const u32 *r, const u32 *imm) {
	return (r[0] != imm[0]);
}

static u32 cond11(const u32 *r, const u32 *imm) {
	return (r[0] != r[1] && r[2] == imm[0]);
}

static u32 cond12(const u32 *r, const u32 *imm) {
	return (r[0] != r[1] && r[2] > imm[0]);
}

static u32 cond3(const u32 *r, const u32 OSAL_UNUSED *imm) {
	return (r[0] != r[1]);
}

static u32 cond13(const u32 *r, const u32 *imm) {
	return (r[0] & imm[0]);
}

static u32 cond8(const u32 *r, const u32 *imm) {
	return (r[0] < (r[1] - imm[0]));
}

static u32 cond2(const u32 *r, const u32 *imm) {
	return (r[0] > imm[0]);
}

/* Array of Idle Check conditions */
static u32 (*cond_arr[])(const u32 *r, const u32 *imm) = {
	cond0,
	cond1,
	cond2,
	cond3,
	cond4,
	cond5,
	cond6,
	cond7,
	cond8,
	cond9,
	cond10,
	cond11,
	cond12,
	cond13,
};

#endif /* __PREVENT_COND_ARR__ */


/******************************* Data Types **********************************/

enum platform_ids {
	PLATFORM_ASIC,
	PLATFORM_EMUL_FULL,
	PLATFORM_EMUL_REDUCED,
	PLATFORM_FPGA,
	MAX_PLATFORM_IDS
};

struct chip_platform_defs {
	u8 num_ports;
	u8 num_pfs;
	u8 num_vfs;
};

/* Chip constant definitions */
struct chip_defs {
	const char *name;
	struct chip_platform_defs per_platform[MAX_PLATFORM_IDS];
};

/* Platform constant definitions */
struct platform_defs {
	const char *name;
	u32 delay_factor;
	u32 dmae_thresh;
	u32 log_thresh;
};

/* Storm constant definitions.
 * Addresses are in bytes, sizes are in quad-regs.
 */
struct storm_defs {
	char letter;
	enum block_id block_id;
	enum dbg_bus_clients dbg_client_id[MAX_CHIP_IDS];
	bool has_vfc;
	u32 sem_fast_mem_addr;
	u32 sem_frame_mode_addr;
	u32 sem_slow_enable_addr;
	u32 sem_slow_mode_addr;
	u32 sem_slow_mode1_conf_addr;
	u32 sem_sync_dbg_empty_addr;
	u32 sem_slow_dbg_empty_addr;
	u32 cm_ctx_wr_addr;
	u32 cm_conn_ag_ctx_lid_size;
	u32 cm_conn_ag_ctx_rd_addr;
	u32 cm_conn_st_ctx_lid_size;
	u32 cm_conn_st_ctx_rd_addr;
	u32 cm_task_ag_ctx_lid_size;
	u32 cm_task_ag_ctx_rd_addr;
	u32 cm_task_st_ctx_lid_size;
	u32 cm_task_st_ctx_rd_addr;
};

/* Block constant definitions */
struct block_defs {
	const char *name;
	bool exists[MAX_CHIP_IDS];
	bool associated_to_storm;

	/* Valid only if associated_to_storm is true */
	u32 storm_id;
	enum dbg_bus_clients dbg_client_id[MAX_CHIP_IDS];
	u32 dbg_select_addr;
	u32 dbg_enable_addr;
	u32 dbg_shift_addr;
	u32 dbg_force_valid_addr;
	u32 dbg_force_frame_addr;
	bool has_reset_bit;

	/* If true, block is taken out of reset before dump */
	bool unreset;
	enum dbg_reset_regs reset_reg;

	/* Bit offset in reset register */
	u8 reset_bit_offset;
};

/* Reset register definitions */
struct reset_reg_defs {
	u32 addr;
	bool exists[MAX_CHIP_IDS];
	u32 unreset_val[MAX_CHIP_IDS];
};

/* Debug Bus Constraint operation constant definitions */
struct dbg_bus_constraint_op_defs {
	u8 hw_op_val;
	bool is_cyclic;
};

/* Storm Mode definitions */
struct storm_mode_defs {
	const char *name;
	bool is_fast_dbg;
	u8 id_in_hw;
};

struct grc_param_defs {
	u32 default_val[MAX_CHIP_IDS];
	u32 min;
	u32 max;
	bool is_preset;
	u32 exclude_all_preset_val;
	u32 crash_preset_val;
};

/* address is in 128b units. Width is in bits. */
struct rss_mem_defs {
	const char *mem_name;
	const char *type_name;
	u32 addr;
	u32 entry_width;
	u32 num_entries[MAX_CHIP_IDS];
};

struct vfc_ram_defs {
	const char *mem_name;
	const char *type_name;
	u32 base_row;
	u32 num_rows;
};

struct big_ram_defs {
	const char *instance_name;
	enum mem_groups mem_group_id;
	enum mem_groups ram_mem_group_id;
	enum dbg_grc_params grc_param;
	u32 addr_reg_addr;
	u32 data_reg_addr;
	u32 is_256b_reg_addr;
	u32 is_256b_bit_offset[MAX_CHIP_IDS];
	u32 ram_size[MAX_CHIP_IDS]; /* In dwords */
};

struct phy_defs {
	const char *phy_name;

	/* PHY base GRC address */
	u32 base_addr;

	/* Relative address of indirect TBUS address register (bits 0..7) */
	u32 tbus_addr_lo_addr;

	/* Relative address of indirect TBUS address register (bits 8..10) */
	u32 tbus_addr_hi_addr;

	/* Relative address of indirect TBUS data register (bits 0..7) */
	u32 tbus_data_lo_addr;

	/* Relative address of indirect TBUS data register (bits 8..11) */
	u32 tbus_data_hi_addr;
};

/******************************** Constants **********************************/

#define MAX_LCIDS			320
#define MAX_LTIDS			320

#define NUM_IOR_SETS			2
#define IORS_PER_SET			176
#define IOR_SET_OFFSET(set_id)		((set_id) * 256)

#define BYTES_IN_DWORD			sizeof(u32)

/* Cyclic  right */
#define SHR(val, val_width, amount)	(((val) | ((val) << (val_width))) 					>> (amount)) & ((1 << (val_width)) - 1)

/* In the macros below, size and offset are specified in bits */
#define CEIL_DWORDS(size)		DIV_ROUND_UP(size, 32)
#define FIELD_BIT_OFFSET(type, field)	type##_##field##_##OFFSET
#define FIELD_BIT_SIZE(type, field)	type##_##field##_##SIZE
#define FIELD_DWORD_OFFSET(type, field)		(int)(FIELD_BIT_OFFSET(type, field) / 32)
#define FIELD_DWORD_SHIFT(type, field)	(FIELD_BIT_OFFSET(type, field) % 32)
#define FIELD_BIT_MASK(type, field)		(((1 << FIELD_BIT_SIZE(type, field)) - 1) 	<< FIELD_DWORD_SHIFT(type, field))

#define SET_VAR_FIELD(var, type, field, val) 	var[FIELD_DWORD_OFFSET(type, field)] &= 		(~FIELD_BIT_MASK(type, field)); 	var[FIELD_DWORD_OFFSET(type, field)] |= 		(val) << FIELD_DWORD_SHIFT(type, field)

#define ARR_REG_WR(dev, ptt, addr, arr, arr_size) 	for (i = 0; i < (arr_size); i++) 		ecore_wr(dev, ptt, addr, (arr)[i])

#define ARR_REG_RD(dev, ptt, addr, arr, arr_size) 	for (i = 0; i < (arr_size); i++) 		(arr)[i] = ecore_rd(dev, ptt, addr)

#define CHECK_ARR_SIZE(arr, size) 	OSAL_BUILD_BUG_ON(!(OSAL_ARRAY_SIZE(arr) == size))

#ifndef DWORDS_TO_BYTES
#define DWORDS_TO_BYTES(dwords)		((dwords) * BYTES_IN_DWORD)
#endif
#ifndef BYTES_TO_DWORDS
#define BYTES_TO_DWORDS(bytes)		((bytes) / BYTES_IN_DWORD)
#endif

/* extra lines include a signature line + optional latency events line */
#ifndef NUM_DBG_LINES
#define NUM_EXTRA_DBG_LINES(block_desc)		(1 + (block_desc->has_latency_events ? 1 : 0))
#define NUM_DBG_LINES(block_desc)		(block_desc->num_of_lines + NUM_EXTRA_DBG_LINES(block_desc))
#endif

#define USE_DMAE				true
#define PROTECT_WIDE_BUS		true

#define RAM_LINES_TO_DWORDS(lines)	((lines) * 2)
#define RAM_LINES_TO_BYTES(lines)		DWORDS_TO_BYTES(RAM_LINES_TO_DWORDS(lines))

#define REG_DUMP_LEN_SHIFT		24
#define MEM_DUMP_ENTRY_SIZE_DWORDS		BYTES_TO_DWORDS(sizeof(struct dbg_dump_mem))

#define IDLE_CHK_RULE_SIZE_DWORDS		BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_rule))

#define IDLE_CHK_RESULT_HDR_DWORDS		BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_result_hdr))

#define IDLE_CHK_RESULT_REG_HDR_DWORDS		BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_result_reg_hdr))

#define IDLE_CHK_MAX_ENTRIES_SIZE	32

/* The sizes and offsets below are specified in bits */
#define VFC_CAM_CMD_STRUCT_SIZE		64
#define VFC_CAM_CMD_ROW_OFFSET		48
#define VFC_CAM_CMD_ROW_SIZE		9
#define VFC_CAM_ADDR_STRUCT_SIZE	16
#define VFC_CAM_ADDR_OP_OFFSET		0
#define VFC_CAM_ADDR_OP_SIZE		4
#define VFC_CAM_RESP_STRUCT_SIZE	256
#define VFC_RAM_ADDR_STRUCT_SIZE	16
#define VFC_RAM_ADDR_OP_OFFSET		0
#define VFC_RAM_ADDR_OP_SIZE		2
#define VFC_RAM_ADDR_ROW_OFFSET		2
#define VFC_RAM_ADDR_ROW_SIZE		10
#define VFC_RAM_RESP_STRUCT_SIZE	256

#define VFC_CAM_CMD_DWORDS		CEIL_DWORDS(VFC_CAM_CMD_STRUCT_SIZE)
#define VFC_CAM_ADDR_DWORDS		CEIL_DWORDS(VFC_CAM_ADDR_STRUCT_SIZE)
#define VFC_CAM_RESP_DWORDS		CEIL_DWORDS(VFC_CAM_RESP_STRUCT_SIZE)
#define VFC_RAM_CMD_DWORDS		VFC_CAM_CMD_DWORDS
#define VFC_RAM_ADDR_DWORDS		CEIL_DWORDS(VFC_RAM_ADDR_STRUCT_SIZE)
#define VFC_RAM_RESP_DWORDS		CEIL_DWORDS(VFC_RAM_RESP_STRUCT_SIZE)

#define NUM_VFC_RAM_TYPES		4

#define VFC_CAM_NUM_ROWS		512

#define VFC_OPCODE_CAM_RD		14
#define VFC_OPCODE_RAM_RD		0

#define NUM_RSS_MEM_TYPES		5

#define NUM_BIG_RAM_TYPES		3

#define NUM_PHY_TBUS_ADDRESSES		2048
#define PHY_DUMP_SIZE_DWORDS		(NUM_PHY_TBUS_ADDRESSES / 2)

#define SEM_FAST_MODE23_SRC_ENABLE_VAL	0x0
#define SEM_FAST_MODE23_SRC_DISABLE_VAL	0x7
#define SEM_FAST_MODE4_SRC_ENABLE_VAL	0x0
#define SEM_FAST_MODE4_SRC_DISABLE_VAL	0x3
#define SEM_FAST_MODE6_SRC_ENABLE_VAL	0x10
#define SEM_FAST_MODE6_SRC_DISABLE_VAL	0x3f

#define SEM_SLOW_MODE1_DATA_ENABLE	0x1

#define VALUES_PER_CYCLE		4
#define MAX_CYCLE_VALUES_MASK		((1 << VALUES_PER_CYCLE) - 1)

#define MAX_DWORDS_PER_CYCLE		8

#define HW_ID_BITS			3

#define NUM_CALENDAR_SLOTS		16

#define MAX_TRIGGER_STATES		3
#define TRIGGER_SETS_PER_STATE		2
#define MAX_CONSTRAINTS			4

#define SEM_FILTER_CID_EN_MASK		0x00b
#define SEM_FILTER_EID_MASK_EN_MASK	0x013
#define SEM_FILTER_EID_RANGE_EN_MASK	0x113

#define CHUNK_SIZE_IN_DWORDS		64
#define CHUNK_SIZE_IN_BYTES		DWORDS_TO_BYTES(CHUNK_SIZE_IN_DWORDS)

#define INT_BUF_NUM_OF_LINES		192
#define INT_BUF_LINE_SIZE_IN_DWORDS	16
#define INT_BUF_SIZE_IN_DWORDS			(INT_BUF_NUM_OF_LINES * INT_BUF_LINE_SIZE_IN_DWORDS)
#define INT_BUF_SIZE_IN_CHUNKS			(INT_BUF_SIZE_IN_DWORDS / CHUNK_SIZE_IN_DWORDS)

#define PCI_BUF_LINE_SIZE_IN_DWORDS	8
#define PCI_BUF_LINE_SIZE_IN_BYTES		DWORDS_TO_BYTES(PCI_BUF_LINE_SIZE_IN_DWORDS)

#define TARGET_EN_MASK_PCI		0x3
#define TARGET_EN_MASK_NIG		0x4

#define PCI_REQ_CREDIT			1
#define PCI_PHYS_ADDR_TYPE		0

#define OPAQUE_FID(pci_func)		((pci_func << 4) | 0xff00)

#define RESET_REG_UNRESET_OFFSET	4

#define PCI_PKT_SIZE_IN_CHUNKS		1
#define PCI_PKT_SIZE_IN_BYTES			(PCI_PKT_SIZE_IN_CHUNKS * CHUNK_SIZE_IN_BYTES)

#define NIG_PKT_SIZE_IN_CHUNKS		4

#define FLUSH_DELAY_MS			500
#define STALL_DELAY_MS			500

#define SRC_MAC_ADDR_LO16		0x0a0b
#define SRC_MAC_ADDR_HI32		0x0c0d0e0f
#define ETH_TYPE			0x1000

#define STATIC_DEBUG_LINE_DWORDS	9

#define NUM_COMMON_GLOBAL_PARAMS	8

#define FW_IMG_KUKU			0
#define FW_IMG_MAIN			1
#define FW_IMG_L2B			2

#ifndef REG_FIFO_ELEMENT_DWORDS
#define REG_FIFO_ELEMENT_DWORDS		2
#endif
#define REG_FIFO_DEPTH_ELEMENTS		32
#define REG_FIFO_DEPTH_DWORDS			(REG_FIFO_ELEMENT_DWORDS * REG_FIFO_DEPTH_ELEMENTS)

#ifndef IGU_FIFO_ELEMENT_DWORDS
#define IGU_FIFO_ELEMENT_DWORDS		4
#endif
#define IGU_FIFO_DEPTH_ELEMENTS		64
#define IGU_FIFO_DEPTH_DWORDS			(IGU_FIFO_ELEMENT_DWORDS * IGU_FIFO_DEPTH_ELEMENTS)

#define SEMI_SYNC_FIFO_POLLING_DELAY_MS	5
#define SEMI_SYNC_FIFO_POLLING_COUNT	20

#ifndef PROTECTION_OVERRIDE_ELEMENT_DWORDS
#define PROTECTION_OVERRIDE_ELEMENT_DWORDS 2
#endif
#define PROTECTION_OVERRIDE_DEPTH_ELEMENTS 20
#define PROTECTION_OVERRIDE_DEPTH_DWORDS   	(PROTECTION_OVERRIDE_DEPTH_ELEMENTS 	* PROTECTION_OVERRIDE_ELEMENT_DWORDS)

#define MCP_SPAD_TRACE_OFFSIZE_ADDR		(MCP_REG_SCRATCH + 	OFFSETOF(struct static_init, sections[SPAD_SECTION_TRACE]))

#define EMPTY_FW_VERSION_STR		"???_???_???_???"
#define EMPTY_FW_IMAGE_STR		"???????????????"


/***************************** Constant Arrays *******************************/

struct dbg_array {
	const u32 *ptr;
	u32 size_in_dwords;
};

/* Debug arrays */
#ifdef USE_DBG_BIN_FILE
static struct dbg_array s_dbg_arrays[MAX_BIN_DBG_BUFFER_TYPE] = { { OSAL_NULL } };
#else
static struct dbg_array s_dbg_arrays[MAX_BIN_DBG_BUFFER_TYPE] = {

	/* BIN_BUF_DBG_MODE_TREE */
	{ (const u32 *)dbg_modes_tree_buf, OSAL_ARRAY_SIZE(dbg_modes_tree_buf)},

	/* BIN_BUF_DBG_DUMP_REG */
	{ dump_reg, OSAL_ARRAY_SIZE(dump_reg) },

	/* BIN_BUF_DBG_DUMP_MEM */
	{ dump_mem, OSAL_ARRAY_SIZE(dump_mem) },

	/* BIN_BUF_DBG_IDLE_CHK_REGS */
	{ idle_chk_regs, OSAL_ARRAY_SIZE(idle_chk_regs) },

	/* BIN_BUF_DBG_IDLE_CHK_IMMS */
	{ idle_chk_imms, OSAL_ARRAY_SIZE(idle_chk_imms) },

	/* BIN_BUF_DBG_IDLE_CHK_RULES */
	{ idle_chk_rules, OSAL_ARRAY_SIZE(idle_chk_rules) },

	/* BIN_BUF_DBG_IDLE_CHK_PARSING_DATA */
	{ OSAL_NULL, 0 },

	/* BIN_BUF_DBG_ATTN_BLOCKS */
	{ attn_block, OSAL_ARRAY_SIZE(attn_block) },

	/* BIN_BUF_DBG_ATTN_REGSS */
	{ attn_reg, OSAL_ARRAY_SIZE(attn_reg) },

	/* BIN_BUF_DBG_ATTN_INDEXES */
	{ OSAL_NULL, 0 },

	/* BIN_BUF_DBG_ATTN_NAME_OFFSETS */
	{ OSAL_NULL, 0 },

	/* BIN_BUF_DBG_BUS_BLOCKS */
	{ dbg_bus_blocks, OSAL_ARRAY_SIZE(dbg_bus_blocks) },

	/* BIN_BUF_DBG_BUS_LINES */
	{ dbg_bus_lines, OSAL_ARRAY_SIZE(dbg_bus_lines) },

	/* BIN_BUF_DBG_BUS_BLOCKS_USER_DATA */
	{ OSAL_NULL, 0 },

	/* BIN_BUF_DBG_BUS_LINE_NAME_OFFSETS */
	{ OSAL_NULL, 0 },

	/* BIN_BUF_DBG_PARSING_STRINGS */
	{ OSAL_NULL, 0 }
};
#endif

/* Chip constant definitions array */
static struct chip_defs s_chip_defs[MAX_CHIP_IDS] = {
	{ "bb",

		/* ASIC */
		{ { MAX_NUM_PORTS_BB, MAX_NUM_PFS_BB, MAX_NUM_VFS_BB },

		/* EMUL_FULL */
		{ MAX_NUM_PORTS_BB, MAX_NUM_PFS_BB, MAX_NUM_VFS_BB },

		/* EMUL_REDUCED */
		{ MAX_NUM_PORTS_BB, MAX_NUM_PFS_BB, MAX_NUM_VFS_BB },

		/* FPGA */
		{ MAX_NUM_PORTS_BB, MAX_NUM_PFS_BB, MAX_NUM_VFS_BB } } },

	{ "ah",

		/* ASIC */
		{ { MAX_NUM_PORTS_K2, MAX_NUM_PFS_K2, MAX_NUM_VFS_K2 },

		/* EMUL_FULL */
		{ MAX_NUM_PORTS_K2, MAX_NUM_PFS_K2, MAX_NUM_VFS_K2 },

		/* EMUL_REDUCED */
		{ MAX_NUM_PORTS_K2, MAX_NUM_PFS_K2, MAX_NUM_VFS_K2 },

		/* FPGA */
		{ MAX_NUM_PORTS_K2, 8, MAX_NUM_VFS_K2 } } },

	{ "e5",

		/* ASIC */
		{ { MAX_NUM_PORTS_E5, MAX_NUM_PFS_E5, MAX_NUM_VFS_E5 },

		/* EMUL_FULL */
		{ MAX_NUM_PORTS_E5, MAX_NUM_PFS_E5, MAX_NUM_VFS_E5 },

		/* EMUL_REDUCED */
		{ MAX_NUM_PORTS_E5, MAX_NUM_PFS_E5, MAX_NUM_VFS_E5 },

		/* FPGA */
		{ MAX_NUM_PORTS_E5, 8, MAX_NUM_VFS_E5 } } }
};

/* Storm constant definitions array */
static struct storm_defs s_storm_defs[] = {

	/* Tstorm */
	{	'T', BLOCK_TSEM,
		{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT }, true,
		TSEM_REG_FAST_MEMORY,
		TSEM_REG_DBG_FRAME_MODE_BB_K2, TSEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		TSEM_REG_SLOW_DBG_MODE_BB_K2, TSEM_REG_DBG_MODE1_CFG_BB_K2,
		TSEM_REG_SYNC_DBG_EMPTY, TSEM_REG_SLOW_DBG_EMPTY_BB_K2,
		TCM_REG_CTX_RBC_ACCS,
		4, TCM_REG_AGG_CON_CTX,
		16, TCM_REG_SM_CON_CTX,
		2, TCM_REG_AGG_TASK_CTX,
		4, TCM_REG_SM_TASK_CTX },

	/* Mstorm */
	{	'M', BLOCK_MSEM,
		{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM }, false,
		MSEM_REG_FAST_MEMORY,
		MSEM_REG_DBG_FRAME_MODE_BB_K2, MSEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		MSEM_REG_SLOW_DBG_MODE_BB_K2, MSEM_REG_DBG_MODE1_CFG_BB_K2,
		MSEM_REG_SYNC_DBG_EMPTY, MSEM_REG_SLOW_DBG_EMPTY_BB_K2,
		MCM_REG_CTX_RBC_ACCS,
		1, MCM_REG_AGG_CON_CTX,
		10, MCM_REG_SM_CON_CTX,
		2, MCM_REG_AGG_TASK_CTX,
		7, MCM_REG_SM_TASK_CTX },

	/* Ustorm */
	{	'U', BLOCK_USEM,
		{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU }, false,
		USEM_REG_FAST_MEMORY,
		USEM_REG_DBG_FRAME_MODE_BB_K2, USEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		USEM_REG_SLOW_DBG_MODE_BB_K2, USEM_REG_DBG_MODE1_CFG_BB_K2,
		USEM_REG_SYNC_DBG_EMPTY, USEM_REG_SLOW_DBG_EMPTY_BB_K2,
		UCM_REG_CTX_RBC_ACCS,
		2, UCM_REG_AGG_CON_CTX,
		13, UCM_REG_SM_CON_CTX,
		3, UCM_REG_AGG_TASK_CTX,
		3, UCM_REG_SM_TASK_CTX },

	/* Xstorm */
	{	'X', BLOCK_XSEM,
		{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX }, false,
		XSEM_REG_FAST_MEMORY,
		XSEM_REG_DBG_FRAME_MODE_BB_K2, XSEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		XSEM_REG_SLOW_DBG_MODE_BB_K2, XSEM_REG_DBG_MODE1_CFG_BB_K2,
		XSEM_REG_SYNC_DBG_EMPTY, XSEM_REG_SLOW_DBG_EMPTY_BB_K2,
		XCM_REG_CTX_RBC_ACCS,
		9, XCM_REG_AGG_CON_CTX,
		15, XCM_REG_SM_CON_CTX,
		0, 0,
		0, 0 },

	/* Ystorm */
	{	'Y', BLOCK_YSEM,
		{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY, DBG_BUS_CLIENT_RBCY }, false,
		YSEM_REG_FAST_MEMORY,
		YSEM_REG_DBG_FRAME_MODE_BB_K2, YSEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		YSEM_REG_SLOW_DBG_MODE_BB_K2, YSEM_REG_DBG_MODE1_CFG_BB_K2,
		YSEM_REG_SYNC_DBG_EMPTY, TSEM_REG_SLOW_DBG_EMPTY_BB_K2,
		YCM_REG_CTX_RBC_ACCS,
		2, YCM_REG_AGG_CON_CTX,
		3, YCM_REG_SM_CON_CTX,
		2, YCM_REG_AGG_TASK_CTX,
		12, YCM_REG_SM_TASK_CTX },

	/* Pstorm */
	{	'P', BLOCK_PSEM,
		{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS }, true,
		PSEM_REG_FAST_MEMORY,
		PSEM_REG_DBG_FRAME_MODE_BB_K2, PSEM_REG_SLOW_DBG_ACTIVE_BB_K2,
		PSEM_REG_SLOW_DBG_MODE_BB_K2, PSEM_REG_DBG_MODE1_CFG_BB_K2,
		PSEM_REG_SYNC_DBG_EMPTY, PSEM_REG_SLOW_DBG_EMPTY_BB_K2,
		PCM_REG_CTX_RBC_ACCS,
		0, 0,
		10, PCM_REG_SM_CON_CTX,
		0, 0,
		0, 0 }
};

/* Block definitions array */

static struct block_defs block_grc_defs = {
	"grc", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN },
	GRC_REG_DBG_SELECT, GRC_REG_DBG_DWORD_ENABLE,
	GRC_REG_DBG_SHIFT, GRC_REG_DBG_FORCE_VALID,
	GRC_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_UA, 1 };

static struct block_defs block_miscs_defs = {
	"miscs", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_misc_defs = {
	"misc", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_dbu_defs = {
	"dbu", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_pglue_b_defs = {
	"pglue_b", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH },
	PGLUE_B_REG_DBG_SELECT, PGLUE_B_REG_DBG_DWORD_ENABLE,
	PGLUE_B_REG_DBG_SHIFT, PGLUE_B_REG_DBG_FORCE_VALID,
	PGLUE_B_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 1 };

static struct block_defs block_cnig_defs = {
	"cnig", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCW, DBG_BUS_CLIENT_RBCW },
	CNIG_REG_DBG_SELECT_K2_E5, CNIG_REG_DBG_DWORD_ENABLE_K2_E5,
	CNIG_REG_DBG_SHIFT_K2_E5, CNIG_REG_DBG_FORCE_VALID_K2_E5,
	CNIG_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 0 };

static struct block_defs block_cpmu_defs = {
	"cpmu", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 8 };

static struct block_defs block_ncsi_defs = {
	"ncsi", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ },
	NCSI_REG_DBG_SELECT, NCSI_REG_DBG_DWORD_ENABLE,
	NCSI_REG_DBG_SHIFT, NCSI_REG_DBG_FORCE_VALID,
	NCSI_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 5 };

static struct block_defs block_opte_defs = {
	"opte", { true, true, false }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 4 };

static struct block_defs block_bmb_defs = {
	"bmb", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCB, DBG_BUS_CLIENT_RBCB },
	BMB_REG_DBG_SELECT, BMB_REG_DBG_DWORD_ENABLE,
	BMB_REG_DBG_SHIFT, BMB_REG_DBG_FORCE_VALID,
	BMB_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_UA, 7 };

static struct block_defs block_pcie_defs = {
	"pcie", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH },
	PCIE_REG_DBG_COMMON_SELECT_K2_E5, PCIE_REG_DBG_COMMON_DWORD_ENABLE_K2_E5,
	PCIE_REG_DBG_COMMON_SHIFT_K2_E5, PCIE_REG_DBG_COMMON_FORCE_VALID_K2_E5,
	PCIE_REG_DBG_COMMON_FORCE_FRAME_K2_E5,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_mcp_defs = {
	"mcp", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_mcp2_defs = {
	"mcp2", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ },
	MCP2_REG_DBG_SELECT, MCP2_REG_DBG_DWORD_ENABLE,
	MCP2_REG_DBG_SHIFT, MCP2_REG_DBG_FORCE_VALID,
	MCP2_REG_DBG_FORCE_FRAME,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_pswhst_defs = {
	"pswhst", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWHST_REG_DBG_SELECT, PSWHST_REG_DBG_DWORD_ENABLE,
	PSWHST_REG_DBG_SHIFT, PSWHST_REG_DBG_FORCE_VALID,
	PSWHST_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 0 };

static struct block_defs block_pswhst2_defs = {
	"pswhst2", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWHST2_REG_DBG_SELECT, PSWHST2_REG_DBG_DWORD_ENABLE,
	PSWHST2_REG_DBG_SHIFT, PSWHST2_REG_DBG_FORCE_VALID,
	PSWHST2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 0 };

static struct block_defs block_pswrd_defs = {
	"pswrd", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWRD_REG_DBG_SELECT, PSWRD_REG_DBG_DWORD_ENABLE,
	PSWRD_REG_DBG_SHIFT, PSWRD_REG_DBG_FORCE_VALID,
	PSWRD_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 2 };

static struct block_defs block_pswrd2_defs = {
	"pswrd2", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWRD2_REG_DBG_SELECT, PSWRD2_REG_DBG_DWORD_ENABLE,
	PSWRD2_REG_DBG_SHIFT,	PSWRD2_REG_DBG_FORCE_VALID,
	PSWRD2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 2 };

static struct block_defs block_pswwr_defs = {
	"pswwr", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWWR_REG_DBG_SELECT, PSWWR_REG_DBG_DWORD_ENABLE,
	PSWWR_REG_DBG_SHIFT, PSWWR_REG_DBG_FORCE_VALID,
	PSWWR_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 3 };

static struct block_defs block_pswwr2_defs = {
	"pswwr2", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISC_PL_HV, 3 };

static struct block_defs block_pswrq_defs = {
	"pswrq", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWRQ_REG_DBG_SELECT, PSWRQ_REG_DBG_DWORD_ENABLE,
	PSWRQ_REG_DBG_SHIFT, PSWRQ_REG_DBG_FORCE_VALID,
	PSWRQ_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 1 };

static struct block_defs block_pswrq2_defs = {
	"pswrq2", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PSWRQ2_REG_DBG_SELECT, PSWRQ2_REG_DBG_DWORD_ENABLE,
	PSWRQ2_REG_DBG_SHIFT, PSWRQ2_REG_DBG_FORCE_VALID,
	PSWRQ2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 1 };

static struct block_defs block_pglcs_defs =	{
	"pglcs", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH },
	PGLCS_REG_DBG_SELECT_K2_E5, PGLCS_REG_DBG_DWORD_ENABLE_K2_E5,
	PGLCS_REG_DBG_SHIFT_K2_E5, PGLCS_REG_DBG_FORCE_VALID_K2_E5,
	PGLCS_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 2 };

static struct block_defs block_ptu_defs ={
	"ptu", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	PTU_REG_DBG_SELECT, PTU_REG_DBG_DWORD_ENABLE,
	PTU_REG_DBG_SHIFT, PTU_REG_DBG_FORCE_VALID,
	PTU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 20 };

static struct block_defs block_dmae_defs = {
	"dmae", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	DMAE_REG_DBG_SELECT, DMAE_REG_DBG_DWORD_ENABLE,
	DMAE_REG_DBG_SHIFT, DMAE_REG_DBG_FORCE_VALID,
	DMAE_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 28 };

static struct block_defs block_tcm_defs = {
	"tcm", { true, true, true }, true, DBG_TSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT },
	TCM_REG_DBG_SELECT, TCM_REG_DBG_DWORD_ENABLE,
	TCM_REG_DBG_SHIFT, TCM_REG_DBG_FORCE_VALID,
	TCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 5 };

static struct block_defs block_mcm_defs = {
	"mcm", { true, true, true }, true, DBG_MSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	MCM_REG_DBG_SELECT, MCM_REG_DBG_DWORD_ENABLE,
	MCM_REG_DBG_SHIFT, MCM_REG_DBG_FORCE_VALID,
	MCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 3 };

static struct block_defs block_ucm_defs = {
	"ucm", { true, true, true }, true, DBG_USTORM_ID,
	{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU },
	UCM_REG_DBG_SELECT, UCM_REG_DBG_DWORD_ENABLE,
	UCM_REG_DBG_SHIFT, UCM_REG_DBG_FORCE_VALID,
	UCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 8 };

static struct block_defs block_xcm_defs = {
	"xcm", { true, true, true }, true, DBG_XSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX },
	XCM_REG_DBG_SELECT, XCM_REG_DBG_DWORD_ENABLE,
	XCM_REG_DBG_SHIFT, XCM_REG_DBG_FORCE_VALID,
	XCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 19 };

static struct block_defs block_ycm_defs = {
	"ycm", { true, true, true }, true, DBG_YSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY, DBG_BUS_CLIENT_RBCY },
	YCM_REG_DBG_SELECT, YCM_REG_DBG_DWORD_ENABLE,
	YCM_REG_DBG_SHIFT, YCM_REG_DBG_FORCE_VALID,
	YCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 5 };

static struct block_defs block_pcm_defs = {
	"pcm", { true, true, true }, true, DBG_PSTORM_ID,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS },
	PCM_REG_DBG_SELECT, PCM_REG_DBG_DWORD_ENABLE,
	PCM_REG_DBG_SHIFT, PCM_REG_DBG_FORCE_VALID,
	PCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 4 };

static struct block_defs block_qm_defs = {
	"qm", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCQ, DBG_BUS_CLIENT_RBCQ },
	QM_REG_DBG_SELECT, QM_REG_DBG_DWORD_ENABLE,
	QM_REG_DBG_SHIFT, QM_REG_DBG_FORCE_VALID,
	QM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 16 };

static struct block_defs block_tm_defs = {
	"tm", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS },
	TM_REG_DBG_SELECT, TM_REG_DBG_DWORD_ENABLE,
	TM_REG_DBG_SHIFT, TM_REG_DBG_FORCE_VALID,
	TM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 17 };

static struct block_defs block_dorq_defs = {
	"dorq", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY, DBG_BUS_CLIENT_RBCY },
	DORQ_REG_DBG_SELECT, DORQ_REG_DBG_DWORD_ENABLE,
	DORQ_REG_DBG_SHIFT, DORQ_REG_DBG_FORCE_VALID,
	DORQ_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 18 };

static struct block_defs block_brb_defs = {
	"brb", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR },
	BRB_REG_DBG_SELECT, BRB_REG_DBG_DWORD_ENABLE,
	BRB_REG_DBG_SHIFT, BRB_REG_DBG_FORCE_VALID,
	BRB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 0 };

static struct block_defs block_src_defs = {
	"src", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF },
	SRC_REG_DBG_SELECT, SRC_REG_DBG_DWORD_ENABLE,
	SRC_REG_DBG_SHIFT, SRC_REG_DBG_FORCE_VALID,
	SRC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 2 };

static struct block_defs block_prs_defs = {
	"prs", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR },
	PRS_REG_DBG_SELECT, PRS_REG_DBG_DWORD_ENABLE,
	PRS_REG_DBG_SHIFT, PRS_REG_DBG_FORCE_VALID,
	PRS_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 1 };

static struct block_defs block_tsdm_defs = {
	"tsdm", { true, true, true }, true, DBG_TSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT },
	TSDM_REG_DBG_SELECT, TSDM_REG_DBG_DWORD_ENABLE,
	TSDM_REG_DBG_SHIFT, TSDM_REG_DBG_FORCE_VALID,
	TSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 3 };

static struct block_defs block_msdm_defs = {
	"msdm", { true, true, true }, true, DBG_MSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	MSDM_REG_DBG_SELECT, MSDM_REG_DBG_DWORD_ENABLE,
	MSDM_REG_DBG_SHIFT, MSDM_REG_DBG_FORCE_VALID,
	MSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 6 };

static struct block_defs block_usdm_defs = {
	"usdm", { true, true, true }, true, DBG_USTORM_ID,
	{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU },
	USDM_REG_DBG_SELECT, USDM_REG_DBG_DWORD_ENABLE,
	USDM_REG_DBG_SHIFT, USDM_REG_DBG_FORCE_VALID,
	USDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 7
	};
static struct block_defs block_xsdm_defs = {
	"xsdm", { true, true, true }, true, DBG_XSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX },
	XSDM_REG_DBG_SELECT, XSDM_REG_DBG_DWORD_ENABLE,
	XSDM_REG_DBG_SHIFT, XSDM_REG_DBG_FORCE_VALID,
	XSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 20 };

static struct block_defs block_ysdm_defs = {
	"ysdm", { true, true, true }, true, DBG_YSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY, DBG_BUS_CLIENT_RBCY },
	YSDM_REG_DBG_SELECT, YSDM_REG_DBG_DWORD_ENABLE,
	YSDM_REG_DBG_SHIFT, YSDM_REG_DBG_FORCE_VALID,
	YSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 8 };

static struct block_defs block_psdm_defs = {
	"psdm", { true, true, true }, true, DBG_PSTORM_ID,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS },
	PSDM_REG_DBG_SELECT, PSDM_REG_DBG_DWORD_ENABLE,
	PSDM_REG_DBG_SHIFT, PSDM_REG_DBG_FORCE_VALID,
	PSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 7 };

static struct block_defs block_tsem_defs = {
	"tsem", { true, true, true }, true, DBG_TSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT },
	TSEM_REG_DBG_SELECT, TSEM_REG_DBG_DWORD_ENABLE,
	TSEM_REG_DBG_SHIFT, TSEM_REG_DBG_FORCE_VALID,
	TSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 4 };

static struct block_defs block_msem_defs = {
	"msem", { true, true, true }, true, DBG_MSTORM_ID,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	MSEM_REG_DBG_SELECT, MSEM_REG_DBG_DWORD_ENABLE,
	MSEM_REG_DBG_SHIFT, MSEM_REG_DBG_FORCE_VALID,
	MSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 9 };

static struct block_defs block_usem_defs = {
	"usem", { true, true, true }, true, DBG_USTORM_ID,
	{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU },
	USEM_REG_DBG_SELECT, USEM_REG_DBG_DWORD_ENABLE,
	USEM_REG_DBG_SHIFT, USEM_REG_DBG_FORCE_VALID,
	USEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 9 };

static struct block_defs block_xsem_defs = {
	"xsem", { true, true, true }, true, DBG_XSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX },
	XSEM_REG_DBG_SELECT, XSEM_REG_DBG_DWORD_ENABLE,
	XSEM_REG_DBG_SHIFT, XSEM_REG_DBG_FORCE_VALID,
	XSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 21 };

static struct block_defs block_ysem_defs = {
	"ysem", { true, true, true }, true, DBG_YSTORM_ID,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY, DBG_BUS_CLIENT_RBCY },
	YSEM_REG_DBG_SELECT, YSEM_REG_DBG_DWORD_ENABLE,
	YSEM_REG_DBG_SHIFT, YSEM_REG_DBG_FORCE_VALID,
	YSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 11 };

static struct block_defs block_psem_defs = {
	"psem", { true, true, true }, true, DBG_PSTORM_ID,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS },
	PSEM_REG_DBG_SELECT, PSEM_REG_DBG_DWORD_ENABLE,
	PSEM_REG_DBG_SHIFT, PSEM_REG_DBG_FORCE_VALID,
	PSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 10 };

static struct block_defs block_rss_defs = {
	"rss", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT },
	RSS_REG_DBG_SELECT, RSS_REG_DBG_DWORD_ENABLE,
	RSS_REG_DBG_SHIFT, RSS_REG_DBG_FORCE_VALID,
	RSS_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 18 };

static struct block_defs block_tmld_defs = {
	"tmld", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	TMLD_REG_DBG_SELECT, TMLD_REG_DBG_DWORD_ENABLE,
	TMLD_REG_DBG_SHIFT, TMLD_REG_DBG_FORCE_VALID,
	TMLD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 13 };

static struct block_defs block_muld_defs = {
	"muld", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU },
	MULD_REG_DBG_SELECT, MULD_REG_DBG_DWORD_ENABLE,
	MULD_REG_DBG_SHIFT, MULD_REG_DBG_FORCE_VALID,
	MULD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 14 };

static struct block_defs block_yuld_defs = {
	"yuld", { true, true, false }, false, 0,
	{ DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, MAX_DBG_BUS_CLIENTS },
	YULD_REG_DBG_SELECT_BB_K2, YULD_REG_DBG_DWORD_ENABLE_BB_K2,
	YULD_REG_DBG_SHIFT_BB_K2, YULD_REG_DBG_FORCE_VALID_BB_K2,
	YULD_REG_DBG_FORCE_FRAME_BB_K2,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 15 };

static struct block_defs block_xyld_defs = {
	"xyld", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX },
	XYLD_REG_DBG_SELECT, XYLD_REG_DBG_DWORD_ENABLE,
	XYLD_REG_DBG_SHIFT, XYLD_REG_DBG_FORCE_VALID,
	XYLD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 12 };

static struct block_defs block_ptld_defs = {
	"ptld", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCT },
	PTLD_REG_DBG_SELECT_E5, PTLD_REG_DBG_DWORD_ENABLE_E5,
	PTLD_REG_DBG_SHIFT_E5, PTLD_REG_DBG_FORCE_VALID_E5,
	PTLD_REG_DBG_FORCE_FRAME_E5,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 28 };

static struct block_defs block_ypld_defs = {
	"ypld", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCS },
	YPLD_REG_DBG_SELECT_E5, YPLD_REG_DBG_DWORD_ENABLE_E5,
	YPLD_REG_DBG_SHIFT_E5, YPLD_REG_DBG_FORCE_VALID_E5,
	YPLD_REG_DBG_FORCE_FRAME_E5,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 27 };

static struct block_defs block_prm_defs = {
	"prm", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	PRM_REG_DBG_SELECT, PRM_REG_DBG_DWORD_ENABLE,
	PRM_REG_DBG_SHIFT, PRM_REG_DBG_FORCE_VALID,
	PRM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 21 };

static struct block_defs block_pbf_pb1_defs = {
	"pbf_pb1", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV, DBG_BUS_CLIENT_RBCV },
	PBF_PB1_REG_DBG_SELECT, PBF_PB1_REG_DBG_DWORD_ENABLE,
	PBF_PB1_REG_DBG_SHIFT, PBF_PB1_REG_DBG_FORCE_VALID,
	PBF_PB1_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 11 };

static struct block_defs block_pbf_pb2_defs = {
	"pbf_pb2", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV, DBG_BUS_CLIENT_RBCV },
	PBF_PB2_REG_DBG_SELECT, PBF_PB2_REG_DBG_DWORD_ENABLE,
	PBF_PB2_REG_DBG_SHIFT, PBF_PB2_REG_DBG_FORCE_VALID,
	PBF_PB2_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 12 };

static struct block_defs block_rpb_defs = {
	"rpb", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	RPB_REG_DBG_SELECT, RPB_REG_DBG_DWORD_ENABLE,
	RPB_REG_DBG_SHIFT, RPB_REG_DBG_FORCE_VALID,
	RPB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 13 };

static struct block_defs block_btb_defs = {
	"btb", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCV, DBG_BUS_CLIENT_RBCV },
	BTB_REG_DBG_SELECT, BTB_REG_DBG_DWORD_ENABLE,
	BTB_REG_DBG_SHIFT, BTB_REG_DBG_FORCE_VALID,
	BTB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 10 };

static struct block_defs block_pbf_defs = {
	"pbf", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV, DBG_BUS_CLIENT_RBCV },
	PBF_REG_DBG_SELECT, PBF_REG_DBG_DWORD_ENABLE,
	PBF_REG_DBG_SHIFT, PBF_REG_DBG_FORCE_VALID,
	PBF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 15 };

static struct block_defs block_rdif_defs = {
	"rdif", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM, DBG_BUS_CLIENT_RBCM },
	RDIF_REG_DBG_SELECT, RDIF_REG_DBG_DWORD_ENABLE,
	RDIF_REG_DBG_SHIFT, RDIF_REG_DBG_FORCE_VALID,
	RDIF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 16 };

static struct block_defs block_tdif_defs = {
	"tdif", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS },
	TDIF_REG_DBG_SELECT, TDIF_REG_DBG_DWORD_ENABLE,
	TDIF_REG_DBG_SHIFT, TDIF_REG_DBG_FORCE_VALID,
	TDIF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 17 };

static struct block_defs block_cdu_defs = {
	"cdu", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF },
	CDU_REG_DBG_SELECT, CDU_REG_DBG_DWORD_ENABLE,
	CDU_REG_DBG_SHIFT, CDU_REG_DBG_FORCE_VALID,
	CDU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 23 };

static struct block_defs block_ccfc_defs = {
	"ccfc", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF },
	CCFC_REG_DBG_SELECT, CCFC_REG_DBG_DWORD_ENABLE,
	CCFC_REG_DBG_SHIFT, CCFC_REG_DBG_FORCE_VALID,
	CCFC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 24 };

static struct block_defs block_tcfc_defs = {
	"tcfc", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF },
	TCFC_REG_DBG_SELECT, TCFC_REG_DBG_DWORD_ENABLE,
	TCFC_REG_DBG_SHIFT, TCFC_REG_DBG_FORCE_VALID,
	TCFC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 25 };

static struct block_defs block_igu_defs = {
	"igu", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	IGU_REG_DBG_SELECT, IGU_REG_DBG_DWORD_ENABLE,
	IGU_REG_DBG_SHIFT, IGU_REG_DBG_FORCE_VALID,
	IGU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 27 };

static struct block_defs block_cau_defs = {
	"cau", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP },
	CAU_REG_DBG_SELECT, CAU_REG_DBG_DWORD_ENABLE,
	CAU_REG_DBG_SHIFT, CAU_REG_DBG_FORCE_VALID,
	CAU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 19 };

/* TODO: add debug bus parameters when E5 RGFS RF is added */
static struct block_defs block_rgfs_defs = {
	"rgfs", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 29 };

static struct block_defs block_rgsrc_defs = {
	"rgsrc", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH },
	RGSRC_REG_DBG_SELECT_E5, RGSRC_REG_DBG_DWORD_ENABLE_E5,
	RGSRC_REG_DBG_SHIFT_E5, RGSRC_REG_DBG_FORCE_VALID_E5,
	RGSRC_REG_DBG_FORCE_FRAME_E5,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 30 };

/* TODO: add debug bus parameters when E5 TGFS RF is added */
static struct block_defs block_tgfs_defs = {
	"tgfs", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 30 };

static struct block_defs block_tgsrc_defs = {
	"tgsrc", { false, false, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCV },
	TGSRC_REG_DBG_SELECT_E5, TGSRC_REG_DBG_DWORD_ENABLE_E5,
	TGSRC_REG_DBG_SHIFT_E5, TGSRC_REG_DBG_FORCE_VALID_E5,
	TGSRC_REG_DBG_FORCE_FRAME_E5,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 31 };

static struct block_defs block_umac_defs = {
	"umac", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ },
	UMAC_REG_DBG_SELECT_K2_E5, UMAC_REG_DBG_DWORD_ENABLE_K2_E5,
	UMAC_REG_DBG_SHIFT_K2_E5, UMAC_REG_DBG_FORCE_VALID_K2_E5,
	UMAC_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 6 };

static struct block_defs block_xmac_defs = {
	"xmac", { true, false, false }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0	};

static struct block_defs block_dbg_defs = {
	"dbg", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 3 };

static struct block_defs block_nig_defs = {
	"nig", { true, true, true }, false, 0,
	{ DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN },
	NIG_REG_DBG_SELECT, NIG_REG_DBG_DWORD_ENABLE,
	NIG_REG_DBG_SHIFT, NIG_REG_DBG_FORCE_VALID,
	NIG_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 0 };

static struct block_defs block_wol_defs = {
	"wol", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ },
	WOL_REG_DBG_SELECT_K2_E5, WOL_REG_DBG_DWORD_ENABLE_K2_E5,
	WOL_REG_DBG_SHIFT_K2_E5, WOL_REG_DBG_FORCE_VALID_K2_E5,
	WOL_REG_DBG_FORCE_FRAME_K2_E5,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 7 };

static struct block_defs block_bmbn_defs = {
	"bmbn", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCB, DBG_BUS_CLIENT_RBCB },
	BMBN_REG_DBG_SELECT_K2_E5, BMBN_REG_DBG_DWORD_ENABLE_K2_E5,
	BMBN_REG_DBG_SHIFT_K2_E5, BMBN_REG_DBG_FORCE_VALID_K2_E5,
	BMBN_REG_DBG_FORCE_FRAME_K2_E5,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_ipc_defs = {
	"ipc", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_UA, 8 };

static struct block_defs block_nwm_defs = {
	"nwm", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCW, DBG_BUS_CLIENT_RBCW },
	NWM_REG_DBG_SELECT_K2_E5, NWM_REG_DBG_DWORD_ENABLE_K2_E5,
	NWM_REG_DBG_SHIFT_K2_E5, NWM_REG_DBG_FORCE_VALID_K2_E5,
	NWM_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV_2, 0 };

static struct block_defs block_nws_defs = {
	"nws", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCW, DBG_BUS_CLIENT_RBCW },
	NWS_REG_DBG_SELECT_K2_E5, NWS_REG_DBG_DWORD_ENABLE_K2_E5,
	NWS_REG_DBG_SHIFT_K2_E5, NWS_REG_DBG_FORCE_VALID_K2_E5,
	NWS_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 12 };

static struct block_defs block_ms_defs = {
	"ms", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ },
	MS_REG_DBG_SELECT_K2_E5, MS_REG_DBG_DWORD_ENABLE_K2_E5,
	MS_REG_DBG_SHIFT_K2_E5, MS_REG_DBG_FORCE_VALID_K2_E5,
	MS_REG_DBG_FORCE_FRAME_K2_E5,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 13 };

static struct block_defs block_phy_pcie_defs = {
	"phy_pcie", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH },
	PCIE_REG_DBG_COMMON_SELECT_K2_E5, PCIE_REG_DBG_COMMON_DWORD_ENABLE_K2_E5,
	PCIE_REG_DBG_COMMON_SHIFT_K2_E5, PCIE_REG_DBG_COMMON_FORCE_VALID_K2_E5,
	PCIE_REG_DBG_COMMON_FORCE_FRAME_K2_E5,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_led_defs = {
	"led", { false, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 14 };

static struct block_defs block_avs_wrap_defs = {
	"avs_wrap", { false, true, false }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_UA, 11 };

static struct block_defs block_pxpreqbus_defs = {
	"pxpreqbus", { false, false, false }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_misc_aeu_defs = {
	"misc_aeu", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };

static struct block_defs block_bar0_map_defs = {
	"bar0_map", { true, true, true }, false, 0,
	{ MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS },
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0 };


static struct block_defs* s_block_defs[MAX_BLOCK_ID] = {
	&block_grc_defs,
 	&block_miscs_defs,
 	&block_misc_defs,
 	&block_dbu_defs,
 	&block_pglue_b_defs,
 	&block_cnig_defs,
 	&block_cpmu_defs,
 	&block_ncsi_defs,
 	&block_opte_defs,
 	&block_bmb_defs,
 	&block_pcie_defs,
 	&block_mcp_defs,
 	&block_mcp2_defs,
 	&block_pswhst_defs,
 	&block_pswhst2_defs,
 	&block_pswrd_defs,
 	&block_pswrd2_defs,
 	&block_pswwr_defs,
 	&block_pswwr2_defs,
 	&block_pswrq_defs,
 	&block_pswrq2_defs,
 	&block_pglcs_defs,
 	&block_dmae_defs,
 	&block_ptu_defs,
 	&block_tcm_defs,
 	&block_mcm_defs,
 	&block_ucm_defs,
 	&block_xcm_defs,
 	&block_ycm_defs,
 	&block_pcm_defs,
 	&block_qm_defs,
 	&block_tm_defs,
 	&block_dorq_defs,
 	&block_brb_defs,
 	&block_src_defs,
 	&block_prs_defs,
 	&block_tsdm_defs,
 	&block_msdm_defs,
 	&block_usdm_defs,
 	&block_xsdm_defs,
 	&block_ysdm_defs,
 	&block_psdm_defs,
 	&block_tsem_defs,
 	&block_msem_defs,
 	&block_usem_defs,
 	&block_xsem_defs,
 	&block_ysem_defs,
 	&block_psem_defs,
 	&block_rss_defs,
 	&block_tmld_defs,
 	&block_muld_defs,
 	&block_yuld_defs,
 	&block_xyld_defs,
 	&block_ptld_defs,
 	&block_ypld_defs,
 	&block_prm_defs,
 	&block_pbf_pb1_defs,
 	&block_pbf_pb2_defs,
 	&block_rpb_defs,
 	&block_btb_defs,
 	&block_pbf_defs,
 	&block_rdif_defs,
 	&block_tdif_defs,
 	&block_cdu_defs,
 	&block_ccfc_defs,
 	&block_tcfc_defs,
 	&block_igu_defs,
 	&block_cau_defs,
 	&block_rgfs_defs,
 	&block_rgsrc_defs,
 	&block_tgfs_defs,
 	&block_tgsrc_defs,
 	&block_umac_defs,
 	&block_xmac_defs,
 	&block_dbg_defs,
 	&block_nig_defs,
 	&block_wol_defs,
 	&block_bmbn_defs,
 	&block_ipc_defs,
 	&block_nwm_defs,
 	&block_nws_defs,
 	&block_ms_defs,
 	&block_phy_pcie_defs,
 	&block_led_defs,
 	&block_avs_wrap_defs,
 	&block_pxpreqbus_defs,
 	&block_misc_aeu_defs,
 	&block_bar0_map_defs,

};


/* Constraint operation types */
static struct dbg_bus_constraint_op_defs s_constraint_op_defs[] = {

	/* DBG_BUS_CONSTRAINT_OP_EQ */
	{ 0, false },

	/* DBG_BUS_CONSTRAINT_OP_NE */
	{ 5, false },

	/* DBG_BUS_CONSTRAINT_OP_LT */
	{ 1, false },

	/* DBG_BUS_CONSTRAINT_OP_LTC */
	{ 1, true },

	/* DBG_BUS_CONSTRAINT_OP_LE */
	{ 2, false },

	/* DBG_BUS_CONSTRAINT_OP_LEC */
	{ 2, true },

	/* DBG_BUS_CONSTRAINT_OP_GT */
	{ 4, false },

	/* DBG_BUS_CONSTRAINT_OP_GTC */
	{ 4, true },

	/* DBG_BUS_CONSTRAINT_OP_GE */
	{ 3, false },

	/* DBG_BUS_CONSTRAINT_OP_GEC */
	{ 3, true }
};

static const char* s_dbg_target_names[] = {

	/* DBG_BUS_TARGET_ID_INT_BUF */
	"int-buf",

	/* DBG_BUS_TARGET_ID_NIG */
	"nw",

	/* DBG_BUS_TARGET_ID_PCI */
	"pci-buf"
};

static struct storm_mode_defs s_storm_mode_defs[] = {

	/* DBG_BUS_STORM_MODE_PRINTF */
	{ "printf", true, 0 },

	/* DBG_BUS_STORM_MODE_PRAM_ADDR */
	{ "pram_addr", true, 1 },

	/* DBG_BUS_STORM_MODE_DRA_RW */
	{ "dra_rw", true, 2 },

	/* DBG_BUS_STORM_MODE_DRA_W */
	{ "dra_w", true, 3 },

	/* DBG_BUS_STORM_MODE_LD_ST_ADDR */
	{ "ld_st_addr", true, 4 },

	/* DBG_BUS_STORM_MODE_DRA_FSM */
	{ "dra_fsm", true, 5 },

	/* DBG_BUS_STORM_MODE_RH */
	{ "rh", true, 6 },

	/* DBG_BUS_STORM_MODE_FOC */
	{ "foc", false, 1 },

	/* DBG_BUS_STORM_MODE_EXT_STORE */
	{ "ext_store", false, 3 }
};

static struct platform_defs s_platform_defs[] = {

	/* PLATFORM_ASIC */
	{ "asic", 1, 256, 32768 },

	/* PLATFORM_EMUL_FULL */
	{ "emul_full", 2000, 8, 4096 },

	/* PLATFORM_EMUL_REDUCED */
	{ "emul_reduced", 2000, 8, 4096 },

	/* PLATFORM_FPGA */
	{ "fpga", 200, 32, 8192 }
};

static struct grc_param_defs s_grc_param_defs[] = {

	/* DBG_GRC_PARAM_DUMP_TSTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_MSTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_USTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_XSTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_YSTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_PSTORM */
	{ { 1, 1, 1 }, 0, 1, false, 1, 1 },

	/* DBG_GRC_PARAM_DUMP_REGS */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_RAM */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_PBUF */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_IOR */
	{ { 0, 0, 0 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_VFC */
	{ { 0, 0, 0 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_CM_CTX */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_ILT */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_RSS */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_CAU */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_QM */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_MCP */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_RESERVED */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_CFC */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_IGU */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_BRB */
	{ { 0, 0, 0 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_BTB */
	{ { 0, 0, 0 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_BMB */
	{ { 0, 0, 0 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_NIG */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_MULD */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_PRS */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_DMAE */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_TM */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_SDM */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_DIF */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_STATIC */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_UNSTALL */
	{ { 0, 0, 0 }, 0, 1, false, 0, 0 },

	/* DBG_GRC_PARAM_NUM_LCIDS */
	{ { MAX_LCIDS, MAX_LCIDS, MAX_LCIDS }, 1, MAX_LCIDS, false, MAX_LCIDS, MAX_LCIDS },

	/* DBG_GRC_PARAM_NUM_LTIDS */
	{ { MAX_LTIDS, MAX_LTIDS, MAX_LTIDS }, 1, MAX_LTIDS, false, MAX_LTIDS, MAX_LTIDS },

	/* DBG_GRC_PARAM_EXCLUDE_ALL */
	{ { 0, 0, 0 }, 0, 1, true, 0, 0 },

	/* DBG_GRC_PARAM_CRASH */
	{ { 0, 0, 0 }, 0, 1, true, 0, 0 },

	/* DBG_GRC_PARAM_PARITY_SAFE */
	{ { 0, 0, 0 }, 0, 1, false, 1, 0 },

	/* DBG_GRC_PARAM_DUMP_CM */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_DUMP_PHY */
	{ { 1, 1, 1 }, 0, 1, false, 0, 1 },

	/* DBG_GRC_PARAM_NO_MCP */
	{ { 0, 0, 0 }, 0, 1, false, 0, 0 },

	/* DBG_GRC_PARAM_NO_FW_VER */
	{ { 0, 0, 0 }, 0, 1, false, 0, 0 }
};

static struct rss_mem_defs s_rss_mem_defs[] = {
	{ "rss_mem_cid", "rss_cid", 0, 32,
	{ 256, 320, 512 } },

	{ "rss_mem_key_msb", "rss_key", 1024, 256,
	{ 128, 208, 257 } },

	{ "rss_mem_key_lsb", "rss_key", 2048, 64,
	{ 128, 208, 257 } },

	{ "rss_mem_info", "rss_info", 3072, 16,
	{ 128, 208, 256 } },

	{ "rss_mem_ind", "rss_ind", 4096, 16,
	{ 16384, 26624, 32768 } }
};

static struct vfc_ram_defs s_vfc_ram_defs[] = {
	{ "vfc_ram_tt1", "vfc_ram", 0, 512 },
	{ "vfc_ram_mtt2", "vfc_ram", 512, 128 },
	{ "vfc_ram_stt2", "vfc_ram", 640, 32 },
	{ "vfc_ram_ro_vect", "vfc_ram", 672, 32 }
};

static struct big_ram_defs s_big_ram_defs[] = {
	{ "BRB", MEM_GROUP_BRB_MEM, MEM_GROUP_BRB_RAM, DBG_GRC_PARAM_DUMP_BRB,
	  BRB_REG_BIG_RAM_ADDRESS, BRB_REG_BIG_RAM_DATA, MISC_REG_BLOCK_256B_EN, { 0, 0, 0 },
	  { 153600, 180224, 282624 } },

	{ "BTB", MEM_GROUP_BTB_MEM, MEM_GROUP_BTB_RAM, DBG_GRC_PARAM_DUMP_BTB,
	  BTB_REG_BIG_RAM_ADDRESS, BTB_REG_BIG_RAM_DATA, MISC_REG_BLOCK_256B_EN, { 0, 1, 1 },
	  { 92160, 117760, 168960 } },

	{ "BMB", MEM_GROUP_BMB_MEM, MEM_GROUP_BMB_RAM, DBG_GRC_PARAM_DUMP_BMB,
	  BMB_REG_BIG_RAM_ADDRESS, BMB_REG_BIG_RAM_DATA, MISCS_REG_BLOCK_256B_EN, { 0, 0, 0 },
	  { 36864, 36864, 36864 } }
};

static struct reset_reg_defs s_reset_regs_defs[] = {

	/* DBG_RESET_REG_MISCS_PL_UA */
	{ MISCS_REG_RESET_PL_UA, { true, true, true }, { 0x0, 0x0, 0x0 } },

	/* DBG_RESET_REG_MISCS_PL_HV */
	{ MISCS_REG_RESET_PL_HV, { true, true, true }, { 0x0, 0x400, 0x600 } },

	/* DBG_RESET_REG_MISCS_PL_HV_2 */
	{ MISCS_REG_RESET_PL_HV_2_K2_E5, { false, true, true }, { 0x0, 0x0, 0x0 } },

	/* DBG_RESET_REG_MISC_PL_UA */
	{ MISC_REG_RESET_PL_UA, { true, true, true }, { 0x0, 0x0, 0x0 } },

	/* DBG_RESET_REG_MISC_PL_HV */
	{ MISC_REG_RESET_PL_HV, { true, true, true }, { 0x0, 0x0, 0x0 } },

	/* DBG_RESET_REG_MISC_PL_PDA_VMAIN_1 */
	{ MISC_REG_RESET_PL_PDA_VMAIN_1, { true, true, true }, { 0x4404040, 0x4404040, 0x404040 } },

	/* DBG_RESET_REG_MISC_PL_PDA_VMAIN_2 */
	{ MISC_REG_RESET_PL_PDA_VMAIN_2, { true, true, true }, { 0x7, 0x7c00007, 0x5c08007 } },

	/* DBG_RESET_REG_MISC_PL_PDA_VAUX */
	{ MISC_REG_RESET_PL_PDA_VAUX, { true, true, true }, { 0x2, 0x2, 0x2 } },
};

static struct phy_defs s_phy_defs[] = {
	{ "nw_phy", NWS_REG_NWS_CMU_K2, PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_7_0_K2_E5, PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_15_8_K2_E5, PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_7_0_K2_E5, PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_11_8_K2_E5 },
	{ "sgmii_phy", MS_REG_MS_CMU_K2_E5, PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X132_K2_E5, PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X133_K2_E5, PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X130_K2_E5, PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X131_K2_E5 },
	{ "pcie_phy0", PHY_PCIE_REG_PHY0_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131_K2_E5 },
	{ "pcie_phy1", PHY_PCIE_REG_PHY1_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130_K2_E5, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131_K2_E5 },
};

/* The order of indexes that should be applied to a PCI buffer line */
static const u8 s_pci_buf_line_ind[PCI_BUF_LINE_SIZE_IN_DWORDS] = { 1, 0, 3, 2, 5, 4, 7, 6 };

/******************************** Variables **********************************/

/* The version of the calling app */
static u32 s_app_ver;

/**************************** Private Functions ******************************/

static void ecore_static_asserts(void)
{
	CHECK_ARR_SIZE(s_dbg_arrays, MAX_BIN_DBG_BUFFER_TYPE);
	CHECK_ARR_SIZE(s_big_ram_defs, NUM_BIG_RAM_TYPES);
	CHECK_ARR_SIZE(s_vfc_ram_defs, NUM_VFC_RAM_TYPES);
	CHECK_ARR_SIZE(s_rss_mem_defs, NUM_RSS_MEM_TYPES);
	CHECK_ARR_SIZE(s_chip_defs, MAX_CHIP_IDS);
	CHECK_ARR_SIZE(s_platform_defs, MAX_PLATFORM_IDS);
	CHECK_ARR_SIZE(s_storm_defs, MAX_DBG_STORMS);
	CHECK_ARR_SIZE(s_constraint_op_defs, MAX_DBG_BUS_CONSTRAINT_OPS);
	CHECK_ARR_SIZE(s_dbg_target_names, MAX_DBG_BUS_TARGETS);
	CHECK_ARR_SIZE(s_storm_mode_defs, MAX_DBG_BUS_STORM_MODES);
	CHECK_ARR_SIZE(s_grc_param_defs, MAX_DBG_GRC_PARAMS);
	CHECK_ARR_SIZE(s_reset_regs_defs, MAX_DBG_RESET_REGS);
}

/* Reads and returns a single dword from the specified unaligned buffer. */
static u32 ecore_read_unaligned_dword(u8 *buf)
{
	u32 dword;

	OSAL_MEMCPY((u8 *)&dword, buf, sizeof(dword));
	return dword;
}

/* Returns the difference in bytes between the specified physical addresses.
 * Assumes that the first address is bigger then the second, and that the
 * difference is a 32-bit value.
 */
static u32 ecore_phys_addr_diff(struct dbg_bus_mem_addr *a,
								struct dbg_bus_mem_addr *b)
{
	return a->hi == b->hi ? a->lo - b->lo : b->lo - a->lo;
}

/* Sets the value of the specified GRC param */
static void ecore_grc_set_param(struct ecore_hwfn *p_hwfn,
				 enum dbg_grc_params grc_param,
				 u32 val)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	dev_data->grc.param_val[grc_param] = val;
}

/* Returns the value of the specified GRC param */
static u32 ecore_grc_get_param(struct ecore_hwfn *p_hwfn,
							   enum dbg_grc_params grc_param)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return dev_data->grc.param_val[grc_param];
}

/* Initializes the GRC parameters */
static void ecore_dbg_grc_init_params(struct ecore_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	if (!dev_data->grc.params_initialized) {
		ecore_dbg_grc_set_params_default(p_hwfn);
		dev_data->grc.params_initialized = 1;
	}
}

/* Initializes debug data for the specified device */
static enum dbg_status ecore_dbg_dev_init(struct ecore_hwfn *p_hwfn,
										  struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	if (dev_data->initialized)
		return DBG_STATUS_OK;

	if (!s_app_ver)
		return DBG_STATUS_APP_VERSION_NOT_SET;

	if (ECORE_IS_E5(p_hwfn->p_dev)) {
		dev_data->chip_id = CHIP_E5;
		dev_data->mode_enable[MODE_E5] = 1;
	}
	else if (ECORE_IS_K2(p_hwfn->p_dev)) {
		dev_data->chip_id = CHIP_K2;
		dev_data->mode_enable[MODE_K2] = 1;
	}
	else if (ECORE_IS_BB_B0(p_hwfn->p_dev)) {
		dev_data->chip_id = CHIP_BB;
		dev_data->mode_enable[MODE_BB] = 1;
	}
	else {
		return DBG_STATUS_UNKNOWN_CHIP;
	}

#ifdef ASIC_ONLY
	dev_data->platform_id = PLATFORM_ASIC;
	dev_data->mode_enable[MODE_ASIC] = 1;
#else
	if (CHIP_REV_IS_ASIC(p_hwfn->p_dev)) {
		dev_data->platform_id = PLATFORM_ASIC;
		dev_data->mode_enable[MODE_ASIC] = 1;
	}
	else if (CHIP_REV_IS_EMUL(p_hwfn->p_dev)) {
		if (ecore_rd(p_hwfn, p_ptt, MISCS_REG_ECO_RESERVED) & 0x20000000) {
			dev_data->platform_id = PLATFORM_EMUL_FULL;
			dev_data->mode_enable[MODE_EMUL_FULL] = 1;
		}
		else {
			dev_data->platform_id = PLATFORM_EMUL_REDUCED;
			dev_data->mode_enable[MODE_EMUL_REDUCED] = 1;
		}
	}
	else if (CHIP_REV_IS_FPGA(p_hwfn->p_dev)) {
		dev_data->platform_id = PLATFORM_FPGA;
		dev_data->mode_enable[MODE_FPGA] = 1;
	}
	else {
		return DBG_STATUS_UNKNOWN_CHIP;
	}
#endif

	/* Initializes the GRC parameters */
	ecore_dbg_grc_init_params(p_hwfn);

	dev_data->use_dmae = USE_DMAE;
	dev_data->num_regs_read = 0;
	dev_data->initialized = 1;

	return DBG_STATUS_OK;
}

static struct dbg_bus_block* get_dbg_bus_block_desc(struct ecore_hwfn *p_hwfn,
														  enum block_id block_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return (struct dbg_bus_block *)&dbg_bus_blocks[block_id * MAX_CHIP_IDS + dev_data->chip_id];
}

/* Returns OSAL_NULL for signature line, latency line and non-existing lines */
static struct dbg_bus_line* get_dbg_bus_line_desc(struct ecore_hwfn *p_hwfn,
														enum block_id block_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_block_data *block_bus;
	struct dbg_bus_block *block_desc;

	block_bus = &dev_data->bus.blocks[block_id];
	block_desc = get_dbg_bus_block_desc(p_hwfn, block_id);

	if (!block_bus->line_num ||
		(block_bus->line_num == 1 && block_desc->has_latency_events) ||
		block_bus->line_num >= NUM_DBG_LINES(block_desc))
		return OSAL_NULL;

	return (struct dbg_bus_line *)&dbg_bus_lines[block_desc->lines_offset + block_bus->line_num - NUM_EXTRA_DBG_LINES(block_desc)];
}

/* Reads the FW info structure for the specified Storm from the chip,
 * and writes it to the specified fw_info pointer.
 */
static void ecore_read_fw_info(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   u8 storm_id,
							   struct fw_info *fw_info)
{
	struct storm_defs *storm = &s_storm_defs[storm_id];
	struct fw_info_location fw_info_location;
	u32 addr, i, *dest;

	OSAL_MEMSET(&fw_info_location, 0, sizeof(fw_info_location));
	OSAL_MEMSET(fw_info, 0, sizeof(*fw_info));

	/* Read first the address that points to fw_info location.
	 * The address is located in the last line of the Storm RAM.
	 */
	addr = storm->sem_fast_mem_addr + SEM_FAST_REG_INT_RAM +
		(ECORE_IS_E5(p_hwfn->p_dev) ?
			DWORDS_TO_BYTES(SEM_FAST_REG_INT_RAM_SIZE_E5) :
			DWORDS_TO_BYTES(SEM_FAST_REG_INT_RAM_SIZE_BB_K2))
		- sizeof(fw_info_location);

	dest = (u32 *)&fw_info_location;

	for (i = 0; i < BYTES_TO_DWORDS(sizeof(fw_info_location)); i++, addr += BYTES_IN_DWORD)
		dest[i] = ecore_rd(p_hwfn, p_ptt, addr);

	/* Read FW version info from Storm RAM */
	if (fw_info_location.size > 0 && fw_info_location.size <= sizeof(*fw_info)) {
		addr = fw_info_location.grc_addr;
		dest = (u32 *)fw_info;
		for (i = 0; i < BYTES_TO_DWORDS(fw_info_location.size); i++, addr += BYTES_IN_DWORD)
			dest[i] = ecore_rd(p_hwfn, p_ptt, addr);
	}
}

/* Dumps the specified string to the specified buffer.
 * Returns the dumped size in bytes.
 */
static u32 ecore_dump_str(char *dump_buf,
						  bool dump,
						  const char *str)
{
	if (dump)
		OSAL_STRCPY(dump_buf, str);

	return (u32)OSAL_STRLEN(str) + 1;
}

/* Dumps zeros to align the specified buffer to dwords.
 * Returns the dumped size in bytes.
 */
static u32 ecore_dump_align(char *dump_buf,
							bool dump,
							u32 byte_offset)
{
	u8 offset_in_dword, align_size;

	offset_in_dword = (u8)(byte_offset & 0x3);
	align_size = offset_in_dword ? BYTES_IN_DWORD - offset_in_dword : 0;

	if (dump && align_size)
		OSAL_MEMSET(dump_buf, 0, align_size);

	return align_size;
}

/* Writes the specified string param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_str_param(u32 *dump_buf,
								bool dump,
								const char *param_name,
								const char *param_val)
{
	char *char_buf = (char *)dump_buf;
	u32 offset = 0;

	/* Dump param name */
	offset += ecore_dump_str(char_buf + offset, dump, param_name);

	/* Indicate a string param value */
	if (dump)
		*(char_buf + offset) = 1;
	offset++;

	/* Dump param value */
	offset += ecore_dump_str(char_buf + offset, dump, param_val);

	/* Align buffer to next dword */
	offset += ecore_dump_align(char_buf + offset, dump, offset);

	return BYTES_TO_DWORDS(offset);
}

/* Writes the specified numeric param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_num_param(u32 *dump_buf,
								bool dump,
								const char *param_name,
								u32 param_val)
{
	char *char_buf = (char *)dump_buf;
	u32 offset = 0;

	/* Dump param name */
	offset += ecore_dump_str(char_buf + offset, dump, param_name);

	/* Indicate a numeric param value */
	if (dump)
		*(char_buf + offset) = 0;
	offset++;

	/* Align buffer to next dword */
	offset += ecore_dump_align(char_buf + offset, dump, offset);

	/* Dump param value (and change offset from bytes to dwords) */
	offset = BYTES_TO_DWORDS(offset);
	if (dump)
		*(dump_buf + offset) = param_val;
	offset++;

	return offset;
}

/* Reads the FW version and writes it as a param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_fw_ver_param(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char fw_ver_str[16] = EMPTY_FW_VERSION_STR;
	char fw_img_str[16] = EMPTY_FW_IMAGE_STR;
	struct fw_info fw_info = { { 0 }, { 0 } };
	u32 offset = 0;

	if (dump && !ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_FW_VER)) {
		/* Read FW image/version from PRAM in a non-reset SEMI */
		bool found = false;
		u8 storm_id;

		for (storm_id = 0; storm_id < MAX_DBG_STORMS && !found; storm_id++) {
			struct storm_defs *storm = &s_storm_defs[storm_id];

			/* Read FW version/image */
			if (dev_data->block_in_reset[storm->block_id])
				continue;

			/* Read FW info for the current Storm */
			ecore_read_fw_info(p_hwfn, p_ptt, storm_id, &fw_info);

			/* Create FW version/image strings */
			if (OSAL_SNPRINTF(fw_ver_str, sizeof(fw_ver_str), "%d_%d_%d_%d", fw_info.ver.num.major, fw_info.ver.num.minor, fw_info.ver.num.rev, fw_info.ver.num.eng) < 0)
				DP_NOTICE(p_hwfn, true, "Unexpected debug error: invalid FW version string\n");
			switch (fw_info.ver.image_id) {
			case FW_IMG_KUKU: OSAL_STRCPY(fw_img_str, "kuku"); break;
			case FW_IMG_MAIN: OSAL_STRCPY(fw_img_str, "main"); break;
			case FW_IMG_L2B: OSAL_STRCPY(fw_img_str, "l2b"); break;
			default: OSAL_STRCPY(fw_img_str, "unknown"); break;
			}

			found = true;
		}
	}

	/* Dump FW version, image and timestamp */
	offset += ecore_dump_str_param(dump_buf + offset, dump, "fw-version", fw_ver_str);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "fw-image", fw_img_str);
	offset += ecore_dump_num_param(dump_buf + offset, dump, "fw-timestamp", fw_info.ver.timestamp);

	return offset;
}

/* Reads the MFW version and writes it as a param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_mfw_ver_param(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									u32 *dump_buf,
									bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char mfw_ver_str[16] = EMPTY_FW_VERSION_STR;

	if (dump && dev_data->platform_id == PLATFORM_ASIC && !ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_FW_VER)) {
		u32 public_data_addr, global_section_offsize_addr, global_section_offsize, global_section_addr, mfw_ver;

		/* Find MCP public data GRC address. Needs to be ORed with
		 * MCP_REG_SCRATCH due to a HW bug.
		 */
		public_data_addr = ecore_rd(p_hwfn, p_ptt, MISC_REG_SHARED_MEM_ADDR) | MCP_REG_SCRATCH;

		/* Find MCP public global section offset */
		global_section_offsize_addr = public_data_addr + OFFSETOF(struct mcp_public_data, sections) + sizeof(offsize_t) * PUBLIC_GLOBAL;
		global_section_offsize = ecore_rd(p_hwfn, p_ptt, global_section_offsize_addr);
		global_section_addr = MCP_REG_SCRATCH + (global_section_offsize & OFFSIZE_OFFSET_MASK) * 4;

		/* Read MFW version from MCP public global section */
		mfw_ver = ecore_rd(p_hwfn, p_ptt, global_section_addr + OFFSETOF(struct public_global, mfw_ver));

		/* Dump MFW version param */
		if (OSAL_SNPRINTF(mfw_ver_str, sizeof(mfw_ver_str), "%d_%d_%d_%d", (u8)(mfw_ver >> 24), (u8)(mfw_ver >> 16), (u8)(mfw_ver >> 8), (u8)mfw_ver) < 0)
			DP_NOTICE(p_hwfn, true, "Unexpected debug error: invalid MFW version string\n");
	}

	return ecore_dump_str_param(dump_buf, dump, "mfw-version", mfw_ver_str);
}

/* Writes a section header to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_section_hdr(u32 *dump_buf,
								  bool dump,
								  const char *name,
								  u32 num_params)
{
	return ecore_dump_num_param(dump_buf, dump, name, num_params);
}

/* Writes the common global params to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_dump_common_global_params(struct ecore_hwfn *p_hwfn,
										   struct ecore_ptt *p_ptt,
										   u32 *dump_buf,
										   bool dump,
										   u8 num_specific_global_params)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0;
	u8 num_params;

	/* Dump global params section header */
	num_params = NUM_COMMON_GLOBAL_PARAMS + num_specific_global_params;
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "global_params", num_params);

	/* Store params */
	offset += ecore_dump_fw_ver_param(p_hwfn, p_ptt, dump_buf + offset, dump);
	offset += ecore_dump_mfw_ver_param(p_hwfn, p_ptt, dump_buf + offset, dump);
	offset += ecore_dump_num_param(dump_buf + offset, dump, "tools-version", TOOLS_VERSION);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "chip", s_chip_defs[dev_data->chip_id].name);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "platform", s_platform_defs[dev_data->platform_id].name);
	offset += ecore_dump_num_param(dump_buf + offset, dump, "pci-func", p_hwfn->abs_pf_id);

	return offset;
}

/* Writes the "last" section (including CRC) to the specified buffer at the
 * given offset. Returns the dumped size in dwords.
 */
static u32 ecore_dump_last_section(u32 *dump_buf,
								   u32 offset,
								   bool dump)
{
	u32 start_offset = offset;

	/* Dump CRC section header */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "last", 0);

	/* Calculate CRC32 and add it to the dword after the "last" section */
	if (dump)
		*(dump_buf + offset) = ~OSAL_CRC32(0xffffffff, (u8 *)dump_buf, DWORDS_TO_BYTES(offset));

	offset++;

	return offset - start_offset;
}

/* Update blocks reset state  */
static void ecore_update_blocks_reset_state(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_val[MAX_DBG_RESET_REGS] = { 0 };
	u32 i;

	/* Read reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++)
		if (s_reset_regs_defs[i].exists[dev_data->chip_id])
			reg_val[i] = ecore_rd(p_hwfn, p_ptt, s_reset_regs_defs[i].addr);

	/* Check if blocks are in reset */
	for (i = 0; i < MAX_BLOCK_ID; i++) {
		struct block_defs *block = s_block_defs[i];

		dev_data->block_in_reset[i] = block->has_reset_bit && !(reg_val[block->reset_reg] & (1 << block->reset_bit_offset));
	}
}

/* Enable / disable the Debug block */
static void ecore_bus_enable_dbg_block(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   bool enable)
{
	ecore_wr(p_hwfn, p_ptt, DBG_REG_DBG_BLOCK_ON, enable ? 1 : 0);
}

/* Resets the Debug block */
static void ecore_bus_reset_dbg_block(struct ecore_hwfn *p_hwfn,
									  struct ecore_ptt *p_ptt)
{
	u32 dbg_reset_reg_addr, old_reset_reg_val, new_reset_reg_val;
	struct block_defs *dbg_block = s_block_defs[BLOCK_DBG];

	dbg_reset_reg_addr = s_reset_regs_defs[dbg_block->reset_reg].addr;
	old_reset_reg_val = ecore_rd(p_hwfn, p_ptt, dbg_reset_reg_addr);
	new_reset_reg_val = old_reset_reg_val & ~(1 << dbg_block->reset_bit_offset);

	ecore_wr(p_hwfn, p_ptt, dbg_reset_reg_addr, new_reset_reg_val);
	ecore_wr(p_hwfn, p_ptt, dbg_reset_reg_addr, old_reset_reg_val);
}

static void ecore_bus_set_framing_mode(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   enum dbg_bus_frame_modes mode)
{
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FRAMING_MODE, (u8)mode);
}

/* Enable / disable Debug Bus clients according to the specified mask
 * (1 = enable, 0 = disable).
 */
static void ecore_bus_enable_clients(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt,
									 u32 client_mask)
{
	ecore_wr(p_hwfn, p_ptt, DBG_REG_CLIENT_ENABLE, client_mask);
}

/* Enables the specified Storm for Debug Bus. Assumes a valid Storm ID. */
static void ecore_bus_enable_storm(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   enum dbg_storms storm_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 base_addr, sem_filter_params = 0;
	struct dbg_bus_storm_data *storm_bus;
	struct storm_mode_defs *storm_mode;
	struct storm_defs *storm;

	storm = &s_storm_defs[storm_id];
	storm_bus = &dev_data->bus.storms[storm_id];
	storm_mode = &s_storm_mode_defs[storm_bus->mode];
	base_addr = storm->sem_fast_mem_addr;

	/* Config SEM */
	if (storm_mode->is_fast_dbg) {

		/* Enable fast debug */
		ecore_wr(p_hwfn, p_ptt, storm->sem_frame_mode_addr, DBG_BUS_SEMI_FRAME_MODE_0SLOW_4FAST);
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DEBUG_MODE, storm_mode->id_in_hw);
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DEBUG_ACTIVE, 1);

		/* Enable messages. Must be done after enabling
		 * SEM_FAST_REG_DEBUG_ACTIVE, otherwise messages will
		 * be dropped after the SEMI sync fifo is filled.
		 */
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DBG_MODE23_SRC_DISABLE, SEM_FAST_MODE23_SRC_ENABLE_VAL);
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DBG_MODE4_SRC_DISABLE, SEM_FAST_MODE4_SRC_ENABLE_VAL);
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DBG_MODE6_SRC_DISABLE, SEM_FAST_MODE6_SRC_ENABLE_VAL);
	}
	else {

		/* Enable slow debug */
		ecore_wr(p_hwfn, p_ptt, storm->sem_frame_mode_addr, DBG_BUS_SEMI_FRAME_MODE_4SLOW_0FAST);
		ecore_wr(p_hwfn, p_ptt, storm->sem_slow_enable_addr, 1);
		ecore_wr(p_hwfn, p_ptt, storm->sem_slow_mode_addr, storm_mode->id_in_hw);
		ecore_wr(p_hwfn, p_ptt, storm->sem_slow_mode1_conf_addr, SEM_SLOW_MODE1_DATA_ENABLE);
	}

	/* Config SEM cid filter */
	if (storm_bus->cid_filter_en) {
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_FILTER_CID, storm_bus->cid);
		sem_filter_params |= SEM_FILTER_CID_EN_MASK;
	}

	/* Config SEM eid filter */
	if (storm_bus->eid_filter_en) {
		const union dbg_bus_storm_eid_params *eid_filter = &storm_bus->eid_filter_params;

		if (storm_bus->eid_range_not_mask) {
			ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_EVENT_ID_RANGE_STRT, eid_filter->range.min);
			ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_EVENT_ID_RANGE_END, eid_filter->range.max);
			sem_filter_params |= SEM_FILTER_EID_RANGE_EN_MASK;
		}
		else {
			ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_FILTER_EVENT_ID, eid_filter->mask.val);
			ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_EVENT_ID_MASK, ~eid_filter->mask.mask);
			sem_filter_params |= SEM_FILTER_EID_MASK_EN_MASK;
		}
	}

	/* Config accumulaed SEM filter parameters (if any) */
	if (sem_filter_params)
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_RECORD_FILTER_ENABLE, sem_filter_params);
}

/* Disables Debug Bus block inputs */
static enum dbg_status ecore_bus_disable_inputs(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												bool empty_semi_fifos)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 storm_id, num_fifos_to_empty = MAX_DBG_STORMS;
	bool is_fifo_empty[MAX_DBG_STORMS] = { false };
	u32 block_id;

	/* Disable messages output in all Storms */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (dev_data->block_in_reset[storm->block_id])
			continue;

		ecore_wr(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_DBG_MODE23_SRC_DISABLE, SEM_FAST_MODE23_SRC_DISABLE_VAL);
		ecore_wr(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_DBG_MODE4_SRC_DISABLE, SEM_FAST_MODE4_SRC_DISABLE_VAL);
		ecore_wr(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_DBG_MODE6_SRC_DISABLE, SEM_FAST_MODE6_SRC_DISABLE_VAL);
	}

	/* Try to empty the SEMI sync fifo. Must be done after messages output
	 * were disabled in all Storms.
	 */
	while (num_fifos_to_empty) {
		for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
			struct storm_defs *storm = &s_storm_defs[storm_id];

			if (is_fifo_empty[storm_id])
				continue;

			/* Check if sync fifo got empty */
			if (dev_data->block_in_reset[storm->block_id] || ecore_rd(p_hwfn, p_ptt, storm->sem_sync_dbg_empty_addr)) {
				is_fifo_empty[storm_id] = true;
				num_fifos_to_empty--;
			}
		}

		/* Check if need to continue polling */
		if (num_fifos_to_empty) {
			u32 polling_ms = SEMI_SYNC_FIFO_POLLING_DELAY_MS * s_platform_defs[dev_data->platform_id].delay_factor;
			u32 polling_count = 0;

			if (empty_semi_fifos && polling_count < SEMI_SYNC_FIFO_POLLING_COUNT) {
				OSAL_MSLEEP(polling_ms);
				polling_count++;
			}
			else {
				DP_NOTICE(p_hwfn, false, "Warning: failed to empty the SEMI sync FIFO. It means that the last few messages from the SEMI could not be sent to the DBG block. This can happen when the DBG block is blocked (e.g. due to a PCI problem).\n");
				break;
			}
		}
	}

	/* Disable debug in all Storms */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];
		u32 base_addr = storm->sem_fast_mem_addr;

		if (dev_data->block_in_reset[storm->block_id])
			continue;

		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_DEBUG_ACTIVE, 0);
		ecore_wr(p_hwfn, p_ptt, base_addr + SEM_FAST_REG_RECORD_FILTER_ENABLE, DBG_BUS_FILTER_TYPE_OFF);
		ecore_wr(p_hwfn, p_ptt, storm->sem_frame_mode_addr, DBG_BUS_FRAME_MODE_4HW_0ST);
		ecore_wr(p_hwfn, p_ptt, storm->sem_slow_enable_addr, 0);
	}

	/* Disable all clients */
	ecore_bus_enable_clients(p_hwfn, p_ptt, 0);

	/* Disable all blocks */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		struct block_defs *block = s_block_defs[block_id];

		if (block->dbg_client_id[dev_data->chip_id] != MAX_DBG_BUS_CLIENTS && !dev_data->block_in_reset[block_id])
			ecore_wr(p_hwfn, p_ptt, block->dbg_enable_addr, 0);
	}

	/* Disable timestamp */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TIMESTAMP_VALID_EN, 0);

	/* Disable filters and triggers */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FILTER_ENABLE, DBG_BUS_FILTER_TYPE_OFF);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_ENABLE, 0);

	return DBG_STATUS_OK;
}

/* Sets a Debug Bus trigger/filter constraint */
static void ecore_bus_set_constraint(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt,
									 bool is_filter,
									 u8 constraint_id,
									 u8 hw_op_val,
									 u32 data_val,
									 u32 data_mask,
									 u8 frame_bit,
									 u8 frame_mask,
									 u16 dword_offset,
									 u16 range,
									 u8 cyclic_bit,
									 u8 must_bit)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_offset = constraint_id * BYTES_IN_DWORD;
	u8 curr_trigger_state;

	/* For trigger only - set register offset according to state */
	if (!is_filter) {
		curr_trigger_state = dev_data->bus.next_trigger_state - 1;
		reg_offset += curr_trigger_state * TRIGGER_SETS_PER_STATE * MAX_CONSTRAINTS * BYTES_IN_DWORD;
	}

	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_OPRTN_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_OPRTN_0) + reg_offset, hw_op_val);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_DATA_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_DATA_0) + reg_offset, data_val);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_DATA_MASK_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_DATA_MASK_0) + reg_offset, data_mask);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_FRAME_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_FRAME_0) + reg_offset, frame_bit);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_FRAME_MASK_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_FRAME_MASK_0) + reg_offset, frame_mask);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_OFFSET_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_OFFSET_0) + reg_offset, dword_offset);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_RANGE_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_RANGE_0) + reg_offset, range);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_CYCLIC_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_CYCLIC_0) + reg_offset, cyclic_bit);
	ecore_wr(p_hwfn, p_ptt, (is_filter ? DBG_REG_FILTER_CNSTR_MUST_0 : DBG_REG_TRIGGER_STATE_SET_CNSTR_MUST_0) + reg_offset, must_bit);
}

/* Reads the specified DBG Bus internal buffer range and copy it to the
 * specified buffer. Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_int_buf_range(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										bool dump,
										u32 start_line,
										u32 end_line)
{
	u32 line, reg_addr, i, offset = 0;

	if (!dump)
		return (end_line - start_line + 1) * INT_BUF_LINE_SIZE_IN_DWORDS;

	for (line = start_line, reg_addr = DBG_REG_INTR_BUFFER + DWORDS_TO_BYTES(start_line * INT_BUF_LINE_SIZE_IN_DWORDS);
		line <= end_line;
		line++, offset += INT_BUF_LINE_SIZE_IN_DWORDS)
		for (i = 0; i < INT_BUF_LINE_SIZE_IN_DWORDS; i++, reg_addr += BYTES_IN_DWORD)
			dump_buf[offset + INT_BUF_LINE_SIZE_IN_DWORDS - 1 - i] = ecore_rd(p_hwfn, p_ptt, reg_addr);

	return offset;
}

/* Reads the DBG Bus internal buffer and copy its contents to a buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_int_buf(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  u32 *dump_buf,
								  bool dump)
{
	u32 last_written_line, offset = 0;

	last_written_line = ecore_rd(p_hwfn, p_ptt, DBG_REG_INTR_BUFFER_WR_PTR);

	if (ecore_rd(p_hwfn, p_ptt, DBG_REG_WRAP_ON_INT_BUFFER)) {

		/* Internal buffer was wrapped: first dump from write pointer
		 * to buffer end, then dump from buffer start to write pointer.
		 */
		if (last_written_line < INT_BUF_NUM_OF_LINES - 1)
			offset += ecore_bus_dump_int_buf_range(p_hwfn, p_ptt, dump_buf + offset, dump, last_written_line + 1, INT_BUF_NUM_OF_LINES - 1);
		offset += ecore_bus_dump_int_buf_range(p_hwfn, p_ptt, dump_buf + offset, dump, 0, last_written_line);
	}
	else if (last_written_line) {

		/* Internal buffer wasn't wrapped: dump from buffer start until
		 *  write pointer.
		 */
		if (!ecore_rd(p_hwfn, p_ptt, DBG_REG_INTR_BUFFER_RD_PTR))
			offset += ecore_bus_dump_int_buf_range(p_hwfn, p_ptt, dump_buf + offset, dump, 0, last_written_line);
		else
			DP_NOTICE(p_hwfn, true, "Unexpected Debug Bus error: internal buffer read pointer is not zero\n");
	}

	return offset;
}

/* Reads the specified DBG Bus PCI buffer range and copy it to the specified
 * buffer. Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_pci_buf_range(struct ecore_hwfn *p_hwfn,
										u32 *dump_buf,
										bool dump,
										u32 start_line,
										u32 end_line)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0;

	/* Extract PCI buffer pointer from virtual address */
	void *virt_addr_lo = &dev_data->bus.pci_buf.virt_addr.lo;
	u32 *pci_buf_start = (u32 *)(osal_uintptr_t)*((u64 *)virt_addr_lo);
	u32 *pci_buf, line, i;

	if (!dump)
		return (end_line - start_line + 1) * PCI_BUF_LINE_SIZE_IN_DWORDS;

	for (line = start_line, pci_buf = pci_buf_start + start_line * PCI_BUF_LINE_SIZE_IN_DWORDS;
	line <= end_line;
		line++, offset += PCI_BUF_LINE_SIZE_IN_DWORDS)
		for (i = 0; i < PCI_BUF_LINE_SIZE_IN_DWORDS; i++, pci_buf++)
			dump_buf[offset + s_pci_buf_line_ind[i]] = *pci_buf;

	return offset;
}

/* Copies the DBG Bus PCI buffer to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_pci_buf(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  u32 *dump_buf,
								  bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 next_wr_byte_offset, next_wr_line_offset;
	struct dbg_bus_mem_addr next_wr_phys_addr;
	u32 pci_buf_size_in_lines, offset = 0;

	pci_buf_size_in_lines = dev_data->bus.pci_buf.size / PCI_BUF_LINE_SIZE_IN_BYTES;

	/* Extract write pointer (physical address) */
	next_wr_phys_addr.lo = ecore_rd(p_hwfn, p_ptt, DBG_REG_EXT_BUFFER_WR_PTR);
	next_wr_phys_addr.hi = ecore_rd(p_hwfn, p_ptt, DBG_REG_EXT_BUFFER_WR_PTR + BYTES_IN_DWORD);

	/* Convert write pointer to offset */
	next_wr_byte_offset = ecore_phys_addr_diff(&next_wr_phys_addr, &dev_data->bus.pci_buf.phys_addr);
	if ((next_wr_byte_offset % PCI_BUF_LINE_SIZE_IN_BYTES) || next_wr_byte_offset > dev_data->bus.pci_buf.size)
		return 0;
	next_wr_line_offset = next_wr_byte_offset / PCI_BUF_LINE_SIZE_IN_BYTES;

	/* PCI buffer wrapped: first dump from write pointer to buffer end. */
	if (ecore_rd(p_hwfn, p_ptt, DBG_REG_WRAP_ON_EXT_BUFFER))
		offset += ecore_bus_dump_pci_buf_range(p_hwfn, dump_buf + offset, dump, next_wr_line_offset, pci_buf_size_in_lines - 1);

	/* Dump from buffer start until write pointer */
	if (next_wr_line_offset)
		offset += ecore_bus_dump_pci_buf_range(p_hwfn, dump_buf + offset, dump, 0, next_wr_line_offset - 1);

	return offset;
}

/* Copies the DBG Bus recorded data to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_data(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   u32 *dump_buf,
							   bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	switch (dev_data->bus.target) {
	case DBG_BUS_TARGET_ID_INT_BUF:
		return ecore_bus_dump_int_buf(p_hwfn, p_ptt, dump_buf, dump);
	case DBG_BUS_TARGET_ID_PCI:
		return ecore_bus_dump_pci_buf(p_hwfn, p_ptt, dump_buf, dump);
	default:
		break;
	}

	return 0;
}

/* Frees the Debug Bus PCI buffer */
static void ecore_bus_free_pci_buf(struct ecore_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	dma_addr_t pci_buf_phys_addr;
	void *virt_addr_lo;
	u32 *pci_buf;

	/* Extract PCI buffer pointer from virtual address */
	virt_addr_lo = &dev_data->bus.pci_buf.virt_addr.lo;
	pci_buf = (u32 *)(osal_uintptr_t)*((u64 *)virt_addr_lo);

	if (!dev_data->bus.pci_buf.size)
		return;

	OSAL_MEMCPY(&pci_buf_phys_addr, &dev_data->bus.pci_buf.phys_addr, sizeof(pci_buf_phys_addr));

	OSAL_DMA_FREE_COHERENT(p_hwfn->p_dev, pci_buf, pci_buf_phys_addr, dev_data->bus.pci_buf.size);

	dev_data->bus.pci_buf.size = 0;
}

/* Dumps the list of DBG Bus inputs (blocks/Storms) to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_inputs(struct ecore_hwfn *p_hwfn,
								 u32 *dump_buf,
								 bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char storm_name[8] = "?storm";
	u32 block_id, offset = 0;
	u8 storm_id;

	/* Store storms */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct dbg_bus_storm_data *storm_bus = &dev_data->bus.storms[storm_id];
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (!dev_data->bus.storms[storm_id].enabled)
			continue;

		/* Dump section header */
		storm_name[0] = storm->letter;
		offset += ecore_dump_section_hdr(dump_buf + offset, dump, "bus_input", 3);
		offset += ecore_dump_str_param(dump_buf + offset, dump, "name", storm_name);
		offset += ecore_dump_num_param(dump_buf + offset, dump, "id", storm_bus->hw_id);
		offset += ecore_dump_str_param(dump_buf + offset, dump, "mode", s_storm_mode_defs[storm_bus->mode].name);
	}

	/* Store blocks */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		struct dbg_bus_block_data *block_bus = &dev_data->bus.blocks[block_id];
		struct block_defs *block = s_block_defs[block_id];

		if (!GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
			continue;

		/* Dump section header */
		offset += ecore_dump_section_hdr(dump_buf + offset, dump, "bus_input", 4);
		offset += ecore_dump_str_param(dump_buf + offset, dump, "name", block->name);
		offset += ecore_dump_num_param(dump_buf + offset, dump, "line", block_bus->line_num);
		offset += ecore_dump_num_param(dump_buf + offset, dump, "en", GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK));
		offset += ecore_dump_num_param(dump_buf + offset, dump, "shr", GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT));
	}

	return offset;
}

/* Dumps the Debug Bus header (params, inputs, data header) to the specified
 * buffer. Returns the dumped size in dwords.
 */
static u32 ecore_bus_dump_hdr(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char hw_id_mask_str[16];
	u32 offset = 0;

	if (OSAL_SNPRINTF(hw_id_mask_str, sizeof(hw_id_mask_str), "0x%x", dev_data->bus.hw_id_mask) < 0)
		DP_NOTICE(p_hwfn, true, "Unexpected debug error: invalid HW ID mask\n");

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 5);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "debug-bus");
	offset += ecore_dump_str_param(dump_buf + offset, dump, "wrap-mode", dev_data->bus.one_shot_en ? "one-shot" : "wrap-around");
	offset += ecore_dump_num_param(dump_buf + offset, dump, "hw-dwords", dev_data->bus.hw_dwords);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "hw-id-mask", hw_id_mask_str);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "target", s_dbg_target_names[dev_data->bus.target]);

	offset += ecore_bus_dump_inputs(p_hwfn, dump_buf + offset, dump);

	if (dev_data->bus.target != DBG_BUS_TARGET_ID_NIG) {
		u32 recorded_dwords = 0;
		
		if (dump)
			recorded_dwords = ecore_bus_dump_data(p_hwfn, p_ptt, OSAL_NULL, false);

		offset += ecore_dump_section_hdr(dump_buf + offset, dump, "bus_data", 1);
		offset += ecore_dump_num_param(dump_buf + offset, dump, "size", recorded_dwords);
	}

	return offset;
}

static bool ecore_is_mode_match(struct ecore_hwfn *p_hwfn,
								u16 *modes_buf_offset)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	bool arg1, arg2;
	u8 tree_val;

	/* Get next element from modes tree buffer */
	tree_val = ((u8 *)s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr)[(*modes_buf_offset)++];

	switch (tree_val) {
	case INIT_MODE_OP_NOT:
		return !ecore_is_mode_match(p_hwfn, modes_buf_offset);
	case INIT_MODE_OP_OR:
	case INIT_MODE_OP_AND:
		arg1 = ecore_is_mode_match(p_hwfn, modes_buf_offset);
		arg2 = ecore_is_mode_match(p_hwfn, modes_buf_offset);
		return (tree_val == INIT_MODE_OP_OR) ? (arg1 || arg2) : (arg1 && arg2);
	default: return dev_data->mode_enable[tree_val - MAX_INIT_MODE_OPS] > 0;
	}
}

/* Returns true if the specified entity (indicated by GRC param) should be
 * included in the dump, false otherwise.
 */
static bool ecore_grc_is_included(struct ecore_hwfn *p_hwfn,
								  enum dbg_grc_params grc_param)
{
	return ecore_grc_get_param(p_hwfn, grc_param) > 0;
}

/* Returns true of the specified Storm should be included in the dump, false
 * otherwise.
 */
static bool ecore_grc_is_storm_included(struct ecore_hwfn *p_hwfn,
										enum dbg_storms storm)
{
	return ecore_grc_get_param(p_hwfn, (enum dbg_grc_params)storm) > 0;
}

/* Returns true if the specified memory should be included in the dump, false
 * otherwise.
 */
static bool ecore_grc_is_mem_included(struct ecore_hwfn *p_hwfn,
									  enum block_id block_id,
									  u8 mem_group_id)
{
	struct block_defs *block = s_block_defs[block_id];
	u8 i;

	/* Check Storm match */
	if (block->associated_to_storm &&
		!ecore_grc_is_storm_included(p_hwfn, (enum dbg_storms)block->storm_id))
		return false;

	for (i = 0; i < NUM_BIG_RAM_TYPES; i++) {
		struct big_ram_defs *big_ram = &s_big_ram_defs[i];

		if (mem_group_id == big_ram->mem_group_id || mem_group_id == big_ram->ram_mem_group_id)
			return ecore_grc_is_included(p_hwfn, big_ram->grc_param);
	}

	switch (mem_group_id) {
	case MEM_GROUP_PXP_ILT:
	case MEM_GROUP_PXP_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PXP);
	case MEM_GROUP_RAM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_RAM);
	case MEM_GROUP_PBUF:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PBUF);
	case MEM_GROUP_CAU_MEM:
	case MEM_GROUP_CAU_SB:
	case MEM_GROUP_CAU_PI:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CAU);
	case MEM_GROUP_QM_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_QM);
	case MEM_GROUP_CFC_MEM:
	case MEM_GROUP_CONN_CFC_MEM:
	case MEM_GROUP_TASK_CFC_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CFC) || ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM_CTX);
	case MEM_GROUP_IGU_MEM:
	case MEM_GROUP_IGU_MSIX:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IGU);
	case MEM_GROUP_MULD_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MULD);
	case MEM_GROUP_PRS_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PRS);
	case MEM_GROUP_DMAE_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DMAE);
	case MEM_GROUP_TM_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_TM);
	case MEM_GROUP_SDM_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_SDM);
	case MEM_GROUP_TDIF_CTX:
	case MEM_GROUP_RDIF_CTX:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DIF);
	case MEM_GROUP_CM_MEM:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM);
	case MEM_GROUP_IOR:
		return ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR);
	default:
		return true;
	}
}

/* Stalls all Storms */
static void ecore_grc_stall_storms(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   bool stall)
{
	u32 reg_addr;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (!ecore_grc_is_storm_included(p_hwfn, (enum dbg_storms)storm_id))
			continue;

		reg_addr = s_storm_defs[storm_id].sem_fast_mem_addr + SEM_FAST_REG_STALL_0_BB_K2;
		ecore_wr(p_hwfn, p_ptt, reg_addr, stall ? 1 : 0);
	}

	OSAL_MSLEEP(STALL_DELAY_MS);
}

/* Takes all blocks out of reset */
static void ecore_grc_unreset_blocks(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_val[MAX_DBG_RESET_REGS] = { 0 };
	u32 block_id, i;

	/* Fill reset regs values */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		struct block_defs *block = s_block_defs[block_id];

		if (block->exists[dev_data->chip_id] && block->has_reset_bit && block->unreset)
			reg_val[block->reset_reg] |= (1 << block->reset_bit_offset);
	}

	/* Write reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++) {
		if (!s_reset_regs_defs[i].exists[dev_data->chip_id])
			continue;

		reg_val[i] |= s_reset_regs_defs[i].unreset_val[dev_data->chip_id];

		if (reg_val[i])
			ecore_wr(p_hwfn, p_ptt, s_reset_regs_defs[i].addr + RESET_REG_UNRESET_OFFSET, reg_val[i]);
	}
}

/* Returns the attention block data of the specified block */
static const struct dbg_attn_block_type_data* ecore_get_block_attn_data(enum block_id block_id,
																		enum dbg_attn_type attn_type)
{
	const struct dbg_attn_block *base_attn_block_arr = (const struct dbg_attn_block *)s_dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr;

	return &base_attn_block_arr[block_id].per_type_data[attn_type];
}

/* Returns the attention registers of the specified block */
static const struct dbg_attn_reg* ecore_get_block_attn_regs(enum block_id block_id,
															enum dbg_attn_type attn_type,
															u8 *num_attn_regs)
{
	const struct dbg_attn_block_type_data *block_type_data = ecore_get_block_attn_data(block_id, attn_type);

	*num_attn_regs = block_type_data->num_regs;

	return &((const struct dbg_attn_reg *)s_dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)[block_type_data->regs_offset];
}

/* For each block, clear the status of all parities */
static void ecore_grc_clear_all_prty(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	const struct dbg_attn_reg *attn_reg_arr;
	u8 reg_idx, num_attn_regs;
	u32 block_id;

	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		if (dev_data->block_in_reset[block_id])
			continue;

		attn_reg_arr = ecore_get_block_attn_regs((enum block_id)block_id, ATTN_TYPE_PARITY, &num_attn_regs);

		for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
			const struct dbg_attn_reg *reg_data = &attn_reg_arr[reg_idx];
			u16 modes_buf_offset;
			bool eval_mode;

			/* Check mode */
			eval_mode = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
			modes_buf_offset = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);

			/* If Mode match: clear parity status */
			if (!eval_mode || ecore_is_mode_match(p_hwfn, &modes_buf_offset))
				ecore_rd(p_hwfn, p_ptt, DWORDS_TO_BYTES(reg_data->sts_clr_address));
		}
	}
}

/* Dumps GRC registers section header. Returns the dumped size in dwords.
 * the following parameters are dumped:
 * - count:	 no. of dumped entries
 * - split:	 split type
 * - id:	 split ID (dumped only if split_id >= 0)
 * - param_name: user parameter value (dumped only if param_name != OSAL_NULL
 *		 and param_val != OSAL_NULL).
 */
static u32 ecore_grc_dump_regs_hdr(u32 *dump_buf,
								   bool dump,
								   u32 num_reg_entries,
								   const char *split_type,
								   int split_id,
								   const char *param_name,
								   const char *param_val)
{
	u8 num_params = 2 + (split_id >= 0 ? 1 : 0) + (param_name ? 1 : 0);
	u32 offset = 0;

	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "grc_regs", num_params);
	offset += ecore_dump_num_param(dump_buf + offset, dump, "count", num_reg_entries);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "split", split_type);
	if (split_id >= 0)
		offset += ecore_dump_num_param(dump_buf + offset, dump, "id", split_id);
	if (param_name && param_val)
		offset += ecore_dump_str_param(dump_buf + offset, dump, param_name, param_val);

	return offset;
}

/* Reads the specified registers into the specified buffer.
 * The addr and len arguments are specified in dwords.
 */
void ecore_read_regs(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u32 *buf,
					 u32 addr,
					 u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		buf[i] = ecore_rd(p_hwfn, p_ptt, DWORDS_TO_BYTES(addr + i));
}

/* Dumps the GRC registers in the specified address range.
 * Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 ecore_grc_dump_addr_range(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt,
									 u32 *dump_buf,
									 bool dump,
									 u32 addr,
									 u32 len,
									 bool wide_bus)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	if (!dump)
		return len;

	/* Print log if needed */
	dev_data->num_regs_read += len;
	if (dev_data->num_regs_read >= s_platform_defs[dev_data->platform_id].log_thresh) {
		DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "Dumping %d registers...\n", dev_data->num_regs_read);
		dev_data->num_regs_read = 0;
	}

	/* Try reading using DMAE */
	if (dev_data->use_dmae && (len >= s_platform_defs[dev_data->platform_id].dmae_thresh || (PROTECT_WIDE_BUS && wide_bus))) {
		if (!ecore_dmae_grc2host(p_hwfn, p_ptt, DWORDS_TO_BYTES(addr), (u64)(osal_uintptr_t)(dump_buf), len, OSAL_NULL))
			return len;
		dev_data->use_dmae = 0;
		DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "Failed reading from chip using DMAE, using GRC instead\n");
	}

	/* Read registers */
	ecore_read_regs(p_hwfn, p_ptt, dump_buf, addr, len);

	return len;
}

/* Dumps GRC registers sequence header. Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 ecore_grc_dump_reg_entry_hdr(u32 *dump_buf,
										bool dump,
										u32 addr,
										u32 len)
{
	if (dump)
		*dump_buf = addr | (len << REG_DUMP_LEN_SHIFT);

	return 1;
}

/* Dumps GRC registers sequence. Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 ecore_grc_dump_reg_entry(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									u32 *dump_buf,
									bool dump,
									u32 addr,
									u32 len,
									bool wide_bus)
{
	u32 offset = 0;

	offset += ecore_grc_dump_reg_entry_hdr(dump_buf, dump, addr, len);
	offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, addr, len, wide_bus);

	return offset;
}

/* Dumps GRC registers sequence with skip cycle.
 * Returns the dumped size in dwords.
 * - addr:	start GRC address in dwords
 * - total_len:	total no. of dwords to dump
 * - read_len:	no. consecutive dwords to read
 * - skip_len:	no. of dwords to skip (and fill with zeros)
 */
static u32 ecore_grc_dump_reg_entry_skip(struct ecore_hwfn *p_hwfn,
										 struct ecore_ptt *p_ptt,
										 u32 *dump_buf,
										 bool dump,
										 u32 addr,
										 u32 total_len,
										 u32 read_len,
										 u32 skip_len)
{
	u32 offset = 0, reg_offset = 0;

	offset += ecore_grc_dump_reg_entry_hdr(dump_buf, dump, addr, total_len);

	if (!dump)
		return offset + total_len;

	while (reg_offset < total_len) {
		u32 curr_len = OSAL_MIN_T(u32, read_len, total_len - reg_offset);

		offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, addr, curr_len, false);
		reg_offset += curr_len;
		addr += curr_len;

		if (reg_offset < total_len) {
			curr_len = OSAL_MIN_T(u32, skip_len, total_len - skip_len);
			OSAL_MEMSET(dump_buf + offset, 0, DWORDS_TO_BYTES(curr_len));
			offset += curr_len;
			reg_offset += curr_len;
			addr += curr_len;
		}
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_regs_entries(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   struct dbg_array input_regs_arr,
									   u32 *dump_buf,
									   bool dump,
									   bool block_enable[MAX_BLOCK_ID],
									   u32 *num_dumped_reg_entries)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	*num_dumped_reg_entries = 0;

	while (input_offset < input_regs_arr.size_in_dwords) {
		const struct dbg_dump_cond_hdr *cond_hdr = (const struct dbg_dump_cond_hdr *)&input_regs_arr.ptr[input_offset++];
		u16 modes_buf_offset;
		bool eval_mode;

		/* Check mode/block */
		eval_mode = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = ecore_is_mode_match(p_hwfn, &modes_buf_offset);
		}

		if (!mode_match || !block_enable[cond_hdr->block_id]) {
			input_offset += cond_hdr->data_size;
			continue;
		}

		for (i = 0; i < cond_hdr->data_size; i++, input_offset++) {
			const struct dbg_dump_reg *reg = (const struct dbg_dump_reg *)&input_regs_arr.ptr[input_offset];

			offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump,
				GET_FIELD(reg->data, DBG_DUMP_REG_ADDRESS),
				GET_FIELD(reg->data, DBG_DUMP_REG_LENGTH),
				GET_FIELD(reg->data, DBG_DUMP_REG_WIDE_BUS));
			(*num_dumped_reg_entries)++;
		}
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_split_data(struct ecore_hwfn *p_hwfn,
									 struct ecore_ptt *p_ptt,
									 struct dbg_array input_regs_arr,
									 u32 *dump_buf,
									 bool dump,
									 bool block_enable[MAX_BLOCK_ID],
									 const char *split_type_name,
									 u32 split_id,
									 const char *param_name,
									 const char *param_val)
{
	u32 num_dumped_reg_entries, offset;

	/* Calculate register dump header size (and skip it for now) */
	offset = ecore_grc_dump_regs_hdr(dump_buf, false, 0, split_type_name, split_id, param_name, param_val);

	/* Dump registers */
	offset += ecore_grc_dump_regs_entries(p_hwfn, p_ptt, input_regs_arr, dump_buf + offset, dump, block_enable, &num_dumped_reg_entries);

	/* Write register dump header */
	if (dump && num_dumped_reg_entries > 0)
		ecore_grc_dump_regs_hdr(dump_buf, dump, num_dumped_reg_entries, split_type_name, split_id, param_name, param_val);

	return num_dumped_reg_entries > 0 ? offset : 0;
}

/* Dumps registers according to the input registers array. Returns the dumped
 * size in dwords.
 */
static u32 ecore_grc_dump_registers(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									u32 *dump_buf,
									bool dump,
									bool block_enable[MAX_BLOCK_ID],
									const char *param_name,
									const char *param_val)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct chip_platform_defs *chip_platform;
	u32 offset = 0, input_offset = 0;
	u8 port_id, pf_id, vf_id;

	chip_platform = &s_chip_defs[dev_data->chip_id].per_platform[dev_data->platform_id];

	while (input_offset < s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].size_in_dwords) {
		const struct dbg_dump_split_hdr *split_hdr;
		struct dbg_array curr_input_regs_arr;
		u32 split_data_size;
		u8 split_type_id;

		split_hdr = (const struct dbg_dump_split_hdr *)&s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr[input_offset++];
		split_type_id = GET_FIELD(split_hdr->hdr, DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		split_data_size = GET_FIELD(split_hdr->hdr, DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		curr_input_regs_arr.ptr = &s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr[input_offset];
		curr_input_regs_arr.size_in_dwords = split_data_size;

		switch(split_type_id) {
		case SPLIT_TYPE_NONE:
			offset += ecore_grc_dump_split_data(p_hwfn, p_ptt, curr_input_regs_arr, dump_buf + offset, dump, block_enable, "eng", (u32)(-1), param_name, param_val);
			break;

		case SPLIT_TYPE_PORT:
			for (port_id = 0; port_id < chip_platform->num_ports; port_id++) {
				if (dump)
					ecore_port_pretend(p_hwfn, p_ptt, port_id);
				offset += ecore_grc_dump_split_data(p_hwfn, p_ptt, curr_input_regs_arr, dump_buf + offset, dump, block_enable, "port", port_id, param_name, param_val);
			}
			break;

		case SPLIT_TYPE_PF:
		case SPLIT_TYPE_PORT_PF:
			for (pf_id = 0; pf_id < chip_platform->num_pfs; pf_id++) {
				if (dump)
					ecore_fid_pretend(p_hwfn, p_ptt, (pf_id << PXP_PRETEND_CONCRETE_FID_PFID_SHIFT));
				offset += ecore_grc_dump_split_data(p_hwfn, p_ptt, curr_input_regs_arr, dump_buf + offset, dump, block_enable, "pf", pf_id, param_name, param_val);
			}
			break;

		case SPLIT_TYPE_VF:
			for (vf_id = 0; vf_id < chip_platform->num_vfs; vf_id++) {
				if (dump)
					ecore_fid_pretend(p_hwfn, p_ptt, (1 << PXP_PRETEND_CONCRETE_FID_VFVALID_SHIFT) | (vf_id << PXP_PRETEND_CONCRETE_FID_VFID_SHIFT));
				offset += ecore_grc_dump_split_data(p_hwfn, p_ptt, curr_input_regs_arr, dump_buf + offset, dump, block_enable, "vf", vf_id, param_name, param_val);
			}
			break;

		default:
			break;
		}

		input_offset += split_data_size;
	}

	/* Pretend to original PF */
	if (dump)
		ecore_fid_pretend(p_hwfn, p_ptt, (p_hwfn->rel_pf_id << PXP_PRETEND_CONCRETE_FID_PFID_SHIFT));

	return offset;
}

/* Dump reset registers. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_reset_regs(struct ecore_hwfn *p_hwfn,
	struct ecore_ptt *p_ptt,
	u32 *dump_buf,
	bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i, offset = 0, num_regs = 0;

	/* Calculate header size */
	offset += ecore_grc_dump_regs_hdr(dump_buf, false, 0, "eng", -1, OSAL_NULL, OSAL_NULL);

	/* Write reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++) {
		if (!s_reset_regs_defs[i].exists[dev_data->chip_id])
			continue;

		offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(s_reset_regs_defs[i].addr), 1, false);
		num_regs++;
	}

	/* Write header */
	if (dump)
		ecore_grc_dump_regs_hdr(dump_buf, true, num_regs, "eng", -1, OSAL_NULL, OSAL_NULL);

	return offset;
}

/* Dump registers that are modified during GRC Dump and therefore must be
 * dumped first. Returns the dumped size in dwords.
 */
static u32 ecore_grc_dump_modified_regs(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_id, offset = 0, num_reg_entries = 0;
	const struct dbg_attn_reg *attn_reg_arr;
	u8 storm_id, reg_idx, num_attn_regs;

	/* Calculate header size */
	offset += ecore_grc_dump_regs_hdr(dump_buf, false, 0, "eng", -1, OSAL_NULL, OSAL_NULL);

	/* Write parity registers */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		if (dev_data->block_in_reset[block_id] && dump)
			continue;

		attn_reg_arr = ecore_get_block_attn_regs((enum block_id)block_id, ATTN_TYPE_PARITY, &num_attn_regs);

		for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
			const struct dbg_attn_reg *reg_data = &attn_reg_arr[reg_idx];
			u16 modes_buf_offset;
			bool eval_mode;

			/* Check mode */
			eval_mode = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
			modes_buf_offset = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
			if (eval_mode && !ecore_is_mode_match(p_hwfn, &modes_buf_offset))
				continue;

			/* Mode match: read & dump registers */
			offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump, reg_data->mask_address, 1, false);
			offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump, GET_FIELD(reg_data->data, DBG_ATTN_REG_STS_ADDRESS), 1, false);
			num_reg_entries += 2;
		}
	}

	/* Write Storm stall status registers */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (dev_data->block_in_reset[storm->block_id] && dump)
			continue;

		offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump,
			BYTES_TO_DWORDS(storm->sem_fast_mem_addr + SEM_FAST_REG_STALLED), 1, false);
		num_reg_entries++;
	}

	/* Write header */
	if (dump)
		ecore_grc_dump_regs_hdr(dump_buf, true, num_reg_entries, "eng", -1, OSAL_NULL, OSAL_NULL);

	return offset;
}

/* Dumps registers that can't be represented in the debug arrays */
static u32 ecore_grc_dump_special_regs(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   u32 *dump_buf,
									   bool dump)
{
	u32 offset = 0;

	offset += ecore_grc_dump_regs_hdr(dump_buf, dump, 2, "eng", -1, OSAL_NULL, OSAL_NULL);

	/* Dump R/TDIF_REG_DEBUG_ERROR_INFO_SIZE (every 8'th register should be
	 * skipped).
	 */
	offset += ecore_grc_dump_reg_entry_skip(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(RDIF_REG_DEBUG_ERROR_INFO), RDIF_REG_DEBUG_ERROR_INFO_SIZE, 7, 1);
	offset += ecore_grc_dump_reg_entry_skip(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(TDIF_REG_DEBUG_ERROR_INFO), TDIF_REG_DEBUG_ERROR_INFO_SIZE, 7, 1);

	return offset;
}

/* Dumps a GRC memory header (section and params). Returns the dumped size in
 * dwords. The following parameters are dumped:
 * - name:	   dumped only if it's not OSAL_NULL.
 * - addr:	   in dwords, dumped only if name is OSAL_NULL.
 * - len:	   in dwords, always dumped.
 * - width:	   dumped if it's not zero.
 * - packed:	   dumped only if it's not false.
 * - mem_group:	   always dumped.
 * - is_storm:	   true only if the memory is related to a Storm.
 * - storm_letter: valid only if is_storm is true.
 *
 */
static u32 ecore_grc_dump_mem_hdr(struct ecore_hwfn *p_hwfn,
								  u32 *dump_buf,
								  bool dump,
								  const char *name,
								  u32 addr,
								  u32 len,
								  u32 bit_width,
								  bool packed,
								  const char *mem_group,
								  bool is_storm,
								  char storm_letter)
{
	u8 num_params = 3;
	u32 offset = 0;
	char buf[64];

	if (!len)
		DP_NOTICE(p_hwfn, true, "Unexpected GRC Dump error: dumped memory size must be non-zero\n");

	if (bit_width)
		num_params++;
	if (packed)
		num_params++;

	/* Dump section header */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "grc_mem", num_params);

	if (name) {

		/* Dump name */
		if (is_storm) {
			OSAL_STRCPY(buf, "?STORM_");
			buf[0] = storm_letter;
			OSAL_STRCPY(buf + OSAL_STRLEN(buf), name);
		}
		else {
			OSAL_STRCPY(buf, name);
		}

		offset += ecore_dump_str_param(dump_buf + offset, dump, "name", buf);
	}
	else {

		/* Dump address */
		u32 addr_in_bytes = DWORDS_TO_BYTES(addr);

		offset += ecore_dump_num_param(dump_buf + offset, dump, "addr", addr_in_bytes);
	}

	/* Dump len */
	offset += ecore_dump_num_param(dump_buf + offset, dump, "len", len);

	/* Dump bit width */
	if (bit_width)
		offset += ecore_dump_num_param(dump_buf + offset, dump, "width", bit_width);

	/* Dump packed */
	if (packed)
		offset += ecore_dump_num_param(dump_buf + offset, dump, "packed", 1);

	/* Dump reg type */
	if (is_storm) {
		OSAL_STRCPY(buf, "?STORM_");
		buf[0] = storm_letter;
		OSAL_STRCPY(buf + OSAL_STRLEN(buf), mem_group);
	}
	else {
		OSAL_STRCPY(buf, mem_group);
	}

	offset += ecore_dump_str_param(dump_buf + offset, dump, "type", buf);

	return offset;
}

/* Dumps a single GRC memory. If name is OSAL_NULL, the memory is stored by address.
 * Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 ecore_grc_dump_mem(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump,
							  const char *name,
							  u32 addr,
							  u32 len,
							  bool wide_bus,
							  u32 bit_width,
							  bool packed,
							  const char *mem_group,
							  bool is_storm,
							  char storm_letter)
{
	u32 offset = 0;

	offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, name, addr, len, bit_width, packed, mem_group, is_storm, storm_letter);
	offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, addr, len, wide_bus);

	return offset;
}

/* Dumps GRC memories entries. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_mem_entries(struct ecore_hwfn *p_hwfn,
									  struct ecore_ptt *p_ptt,
									  struct dbg_array input_mems_arr,
									  u32 *dump_buf,
									  bool dump)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	while (input_offset < input_mems_arr.size_in_dwords) {
		const struct dbg_dump_cond_hdr *cond_hdr;
		u16 modes_buf_offset;
		u32 num_entries;
		bool eval_mode;

		cond_hdr = (const struct dbg_dump_cond_hdr *)&input_mems_arr.ptr[input_offset++];
		num_entries = cond_hdr->data_size / MEM_DUMP_ENTRY_SIZE_DWORDS;

		/* Check required mode */
		eval_mode = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = ecore_is_mode_match(p_hwfn, &modes_buf_offset);
		}

		if (!mode_match) {
			input_offset += cond_hdr->data_size;
			continue;
		}

		for (i = 0; i < num_entries; i++, input_offset += MEM_DUMP_ENTRY_SIZE_DWORDS) {
			const struct dbg_dump_mem *mem = (const struct dbg_dump_mem *)&input_mems_arr.ptr[input_offset];
			u8 mem_group_id = GET_FIELD(mem->dword0, DBG_DUMP_MEM_MEM_GROUP_ID);
			bool is_storm = false, mem_wide_bus;
			char storm_letter = 'a';
			u32 mem_addr, mem_len;

			if (mem_group_id >= MEM_GROUPS_NUM) {
				DP_NOTICE(p_hwfn, true, "Invalid mem_group_id\n");
				return 0;
			}

			if (!ecore_grc_is_mem_included(p_hwfn, (enum block_id)cond_hdr->block_id, mem_group_id))
				continue;

			mem_addr = GET_FIELD(mem->dword0, DBG_DUMP_MEM_ADDRESS);
			mem_len = GET_FIELD(mem->dword1, DBG_DUMP_MEM_LENGTH);
			mem_wide_bus = GET_FIELD(mem->dword1, DBG_DUMP_MEM_WIDE_BUS);

			/* Update memory length for CCFC/TCFC memories
			 * according to number of LCIDs/LTIDs.
			 */
			if (mem_group_id == MEM_GROUP_CONN_CFC_MEM) {
				if (mem_len % MAX_LCIDS) {
					DP_NOTICE(p_hwfn, true, "Invalid CCFC connection memory size\n");
					return 0;
				}

				mem_len = ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LCIDS) * (mem_len / MAX_LCIDS);
			}
			else if (mem_group_id == MEM_GROUP_TASK_CFC_MEM) {
				if (mem_len % MAX_LTIDS) {
					DP_NOTICE(p_hwfn, true, "Invalid TCFC task memory size\n");
					return 0;
				}

				mem_len = ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LTIDS) * (mem_len / MAX_LTIDS);
			}

			/* If memory is associated with Storm, udpate Storm
			 * details.
			 */
			if (s_block_defs[cond_hdr->block_id]->associated_to_storm) {
				is_storm = true;
				storm_letter = s_storm_defs[s_block_defs[cond_hdr->block_id]->storm_id].letter;
			}

			/* Dump memory */
			offset += ecore_grc_dump_mem(p_hwfn, p_ptt, dump_buf + offset, dump, OSAL_NULL, mem_addr, mem_len, mem_wide_bus,
				0, false, s_mem_group_names[mem_group_id], is_storm, storm_letter);
		}
	}

	return offset;
}

/* Dumps GRC memories according to the input array dump_mem.
 * Returns the dumped size in dwords.
 */
static u32 ecore_grc_dump_memories(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   bool dump)
{
	u32 offset = 0, input_offset = 0;

	while (input_offset < s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].size_in_dwords) {
		const struct dbg_dump_split_hdr *split_hdr;
		struct dbg_array curr_input_mems_arr;
		u32 split_data_size;
		u8 split_type_id;

		split_hdr = (const struct dbg_dump_split_hdr *)&s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr[input_offset++];
		split_type_id = GET_FIELD(split_hdr->hdr, DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		split_data_size = GET_FIELD(split_hdr->hdr, DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		curr_input_mems_arr.ptr = &s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr[input_offset];
		curr_input_mems_arr.size_in_dwords = split_data_size;

		switch (split_type_id) {
		case SPLIT_TYPE_NONE:
			offset += ecore_grc_dump_mem_entries(p_hwfn, p_ptt, curr_input_mems_arr, dump_buf + offset, dump);
			break;

		default:
			DP_NOTICE(p_hwfn, true, "Dumping split memories is currently not supported\n");
			break;
		}

		input_offset += split_data_size;
	}

	return offset;
}

/* Dumps GRC context data for the specified Storm.
 * Returns the dumped size in dwords.
 * The lid_size argument is specified in quad-regs.
 */
static u32 ecore_grc_dump_ctx_data(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   bool dump,
								   const char *name,
								   u32 num_lids,
								   u32 lid_size,
								   u32 rd_reg_addr,
								   u8 storm_id)
{
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 i, lid, total_size, offset = 0;

	if (!lid_size)
		return 0;

	lid_size *= BYTES_IN_DWORD;
	total_size = num_lids * lid_size;

	offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, name, 0, total_size, lid_size * 32, false, name, true, storm->letter);

	if (!dump)
		return offset + total_size;

	/* Dump context data */
	for (lid = 0; lid < num_lids; lid++) {
		for (i = 0; i < lid_size; i++, offset++) {
			ecore_wr(p_hwfn, p_ptt, storm->cm_ctx_wr_addr, (i << 9) | lid);
			*(dump_buf + offset) = ecore_rd(p_hwfn, p_ptt, rd_reg_addr);
		}
	}

	return offset;
}

/* Dumps GRC contexts. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_ctx(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	u32 offset = 0;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (!ecore_grc_is_storm_included(p_hwfn, (enum dbg_storms)storm_id))
			continue;

		/* Dump Conn AG context size */
		offset += ecore_grc_dump_ctx_data(p_hwfn, p_ptt, dump_buf + offset, dump, "CONN_AG_CTX", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LCIDS),
			storm->cm_conn_ag_ctx_lid_size, storm->cm_conn_ag_ctx_rd_addr, storm_id);

		/* Dump Conn ST context size */
		offset += ecore_grc_dump_ctx_data(p_hwfn, p_ptt, dump_buf + offset, dump, "CONN_ST_CTX", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LCIDS),
			storm->cm_conn_st_ctx_lid_size, storm->cm_conn_st_ctx_rd_addr, storm_id);

		/* Dump Task AG context size */
		offset += ecore_grc_dump_ctx_data(p_hwfn, p_ptt, dump_buf + offset, dump, "TASK_AG_CTX", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LTIDS),
			storm->cm_task_ag_ctx_lid_size, storm->cm_task_ag_ctx_rd_addr, storm_id);

		/* Dump Task ST context size */
		offset += ecore_grc_dump_ctx_data(p_hwfn, p_ptt, dump_buf + offset, dump, "TASK_ST_CTX", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LTIDS),
			storm->cm_task_st_ctx_lid_size, storm->cm_task_st_ctx_rd_addr, storm_id);
	}

	return offset;
}

/* Dumps GRC IORs data. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_iors(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   u32 *dump_buf,
							   bool dump)
{
	char buf[10] = "IOR_SET_?";
	u32 addr, offset = 0;
	u8 storm_id, set_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (!ecore_grc_is_storm_included(p_hwfn, (enum dbg_storms)storm_id))
			continue;

		for (set_id = 0; set_id < NUM_IOR_SETS; set_id++) {
			addr = BYTES_TO_DWORDS(storm->sem_fast_mem_addr + SEM_FAST_REG_STORM_REG_FILE) + IOR_SET_OFFSET(set_id);
			buf[OSAL_STRLEN(buf) - 1] = '0' + set_id;
			offset += ecore_grc_dump_mem(p_hwfn, p_ptt, dump_buf + offset, dump, buf, addr, IORS_PER_SET, false, 32, false, "ior", true, storm->letter);
		}
	}

	return offset;
}

/* Dump VFC CAM. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_vfc_cam(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  u32 *dump_buf,
								  bool dump,
								  u8 storm_id)
{
	u32 total_size = VFC_CAM_NUM_ROWS * VFC_CAM_RESP_DWORDS;
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 cam_addr[VFC_CAM_ADDR_DWORDS] = { 0 };
	u32 cam_cmd[VFC_CAM_CMD_DWORDS] = { 0 };
	u32 row, i, offset = 0;

	offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, "vfc_cam", 0, total_size, 256, false, "vfc_cam", true, storm->letter);

	if (!dump)
		return offset + total_size;

	/* Prepare CAM address */
	SET_VAR_FIELD(cam_addr, VFC_CAM_ADDR, OP, VFC_OPCODE_CAM_RD);

	for (row = 0; row < VFC_CAM_NUM_ROWS; row++, offset += VFC_CAM_RESP_DWORDS) {

		/* Write VFC CAM command */
		SET_VAR_FIELD(cam_cmd, VFC_CAM_CMD, ROW, row);
		ARR_REG_WR(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_DATA_WR, cam_cmd, VFC_CAM_CMD_DWORDS);

		/* Write VFC CAM address */
		ARR_REG_WR(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_ADDR, cam_addr, VFC_CAM_ADDR_DWORDS);

		/* Read VFC CAM read response */
		ARR_REG_RD(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_DATA_RD, dump_buf + offset, VFC_CAM_RESP_DWORDS);
	}

	return offset;
}

/* Dump VFC RAM. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_vfc_ram(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  u32 *dump_buf,
								  bool dump,
								  u8 storm_id,
								  struct vfc_ram_defs *ram_defs)
{
	u32 total_size = ram_defs->num_rows * VFC_RAM_RESP_DWORDS;
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 ram_addr[VFC_RAM_ADDR_DWORDS] = { 0 };
	u32 ram_cmd[VFC_RAM_CMD_DWORDS] = { 0 };
	u32 row, i, offset = 0;

	offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, ram_defs->mem_name, 0, total_size, 256, false, ram_defs->type_name, true, storm->letter);

	/* Prepare RAM address */
	SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, OP, VFC_OPCODE_RAM_RD);

	if (!dump)
		return offset + total_size;

	for (row = ram_defs->base_row; row < ram_defs->base_row + ram_defs->num_rows; row++, offset += VFC_RAM_RESP_DWORDS) {

		/* Write VFC RAM command */
		ARR_REG_WR(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_DATA_WR, ram_cmd, VFC_RAM_CMD_DWORDS);

		/* Write VFC RAM address */
		SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, ROW, row);
		ARR_REG_WR(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_ADDR, ram_addr, VFC_RAM_ADDR_DWORDS);

		/* Read VFC RAM read response */
		ARR_REG_RD(p_hwfn, p_ptt, storm->sem_fast_mem_addr + SEM_FAST_REG_VFC_DATA_RD, dump_buf + offset, VFC_RAM_RESP_DWORDS);
	}

	return offset;
}

/* Dumps GRC VFC data. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_vfc(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 storm_id, i;
	u32 offset = 0;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (!ecore_grc_is_storm_included(p_hwfn, (enum dbg_storms)storm_id) ||
			!s_storm_defs[storm_id].has_vfc ||
			(storm_id == DBG_PSTORM_ID && dev_data->platform_id != PLATFORM_ASIC))
			continue;

		/* Read CAM */
		offset += ecore_grc_dump_vfc_cam(p_hwfn, p_ptt, dump_buf + offset, dump, storm_id);

		/* Read RAM */
		for (i = 0; i < NUM_VFC_RAM_TYPES; i++)
			offset += ecore_grc_dump_vfc_ram(p_hwfn, p_ptt, dump_buf + offset, dump, storm_id, &s_vfc_ram_defs[i]);
	}

	return offset;
}

/* Dumps GRC RSS data. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_rss(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0;
	u8 rss_mem_id;

	for (rss_mem_id = 0; rss_mem_id < NUM_RSS_MEM_TYPES; rss_mem_id++) {
		u32 rss_addr, num_entries, total_dwords;
		struct rss_mem_defs *rss_defs;
		bool packed;

		rss_defs = &s_rss_mem_defs[rss_mem_id];
		rss_addr = rss_defs->addr;
		num_entries = rss_defs->num_entries[dev_data->chip_id];
		total_dwords = (num_entries * rss_defs->entry_width) / 32;
		packed = (rss_defs->entry_width == 16);

		offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, rss_defs->mem_name, 0, total_dwords,
			rss_defs->entry_width, packed, rss_defs->type_name, false, 0);

		/* Dump RSS data */
		if (!dump) {
			offset += total_dwords;
			continue;
		}

		while (total_dwords) {
			u32 num_dwords_to_read = OSAL_MIN_T(u32, RSS_REG_RSS_RAM_DATA_SIZE, total_dwords);
			ecore_wr(p_hwfn, p_ptt, RSS_REG_RSS_RAM_ADDR, rss_addr);
			offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(RSS_REG_RSS_RAM_DATA), num_dwords_to_read, false);
			total_dwords -= num_dwords_to_read;
			rss_addr++;
		}
	}

	return offset;
}

/* Dumps GRC Big RAM. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_big_ram(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  u32 *dump_buf,
								  bool dump,
								  u8 big_ram_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_size, ram_size, offset = 0, reg_val, i;
	char mem_name[12] = "???_BIG_RAM";
	char type_name[8] = "???_RAM";
	struct big_ram_defs *big_ram;

	big_ram = &s_big_ram_defs[big_ram_id];
	ram_size = big_ram->ram_size[dev_data->chip_id];

	reg_val = ecore_rd(p_hwfn, p_ptt, big_ram->is_256b_reg_addr);
	block_size = reg_val & (1 << big_ram->is_256b_bit_offset[dev_data->chip_id]) ? 256 : 128;

	OSAL_STRNCPY(type_name, big_ram->instance_name, OSAL_STRLEN(big_ram->instance_name));
	OSAL_STRNCPY(mem_name, big_ram->instance_name, OSAL_STRLEN(big_ram->instance_name));

	/* Dump memory header */
	offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, mem_name, 0, ram_size, block_size * 8, false, type_name, false, 0);

	/* Read and dump Big RAM data */
	if (!dump)
		return offset + ram_size;

	/* Dump Big RAM */
	for (i = 0; i < DIV_ROUND_UP(ram_size, BRB_REG_BIG_RAM_DATA_SIZE); i++) {
		ecore_wr(p_hwfn, p_ptt, big_ram->addr_reg_addr, i);
		offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(big_ram->data_reg_addr), BRB_REG_BIG_RAM_DATA_SIZE, false);
	}

	return offset;
}

static u32 ecore_grc_dump_mcp(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	bool block_enable[MAX_BLOCK_ID] = { 0 };
	bool halted = false;
	u32 offset = 0;

	/* Halt MCP */
	if (dump && dev_data->platform_id == PLATFORM_ASIC && !ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP)) {
		halted = !ecore_mcp_halt(p_hwfn, p_ptt);
		if (!halted)
			DP_NOTICE(p_hwfn, false, "MCP halt failed!\n");
	}

	/* Dump MCP scratchpad */
	offset += ecore_grc_dump_mem(p_hwfn, p_ptt, dump_buf + offset, dump, OSAL_NULL, BYTES_TO_DWORDS(MCP_REG_SCRATCH),
		ECORE_IS_E5(p_hwfn->p_dev) ? MCP_REG_SCRATCH_SIZE_E5 : MCP_REG_SCRATCH_SIZE_BB_K2, false, 0, false, "MCP", false, 0);

	/* Dump MCP cpu_reg_file */
	offset += ecore_grc_dump_mem(p_hwfn, p_ptt, dump_buf + offset, dump, OSAL_NULL, BYTES_TO_DWORDS(MCP_REG_CPU_REG_FILE),
		MCP_REG_CPU_REG_FILE_SIZE, false, 0, false, "MCP", false, 0);

	/* Dump MCP registers */
	block_enable[BLOCK_MCP] = true;
	offset += ecore_grc_dump_registers(p_hwfn, p_ptt, dump_buf + offset, dump, block_enable, "block", "MCP");

	/* Dump required non-MCP registers */
	offset += ecore_grc_dump_regs_hdr(dump_buf + offset, dump, 1, "eng", -1, "block", "MCP");
	offset += ecore_grc_dump_reg_entry(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(MISC_REG_SHARED_MEM_ADDR), 1, false);

	/* Release MCP */
	if (halted && ecore_mcp_resume(p_hwfn, p_ptt))
		DP_NOTICE(p_hwfn, false, "Failed to resume MCP after halt!\n");

	return offset;
}

/* Dumps the tbus indirect memory for all PHYs. */
static u32 ecore_grc_dump_phy(struct ecore_hwfn *p_hwfn,
							  struct ecore_ptt *p_ptt,
							  u32 *dump_buf,
							  bool dump)
{
	u32 offset = 0, tbus_lo_offset, tbus_hi_offset;
	char mem_name[32];
	u8 phy_id;

	for (phy_id = 0; phy_id < OSAL_ARRAY_SIZE(s_phy_defs); phy_id++) {
		u32 addr_lo_addr, addr_hi_addr, data_lo_addr, data_hi_addr;
		struct phy_defs *phy_defs;
		u8 *bytes_buf;

		phy_defs = &s_phy_defs[phy_id];
		addr_lo_addr = phy_defs->base_addr + phy_defs->tbus_addr_lo_addr;
		addr_hi_addr = phy_defs->base_addr + phy_defs->tbus_addr_hi_addr;
		data_lo_addr = phy_defs->base_addr + phy_defs->tbus_data_lo_addr;
		data_hi_addr = phy_defs->base_addr + phy_defs->tbus_data_hi_addr;

		if (OSAL_SNPRINTF(mem_name, sizeof(mem_name), "tbus_%s", phy_defs->phy_name) < 0)
			DP_NOTICE(p_hwfn, true, "Unexpected debug error: invalid PHY memory name\n");

		offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, mem_name, 0, PHY_DUMP_SIZE_DWORDS, 16, true, mem_name, false, 0);

		if (!dump) {
			offset += PHY_DUMP_SIZE_DWORDS;
			continue;
		}

		bytes_buf = (u8 *)(dump_buf + offset);
		for (tbus_hi_offset = 0; tbus_hi_offset < (NUM_PHY_TBUS_ADDRESSES >> 8); tbus_hi_offset++) {
			ecore_wr(p_hwfn, p_ptt, addr_hi_addr, tbus_hi_offset);
			for (tbus_lo_offset = 0; tbus_lo_offset < 256; tbus_lo_offset++) {
				ecore_wr(p_hwfn, p_ptt, addr_lo_addr, tbus_lo_offset);
				*(bytes_buf++) = (u8)ecore_rd(p_hwfn, p_ptt, data_lo_addr);
				*(bytes_buf++) = (u8)ecore_rd(p_hwfn, p_ptt, data_hi_addr);
			}
		}

		offset += PHY_DUMP_SIZE_DWORDS;
	}

	return offset;
}

static void ecore_config_dbg_line(struct ecore_hwfn *p_hwfn,
								  struct ecore_ptt *p_ptt,
								  enum block_id block_id,
								  u8 line_id,
								  u8 enable_mask,
								  u8 right_shift,
								  u8 force_valid_mask,
								  u8 force_frame_mask)
{
	struct block_defs *block = s_block_defs[block_id];

	ecore_wr(p_hwfn, p_ptt, block->dbg_select_addr, line_id);
	ecore_wr(p_hwfn, p_ptt, block->dbg_enable_addr, enable_mask);
	ecore_wr(p_hwfn, p_ptt, block->dbg_shift_addr, right_shift);
	ecore_wr(p_hwfn, p_ptt, block->dbg_force_valid_addr, force_valid_mask);
	ecore_wr(p_hwfn, p_ptt, block->dbg_force_frame_addr, force_frame_mask);
}

/* Dumps Static Debug data. Returns the dumped size in dwords. */
static u32 ecore_grc_dump_static_debug(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   u32 *dump_buf,
									   bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_id, line_id, offset = 0;

	/* don't dump static debug if a debug bus recording is in progress */
	if (dump && ecore_rd(p_hwfn, p_ptt, DBG_REG_DBG_BLOCK_ON))
		return 0;

	if (dump) {
		/* Disable all blocks debug output */
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			struct block_defs *block = s_block_defs[block_id];

			if (block->dbg_client_id[dev_data->chip_id] != MAX_DBG_BUS_CLIENTS)
				ecore_wr(p_hwfn, p_ptt, block->dbg_enable_addr, 0);
		}

		ecore_bus_reset_dbg_block(p_hwfn, p_ptt);
		ecore_bus_set_framing_mode(p_hwfn, p_ptt, DBG_BUS_FRAME_MODE_8HW_0ST);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_DEBUG_TARGET, DBG_BUS_TARGET_ID_INT_BUF);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_FULL_MODE, 1);
		ecore_bus_enable_dbg_block(p_hwfn, p_ptt, true);
	}

	/* Dump all static debug lines for each relevant block */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		struct block_defs *block = s_block_defs[block_id];
		struct dbg_bus_block *block_desc;
		u32 block_dwords;

		if (block->dbg_client_id[dev_data->chip_id] == MAX_DBG_BUS_CLIENTS)
			continue;

		block_desc = get_dbg_bus_block_desc(p_hwfn, (enum block_id)block_id);
		block_dwords = NUM_DBG_LINES(block_desc) * STATIC_DEBUG_LINE_DWORDS;

		/* Dump static section params */
		offset += ecore_grc_dump_mem_hdr(p_hwfn, dump_buf + offset, dump, block->name, 0, block_dwords, 32, false, "STATIC", false, 0);

		if (!dump) {
			offset += block_dwords;
			continue;
		}

		/* If all lines are invalid - dump zeros */
		if (dev_data->block_in_reset[block_id]) {
			OSAL_MEMSET(dump_buf + offset, 0, DWORDS_TO_BYTES(block_dwords));
			offset += block_dwords;
			continue;
		}

		/* Enable block's client */
		ecore_bus_enable_clients(p_hwfn, p_ptt, 1 << block->dbg_client_id[dev_data->chip_id]);
		for (line_id = 0; line_id < (u32)NUM_DBG_LINES(block_desc); line_id++) {

			/* Configure debug line ID */
			ecore_config_dbg_line(p_hwfn, p_ptt, (enum block_id)block_id, (u8)line_id, 0xf, 0, 0, 0);

			/* Read debug line info */
			offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(DBG_REG_CALENDAR_OUT_DATA), STATIC_DEBUG_LINE_DWORDS, true);
		}

		/* Disable block's client and debug output */
		ecore_bus_enable_clients(p_hwfn, p_ptt, 0);
		ecore_wr(p_hwfn, p_ptt, block->dbg_enable_addr, 0);
	}

	if (dump) {
		ecore_bus_enable_dbg_block(p_hwfn, p_ptt, false);
		ecore_bus_enable_clients(p_hwfn, p_ptt, 0);
	}

	return offset;
}

/* Performs GRC Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static enum dbg_status ecore_grc_dump(struct ecore_hwfn *p_hwfn,
									  struct ecore_ptt *p_ptt,
									  u32 *dump_buf,
									  bool dump,
									  u32 *num_dumped_dwords)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	bool is_asic, parities_masked = false;
	u8 i, port_mode = 0;
	u32 offset = 0;

	is_asic = dev_data->platform_id == PLATFORM_ASIC;

	*num_dumped_dwords = 0;

	if (dump) {

		/* Find port mode */
		switch (ecore_rd(p_hwfn, p_ptt, MISC_REG_PORT_MODE)) {
		case 0: port_mode = 1; break;
		case 1: port_mode = 2; break;
		case 2: port_mode = 4; break;
		}

		/* Update reset state */
		ecore_update_blocks_reset_state(p_hwfn, p_ptt);
	}

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 4);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "grc-dump");
	offset += ecore_dump_num_param(dump_buf + offset, dump, "num-lcids", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LCIDS));
	offset += ecore_dump_num_param(dump_buf + offset, dump, "num-ltids", ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NUM_LTIDS));
	offset += ecore_dump_num_param(dump_buf + offset, dump, "num-ports", port_mode);

	/* Dump reset registers (dumped before taking blocks out of reset ) */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS))
		offset += ecore_grc_dump_reset_regs(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Take all blocks out of reset (using reset registers) */
	if (dump) {
		ecore_grc_unreset_blocks(p_hwfn, p_ptt);
		ecore_update_blocks_reset_state(p_hwfn, p_ptt);
	}

	/* Disable all parities using MFW command */
	if (dump && is_asic && !ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP)) {
			parities_masked = !ecore_mcp_mask_parities(p_hwfn, p_ptt, 1);
			if (!parities_masked) {
				DP_NOTICE(p_hwfn, false, "Failed to mask parities using MFW\n");
				if (ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_PARITY_SAFE))
					return DBG_STATUS_MCP_COULD_NOT_MASK_PRTY;
			}
		}

	/* Dump modified registers (dumped before modifying them) */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS))
		offset += ecore_grc_dump_modified_regs(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Stall storms */
	if (dump && (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR) || ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_VFC)))
		ecore_grc_stall_storms(p_hwfn, p_ptt, true);

	/* Dump all regs  */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS)) {
		bool block_enable[MAX_BLOCK_ID];

		/* Dump all blocks except MCP */
		for (i = 0; i < MAX_BLOCK_ID; i++)
			block_enable[i] = true;
		block_enable[BLOCK_MCP] = false;
		offset += ecore_grc_dump_registers(p_hwfn, p_ptt, dump_buf + offset, dump, block_enable, OSAL_NULL, OSAL_NULL);

		/* Dump special registers */
		offset += ecore_grc_dump_special_regs(p_hwfn, p_ptt, dump_buf + offset, dump);
	}

	/* Dump memories */
	offset += ecore_grc_dump_memories(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump MCP */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MCP))
		offset += ecore_grc_dump_mcp(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump context */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM_CTX))
		offset += ecore_grc_dump_ctx(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump RSS memories */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_RSS))
		offset += ecore_grc_dump_rss(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump Big RAM */
	for (i = 0; i < NUM_BIG_RAM_TYPES; i++)
		if (ecore_grc_is_included(p_hwfn, s_big_ram_defs[i].grc_param))
			offset += ecore_grc_dump_big_ram(p_hwfn, p_ptt, dump_buf + offset, dump, i);

	/* Dump IORs */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR))
		offset += ecore_grc_dump_iors(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump VFC */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_VFC))
		offset += ecore_grc_dump_vfc(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump PHY tbus */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PHY) && dev_data->chip_id == CHIP_K2 && dev_data->platform_id == PLATFORM_ASIC)
		offset += ecore_grc_dump_phy(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump static debug data  */
	if (ecore_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_STATIC) && dev_data->bus.state == DBG_BUS_STATE_IDLE)
		offset += ecore_grc_dump_static_debug(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	if (dump) {

		/* Unstall storms */
		if (ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_UNSTALL))
			ecore_grc_stall_storms(p_hwfn, p_ptt, false);

		/* Clear parity status */
		if (is_asic)
			ecore_grc_clear_all_prty(p_hwfn, p_ptt);

		/* Enable all parities using MFW command */
		if (parities_masked)
			ecore_mcp_mask_parities(p_hwfn, p_ptt, 0);
	}

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Writes the specified failing Idle Check rule to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_idle_chk_dump_failure(struct ecore_hwfn *p_hwfn,
									   struct ecore_ptt *p_ptt,
									   u32 *dump_buf,
									   bool dump,
									   u16 rule_id,
									   const struct dbg_idle_chk_rule *rule,
									   u16 fail_entry_id,
									   u32 *cond_reg_values)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	const struct dbg_idle_chk_cond_reg *cond_regs;
	const struct dbg_idle_chk_info_reg *info_regs;
	u32 i, next_reg_offset = 0, offset = 0;
	struct dbg_idle_chk_result_hdr *hdr;
	const union dbg_idle_chk_reg *regs;
	u8 reg_id;

	hdr = (struct dbg_idle_chk_result_hdr *)dump_buf;
	regs = &((const union dbg_idle_chk_reg *)s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr)[rule->reg_offset];
	cond_regs = &regs[0].cond_reg;
	info_regs = &regs[rule->num_cond_regs].info_reg;

	/* Dump rule data */
	if (dump) {
		OSAL_MEMSET(hdr, 0, sizeof(*hdr));
		hdr->rule_id = rule_id;
		hdr->mem_entry_id = fail_entry_id;
		hdr->severity = rule->severity;
		hdr->num_dumped_cond_regs = rule->num_cond_regs;
	}

	offset += IDLE_CHK_RESULT_HDR_DWORDS;

	/* Dump condition register values */
	for (reg_id = 0; reg_id < rule->num_cond_regs; reg_id++) {
		const struct dbg_idle_chk_cond_reg *reg = &cond_regs[reg_id];
		struct dbg_idle_chk_result_reg_hdr *reg_hdr;

		reg_hdr = (struct dbg_idle_chk_result_reg_hdr *)(dump_buf + offset);

		/* Write register header */
		if (!dump) {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS + reg->entry_size;
			continue;
		}

		offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
		OSAL_MEMSET(reg_hdr, 0, sizeof(*reg_hdr));
		reg_hdr->start_entry = reg->start_entry;
		reg_hdr->size = reg->entry_size;
		SET_FIELD(reg_hdr->data, DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM, reg->num_entries > 1 || reg->start_entry > 0 ? 1 : 0);
		SET_FIELD(reg_hdr->data, DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID, reg_id);

		/* Write register values */
		for (i = 0; i < reg_hdr->size; i++, next_reg_offset++, offset++)
			dump_buf[offset] = cond_reg_values[next_reg_offset];
	}

	/* Dump info register values */
	for (reg_id = 0; reg_id < rule->num_info_regs; reg_id++) {
		const struct dbg_idle_chk_info_reg *reg = &info_regs[reg_id];
		u32 block_id;

		/* Check if register's block is in reset */
		if (!dump) {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS + reg->size;
			continue;
		}

		block_id = GET_FIELD(reg->data, DBG_IDLE_CHK_INFO_REG_BLOCK_ID);
		if (block_id >= MAX_BLOCK_ID) {
			DP_NOTICE(p_hwfn, true, "Invalid block_id\n");
			return 0;
		}

		if (!dev_data->block_in_reset[block_id]) {
			struct dbg_idle_chk_result_reg_hdr *reg_hdr;
			bool wide_bus, eval_mode, mode_match = true;
			u16 modes_buf_offset;
			u32 addr;

			reg_hdr = (struct dbg_idle_chk_result_reg_hdr *)(dump_buf + offset);

			/* Check mode */
			eval_mode = GET_FIELD(reg->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
			if (eval_mode) {
				modes_buf_offset = GET_FIELD(reg->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
				mode_match = ecore_is_mode_match(p_hwfn, &modes_buf_offset);
			}

			if (!mode_match)
				continue;

			addr = GET_FIELD(reg->data, DBG_IDLE_CHK_INFO_REG_ADDRESS);
			wide_bus = GET_FIELD(reg->data, DBG_IDLE_CHK_INFO_REG_WIDE_BUS);

			/* Write register header */
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
			hdr->num_dumped_info_regs++;
			OSAL_MEMSET(reg_hdr, 0, sizeof(*reg_hdr));
			reg_hdr->size = reg->size;
			SET_FIELD(reg_hdr->data, DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID, rule->num_cond_regs + reg_id);

			/* Write register values */
			offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, addr, reg->size, wide_bus);
		}
	}

	return offset;
}

/* Dumps idle check rule entries. Returns the dumped size in dwords. */
static u32 ecore_idle_chk_dump_rule_entries(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											u32 *dump_buf,
											bool dump,
											const struct dbg_idle_chk_rule *input_rules,
											u32 num_input_rules,
											u32 *num_failing_rules)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 cond_reg_values[IDLE_CHK_MAX_ENTRIES_SIZE];
	u32 i, offset = 0;
	u16 entry_id;
	u8 reg_id;

	*num_failing_rules = 0;

	for (i = 0; i < num_input_rules; i++) {
		const struct dbg_idle_chk_cond_reg *cond_regs;
		const struct dbg_idle_chk_rule *rule;
		const union dbg_idle_chk_reg *regs;
		u16 num_reg_entries = 1;
		bool check_rule = true;
		const u32 *imm_values;

		rule = &input_rules[i];
		regs = &((const union dbg_idle_chk_reg *)s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr)[rule->reg_offset];
		cond_regs = &regs[0].cond_reg;
		imm_values = &s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr[rule->imm_offset];

		/* Check if all condition register blocks are out of reset, and
		 * find maximal number of entries (all condition registers that
		 * are memories must have the same size, which is > 1).
		 */
		for (reg_id = 0; reg_id < rule->num_cond_regs && check_rule; reg_id++) {
			u32 block_id = GET_FIELD(cond_regs[reg_id].data, DBG_IDLE_CHK_COND_REG_BLOCK_ID);

			if (block_id >= MAX_BLOCK_ID) {
				DP_NOTICE(p_hwfn, true, "Invalid block_id\n");
				return 0;
			}

			check_rule = !dev_data->block_in_reset[block_id];
			if (cond_regs[reg_id].num_entries > num_reg_entries)
				num_reg_entries = cond_regs[reg_id].num_entries;
		}

		if (!check_rule && dump)
			continue;

		if (!dump) {
			u32 entry_dump_size = ecore_idle_chk_dump_failure(p_hwfn, p_ptt, dump_buf + offset, false, rule->rule_id, rule, 0, OSAL_NULL);

			offset += num_reg_entries * entry_dump_size;
			(*num_failing_rules) += num_reg_entries;
			continue;
		}

		/* Go over all register entries (number of entries is the same for all
		 * condition registers).
		 */
		for (entry_id = 0; entry_id < num_reg_entries; entry_id++) {
			u32 next_reg_offset = 0;

			/* Read current entry of all condition registers */
			for (reg_id = 0; reg_id < rule->num_cond_regs; reg_id++) {
				const struct dbg_idle_chk_cond_reg *reg = &cond_regs[reg_id];
				u32 padded_entry_size, addr;
				bool wide_bus;

				/* Find GRC address (if it's a memory, the address of the
				 * specific entry is calculated).
				 */
				addr = GET_FIELD(reg->data, DBG_IDLE_CHK_COND_REG_ADDRESS);
				wide_bus = GET_FIELD(reg->data, DBG_IDLE_CHK_COND_REG_WIDE_BUS);
				if (reg->num_entries > 1 || reg->start_entry > 0) {
					padded_entry_size = reg->entry_size > 1 ? OSAL_ROUNDUP_POW_OF_TWO(reg->entry_size) : 1;
					addr += (reg->start_entry + entry_id) * padded_entry_size;
				}

				/* Read registers */
				if (next_reg_offset + reg->entry_size >= IDLE_CHK_MAX_ENTRIES_SIZE) {
					DP_NOTICE(p_hwfn, true, "idle check registers entry is too large\n");
					return 0;
				}

				next_reg_offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, cond_reg_values + next_reg_offset, dump, addr, reg->entry_size, wide_bus);
			}

			/* Call rule condition function. if returns true, it's a failure.*/
			if ((*cond_arr[rule->cond_id])(cond_reg_values, imm_values)) {
				offset += ecore_idle_chk_dump_failure(p_hwfn, p_ptt, dump_buf + offset, dump, rule->rule_id, rule, entry_id, cond_reg_values);
				(*num_failing_rules)++;
			}
		}
	}

	return offset;
}

/* Performs Idle Check Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_idle_chk_dump(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt,
							   u32 *dump_buf,
							   bool dump)
{
	u32 num_failing_rules_offset, offset = 0, input_offset = 0, num_failing_rules = 0;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "idle-chk");

	/* Dump idle check section header with a single parameter */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "idle_chk", 1);
	num_failing_rules_offset = offset;
	offset += ecore_dump_num_param(dump_buf + offset, dump, "num_rules", 0);

	while (input_offset < s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].size_in_dwords) {
		const struct dbg_idle_chk_cond_hdr *cond_hdr = (const struct dbg_idle_chk_cond_hdr *)&s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr[input_offset++];
		bool eval_mode, mode_match = true;
		u32 curr_failing_rules;
		u16 modes_buf_offset;

		/* Check mode */
		eval_mode = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset = GET_FIELD(cond_hdr->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = ecore_is_mode_match(p_hwfn, &modes_buf_offset);
		}

		if (mode_match) {
			offset += ecore_idle_chk_dump_rule_entries(p_hwfn, p_ptt, dump_buf + offset, dump, (const struct dbg_idle_chk_rule *)&s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr[input_offset], cond_hdr->data_size / IDLE_CHK_RULE_SIZE_DWORDS, &curr_failing_rules);
			num_failing_rules += curr_failing_rules;
		}

		input_offset += cond_hdr->data_size;
	}

	/* Overwrite num_rules parameter */
	if (dump)
		ecore_dump_num_param(dump_buf + num_failing_rules_offset, dump, "num_rules", num_failing_rules);

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	return offset;
}

/* Finds the meta data image in NVRAM */
static enum dbg_status ecore_find_nvram_image(struct ecore_hwfn *p_hwfn,
											  struct ecore_ptt *p_ptt,
											  u32 image_type,
											  u32 *nvram_offset_bytes,
											  u32 *nvram_size_bytes)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_txn_size;
	struct mcp_file_att file_att;
	int nvm_result;

	/* Call NVRAM get file command */
	nvm_result = ecore_mcp_nvm_rd_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_NVM_GET_FILE_ATT, image_type, &ret_mcp_resp, &ret_mcp_param, &ret_txn_size, (u32 *)&file_att);

	/* Check response */
	if (nvm_result || (ret_mcp_resp & FW_MSG_CODE_MASK) != FW_MSG_CODE_NVM_OK)
		return DBG_STATUS_NVRAM_GET_IMAGE_FAILED;

	/* Update return values */
	*nvram_offset_bytes = file_att.nvm_start_addr;
	*nvram_size_bytes = file_att.len;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "find_nvram_image: found NVRAM image of type %d in NVRAM offset %d bytes with size %d bytes\n", image_type, *nvram_offset_bytes, *nvram_size_bytes);

	/* Check alignment */
	if (*nvram_size_bytes & 0x3)
		return DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE;

	return DBG_STATUS_OK;
}

/* Reads data from NVRAM */
static enum dbg_status ecore_nvram_read(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 nvram_offset_bytes,
										u32 nvram_size_bytes,
										u32 *ret_buf)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_read_size, bytes_to_copy;
	s32 bytes_left = nvram_size_bytes;
	u32 read_offset = 0;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "nvram_read: reading image of size %d bytes from NVRAM\n", nvram_size_bytes);

	do {
		bytes_to_copy = (bytes_left > MCP_DRV_NVM_BUF_LEN) ? MCP_DRV_NVM_BUF_LEN : bytes_left;

		/* Call NVRAM read command */
		if (ecore_mcp_nvm_rd_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_NVM_READ_NVRAM, (nvram_offset_bytes + read_offset) | (bytes_to_copy << DRV_MB_PARAM_NVM_LEN_OFFSET), &ret_mcp_resp, &ret_mcp_param, &ret_read_size, (u32 *)((u8 *)ret_buf + read_offset)))
			return DBG_STATUS_NVRAM_READ_FAILED;

		/* Check response */
		if ((ret_mcp_resp  & FW_MSG_CODE_MASK) != FW_MSG_CODE_NVM_OK)
			return DBG_STATUS_NVRAM_READ_FAILED;

		/* Update read offset */
		read_offset += ret_read_size;
		bytes_left -= ret_read_size;
	} while (bytes_left > 0);

	return DBG_STATUS_OK;
}

/* Get info on the MCP Trace data in the scratchpad:
 * - trace_data_grc_addr (OUT): trace data GRC address in bytes
 * - trace_data_size (OUT): trace data size in bytes (without the header)
 */
static enum dbg_status ecore_mcp_trace_get_data_info(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *trace_data_grc_addr,
													 u32 *trace_data_size)
{
	u32 spad_trace_offsize, signature;

	/* Read trace section offsize structure from MCP scratchpad */
	spad_trace_offsize = ecore_rd(p_hwfn, p_ptt, MCP_SPAD_TRACE_OFFSIZE_ADDR);

	/* Extract trace section address from offsize (in scratchpad) */
	*trace_data_grc_addr = MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize);

	/* Read signature from MCP trace section */
	signature = ecore_rd(p_hwfn, p_ptt, *trace_data_grc_addr + OFFSETOF(struct mcp_trace, signature));

	if (signature != MFW_TRACE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read trace size from MCP trace section */
	*trace_data_size = ecore_rd(p_hwfn, p_ptt, *trace_data_grc_addr + OFFSETOF(struct mcp_trace, size));

	return DBG_STATUS_OK;
}

/* Reads MCP trace meta data image from NVRAM
 * - running_bundle_id (OUT): running bundle ID (invalid when loaded from file)
 * - trace_meta_offset (OUT): trace meta offset in NVRAM in bytes (invalid when
 *			      loaded from file).
 * - trace_meta_size (OUT):   size in bytes of the trace meta data.
 */
static enum dbg_status ecore_mcp_trace_get_meta_info(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 trace_data_size_bytes,
													 u32 *running_bundle_id,
													 u32 *trace_meta_offset,
													 u32 *trace_meta_size)
{
	u32 spad_trace_offsize, nvram_image_type, running_mfw_addr;

	/* Read MCP trace section offsize structure from MCP scratchpad */
	spad_trace_offsize = ecore_rd(p_hwfn, p_ptt, MCP_SPAD_TRACE_OFFSIZE_ADDR);

	/* Find running bundle ID */
	running_mfw_addr = MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize) + SECTION_SIZE(spad_trace_offsize) + trace_data_size_bytes;
	*running_bundle_id = ecore_rd(p_hwfn, p_ptt, running_mfw_addr);
	if (*running_bundle_id > 1)
		return DBG_STATUS_INVALID_NVRAM_BUNDLE;

	/* Find image in NVRAM */
	nvram_image_type = (*running_bundle_id == DIR_ID_1) ? NVM_TYPE_MFW_TRACE1 : NVM_TYPE_MFW_TRACE2;
	return ecore_find_nvram_image(p_hwfn, p_ptt, nvram_image_type, trace_meta_offset, trace_meta_size);
}

/* Reads the MCP Trace meta data from NVRAM into the specified buffer */
static enum dbg_status ecore_mcp_trace_read_meta(struct ecore_hwfn *p_hwfn,
												 struct ecore_ptt *p_ptt,
												 u32 nvram_offset_in_bytes,
												 u32 size_in_bytes,
												 u32 *buf)
{
	u8 modules_num, module_len, i, *byte_buf = (u8 *)buf;
	enum dbg_status status;
	u32 signature;

	/* Read meta data from NVRAM */
	status = ecore_nvram_read(p_hwfn, p_ptt, nvram_offset_in_bytes, size_in_bytes, buf);
	if (status != DBG_STATUS_OK)
		return status;

	/* Extract and check first signature */
	signature = ecore_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(signature);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Extract number of modules */
	modules_num = *(byte_buf++);

	/* Skip all modules */
	for (i = 0; i < modules_num; i++) {
		module_len = *(byte_buf++);
		byte_buf += module_len;
	}

	/* Extract and check second signature */
	signature = ecore_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(signature);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	return DBG_STATUS_OK;
}

/* Dump MCP Trace */
static enum dbg_status ecore_mcp_trace_dump(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											u32 *dump_buf,
											bool dump,
											u32 *num_dumped_dwords)
{
	u32 trace_meta_offset_bytes = 0, trace_meta_size_bytes = 0, trace_meta_size_dwords = 0;
	u32 trace_data_grc_addr, trace_data_size_bytes, trace_data_size_dwords;
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 running_bundle_id, offset = 0;
	enum dbg_status status;
	bool mcp_access;
	int halted = 0;

	*num_dumped_dwords = 0;

	mcp_access = dev_data->platform_id == PLATFORM_ASIC && !ecore_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP);

	/* Get trace data info */
	status = ecore_mcp_trace_get_data_info(p_hwfn, p_ptt, &trace_data_grc_addr, &trace_data_size_bytes);
	if (status != DBG_STATUS_OK)
		return status;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "mcp-trace");

	/* Halt MCP while reading from scratchpad so the read data will be
	 * consistent. if halt fails, MCP trace is taken anyway, with a small
	 * risk that it may be corrupt.
	 */
	if (dump && mcp_access) {
		halted = !ecore_mcp_halt(p_hwfn, p_ptt);
		if (!halted)
			DP_NOTICE(p_hwfn, false, "MCP halt failed!\n");
	}

	/* Find trace data size */
	trace_data_size_dwords = DIV_ROUND_UP(trace_data_size_bytes + sizeof(struct mcp_trace), BYTES_IN_DWORD);

	/* Dump trace data section header and param */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "mcp_trace_data", 1);
	offset += ecore_dump_num_param(dump_buf + offset, dump, "size", trace_data_size_dwords);

	/* Read trace data from scratchpad into dump buffer */
	offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, BYTES_TO_DWORDS(trace_data_grc_addr), trace_data_size_dwords, false);

	/* Resume MCP (only if halt succeeded) */
	if (halted && ecore_mcp_resume(p_hwfn, p_ptt))
		DP_NOTICE(p_hwfn, false, "Failed to resume MCP after halt!\n");

	/* Dump trace meta section header */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "mcp_trace_meta", 1);

	/* Read trace meta only if NVRAM access is enabled
	 * (trace_meta_size_bytes is dword-aligned).
	 */
	if (OSAL_NVM_IS_ACCESS_ENABLED(p_hwfn) && mcp_access) {
		status = ecore_mcp_trace_get_meta_info(p_hwfn, p_ptt, trace_data_size_bytes, &running_bundle_id, &trace_meta_offset_bytes, &trace_meta_size_bytes);
		if (status == DBG_STATUS_OK)
			trace_meta_size_dwords = BYTES_TO_DWORDS(trace_meta_size_bytes);
	}

	/* Dump trace meta size param */
	offset += ecore_dump_num_param(dump_buf + offset, dump, "size", trace_meta_size_dwords);

	/* Read trace meta image into dump buffer */
	if (dump && trace_meta_size_dwords)
		status = ecore_mcp_trace_read_meta(p_hwfn, p_ptt, trace_meta_offset_bytes, trace_meta_size_bytes, dump_buf + offset);
	if (status == DBG_STATUS_OK)
		offset += trace_meta_size_dwords;

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	/* If no mcp access, indicate that the dump doesn't contain the meta
	 * data from NVRAM.
	 */
	return mcp_access ? status : DBG_STATUS_NVRAM_GET_IMAGE_FAILED;
}

/* Dump GRC FIFO */
static enum dbg_status ecore_reg_fifo_dump(struct ecore_hwfn *p_hwfn,
										   struct ecore_ptt *p_ptt,
										   u32 *dump_buf,
										   bool dump,
										   u32 *num_dumped_dwords)
{
	u32 dwords_read, size_param_offset, offset = 0;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "reg-fifo");

	/* Dump fifo data section header and param. The size param is 0 for
	 * now, and is overwritten after reading the FIFO.
	 */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "reg_fifo_data", 1);
	size_param_offset = offset;
	offset += ecore_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (dump) {
		fifo_has_data = ecore_rd(p_hwfn, p_ptt, GRC_REG_TRACE_FIFO_VALID_DATA) > 0;

		/* Pull available data from fifo. Use DMAE since this is
		 * widebus memory and must be accessed atomically. Test for
		 * dwords_read not passing buffer size since more entries could
		 * be added to the buffer as we
		 * are emptying it.
		 */
		for (dwords_read = 0; fifo_has_data && dwords_read < REG_FIFO_DEPTH_DWORDS; dwords_read += REG_FIFO_ELEMENT_DWORDS) {
			offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, true, BYTES_TO_DWORDS(GRC_REG_TRACE_FIFO), REG_FIFO_ELEMENT_DWORDS, true);
			fifo_has_data = ecore_rd(p_hwfn, p_ptt, GRC_REG_TRACE_FIFO_VALID_DATA) > 0;
		}

		ecore_dump_num_param(dump_buf + size_param_offset, dump, "size", dwords_read);
	}
	else {

		/* FIFO max size is REG_FIFO_DEPTH_DWORDS. There is no way to
		 * test how much data is available, except for reading it.
		 */
		offset += REG_FIFO_DEPTH_DWORDS;
	}

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Dump IGU FIFO */
static enum dbg_status ecore_igu_fifo_dump(struct ecore_hwfn *p_hwfn,
										   struct ecore_ptt *p_ptt,
										   u32 *dump_buf,
										   bool dump,
										   u32 *num_dumped_dwords)
{
	u32 dwords_read, size_param_offset, offset = 0;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "igu-fifo");

	/* Dump fifo data section header and param. The size param is 0 for
	 * now, and is overwritten after reading the FIFO.
	 */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "igu_fifo_data", 1);
	size_param_offset = offset;
	offset += ecore_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (dump) {
		fifo_has_data = ecore_rd(p_hwfn, p_ptt, IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;

		/* Pull available data from fifo. Use DMAE since this is
		 * widebus memory and must be accessed atomically. Test for
		 * dwords_read not passing buffer size since more entries could
		 * be added to the buffer as we are emptying it.
		 */
		for (dwords_read = 0; fifo_has_data && dwords_read < IGU_FIFO_DEPTH_DWORDS; dwords_read += IGU_FIFO_ELEMENT_DWORDS) {
			offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, true, BYTES_TO_DWORDS(IGU_REG_ERROR_HANDLING_MEMORY), IGU_FIFO_ELEMENT_DWORDS, true);
			fifo_has_data = ecore_rd(p_hwfn, p_ptt, IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;
		}

		ecore_dump_num_param(dump_buf + size_param_offset, dump, "size", dwords_read);
	}
	else {

		/* FIFO max size is IGU_FIFO_DEPTH_DWORDS. There is no way to
		 * test how much data is available, except for reading it.
		 */
		offset += IGU_FIFO_DEPTH_DWORDS;
	}

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Protection Override dump */
static enum dbg_status ecore_protection_override_dump(struct ecore_hwfn *p_hwfn,
													  struct ecore_ptt *p_ptt,
													  u32 *dump_buf,
													  bool dump,
													  u32 *num_dumped_dwords)
{
	u32 size_param_offset, override_window_dwords, offset = 0;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "protection-override");

	/* Dump data section header and param. The size param is 0 for now,
	 * and is overwritten after reading the data.
	 */
	offset += ecore_dump_section_hdr(dump_buf + offset, dump, "protection_override_data", 1);
	size_param_offset = offset;
	offset += ecore_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (dump) {
		/* Add override window info to buffer */
		override_window_dwords = ecore_rd(p_hwfn, p_ptt, GRC_REG_NUMBER_VALID_OVERRIDE_WINDOW) * PROTECTION_OVERRIDE_ELEMENT_DWORDS;
		offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, true, BYTES_TO_DWORDS(GRC_REG_PROTECTION_OVERRIDE_WINDOW), override_window_dwords, true);
		ecore_dump_num_param(dump_buf + size_param_offset, dump, "size", override_window_dwords);
	}
	else {
		offset += PROTECTION_OVERRIDE_DEPTH_DWORDS;
	}

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Performs FW Asserts Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 ecore_fw_asserts_dump(struct ecore_hwfn *p_hwfn,
								 struct ecore_ptt *p_ptt,
								 u32 *dump_buf,
								 bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct fw_asserts_ram_section *asserts;
	char storm_letter_str[2] = "?";
	struct fw_info fw_info;
	u32 offset = 0;
	u8 storm_id;

	/* Dump global params */
	offset += ecore_dump_common_global_params(p_hwfn, p_ptt, dump_buf + offset, dump, 1);
	offset += ecore_dump_str_param(dump_buf + offset, dump, "dump-type", "fw-asserts");

	/* Find Storm dump size */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		u32 fw_asserts_section_addr, next_list_idx_addr, next_list_idx, last_list_idx, addr;
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (dev_data->block_in_reset[storm->block_id])
			continue;

		/* Read FW info for the current Storm  */
		ecore_read_fw_info(p_hwfn, p_ptt, storm_id, &fw_info);

		asserts = &fw_info.fw_asserts_section;

		/* Dump FW Asserts section header and params */
		storm_letter_str[0] = storm->letter;
		offset += ecore_dump_section_hdr(dump_buf + offset, dump, "fw_asserts", 2);
		offset += ecore_dump_str_param(dump_buf + offset, dump, "storm", storm_letter_str);
		offset += ecore_dump_num_param(dump_buf + offset, dump, "size", asserts->list_element_dword_size);

		/* Read and dump FW Asserts data */
		if (!dump) {
			offset += asserts->list_element_dword_size;
			continue;
		}

		fw_asserts_section_addr = storm->sem_fast_mem_addr + SEM_FAST_REG_INT_RAM +
			RAM_LINES_TO_BYTES(asserts->section_ram_line_offset);
		next_list_idx_addr = fw_asserts_section_addr + DWORDS_TO_BYTES(asserts->list_next_index_dword_offset);
		next_list_idx = ecore_rd(p_hwfn, p_ptt, next_list_idx_addr);
		last_list_idx = (next_list_idx > 0 ? next_list_idx : asserts->list_num_elements) - 1;
		addr = BYTES_TO_DWORDS(fw_asserts_section_addr) + asserts->list_dword_offset +
					last_list_idx * asserts->list_element_dword_size;
		offset += ecore_grc_dump_addr_range(p_hwfn, p_ptt, dump_buf + offset, dump, addr, asserts->list_element_dword_size, false);
	}

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, dump);

	return offset;
}

/***************************** Public Functions *******************************/

enum dbg_status ecore_dbg_set_bin_ptr(const u8 * const bin_ptr)
{
	struct bin_buffer_hdr *buf_array = (struct bin_buffer_hdr *)bin_ptr;
	u8 buf_id;

	/* convert binary data to debug arrays */
	for (buf_id = 0; buf_id < MAX_BIN_DBG_BUFFER_TYPE; buf_id++) {
		s_dbg_arrays[buf_id].ptr = (u32 *)(bin_ptr + buf_array[buf_id].offset);
		s_dbg_arrays[buf_id].size_in_dwords = BYTES_TO_DWORDS(buf_array[buf_id].length);
	}

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_set_app_ver(u32 ver)
{
	if (ver < TOOLS_VERSION)
		return DBG_STATUS_UNSUPPORTED_APP_VERSION;

	s_app_ver = ver;

	return DBG_STATUS_OK;
}

u32 ecore_dbg_get_fw_func_ver(void)
{
	return TOOLS_VERSION;
}

enum chip_ids ecore_dbg_get_chip_id(struct ecore_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return (enum chip_ids)dev_data->chip_id;
}

enum dbg_status ecore_dbg_bus_reset(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									bool one_shot_en,
									u8 force_hw_dwords,
									bool unify_inputs,
									bool grc_input_en)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	enum dbg_status status;

	status = ecore_dbg_dev_init(p_hwfn, p_ptt);
	if (status != DBG_STATUS_OK)
		return status;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_reset: one_shot_en = %d, force_hw_dwords = %d, unify_inputs = %d, grc_input_en = %d\n", one_shot_en, force_hw_dwords, unify_inputs, grc_input_en);

	if (force_hw_dwords &&
		force_hw_dwords != 4 &&
		force_hw_dwords != 8)
		return DBG_STATUS_INVALID_ARGS;

	if (ecore_rd(p_hwfn, p_ptt, DBG_REG_DBG_BLOCK_ON))
		return DBG_STATUS_DBG_BUS_IN_USE;

	/* Update reset state of all blocks */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Disable all debug inputs */
	status = ecore_bus_disable_inputs(p_hwfn, p_ptt, false);
	if (status != DBG_STATUS_OK)
		return status;

	/* Reset DBG block */
	ecore_bus_reset_dbg_block(p_hwfn, p_ptt);

	/* Set one-shot / wrap-around */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FULL_MODE, one_shot_en ? 0 : 1);

	/* Init state params */
	OSAL_MEMSET(&dev_data->bus, 0, sizeof(dev_data->bus));
	dev_data->bus.target = DBG_BUS_TARGET_ID_INT_BUF;
	dev_data->bus.state = DBG_BUS_STATE_READY;
	dev_data->bus.one_shot_en = one_shot_en;
	dev_data->bus.hw_dwords = force_hw_dwords;
	dev_data->bus.grc_input_en = grc_input_en;
	dev_data->bus.unify_inputs = unify_inputs;
	dev_data->bus.num_enabled_blocks = grc_input_en ? 1 : 0;

	/* Init special DBG block */
	if (grc_input_en)
		SET_FIELD(dev_data->bus.blocks[BLOCK_DBG].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK, 0x1);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_set_pci_output(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 u16 buf_size_kb)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	dma_addr_t pci_buf_phys_addr;
	void *pci_buf;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_set_pci_output: buf_size_kb = %d\n", buf_size_kb);

	if (dev_data->bus.target != DBG_BUS_TARGET_ID_INT_BUF)
		return DBG_STATUS_OUTPUT_ALREADY_SET;
	if (dev_data->bus.state != DBG_BUS_STATE_READY || dev_data->bus.pci_buf.size > 0)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;

	dev_data->bus.target = DBG_BUS_TARGET_ID_PCI;
	dev_data->bus.pci_buf.size = buf_size_kb * 1024;
	if (dev_data->bus.pci_buf.size % PCI_PKT_SIZE_IN_BYTES)
		return DBG_STATUS_INVALID_ARGS;

	pci_buf = OSAL_DMA_ALLOC_COHERENT(p_hwfn->p_dev, &pci_buf_phys_addr, dev_data->bus.pci_buf.size);
	if (!pci_buf)
		return DBG_STATUS_PCI_BUF_ALLOC_FAILED;

	OSAL_MEMCPY(&dev_data->bus.pci_buf.phys_addr, &pci_buf_phys_addr, sizeof(pci_buf_phys_addr));

	dev_data->bus.pci_buf.virt_addr.lo = (u32)((u64)(osal_uintptr_t)pci_buf);
	dev_data->bus.pci_buf.virt_addr.hi = (u32)((u64)(osal_uintptr_t)pci_buf >> 32);

	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_EXT_BUFFER_STRT_ADDR_LSB, dev_data->bus.pci_buf.phys_addr.lo);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_EXT_BUFFER_STRT_ADDR_MSB, dev_data->bus.pci_buf.phys_addr.hi);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TARGET_PACKET_SIZE, PCI_PKT_SIZE_IN_CHUNKS);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_EXT_BUFFER_SIZE, dev_data->bus.pci_buf.size / PCI_PKT_SIZE_IN_BYTES);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_FUNC_NUM, OPAQUE_FID(p_hwfn->rel_pf_id));
	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_LOGIC_ADDR, PCI_PHYS_ADDR_TYPE);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_PCI_REQ_CREDIT, PCI_REQ_CREDIT);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_DEBUG_TARGET, DBG_BUS_TARGET_ID_PCI);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_OUTPUT_ENABLE, TARGET_EN_MASK_PCI);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_set_nw_output(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											u8 port_id,
											u32 dest_addr_lo32,
											u16 dest_addr_hi16,
											u16 data_limit_size_kb,
											bool send_to_other_engine,
											bool rcv_from_other_engine)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_set_nw_output: port_id = %d, dest_addr_lo32 = 0x%x, dest_addr_hi16 = 0x%x, data_limit_size_kb = %d, send_to_other_engine = %d, rcv_from_other_engine = %d\n", port_id, dest_addr_lo32, dest_addr_hi16, data_limit_size_kb, send_to_other_engine, rcv_from_other_engine);

	if (dev_data->bus.target != DBG_BUS_TARGET_ID_INT_BUF)
		return DBG_STATUS_OUTPUT_ALREADY_SET;
	if (dev_data->bus.state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (port_id >= s_chip_defs[dev_data->chip_id].per_platform[dev_data->platform_id].num_ports || (send_to_other_engine && rcv_from_other_engine))
		return DBG_STATUS_INVALID_ARGS;

	dev_data->bus.target = DBG_BUS_TARGET_ID_NIG;
	dev_data->bus.rcv_from_other_engine = rcv_from_other_engine;

	ecore_wr(p_hwfn, p_ptt, DBG_REG_OUTPUT_ENABLE, TARGET_EN_MASK_NIG);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_DEBUG_TARGET, DBG_BUS_TARGET_ID_NIG);

	if (send_to_other_engine)
		ecore_wr(p_hwfn, p_ptt, DBG_REG_OTHER_ENGINE_MODE_BB_K2, DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_TX);
	else
		ecore_wr(p_hwfn, p_ptt, NIG_REG_DEBUG_PORT, port_id);

	if (rcv_from_other_engine) {
		ecore_wr(p_hwfn, p_ptt, DBG_REG_OTHER_ENGINE_MODE_BB_K2, DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_RX);
	}
	else {

		/* Configure ethernet header of 14 bytes */
		ecore_wr(p_hwfn, p_ptt, DBG_REG_ETHERNET_HDR_WIDTH, 0);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_ETHERNET_HDR_7, dest_addr_lo32);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_ETHERNET_HDR_6, (u32)SRC_MAC_ADDR_LO16 | ((u32)dest_addr_hi16 << 16));
		ecore_wr(p_hwfn, p_ptt, DBG_REG_ETHERNET_HDR_5, SRC_MAC_ADDR_HI32);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_ETHERNET_HDR_4, (u32)ETH_TYPE << 16);
		ecore_wr(p_hwfn, p_ptt, DBG_REG_TARGET_PACKET_SIZE, NIG_PKT_SIZE_IN_CHUNKS);
		if (data_limit_size_kb)
			ecore_wr(p_hwfn, p_ptt, DBG_REG_NIG_DATA_LIMIT_SIZE, (data_limit_size_kb * 1024) / CHUNK_SIZE_IN_BYTES);
	}

	return DBG_STATUS_OK;
}

static bool ecore_is_overlapping_enable_mask(struct ecore_hwfn *p_hwfn,
									  u8 enable_mask,
									  u8 right_shift)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 curr_shifted_enable_mask, shifted_enable_mask;
	u32 block_id;

	shifted_enable_mask = SHR(enable_mask, VALUES_PER_CYCLE, right_shift);

	if (dev_data->bus.num_enabled_blocks) {
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			struct dbg_bus_block_data *block_bus = &dev_data->bus.blocks[block_id];

			if (!GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
				continue;

			curr_shifted_enable_mask =
				SHR(GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK),
					VALUES_PER_CYCLE,
					GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT));
			if (shifted_enable_mask & curr_shifted_enable_mask)
				return true;
		}
	}

	return false;
}

enum dbg_status ecore_dbg_bus_enable_block(struct ecore_hwfn *p_hwfn,
										   enum block_id block_id,
										   u8 line_num,
										   u8 enable_mask,
										   u8 right_shift,
										   u8 force_valid_mask,
										   u8 force_frame_mask)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct block_defs *block = s_block_defs[block_id];
	struct dbg_bus_block_data *block_bus;
	struct dbg_bus_block *block_desc;

	block_bus = &dev_data->bus.blocks[block_id];
	block_desc = get_dbg_bus_block_desc(p_hwfn, block_id);

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_enable_block: block = %d, line_num = %d, enable_mask = 0x%x, right_shift = %d, force_valid_mask = 0x%x, force_frame_mask = 0x%x\n", block_id, line_num, enable_mask, right_shift, force_valid_mask, force_frame_mask);

	if (dev_data->bus.state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (block_id >= MAX_BLOCK_ID)
		return DBG_STATUS_INVALID_ARGS;
	if (GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
		return DBG_STATUS_BLOCK_ALREADY_ENABLED;
	if (block->dbg_client_id[dev_data->chip_id] == MAX_DBG_BUS_CLIENTS ||
		line_num >= NUM_DBG_LINES(block_desc) ||
		!enable_mask ||
		enable_mask > MAX_CYCLE_VALUES_MASK ||
		force_valid_mask > MAX_CYCLE_VALUES_MASK ||
		force_frame_mask > MAX_CYCLE_VALUES_MASK ||
		right_shift > VALUES_PER_CYCLE - 1)
		return DBG_STATUS_INVALID_ARGS;
	if (dev_data->block_in_reset[block_id])
		return DBG_STATUS_BLOCK_IN_RESET;
	if (!dev_data->bus.unify_inputs && ecore_is_overlapping_enable_mask(p_hwfn, enable_mask, right_shift))
		return DBG_STATUS_INPUT_OVERLAP;

	dev_data->bus.blocks[block_id].line_num = line_num;
	SET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK, enable_mask);
	SET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT, right_shift);
	SET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_FORCE_VALID_MASK, force_valid_mask);
	SET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_FORCE_FRAME_MASK, force_frame_mask);

	dev_data->bus.num_enabled_blocks++;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_enable_storm(struct ecore_hwfn *p_hwfn,
										   enum dbg_storms storm_id,
										   enum dbg_bus_storm_modes storm_mode)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	struct dbg_bus_storm_data *storm_bus;
	struct storm_defs *storm;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_enable_storm: storm = %d, storm_mode = %d\n", storm_id, storm_mode);

	if (bus->state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (bus->hw_dwords >= 4)
		return DBG_STATUS_HW_ONLY_RECORDING;
	if (storm_id >= MAX_DBG_STORMS)
		return DBG_STATUS_INVALID_ARGS;
	if (storm_mode >= MAX_DBG_BUS_STORM_MODES)
		return DBG_STATUS_INVALID_ARGS;
	if (bus->unify_inputs)
		return DBG_STATUS_INVALID_ARGS;
	if (bus->storms[storm_id].enabled)
		return DBG_STATUS_STORM_ALREADY_ENABLED;

	storm = &s_storm_defs[storm_id];
	storm_bus = &bus->storms[storm_id];

	if (dev_data->block_in_reset[storm->block_id])
		return DBG_STATUS_BLOCK_IN_RESET;

	storm_bus->enabled = true;
	storm_bus->mode = (u8)storm_mode;
	storm_bus->hw_id = bus->num_enabled_storms;

	bus->num_enabled_storms++;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_enable_timestamp(struct ecore_hwfn *p_hwfn,
											   struct ecore_ptt *p_ptt,
											   u8 valid_mask,
											   u8 frame_mask,
											   u32 tick_len)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_enable_timestamp: valid_mask = 0x%x, frame_mask = 0x%x, tick_len = %d\n", valid_mask, frame_mask, tick_len);

	if (dev_data->bus.state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (valid_mask > 0x7 || frame_mask > 0x7)
		return DBG_STATUS_INVALID_ARGS;
	if (!dev_data->bus.unify_inputs && ecore_is_overlapping_enable_mask(p_hwfn, 0x1, 0))
		return DBG_STATUS_INPUT_OVERLAP;

	dev_data->bus.timestamp_input_en = true;
	dev_data->bus.num_enabled_blocks++;

	SET_FIELD(dev_data->bus.blocks[BLOCK_DBG].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK, 0x1);

	ecore_wr(p_hwfn, p_ptt, DBG_REG_TIMESTAMP_VALID_EN, valid_mask);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TIMESTAMP_FRAME_EN, frame_mask);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TIMESTAMP_TICK, tick_len);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_add_eid_range_sem_filter(struct ecore_hwfn *p_hwfn,
													   enum dbg_storms storm_id,
													   u8 min_eid,
													   u8 max_eid)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_storm_data *storm_bus;

	storm_bus = &dev_data->bus.storms[storm_id];

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_add_eid_range_sem_filter: storm = %d, min_eid = 0x%x, max_eid = 0x%x\n", storm_id, min_eid, max_eid);

	if (storm_id >= MAX_DBG_STORMS)
		return DBG_STATUS_INVALID_ARGS;
	if (min_eid > max_eid)
		return DBG_STATUS_INVALID_ARGS;
	if (!storm_bus->enabled)
		return DBG_STATUS_STORM_NOT_ENABLED;

	storm_bus->eid_filter_en = 1;
	storm_bus->eid_range_not_mask = 1;
	storm_bus->eid_filter_params.range.min = min_eid;
	storm_bus->eid_filter_params.range.max = max_eid;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_add_eid_mask_sem_filter(struct ecore_hwfn *p_hwfn,
													  enum dbg_storms storm_id,
													  u8 eid_val,
													  u8 eid_mask)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_storm_data *storm_bus;

	storm_bus = &dev_data->bus.storms[storm_id];

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_add_eid_mask_sem_filter: storm = %d, eid_val = 0x%x, eid_mask = 0x%x\n", storm_id, eid_val, eid_mask);

	if (storm_id >= MAX_DBG_STORMS)
		return DBG_STATUS_INVALID_ARGS;
	if (!storm_bus->enabled)
		return DBG_STATUS_STORM_NOT_ENABLED;

	storm_bus->eid_filter_en = 1;
	storm_bus->eid_range_not_mask = 0;
	storm_bus->eid_filter_params.mask.val = eid_val;
	storm_bus->eid_filter_params.mask.mask = eid_mask;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_add_cid_sem_filter(struct ecore_hwfn *p_hwfn,
												 enum dbg_storms storm_id,
												 u32 cid)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_storm_data *storm_bus;

	storm_bus = &dev_data->bus.storms[storm_id];

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_add_cid_sem_filter: storm = %d, cid = 0x%x\n", storm_id, cid);

	if (storm_id >= MAX_DBG_STORMS)
		return DBG_STATUS_INVALID_ARGS;
	if (!storm_bus->enabled)
		return DBG_STATUS_STORM_NOT_ENABLED;

	storm_bus->cid_filter_en = 1;
	storm_bus->cid = cid;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_enable_filter(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											enum block_id block_id,
											u8 const_msg_len)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_enable_filter: block = %d, const_msg_len = %d\n", block_id, const_msg_len);

	if (dev_data->bus.state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (dev_data->bus.filter_en)
		return DBG_STATUS_FILTER_ALREADY_ENABLED;
	if (block_id >= MAX_BLOCK_ID)
		return DBG_STATUS_INVALID_ARGS;
	if (!GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
		return DBG_STATUS_BLOCK_NOT_ENABLED;
	if (!dev_data->bus.unify_inputs)
		return DBG_STATUS_FILTER_BUG;

	dev_data->bus.filter_en = true;
	dev_data->bus.next_constraint_id = 0;
	dev_data->bus.adding_filter = true;

	/* HW ID is set to 0 due to required unifyInputs */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FILTER_ID_NUM, 0);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FILTER_MSG_LENGTH_ENABLE, const_msg_len > 0 ? 1 : 0);
	if (const_msg_len > 0)
		ecore_wr(p_hwfn, p_ptt, DBG_REG_FILTER_MSG_LENGTH, const_msg_len - 1);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_enable_trigger(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 bool rec_pre_trigger,
											 u8 pre_chunks,
											 bool rec_post_trigger,
											 u32 post_cycles,
											 bool filter_pre_trigger,
											 bool filter_post_trigger)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	enum dbg_bus_post_trigger_types post_trigger_type;
	enum dbg_bus_pre_trigger_types pre_trigger_type;
	struct dbg_bus_data *bus = &dev_data->bus;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_enable_trigger: rec_pre_trigger = %d, pre_chunks = %d, rec_post_trigger = %d, post_cycles = %d, filter_pre_trigger = %d, filter_post_trigger = %d\n", rec_pre_trigger, pre_chunks, rec_post_trigger, post_cycles, filter_pre_trigger, filter_post_trigger);

	if (bus->state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;
	if (bus->trigger_en)
		return DBG_STATUS_TRIGGER_ALREADY_ENABLED;
	if (rec_pre_trigger && pre_chunks >= INT_BUF_SIZE_IN_CHUNKS)
		return DBG_STATUS_INVALID_ARGS;

	bus->trigger_en = true;
	bus->filter_pre_trigger = filter_pre_trigger;
	bus->filter_post_trigger = filter_post_trigger;

	if (rec_pre_trigger) {
		pre_trigger_type = pre_chunks ? DBG_BUS_PRE_TRIGGER_NUM_CHUNKS : DBG_BUS_PRE_TRIGGER_START_FROM_ZERO;
		ecore_wr(p_hwfn, p_ptt, DBG_REG_RCRD_ON_WINDOW_PRE_NUM_CHUNKS, pre_chunks);
	}
	else {
		pre_trigger_type = DBG_BUS_PRE_TRIGGER_DROP;
	}

	if (rec_post_trigger) {
		post_trigger_type = DBG_BUS_POST_TRIGGER_RECORD;
		ecore_wr(p_hwfn, p_ptt, DBG_REG_RCRD_ON_WINDOW_POST_NUM_CYCLES, post_cycles ? post_cycles : 0xffffffff);
	}
	else {
		post_trigger_type = DBG_BUS_POST_TRIGGER_DROP;
	}

	ecore_wr(p_hwfn, p_ptt, DBG_REG_RCRD_ON_WINDOW_PRE_TRGR_EVNT_MODE, pre_trigger_type);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_RCRD_ON_WINDOW_POST_TRGR_EVNT_MODE, post_trigger_type);
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_ENABLE, 1);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_add_trigger_state(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												enum block_id block_id,
												u8 const_msg_len,
												u16 count_to_next)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	struct dbg_bus_block_data *block_bus;
	u8 reg_offset;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_add_trigger_state: block = %d, const_msg_len = %d, count_to_next = %d\n", block_id, const_msg_len, count_to_next);

	block_bus = &bus->blocks[block_id];

	if (!bus->trigger_en)
		return DBG_STATUS_TRIGGER_NOT_ENABLED;
	if (bus->next_trigger_state == MAX_TRIGGER_STATES)
		return DBG_STATUS_TOO_MANY_TRIGGER_STATES;
	if (block_id >= MAX_BLOCK_ID)
		return DBG_STATUS_INVALID_ARGS;
	if (!GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
		return DBG_STATUS_BLOCK_NOT_ENABLED;
	if (!count_to_next)
		return DBG_STATUS_INVALID_ARGS;

	bus->next_constraint_id = 0;
	bus->adding_filter = false;

	/* Store block's shifted enable mask */
	SET_FIELD(bus->trigger_states[dev_data->bus.next_trigger_state].data, DBG_BUS_TRIGGER_STATE_DATA_BLOCK_SHIFTED_ENABLE_MASK, SHR(GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK),
					   VALUES_PER_CYCLE,
					   GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT)));

	/* Set trigger state registers */
	reg_offset = bus->next_trigger_state * BYTES_IN_DWORD;
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_MSG_LENGTH_ENABLE_0 + reg_offset, const_msg_len > 0 ? 1 : 0);
	if (const_msg_len > 0)
		ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_MSG_LENGTH_0 + reg_offset, const_msg_len - 1);

	/* Set trigger set registers */
	reg_offset = bus->next_trigger_state * TRIGGER_SETS_PER_STATE * BYTES_IN_DWORD;
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_SET_COUNT_0 + reg_offset, count_to_next);

	/* Set next state to final state, and overwrite previous next state
	 * (if any).
	 */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_SET_NXT_STATE_0 + reg_offset, MAX_TRIGGER_STATES);
	if (bus->next_trigger_state > 0) {
		reg_offset = (bus->next_trigger_state - 1) * TRIGGER_SETS_PER_STATE * BYTES_IN_DWORD;
		ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_SET_NXT_STATE_0 + reg_offset, bus->next_trigger_state);
	}

	bus->next_trigger_state++;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_add_constraint(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 enum dbg_bus_constraint_ops constraint_op,
											 u32 data_val,
											 u32 data_mask,
											 bool compare_frame,
											 u8 frame_bit,
											 u8 cycle_offset,
											 u8 dword_offset_in_cycle,
											 bool is_mandatory)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	u16 dword_offset, range = 0;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_add_constraint: op = %d, data_val = 0x%x, data_mask = 0x%x, compare_frame = %d, frame_bit = %d, cycle_offset = %d, dword_offset_in_cycle = %d, is_mandatory = %d\n", constraint_op, data_val, data_mask, compare_frame, frame_bit, cycle_offset, dword_offset_in_cycle, is_mandatory);

	if (!bus->filter_en && !dev_data->bus.trigger_en)
		return DBG_STATUS_CANT_ADD_CONSTRAINT;
	if (bus->trigger_en && !bus->adding_filter && !bus->next_trigger_state)
		return DBG_STATUS_CANT_ADD_CONSTRAINT;
	if (bus->next_constraint_id >= MAX_CONSTRAINTS)
		return DBG_STATUS_TOO_MANY_CONSTRAINTS;
	if (constraint_op >= MAX_DBG_BUS_CONSTRAINT_OPS || frame_bit > 1 || dword_offset_in_cycle > 3 || (bus->adding_filter && cycle_offset > 3))
		return DBG_STATUS_INVALID_ARGS;
	if (compare_frame &&
		constraint_op != DBG_BUS_CONSTRAINT_OP_EQ &&
		constraint_op != DBG_BUS_CONSTRAINT_OP_NE)
		return DBG_STATUS_INVALID_ARGS;

	dword_offset = cycle_offset * VALUES_PER_CYCLE + dword_offset_in_cycle;

	if (!bus->adding_filter) {
		u8 curr_trigger_state_id = bus->next_trigger_state - 1;
		struct dbg_bus_trigger_state_data *trigger_state;

		trigger_state = &bus->trigger_states[curr_trigger_state_id];

		/* Check if the selected dword is enabled in the block */
		if (!(GET_FIELD(trigger_state->data, DBG_BUS_TRIGGER_STATE_DATA_BLOCK_SHIFTED_ENABLE_MASK) & (u8)(1 << dword_offset_in_cycle)))
			return DBG_STATUS_INVALID_TRIGGER_DWORD_OFFSET;

		/* Add selected dword to trigger state's dword mask */
		SET_FIELD(trigger_state->data, DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK, GET_FIELD(trigger_state->data, DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK) | (u8)(1 << dword_offset_in_cycle));
	}

	/* Prepare data mask and range */
	if (constraint_op == DBG_BUS_CONSTRAINT_OP_EQ ||
		constraint_op == DBG_BUS_CONSTRAINT_OP_NE) {
		data_mask = ~data_mask;
	}
	else {
		u8 lsb, width;

		/* Extract lsb and width from mask */
		if (!data_mask)
			return DBG_STATUS_INVALID_ARGS;

		for (lsb = 0; lsb < 32 && !(data_mask & 1); lsb++, data_mask >>= 1);
		for (width = 0; width < 32 - lsb && (data_mask & 1); width++, data_mask >>= 1);
		if (data_mask)
			return DBG_STATUS_INVALID_ARGS;
		range = (lsb << 5) | (width - 1);
	}

	/* Add constraint */
	ecore_bus_set_constraint(p_hwfn, p_ptt, dev_data->bus.adding_filter ? 1 : 0,
		dev_data->bus.next_constraint_id,
		s_constraint_op_defs[constraint_op].hw_op_val,
		data_val, data_mask, frame_bit,
		compare_frame ? 0 : 1, dword_offset, range,
		s_constraint_op_defs[constraint_op].is_cyclic ? 1 : 0,
		is_mandatory ? 1 : 0);

	/* If first constraint, fill other 3 constraints with dummy constraints
	 * that always match (using the same offset).
	 */
	if (!dev_data->bus.next_constraint_id) {
		u8 i;

		for (i = 1; i < MAX_CONSTRAINTS; i++)
			ecore_bus_set_constraint(p_hwfn, p_ptt, bus->adding_filter ? 1 : 0,
				i, DBG_BUS_CONSTRAINT_OP_EQ, 0, 0xffffffff,
				0, 1, dword_offset, 0, 0, 1);
	}

	bus->next_constraint_id++;

	return DBG_STATUS_OK;
}

/* Configure the DBG block client mask */
static void ecore_config_dbg_block_client_mask(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	u32 block_id, client_mask = 0;
	u8 storm_id;

	/* Update client mask for Storm inputs */
	if (bus->num_enabled_storms)
		for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
			struct storm_defs *storm = &s_storm_defs[storm_id];

			if (bus->storms[storm_id].enabled)
				client_mask |= (1 << storm->dbg_client_id[dev_data->chip_id]);
		}

	/* Update client mask for block inputs */
	if (bus->num_enabled_blocks) {
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			struct dbg_bus_block_data *block_bus = &bus->blocks[block_id];
			struct block_defs *block = s_block_defs[block_id];

			if (GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK) && block_id != BLOCK_DBG)
				client_mask |= (1 << block->dbg_client_id[dev_data->chip_id]);
		}
	}

	/* Update client mask for GRC input */
	if (bus->grc_input_en)
		client_mask |= (1 << DBG_BUS_CLIENT_CPU);

	/* Update client mask for timestamp input */
	if (bus->timestamp_input_en)
		client_mask |= (1 << DBG_BUS_CLIENT_TIMESTAMP);

	ecore_bus_enable_clients(p_hwfn, p_ptt, client_mask);
}

/* Configure the DBG block framing mode */
static enum dbg_status ecore_config_dbg_block_framing_mode(struct ecore_hwfn *p_hwfn,
													struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	enum dbg_bus_frame_modes dbg_framing_mode;
	u32 block_id;

	if (!bus->hw_dwords && bus->num_enabled_blocks) {
		struct dbg_bus_line *line_desc;
		u8 hw_dwords;

		/* Choose either 4 HW dwords (128-bit mode) or 8 HW dwords
		 * (256-bit mode).
		 */
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			struct dbg_bus_block_data *block_bus = &bus->blocks[block_id];

			if (!GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
				continue;

			line_desc = get_dbg_bus_line_desc(p_hwfn, (enum block_id)block_id);
			hw_dwords = line_desc && GET_FIELD(line_desc->data, DBG_BUS_LINE_IS_256B) ? 8 : 4;

			if (bus->hw_dwords > 0 && bus->hw_dwords != hw_dwords)
				return DBG_STATUS_NON_MATCHING_LINES;

			/* The DBG block doesn't support triggers and
			 * filters on 256b debug lines.
			 */
			if (hw_dwords == 8 && (bus->trigger_en || bus->filter_en))
				return DBG_STATUS_NO_FILTER_TRIGGER_64B;

			bus->hw_dwords = hw_dwords;
		}
	}

	switch (bus->hw_dwords) {
	case 0: dbg_framing_mode = DBG_BUS_FRAME_MODE_0HW_4ST; break;
	case 4: dbg_framing_mode = DBG_BUS_FRAME_MODE_4HW_0ST; break;
	case 8: dbg_framing_mode = DBG_BUS_FRAME_MODE_8HW_0ST; break;
	default: dbg_framing_mode = DBG_BUS_FRAME_MODE_0HW_4ST; break;
	}
	ecore_bus_set_framing_mode(p_hwfn, p_ptt, dbg_framing_mode);

	return DBG_STATUS_OK;
}

/* Configure the DBG block Storm data */
static enum dbg_status ecore_config_storm_inputs(struct ecore_hwfn *p_hwfn,
										  struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	u8 storm_id, i, next_storm_id = 0;
	u32 storm_id_mask = 0;

	/* Check if SEMI sync FIFO is empty */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct dbg_bus_storm_data *storm_bus = &bus->storms[storm_id];
		struct storm_defs *storm = &s_storm_defs[storm_id];

		if (storm_bus->enabled && !ecore_rd(p_hwfn, p_ptt, storm->sem_sync_dbg_empty_addr))
			return DBG_STATUS_SEMI_FIFO_NOT_EMPTY;
	}

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct dbg_bus_storm_data *storm_bus = &bus->storms[storm_id];

		if (storm_bus->enabled)
			storm_id_mask |= (storm_bus->hw_id << (storm_id * HW_ID_BITS));
	}

	ecore_wr(p_hwfn, p_ptt, DBG_REG_STORM_ID_NUM, storm_id_mask);

	/* Disable storm stall if recording to internal buffer in one-shot */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_NO_GRANT_ON_FULL, (dev_data->bus.target == DBG_BUS_TARGET_ID_INT_BUF && bus->one_shot_en) ? 0 : 1);

	/* Configure calendar */
	for (i = 0; i < NUM_CALENDAR_SLOTS; i++, next_storm_id = (next_storm_id + 1) % MAX_DBG_STORMS) {

		/* Find next enabled Storm */
		for (; !dev_data->bus.storms[next_storm_id].enabled; next_storm_id = (next_storm_id + 1) % MAX_DBG_STORMS);

		/* Configure calendar slot */
		ecore_wr(p_hwfn, p_ptt, DBG_REG_CALENDAR_SLOT0 + DWORDS_TO_BYTES(i), next_storm_id);
	}

	return DBG_STATUS_OK;
}

/* Assign HW ID to each dword/qword:
 * if the inputs are unified, HW ID 0 is assigned to all dwords/qwords.
 * Otherwise, we would like to assign a different HW ID to each dword, to avoid
 * data synchronization issues. however, we need to check if there is a trigger
 * state for which more than one dword has a constraint. if there is, we cannot
 * assign a different HW ID to each dword (since a trigger state has a single
 * HW ID), so we assign a different HW ID to each block.
 */
static void ecore_assign_hw_ids(struct ecore_hwfn *p_hwfn,
						 u8 hw_ids[VALUES_PER_CYCLE])
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	bool hw_id_per_dword = true;
	u8 val_id, state_id;
	u32 block_id;

	OSAL_MEMSET(hw_ids, 0, VALUES_PER_CYCLE);

	if (bus->unify_inputs)
		return;

	if (bus->trigger_en) {
		for (state_id = 0; state_id < bus->next_trigger_state && hw_id_per_dword; state_id++) {
			u8 num_dwords = 0;

			for (val_id = 0; val_id < VALUES_PER_CYCLE; val_id++)
				if (GET_FIELD(bus->trigger_states[state_id].data, DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK) & (1 << val_id))
					num_dwords++;

			if (num_dwords > 1)
				hw_id_per_dword = false;
		}
	}

	if (hw_id_per_dword) {

		/* Assign a different HW ID for each dword */
		for (val_id = 0; val_id < VALUES_PER_CYCLE; val_id++)
			hw_ids[val_id] = val_id;
	}
	else {
		u8 shifted_enable_mask, next_hw_id = 0;

		/* Assign HW IDs according to blocks enable /  */
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			struct dbg_bus_block_data *block_bus = &bus->blocks[block_id];

			if (!GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))
				continue;

			block_bus->hw_id = next_hw_id++;
			if (!block_bus->hw_id)
				continue;

			shifted_enable_mask =
				SHR(GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_ENABLE_MASK),
					VALUES_PER_CYCLE,
					GET_FIELD(block_bus->data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT));

			for (val_id = 0; val_id < VALUES_PER_CYCLE; val_id++)
				if (shifted_enable_mask & (1 << val_id))
					hw_ids[val_id] = block_bus->hw_id;
		}
	}
}

/* Configure the DBG block HW blocks data */
static void ecore_config_block_inputs(struct ecore_hwfn *p_hwfn,
							   struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	u8 hw_ids[VALUES_PER_CYCLE];
	u8 val_id, state_id;

	ecore_assign_hw_ids(p_hwfn, hw_ids);

	/* Assign a HW ID to each trigger state */
	if (dev_data->bus.trigger_en) {
		for (state_id = 0; state_id < bus->next_trigger_state; state_id++) {
			for (val_id = 0; val_id < VALUES_PER_CYCLE; val_id++) {
				u8 state_data = bus->trigger_states[state_id].data;

				if (GET_FIELD(state_data, DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK) & (1 << val_id)) {
					ecore_wr(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATE_ID_0 + state_id * BYTES_IN_DWORD, hw_ids[val_id]);
					break;
				}
			}
		}
	}

	/* Configure HW ID mask */
	dev_data->bus.hw_id_mask = 0;
	for (val_id = 0; val_id < VALUES_PER_CYCLE; val_id++)
		bus->hw_id_mask |= (hw_ids[val_id] << (val_id * HW_ID_BITS));
	ecore_wr(p_hwfn, p_ptt, DBG_REG_HW_ID_NUM, bus->hw_id_mask);

	/* Configure additional K2 PCIE registers */
	if (dev_data->chip_id == CHIP_K2 &&
		(GET_FIELD(bus->blocks[BLOCK_PCIE].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK) ||
			GET_FIELD(bus->blocks[BLOCK_PHY_PCIE].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK))) {
		ecore_wr(p_hwfn, p_ptt, PCIE_REG_DBG_REPEAT_THRESHOLD_COUNT_K2_E5, 1);
		ecore_wr(p_hwfn, p_ptt, PCIE_REG_DBG_FW_TRIGGER_ENABLE_K2_E5, 1);
	}
}

enum dbg_status ecore_dbg_bus_start(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	enum dbg_bus_filter_types filter_type;
	enum dbg_status status;
	u32 block_id;
	u8 storm_id;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_start\n");

	if (bus->state != DBG_BUS_STATE_READY)
		return DBG_STATUS_DBG_BLOCK_NOT_RESET;

	/* Check if any input was enabled */
	if (!bus->num_enabled_storms &&
		!bus->num_enabled_blocks &&
		!bus->rcv_from_other_engine)
		return DBG_STATUS_NO_INPUT_ENABLED;

	/* Check if too many input types were enabled (storm+dbgmux) */
	if (bus->num_enabled_storms && bus->num_enabled_blocks)
		return DBG_STATUS_TOO_MANY_INPUTS;

	/* Configure framing mode */
	if ((status = ecore_config_dbg_block_framing_mode(p_hwfn, p_ptt)) != DBG_STATUS_OK)
		return status;

	/* Configure DBG block for Storm inputs */
	if (bus->num_enabled_storms)
		if ((status = ecore_config_storm_inputs(p_hwfn, p_ptt)) != DBG_STATUS_OK)
			return status;

	/* Configure DBG block for block inputs */
	if (bus->num_enabled_blocks)
		ecore_config_block_inputs(p_hwfn, p_ptt);

	/* Configure filter type */
	if (bus->filter_en) {
		if (bus->trigger_en) {
			if (bus->filter_pre_trigger)
				filter_type = bus->filter_post_trigger ? DBG_BUS_FILTER_TYPE_ON : DBG_BUS_FILTER_TYPE_PRE;
			else
				filter_type = bus->filter_post_trigger ? DBG_BUS_FILTER_TYPE_POST : DBG_BUS_FILTER_TYPE_OFF;
		}
		else {
			filter_type = DBG_BUS_FILTER_TYPE_ON;
		}
	}
	else {
		filter_type = DBG_BUS_FILTER_TYPE_OFF;
	}
	ecore_wr(p_hwfn, p_ptt, DBG_REG_FILTER_ENABLE, filter_type);

	/* Restart timestamp */
	ecore_wr(p_hwfn, p_ptt, DBG_REG_TIMESTAMP, 0);

	/* Enable debug block */
	ecore_bus_enable_dbg_block(p_hwfn, p_ptt, 1);

	/* Configure enabled blocks - must be done before the DBG block is
	 * enabled.
	 */
	if (dev_data->bus.num_enabled_blocks) {
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			if (!GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK) || block_id == BLOCK_DBG)
				continue;

			ecore_config_dbg_line(p_hwfn, p_ptt, (enum block_id)block_id,
				dev_data->bus.blocks[block_id].line_num,
				GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK),
				GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_RIGHT_SHIFT),
				GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_FORCE_VALID_MASK),
				GET_FIELD(dev_data->bus.blocks[block_id].data, DBG_BUS_BLOCK_DATA_FORCE_FRAME_MASK));
		}
	}

	/* Configure client mask */
	ecore_config_dbg_block_client_mask(p_hwfn, p_ptt);

	/* Configure enabled Storms - must be done after the DBG block is
	 * enabled.
	 */
	if (dev_data->bus.num_enabled_storms)
		for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++)
			if (dev_data->bus.storms[storm_id].enabled)
				ecore_bus_enable_storm(p_hwfn, p_ptt, (enum dbg_storms)storm_id);

	dev_data->bus.state = DBG_BUS_STATE_RECORDING;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_stop(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	enum dbg_status status = DBG_STATUS_OK;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_stop\n");

	if (bus->state != DBG_BUS_STATE_RECORDING)
		return DBG_STATUS_RECORDING_NOT_STARTED;

	status = ecore_bus_disable_inputs(p_hwfn, p_ptt, true);
	if (status != DBG_STATUS_OK)
		return status;

	ecore_wr(p_hwfn, p_ptt, DBG_REG_CPU_TIMEOUT, 1);

	OSAL_MSLEEP(FLUSH_DELAY_MS);

	ecore_bus_enable_dbg_block(p_hwfn, p_ptt, false);

	/* Check if trigger worked */
	if (bus->trigger_en) {
		u32 trigger_state = ecore_rd(p_hwfn, p_ptt, DBG_REG_TRIGGER_STATUS_CUR_STATE);

		if (trigger_state != MAX_TRIGGER_STATES)
			return DBG_STATUS_DATA_DIDNT_TRIGGER;
	}

	bus->state = DBG_BUS_STATE_STOPPED;

	return status;
}

enum dbg_status ecore_dbg_bus_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												u32 *buf_size)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_bus_data *bus = &dev_data->bus;
	enum dbg_status status;

	status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	/* Add dump header */
	*buf_size = (u32)ecore_bus_dump_hdr(p_hwfn, p_ptt, OSAL_NULL, false);

	switch (bus->target) {
	case DBG_BUS_TARGET_ID_INT_BUF:
		*buf_size += INT_BUF_SIZE_IN_DWORDS; break;
	case DBG_BUS_TARGET_ID_PCI:
		*buf_size += BYTES_TO_DWORDS(bus->pci_buf.size); break;
	default:
		break;
	}

	/* Dump last section */
	*buf_size += ecore_dump_last_section(OSAL_NULL, 0, false);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_bus_dump(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   u32 buf_size_in_dwords,
								   u32 *num_dumped_dwords)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 min_buf_size_in_dwords, block_id, offset = 0;
	struct dbg_bus_data *bus = &dev_data->bus;
	enum dbg_status status;
	u8 storm_id;

	*num_dumped_dwords = 0;

	status = ecore_dbg_bus_get_dump_buf_size(p_hwfn, p_ptt, &min_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_bus_dump: dump_buf = 0x%p, buf_size_in_dwords = %d\n", dump_buf, buf_size_in_dwords);

	if (bus->state != DBG_BUS_STATE_RECORDING && bus->state != DBG_BUS_STATE_STOPPED)
		return DBG_STATUS_RECORDING_NOT_STARTED;

	if (bus->state == DBG_BUS_STATE_RECORDING) {
		enum dbg_status stop_state = ecore_dbg_bus_stop(p_hwfn, p_ptt);
		if (stop_state != DBG_STATUS_OK)
			return stop_state;
	}

	if (buf_size_in_dwords < min_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	if (bus->target == DBG_BUS_TARGET_ID_PCI && !bus->pci_buf.size)
		return DBG_STATUS_PCI_BUF_NOT_ALLOCATED;

	/* Dump header */
	offset += ecore_bus_dump_hdr(p_hwfn, p_ptt, dump_buf + offset, true);

	/* Dump recorded data */
	if (bus->target != DBG_BUS_TARGET_ID_NIG) {
		u32 recorded_dwords = ecore_bus_dump_data(p_hwfn, p_ptt, dump_buf + offset, true);

		if (!recorded_dwords)
			return DBG_STATUS_NO_DATA_RECORDED;
		if (recorded_dwords % CHUNK_SIZE_IN_DWORDS)
			return DBG_STATUS_DUMP_NOT_CHUNK_ALIGNED;
		offset += recorded_dwords;
	}

	/* Dump last section */
	offset += ecore_dump_last_section(dump_buf, offset, true);

	/* If recorded to PCI buffer - free the buffer */
	ecore_bus_free_pci_buf(p_hwfn);

	/* Clear debug bus parameters */
	bus->state = DBG_BUS_STATE_IDLE;
	bus->num_enabled_blocks = 0;
	bus->num_enabled_storms = 0;
	bus->filter_en = bus->trigger_en = 0;

	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++)
		SET_FIELD(bus->blocks[BLOCK_PCIE].data, DBG_BUS_BLOCK_DATA_ENABLE_MASK, 0);

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct dbg_bus_storm_data *storm_bus = &bus->storms[storm_id];

		storm_bus->enabled = false;
		storm_bus->eid_filter_en = storm_bus->cid_filter_en = 0;
	}

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_grc_config(struct ecore_hwfn *p_hwfn,
									 enum dbg_grc_params grc_param,
									 u32 val)
{
	int i;

	DP_VERBOSE(p_hwfn, ECORE_MSG_DEBUG, "dbg_grc_config: paramId = %d, val = %d\n", grc_param, val);

	/* Initializes the GRC parameters (if not initialized). Needed in order
	 * to set the default parameter values for the first time.
	 */
	ecore_dbg_grc_init_params(p_hwfn);

	if (grc_param >= MAX_DBG_GRC_PARAMS)
		return DBG_STATUS_INVALID_ARGS;
	if (val < s_grc_param_defs[grc_param].min ||
		val > s_grc_param_defs[grc_param].max)
		return DBG_STATUS_INVALID_ARGS;

	if (s_grc_param_defs[grc_param].is_preset) {

		/* Preset param */

		/* Disabling a preset is not allowed. Call
		 * dbg_grc_set_params_default instead.
		 */
		if (!val)
			return DBG_STATUS_INVALID_ARGS;

		/* Update all params with the preset values */
		for (i = 0; i < MAX_DBG_GRC_PARAMS; i++) {
			u32 preset_val;

			if (grc_param == DBG_GRC_PARAM_EXCLUDE_ALL)
				preset_val = s_grc_param_defs[i].exclude_all_preset_val;
			else if (grc_param == DBG_GRC_PARAM_CRASH)
				preset_val = s_grc_param_defs[i].crash_preset_val;
			else
				return DBG_STATUS_INVALID_ARGS;

			ecore_grc_set_param(p_hwfn, (enum dbg_grc_params)i, preset_val);
		}
	}
	else {

		/* Regular param - set its value */
		ecore_grc_set_param(p_hwfn, grc_param, val);
	}

	return DBG_STATUS_OK;
}

/* Assign default GRC param values */
void ecore_dbg_grc_set_params_default(struct ecore_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i;

	for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
		dev_data->grc.param_val[i] = s_grc_param_defs[i].default_val[dev_data->chip_id];
}

enum dbg_status ecore_dbg_grc_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	if (!s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr || !s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr || !s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr ||
		!s_dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr || !s_dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	return ecore_grc_dump(p_hwfn, p_ptt, OSAL_NULL, false, buf_size);
}

enum dbg_status ecore_dbg_grc_dump(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   u32 buf_size_in_dwords,
								   u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_grc_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Doesn't do anything, needed for compile time asserts */
	ecore_static_asserts();

	/* GRC Dump */
	status = ecore_grc_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status ecore_dbg_idle_chk_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct idle_chk_data *idle_chk = &dev_data->idle_chk;
	enum dbg_status status;

	*buf_size = 0;

	status = ecore_dbg_dev_init(p_hwfn, p_ptt);
	if (status != DBG_STATUS_OK)
		return status;

	if (!s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr || !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr ||
		!s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr || !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	if (!idle_chk->buf_size_set) {
		idle_chk->buf_size = ecore_idle_chk_dump(p_hwfn, p_ptt, OSAL_NULL, false);
		idle_chk->buf_size_set = true;
	}

	*buf_size = idle_chk->buf_size;

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_idle_chk_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_idle_chk_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Idle Check Dump */
	*num_dumped_dwords = ecore_idle_chk_dump(p_hwfn, p_ptt, dump_buf, true);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_mcp_trace_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													  struct ecore_ptt *p_ptt,
													  u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return ecore_mcp_trace_dump(p_hwfn, p_ptt, OSAL_NULL, false, buf_size);
}

enum dbg_status ecore_dbg_mcp_trace_dump(struct ecore_hwfn *p_hwfn,
										 struct ecore_ptt *p_ptt,
										 u32 *dump_buf,
										 u32 buf_size_in_dwords,
										 u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	status = ecore_dbg_mcp_trace_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK && status != DBG_STATUS_NVRAM_GET_IMAGE_FAILED)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Perform dump */
	status = ecore_mcp_trace_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status ecore_dbg_reg_fifo_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return ecore_reg_fifo_dump(p_hwfn, p_ptt, OSAL_NULL, false, buf_size);
}

enum dbg_status ecore_dbg_reg_fifo_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_reg_fifo_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	status = ecore_reg_fifo_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status ecore_dbg_igu_fifo_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return ecore_igu_fifo_dump(p_hwfn, p_ptt, OSAL_NULL, false, buf_size);
}

enum dbg_status ecore_dbg_igu_fifo_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_igu_fifo_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	status = ecore_igu_fifo_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status ecore_dbg_protection_override_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
																struct ecore_ptt *p_ptt,
																u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return ecore_protection_override_dump(p_hwfn, p_ptt, OSAL_NULL, false, buf_size);
}

enum dbg_status ecore_dbg_protection_override_dump(struct ecore_hwfn *p_hwfn,
												   struct ecore_ptt *p_ptt,
												   u32 *dump_buf,
												   u32 buf_size_in_dwords,
												   u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_protection_override_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	status = ecore_protection_override_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status ecore_dbg_fw_asserts_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													   struct ecore_ptt *p_ptt,
													   u32 *buf_size)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	/* Update reset state */
	ecore_update_blocks_reset_state(p_hwfn, p_ptt);

	*buf_size = ecore_fw_asserts_dump(p_hwfn, p_ptt, OSAL_NULL, false);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_fw_asserts_dump(struct ecore_hwfn *p_hwfn,
										  struct ecore_ptt *p_ptt,
										  u32 *dump_buf,
										  u32 buf_size_in_dwords,
										  u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = ecore_dbg_fw_asserts_get_dump_buf_size(p_hwfn, p_ptt, &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	*num_dumped_dwords = ecore_fw_asserts_dump(p_hwfn, p_ptt, dump_buf, true);

	/* Reveret GRC params to their default */
	ecore_dbg_grc_set_params_default(p_hwfn);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_read_attn(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									enum block_id block_id,
									enum dbg_attn_type attn_type,
									bool clear_status,
									struct dbg_attn_block_result *results)
{
	enum dbg_status status = ecore_dbg_dev_init(p_hwfn, p_ptt);
	u8 reg_idx, num_attn_regs, num_result_regs = 0;
	const struct dbg_attn_reg *attn_reg_arr;

	if (status != DBG_STATUS_OK)
		return status;

	if (!s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr || !s_dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr || !s_dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	attn_reg_arr = ecore_get_block_attn_regs(block_id, attn_type, &num_attn_regs);

	for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
		const struct dbg_attn_reg *reg_data = &attn_reg_arr[reg_idx];
		struct dbg_attn_reg_result *reg_result;
		u32 sts_addr, sts_val;
		u16 modes_buf_offset;
		bool eval_mode;

		/* Check mode */
		eval_mode = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_EVAL_MODE) > 0;
		modes_buf_offset = GET_FIELD(reg_data->mode.data, DBG_MODE_HDR_MODES_BUF_OFFSET);
		if (eval_mode && !ecore_is_mode_match(p_hwfn, &modes_buf_offset))
			continue;

		/* Mode match - read attention status register */
		sts_addr = DWORDS_TO_BYTES(clear_status ? reg_data->sts_clr_address : GET_FIELD(reg_data->data, DBG_ATTN_REG_STS_ADDRESS));
		sts_val = ecore_rd(p_hwfn, p_ptt, sts_addr);
		if (!sts_val)
			continue;

		/* Non-zero attention status - add to results */
		reg_result = &results->reg_results[num_result_regs];
		SET_FIELD(reg_result->data, DBG_ATTN_REG_RESULT_STS_ADDRESS, sts_addr);
		SET_FIELD(reg_result->data, DBG_ATTN_REG_RESULT_NUM_REG_ATTN, GET_FIELD(reg_data->data, DBG_ATTN_REG_NUM_REG_ATTN));
		reg_result->block_attn_offset = reg_data->block_attn_offset;
		reg_result->sts_val = sts_val;
		reg_result->mask_val = ecore_rd(p_hwfn, p_ptt, DWORDS_TO_BYTES(reg_data->mask_address));
		num_result_regs++;
	}

	results->block_id = (u8)block_id;
	results->names_offset = ecore_get_block_attn_data(block_id, attn_type)->names_offset;
	SET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_ATTN_TYPE, attn_type);
	SET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_NUM_REGS, num_result_regs);

	return DBG_STATUS_OK;
}

enum dbg_status ecore_dbg_print_attn(struct ecore_hwfn *p_hwfn,
									 struct dbg_attn_block_result *results)
{
	enum dbg_attn_type attn_type;
	u8 num_regs, i;

	num_regs = GET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_NUM_REGS);
	attn_type = (enum dbg_attn_type)GET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_ATTN_TYPE);

	for (i = 0; i < num_regs; i++) {
		struct dbg_attn_reg_result *reg_result;
		const char *attn_type_str;
		u32 sts_addr;

		reg_result = &results->reg_results[i];
		attn_type_str = (attn_type == ATTN_TYPE_INTERRUPT ? "interrupt" : "parity");
		sts_addr = GET_FIELD(reg_result->data, DBG_ATTN_REG_RESULT_STS_ADDRESS);
		DP_NOTICE(p_hwfn, false, "%s: address 0x%08x, status 0x%08x, mask 0x%08x\n", attn_type_str, sts_addr, reg_result->sts_val, reg_result->mask_val);
	}

	return DBG_STATUS_OK;
}

bool ecore_is_block_in_reset(struct ecore_hwfn *p_hwfn,
							 struct ecore_ptt *p_ptt,
							 enum block_id block_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct block_defs *block = s_block_defs[block_id];
	u32 reset_reg;

	if (!block->has_reset_bit)
		return false;

	reset_reg = block->reset_reg;

	return s_reset_regs_defs[reset_reg].exists[dev_data->chip_id] ?
		!(ecore_rd(p_hwfn, p_ptt, s_reset_regs_defs[reset_reg].addr) & (1 << block->reset_bit_offset)) :	true;
}

