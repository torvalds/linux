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

#ifndef __ECORE_HSI_DEBUG_TOOLS__
#define __ECORE_HSI_DEBUG_TOOLS__ 
/****************************************/
/* Debug Tools HSI constants and macros */
/****************************************/


enum block_addr
{
	GRCBASE_GRC = 0x50000,
	GRCBASE_MISCS = 0x9000,
	GRCBASE_MISC = 0x8000,
	GRCBASE_DBU = 0xa000,
	GRCBASE_PGLUE_B = 0x2a8000,
	GRCBASE_CNIG = 0x218000,
	GRCBASE_CPMU = 0x30000,
	GRCBASE_NCSI = 0x40000,
	GRCBASE_OPTE = 0x53000,
	GRCBASE_BMB = 0x540000,
	GRCBASE_PCIE = 0x54000,
	GRCBASE_MCP = 0xe00000,
	GRCBASE_MCP2 = 0x52000,
	GRCBASE_PSWHST = 0x2a0000,
	GRCBASE_PSWHST2 = 0x29e000,
	GRCBASE_PSWRD = 0x29c000,
	GRCBASE_PSWRD2 = 0x29d000,
	GRCBASE_PSWWR = 0x29a000,
	GRCBASE_PSWWR2 = 0x29b000,
	GRCBASE_PSWRQ = 0x280000,
	GRCBASE_PSWRQ2 = 0x240000,
	GRCBASE_PGLCS = 0x0,
	GRCBASE_DMAE = 0xc000,
	GRCBASE_PTU = 0x560000,
	GRCBASE_TCM = 0x1180000,
	GRCBASE_MCM = 0x1200000,
	GRCBASE_UCM = 0x1280000,
	GRCBASE_XCM = 0x1000000,
	GRCBASE_YCM = 0x1080000,
	GRCBASE_PCM = 0x1100000,
	GRCBASE_QM = 0x2f0000,
	GRCBASE_TM = 0x2c0000,
	GRCBASE_DORQ = 0x100000,
	GRCBASE_BRB = 0x340000,
	GRCBASE_SRC = 0x238000,
	GRCBASE_PRS = 0x1f0000,
	GRCBASE_TSDM = 0xfb0000,
	GRCBASE_MSDM = 0xfc0000,
	GRCBASE_USDM = 0xfd0000,
	GRCBASE_XSDM = 0xf80000,
	GRCBASE_YSDM = 0xf90000,
	GRCBASE_PSDM = 0xfa0000,
	GRCBASE_TSEM = 0x1700000,
	GRCBASE_MSEM = 0x1800000,
	GRCBASE_USEM = 0x1900000,
	GRCBASE_XSEM = 0x1400000,
	GRCBASE_YSEM = 0x1500000,
	GRCBASE_PSEM = 0x1600000,
	GRCBASE_RSS = 0x238800,
	GRCBASE_TMLD = 0x4d0000,
	GRCBASE_MULD = 0x4e0000,
	GRCBASE_YULD = 0x4c8000,
	GRCBASE_XYLD = 0x4c0000,
	GRCBASE_PTLD = 0x5a0000,
	GRCBASE_YPLD = 0x5c0000,
	GRCBASE_PRM = 0x230000,
	GRCBASE_PBF_PB1 = 0xda0000,
	GRCBASE_PBF_PB2 = 0xda4000,
	GRCBASE_RPB = 0x23c000,
	GRCBASE_BTB = 0xdb0000,
	GRCBASE_PBF = 0xd80000,
	GRCBASE_RDIF = 0x300000,
	GRCBASE_TDIF = 0x310000,
	GRCBASE_CDU = 0x580000,
	GRCBASE_CCFC = 0x2e0000,
	GRCBASE_TCFC = 0x2d0000,
	GRCBASE_IGU = 0x180000,
	GRCBASE_CAU = 0x1c0000,
	GRCBASE_RGFS = 0xf00000,
	GRCBASE_RGSRC = 0x320000,
	GRCBASE_TGFS = 0xd00000,
	GRCBASE_TGSRC = 0x322000,
	GRCBASE_UMAC = 0x51000,
	GRCBASE_XMAC = 0x210000,
	GRCBASE_DBG = 0x10000,
	GRCBASE_NIG = 0x500000,
	GRCBASE_WOL = 0x600000,
	GRCBASE_BMBN = 0x610000,
	GRCBASE_IPC = 0x20000,
	GRCBASE_NWM = 0x800000,
	GRCBASE_NWS = 0x700000,
	GRCBASE_MS = 0x6a0000,
	GRCBASE_PHY_PCIE = 0x620000,
	GRCBASE_LED = 0x6b8000,
	GRCBASE_AVS_WRAP = 0x6b0000,
	GRCBASE_PXPREQBUS = 0x56000,
	GRCBASE_MISC_AEU = 0x8000,
	GRCBASE_BAR0_MAP = 0x1c00000,
	MAX_BLOCK_ADDR
};


enum block_id
{
	BLOCK_GRC,
	BLOCK_MISCS,
	BLOCK_MISC,
	BLOCK_DBU,
	BLOCK_PGLUE_B,
	BLOCK_CNIG,
	BLOCK_CPMU,
	BLOCK_NCSI,
	BLOCK_OPTE,
	BLOCK_BMB,
	BLOCK_PCIE,
	BLOCK_MCP,
	BLOCK_MCP2,
	BLOCK_PSWHST,
	BLOCK_PSWHST2,
	BLOCK_PSWRD,
	BLOCK_PSWRD2,
	BLOCK_PSWWR,
	BLOCK_PSWWR2,
	BLOCK_PSWRQ,
	BLOCK_PSWRQ2,
	BLOCK_PGLCS,
	BLOCK_DMAE,
	BLOCK_PTU,
	BLOCK_TCM,
	BLOCK_MCM,
	BLOCK_UCM,
	BLOCK_XCM,
	BLOCK_YCM,
	BLOCK_PCM,
	BLOCK_QM,
	BLOCK_TM,
	BLOCK_DORQ,
	BLOCK_BRB,
	BLOCK_SRC,
	BLOCK_PRS,
	BLOCK_TSDM,
	BLOCK_MSDM,
	BLOCK_USDM,
	BLOCK_XSDM,
	BLOCK_YSDM,
	BLOCK_PSDM,
	BLOCK_TSEM,
	BLOCK_MSEM,
	BLOCK_USEM,
	BLOCK_XSEM,
	BLOCK_YSEM,
	BLOCK_PSEM,
	BLOCK_RSS,
	BLOCK_TMLD,
	BLOCK_MULD,
	BLOCK_YULD,
	BLOCK_XYLD,
	BLOCK_PTLD,
	BLOCK_YPLD,
	BLOCK_PRM,
	BLOCK_PBF_PB1,
	BLOCK_PBF_PB2,
	BLOCK_RPB,
	BLOCK_BTB,
	BLOCK_PBF,
	BLOCK_RDIF,
	BLOCK_TDIF,
	BLOCK_CDU,
	BLOCK_CCFC,
	BLOCK_TCFC,
	BLOCK_IGU,
	BLOCK_CAU,
	BLOCK_RGFS,
	BLOCK_RGSRC,
	BLOCK_TGFS,
	BLOCK_TGSRC,
	BLOCK_UMAC,
	BLOCK_XMAC,
	BLOCK_DBG,
	BLOCK_NIG,
	BLOCK_WOL,
	BLOCK_BMBN,
	BLOCK_IPC,
	BLOCK_NWM,
	BLOCK_NWS,
	BLOCK_MS,
	BLOCK_PHY_PCIE,
	BLOCK_LED,
	BLOCK_AVS_WRAP,
	BLOCK_PXPREQBUS,
	BLOCK_MISC_AEU,
	BLOCK_BAR0_MAP,
	MAX_BLOCK_ID
};


/*
 * binary debug buffer types
 */
enum bin_dbg_buffer_type
{
	BIN_BUF_DBG_MODE_TREE /* init modes tree */,
	BIN_BUF_DBG_DUMP_REG /* GRC Dump registers */,
	BIN_BUF_DBG_DUMP_MEM /* GRC Dump memories */,
	BIN_BUF_DBG_IDLE_CHK_REGS /* Idle Check registers */,
	BIN_BUF_DBG_IDLE_CHK_IMMS /* Idle Check immediates */,
	BIN_BUF_DBG_IDLE_CHK_RULES /* Idle Check rules */,
	BIN_BUF_DBG_IDLE_CHK_PARSING_DATA /* Idle Check parsing data */,
	BIN_BUF_DBG_ATTN_BLOCKS /* Attention blocks */,
	BIN_BUF_DBG_ATTN_REGS /* Attention registers */,
	BIN_BUF_DBG_ATTN_INDEXES /* Attention indexes */,
	BIN_BUF_DBG_ATTN_NAME_OFFSETS /* Attention name offsets */,
	BIN_BUF_DBG_BUS_BLOCKS /* Debug Bus blocks */,
	BIN_BUF_DBG_BUS_LINES /* Debug Bus lines */,
	BIN_BUF_DBG_BUS_BLOCKS_USER_DATA /* Debug Bus blocks user data */,
	BIN_BUF_DBG_BUS_LINE_NAME_OFFSETS /* Debug Bus line name offsets */,
	BIN_BUF_DBG_PARSING_STRINGS /* Debug Tools parsing strings */,
	MAX_BIN_DBG_BUFFER_TYPE
};


/*
 * Attention bit mapping
 */
struct dbg_attn_bit_mapping
{
	u16 data;
#define DBG_ATTN_BIT_MAPPING_VAL_MASK                0x7FFF /* The index of an attention in the blocks attentions list (if is_unused_bit_cnt=0), or a number of consecutive unused attention bits (if is_unused_bit_cnt=1) */
#define DBG_ATTN_BIT_MAPPING_VAL_SHIFT               0
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_MASK  0x1 /* if set, the val field indicates the number of consecutive unused attention bits */
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_SHIFT 15
};


/*
 * Attention block per-type data
 */
struct dbg_attn_block_type_data
{
	u16 names_offset /* Offset of this block attention names in the debug attention name offsets array */;
	u16 reserved1;
	u8 num_regs /* Number of attention registers in this block */;
	u8 reserved2;
	u16 regs_offset /* Offset of this blocks attention registers in the attention registers array (in dbg_attn_reg units) */;
};

/*
 * Block attentions
 */
struct dbg_attn_block
{
	struct dbg_attn_block_type_data per_type_data[2] /* attention block per-type data. Count must match the number of elements in dbg_attn_type. */;
};


/*
 * Attention register result
 */
struct dbg_attn_reg_result
{
	u32 data;
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_MASK   0xFFFFFF /* STS attention register GRC address (in dwords) */
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_SHIFT  0
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_MASK  0xFF /* Number of attention indexes in this register */
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_SHIFT 24
	u16 block_attn_offset /* The offset of this registers attentions within the blocks attentions list (a value in the range 0..number of block attentions-1) */;
	u16 reserved;
	u32 sts_val /* Value read from the STS attention register */;
	u32 mask_val /* Value read from the MASK attention register */;
};

/*
 * Attention block result
 */
struct dbg_attn_block_result
{
	u8 block_id /* Registers block ID */;
	u8 data;
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_MASK  0x3 /* Value from dbg_attn_type enum */
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_SHIFT 0
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_MASK   0x3F /* Number of registers in the block in which at least one attention bit is set */
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_SHIFT  2
	u16 names_offset /* Offset of this registers block attention names in the attention name offsets array */;
	struct dbg_attn_reg_result reg_results[15] /* result data for each register in the block in which at least one attention bit is set */;
};



/*
 * mode header
 */
struct dbg_mode_hdr
{
	u16 data;
#define DBG_MODE_HDR_EVAL_MODE_MASK         0x1 /* indicates if a mode expression should be evaluated (0/1) */
#define DBG_MODE_HDR_EVAL_MODE_SHIFT        0
#define DBG_MODE_HDR_MODES_BUF_OFFSET_MASK  0x7FFF /* offset (in bytes) in modes expression buffer. valid only if eval_mode is set. */
#define DBG_MODE_HDR_MODES_BUF_OFFSET_SHIFT 1
};

/*
 * Attention register
 */
struct dbg_attn_reg
{
	struct dbg_mode_hdr mode /* Mode header */;
	u16 block_attn_offset /* The offset of this registers attentions within the blocks attentions list (a value in the range 0..number of block attentions-1) */;
	u32 data;
#define DBG_ATTN_REG_STS_ADDRESS_MASK   0xFFFFFF /* STS attention register GRC address (in dwords) */
#define DBG_ATTN_REG_STS_ADDRESS_SHIFT  0
#define DBG_ATTN_REG_NUM_REG_ATTN_MASK  0xFF /* Number of attention in this register */
#define DBG_ATTN_REG_NUM_REG_ATTN_SHIFT 24
	u32 sts_clr_address /* STS_CLR attention register GRC address (in dwords) */;
	u32 mask_address /* MASK attention register GRC address (in dwords) */;
};



/*
 * attention types
 */
enum dbg_attn_type
{
	ATTN_TYPE_INTERRUPT,
	ATTN_TYPE_PARITY,
	MAX_DBG_ATTN_TYPE
};


/*
 * Debug Bus block data
 */
struct dbg_bus_block
{
	u8 num_of_lines /* Number of debug lines in this block (excluding signature and latency events). */;
	u8 has_latency_events /* Indicates if this block has a latency events debug line (0/1). */;
	u16 lines_offset /* Offset of this blocks lines in the Debug Bus lines array. */;
};


/*
 * Debug Bus block user data
 */
struct dbg_bus_block_user_data
{
	u8 num_of_lines /* Number of debug lines in this block (excluding signature and latency events). */;
	u8 has_latency_events /* Indicates if this block has a latency events debug line (0/1). */;
	u16 names_offset /* Offset of this blocks lines in the debug bus line name offsets array. */;
};


/*
 * Block Debug line data
 */
struct dbg_bus_line
{
	u8 data;
#define DBG_BUS_LINE_NUM_OF_GROUPS_MASK  0xF /* Number of groups in the line (0-3) */
#define DBG_BUS_LINE_NUM_OF_GROUPS_SHIFT 0
#define DBG_BUS_LINE_IS_256B_MASK        0x1 /* Indicates if this is a 128b line (0) or a 256b line (1). */
#define DBG_BUS_LINE_IS_256B_SHIFT       4
#define DBG_BUS_LINE_RESERVED_MASK       0x7
#define DBG_BUS_LINE_RESERVED_SHIFT      5
	u8 group_sizes /* Four 2-bit values, indicating the size of each group minus 1 (i.e. value=0 means size=1, value=1 means size=2, etc), starting from lsb. The sizes are in dwords (if is_256b=0) or in qwords (if is_256b=1). */;
};


/*
 * condition header for registers dump
 */
struct dbg_dump_cond_hdr
{
	struct dbg_mode_hdr mode /* Mode header */;
	u8 block_id /* block ID */;
	u8 data_size /* size in dwords of the data following this header */;
};


/*
 * memory data for registers dump
 */
struct dbg_dump_mem
{
	u32 dword0;
#define DBG_DUMP_MEM_ADDRESS_MASK       0xFFFFFF /* register address (in dwords) */
#define DBG_DUMP_MEM_ADDRESS_SHIFT      0
#define DBG_DUMP_MEM_MEM_GROUP_ID_MASK  0xFF /* memory group ID */
#define DBG_DUMP_MEM_MEM_GROUP_ID_SHIFT 24
	u32 dword1;
#define DBG_DUMP_MEM_LENGTH_MASK        0xFFFFFF /* register size (in dwords) */
#define DBG_DUMP_MEM_LENGTH_SHIFT       0
#define DBG_DUMP_MEM_WIDE_BUS_MASK      0x1 /* indicates if the register is wide-bus */
#define DBG_DUMP_MEM_WIDE_BUS_SHIFT     24
#define DBG_DUMP_MEM_RESERVED_MASK      0x7F
#define DBG_DUMP_MEM_RESERVED_SHIFT     25
};


/*
 * register data for registers dump
 */
struct dbg_dump_reg
{
	u32 data;
#define DBG_DUMP_REG_ADDRESS_MASK   0x7FFFFF /* register address (in dwords) */
#define DBG_DUMP_REG_ADDRESS_SHIFT  0
#define DBG_DUMP_REG_WIDE_BUS_MASK  0x1 /* indicates if the register is wide-bus */
#define DBG_DUMP_REG_WIDE_BUS_SHIFT 23
#define DBG_DUMP_REG_LENGTH_MASK    0xFF /* register size (in dwords) */
#define DBG_DUMP_REG_LENGTH_SHIFT   24
};


/*
 * split header for registers dump
 */
struct dbg_dump_split_hdr
{
	u32 hdr;
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_MASK      0xFFFFFF /* size in dwords of the data following this header */
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_SHIFT     0
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_MASK  0xFF /* split type ID */
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_SHIFT 24
};


/*
 * condition header for idle check
 */
struct dbg_idle_chk_cond_hdr
{
	struct dbg_mode_hdr mode /* Mode header */;
	u16 data_size /* size in dwords of the data following this header */;
};


/*
 * Idle Check condition register
 */
struct dbg_idle_chk_cond_reg
{
	u32 data;
#define DBG_IDLE_CHK_COND_REG_ADDRESS_MASK   0x7FFFFF /* Register GRC address (in dwords) */
#define DBG_IDLE_CHK_COND_REG_ADDRESS_SHIFT  0
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_MASK  0x1 /* indicates if the register is wide-bus */
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_SHIFT 23
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_MASK  0xFF /* value from block_id enum */
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_SHIFT 24
	u16 num_entries /* number of registers entries to check */;
	u8 entry_size /* size of registers entry (in dwords) */;
	u8 start_entry /* index of the first entry to check */;
};


/*
 * Idle Check info register
 */
struct dbg_idle_chk_info_reg
{
	u32 data;
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_MASK   0x7FFFFF /* Register GRC address (in dwords) */
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_SHIFT  0
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_MASK  0x1 /* indicates if the register is wide-bus */
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_SHIFT 23
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_MASK  0xFF /* value from block_id enum */
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_SHIFT 24
	u16 size /* register size in dwords */;
	struct dbg_mode_hdr mode /* Mode header */;
};


/*
 * Idle Check register
 */
union dbg_idle_chk_reg
{
	struct dbg_idle_chk_cond_reg cond_reg /* condition register */;
	struct dbg_idle_chk_info_reg info_reg /* info register */;
};


/*
 * Idle Check result header
 */
struct dbg_idle_chk_result_hdr
{
	u16 rule_id /* Failing rule index */;
	u16 mem_entry_id /* Failing memory entry index */;
	u8 num_dumped_cond_regs /* number of dumped condition registers */;
	u8 num_dumped_info_regs /* number of dumped condition registers */;
	u8 severity /* from dbg_idle_chk_severity_types enum */;
	u8 reserved;
};


/*
 * Idle Check result register header
 */
struct dbg_idle_chk_result_reg_hdr
{
	u8 data;
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_MASK  0x1 /* indicates if this register is a memory */
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_SHIFT 0
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_MASK  0x7F /* register index within the failing rule */
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_SHIFT 1
	u8 start_entry /* index of the first checked entry */;
	u16 size /* register size in dwords */;
};


/*
 * Idle Check rule
 */
struct dbg_idle_chk_rule
{
	u16 rule_id /* Idle Check rule ID */;
	u8 severity /* value from dbg_idle_chk_severity_types enum */;
	u8 cond_id /* Condition ID */;
	u8 num_cond_regs /* number of condition registers */;
	u8 num_info_regs /* number of info registers */;
	u8 num_imms /* number of immediates in the condition */;
	u8 reserved1;
	u16 reg_offset /* offset of this rules registers in the idle check register array (in dbg_idle_chk_reg units) */;
	u16 imm_offset /* offset of this rules immediate values in the immediate values array (in dwords) */;
};


/*
 * Idle Check rule parsing data
 */
struct dbg_idle_chk_rule_parsing_data
{
	u32 data;
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_MASK  0x1 /* indicates if this register has a FW message */
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_SHIFT 0
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_MASK  0x7FFFFFFF /* Offset of this rules strings in the debug strings array (in bytes) */
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_SHIFT 1
};


/*
 * idle check severity types
 */
enum dbg_idle_chk_severity_types
{
	IDLE_CHK_SEVERITY_ERROR /* idle check failure should cause an error */,
	IDLE_CHK_SEVERITY_ERROR_NO_TRAFFIC /* idle check failure should cause an error only if theres no traffic */,
	IDLE_CHK_SEVERITY_WARNING /* idle check failure should cause a warning */,
	MAX_DBG_IDLE_CHK_SEVERITY_TYPES
};



/*
 * Debug Bus block data
 */
struct dbg_bus_block_data
{
	u16 data;
#define DBG_BUS_BLOCK_DATA_ENABLE_MASK_MASK       0xF /* 4-bit value: bit i set -> dword/qword i is enabled. */
#define DBG_BUS_BLOCK_DATA_ENABLE_MASK_SHIFT      0
#define DBG_BUS_BLOCK_DATA_RIGHT_SHIFT_MASK       0xF /* Number of dwords/qwords to shift right the debug data (0-3) */
#define DBG_BUS_BLOCK_DATA_RIGHT_SHIFT_SHIFT      4
#define DBG_BUS_BLOCK_DATA_FORCE_VALID_MASK_MASK  0xF /* 4-bit value: bit i set -> dword/qword i is forced valid. */
#define DBG_BUS_BLOCK_DATA_FORCE_VALID_MASK_SHIFT 8
#define DBG_BUS_BLOCK_DATA_FORCE_FRAME_MASK_MASK  0xF /* 4-bit value: bit i set -> dword/qword i frame bit is forced. */
#define DBG_BUS_BLOCK_DATA_FORCE_FRAME_MASK_SHIFT 12
	u8 line_num /* Debug line number to select */;
	u8 hw_id /* HW ID associated with the block */;
};


/*
 * Debug Bus Clients
 */
enum dbg_bus_clients
{
	DBG_BUS_CLIENT_RBCN,
	DBG_BUS_CLIENT_RBCP,
	DBG_BUS_CLIENT_RBCR,
	DBG_BUS_CLIENT_RBCT,
	DBG_BUS_CLIENT_RBCU,
	DBG_BUS_CLIENT_RBCF,
	DBG_BUS_CLIENT_RBCX,
	DBG_BUS_CLIENT_RBCS,
	DBG_BUS_CLIENT_RBCH,
	DBG_BUS_CLIENT_RBCZ,
	DBG_BUS_CLIENT_OTHER_ENGINE,
	DBG_BUS_CLIENT_TIMESTAMP,
	DBG_BUS_CLIENT_CPU,
	DBG_BUS_CLIENT_RBCY,
	DBG_BUS_CLIENT_RBCQ,
	DBG_BUS_CLIENT_RBCM,
	DBG_BUS_CLIENT_RBCB,
	DBG_BUS_CLIENT_RBCW,
	DBG_BUS_CLIENT_RBCV,
	MAX_DBG_BUS_CLIENTS
};


/*
 * Debug Bus constraint operation types
 */
enum dbg_bus_constraint_ops
{
	DBG_BUS_CONSTRAINT_OP_EQ /* equal */,
	DBG_BUS_CONSTRAINT_OP_NE /* not equal */,
	DBG_BUS_CONSTRAINT_OP_LT /* less than */,
	DBG_BUS_CONSTRAINT_OP_LTC /* less than (cyclic) */,
	DBG_BUS_CONSTRAINT_OP_LE /* less than or equal */,
	DBG_BUS_CONSTRAINT_OP_LEC /* less than or equal (cyclic) */,
	DBG_BUS_CONSTRAINT_OP_GT /* greater than */,
	DBG_BUS_CONSTRAINT_OP_GTC /* greater than (cyclic) */,
	DBG_BUS_CONSTRAINT_OP_GE /* greater than or equal */,
	DBG_BUS_CONSTRAINT_OP_GEC /* greater than or equal (cyclic) */,
	MAX_DBG_BUS_CONSTRAINT_OPS
};


/*
 * Debug Bus trigger state data
 */
struct dbg_bus_trigger_state_data
{
	u8 data;
#define DBG_BUS_TRIGGER_STATE_DATA_BLOCK_SHIFTED_ENABLE_MASK_MASK  0xF /* 4-bit value: bit i set -> dword i of the trigger state block (after right shift) is enabled. */
#define DBG_BUS_TRIGGER_STATE_DATA_BLOCK_SHIFTED_ENABLE_MASK_SHIFT 0
#define DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK_MASK      0xF /* 4-bit value: bit i set -> dword i is compared by a constraint */
#define DBG_BUS_TRIGGER_STATE_DATA_CONSTRAINT_DWORD_MASK_SHIFT     4
};

/*
 * Debug Bus memory address
 */
struct dbg_bus_mem_addr
{
	u32 lo;
	u32 hi;
};

/*
 * Debug Bus PCI buffer data
 */
struct dbg_bus_pci_buf_data
{
	struct dbg_bus_mem_addr phys_addr /* PCI buffer physical address */;
	struct dbg_bus_mem_addr virt_addr /* PCI buffer virtual address */;
	u32 size /* PCI buffer size in bytes */;
};

/*
 * Debug Bus Storm EID range filter params
 */
struct dbg_bus_storm_eid_range_params
{
	u8 min /* Minimal event ID to filter on */;
	u8 max /* Maximal event ID to filter on */;
};

/*
 * Debug Bus Storm EID mask filter params
 */
struct dbg_bus_storm_eid_mask_params
{
	u8 val /* Event ID value */;
	u8 mask /* Event ID mask. 1s in the mask = dont care bits. */;
};

/*
 * Debug Bus Storm EID filter params
 */
union dbg_bus_storm_eid_params
{
	struct dbg_bus_storm_eid_range_params range /* EID range filter params */;
	struct dbg_bus_storm_eid_mask_params mask /* EID mask filter params */;
};

/*
 * Debug Bus Storm data
 */
struct dbg_bus_storm_data
{
	u8 enabled /* indicates if the Storm is enabled for recording */;
	u8 mode /* Storm debug mode, valid only if the Storm is enabled (use enum dbg_bus_storm_modes) */;
	u8 hw_id /* HW ID associated with the Storm */;
	u8 eid_filter_en /* Indicates if EID filtering is performed (0/1) */;
	u8 eid_range_not_mask /* 1 = EID range filter, 0 = EID mask filter. Valid only if eid_filter_en is set,  */;
	u8 cid_filter_en /* Indicates if CID filtering is performed (0/1) */;
	union dbg_bus_storm_eid_params eid_filter_params /* EID filter params to filter on. Valid only if eid_filter_en is set. */;
	u32 cid /* CID to filter on. Valid only if cid_filter_en is set. */;
};

/*
 * Debug Bus data
 */
struct dbg_bus_data
{
	u32 app_version /* The tools version number of the application */;
	u8 state /* The current debug bus state (use enum dbg_bus_states) */;
	u8 hw_dwords /* HW dwords per cycle */;
	u16 hw_id_mask /* The HW IDs of the recorded HW blocks, where bits i*3..i*3+2 contain the HW ID of dword/qword i */;
	u8 num_enabled_blocks /* Number of blocks enabled for recording */;
	u8 num_enabled_storms /* Number of Storms enabled for recording */;
	u8 target /* Output target (use enum dbg_bus_targets) */;
	u8 one_shot_en /* Indicates if one-shot mode is enabled (0/1) */;
	u8 grc_input_en /* Indicates if GRC recording is enabled (0/1) */;
	u8 timestamp_input_en /* Indicates if timestamp recording is enabled (0/1) */;
	u8 filter_en /* Indicates if the recording filter is enabled (0/1) */;
	u8 adding_filter /* If true, the next added constraint belong to the filter. Otherwise, it belongs to the last added trigger state. Valid only if either filter or triggers are enabled. */;
	u8 filter_pre_trigger /* Indicates if the recording filter should be applied before the trigger. Valid only if both filter and trigger are enabled (0/1) */;
	u8 filter_post_trigger /* Indicates if the recording filter should be applied after the trigger. Valid only if both filter and trigger are enabled (0/1) */;
	u16 reserved;
	u8 trigger_en /* Indicates if the recording trigger is enabled (0/1) */;
	struct dbg_bus_trigger_state_data trigger_states[3] /* trigger states data */;
	u8 next_trigger_state /* ID of next trigger state to be added */;
	u8 next_constraint_id /* ID of next filter/trigger constraint to be added */;
	u8 unify_inputs /* If true, all inputs are associated with HW ID 0. Otherwise, each input is assigned a different HW ID (0/1) */;
	u8 rcv_from_other_engine /* Indicates if the other engine sends it NW recording to this engine (0/1) */;
	struct dbg_bus_pci_buf_data pci_buf /* Debug Bus PCI buffer data. Valid only when the target is DBG_BUS_TARGET_ID_PCI. */;
	struct dbg_bus_block_data blocks[88] /* Debug Bus data for each block */;
	struct dbg_bus_storm_data storms[6] /* Debug Bus data for each block */;
};


/*
 * Debug bus filter types
 */
enum dbg_bus_filter_types
{
	DBG_BUS_FILTER_TYPE_OFF /* filter always off */,
	DBG_BUS_FILTER_TYPE_PRE /* filter before trigger only */,
	DBG_BUS_FILTER_TYPE_POST /* filter after trigger only */,
	DBG_BUS_FILTER_TYPE_ON /* filter always on */,
	MAX_DBG_BUS_FILTER_TYPES
};


/*
 * Debug bus frame modes
 */
enum dbg_bus_frame_modes
{
	DBG_BUS_FRAME_MODE_0HW_4ST=0 /* 0 HW dwords, 4 Storm dwords */,
	DBG_BUS_FRAME_MODE_4HW_0ST=3 /* 4 HW dwords, 0 Storm dwords */,
	DBG_BUS_FRAME_MODE_8HW_0ST=4 /* 8 HW dwords, 0 Storm dwords */,
	MAX_DBG_BUS_FRAME_MODES
};



/*
 * Debug bus other engine mode
 */
enum dbg_bus_other_engine_modes
{
	DBG_BUS_OTHER_ENGINE_MODE_NONE,
	DBG_BUS_OTHER_ENGINE_MODE_DOUBLE_BW_TX,
	DBG_BUS_OTHER_ENGINE_MODE_DOUBLE_BW_RX,
	DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_TX,
	DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_RX,
	MAX_DBG_BUS_OTHER_ENGINE_MODES
};



/*
 * Debug bus post-trigger recording types
 */
enum dbg_bus_post_trigger_types
{
	DBG_BUS_POST_TRIGGER_RECORD /* start recording after trigger */,
	DBG_BUS_POST_TRIGGER_DROP /* drop data after trigger */,
	MAX_DBG_BUS_POST_TRIGGER_TYPES
};


/*
 * Debug bus pre-trigger recording types
 */
enum dbg_bus_pre_trigger_types
{
	DBG_BUS_PRE_TRIGGER_START_FROM_ZERO /* start recording from time 0 */,
	DBG_BUS_PRE_TRIGGER_NUM_CHUNKS /* start recording some chunks before trigger */,
	DBG_BUS_PRE_TRIGGER_DROP /* drop data before trigger */,
	MAX_DBG_BUS_PRE_TRIGGER_TYPES
};


/*
 * Debug bus SEMI frame modes
 */
enum dbg_bus_semi_frame_modes
{
	DBG_BUS_SEMI_FRAME_MODE_0SLOW_4FAST=0 /* 0 slow dwords, 4 fast dwords */,
	DBG_BUS_SEMI_FRAME_MODE_4SLOW_0FAST=3 /* 4 slow dwords, 0 fast dwords */,
	MAX_DBG_BUS_SEMI_FRAME_MODES
};


/*
 * Debug bus states
 */
enum dbg_bus_states
{
	DBG_BUS_STATE_IDLE /* debug bus idle state (not recording) */,
	DBG_BUS_STATE_READY /* debug bus is ready for configuration and recording */,
	DBG_BUS_STATE_RECORDING /* debug bus is currently recording */,
	DBG_BUS_STATE_STOPPED /* debug bus recording has stopped */,
	MAX_DBG_BUS_STATES
};






/*
 * Debug Bus Storm modes
 */
enum dbg_bus_storm_modes
{
	DBG_BUS_STORM_MODE_PRINTF /* store data (fast debug) */,
	DBG_BUS_STORM_MODE_PRAM_ADDR /* pram address (fast debug) */,
	DBG_BUS_STORM_MODE_DRA_RW /* DRA read/write data (fast debug) */,
	DBG_BUS_STORM_MODE_DRA_W /* DRA write data (fast debug) */,
	DBG_BUS_STORM_MODE_LD_ST_ADDR /* load/store address (fast debug) */,
	DBG_BUS_STORM_MODE_DRA_FSM /* DRA state machines (fast debug) */,
	DBG_BUS_STORM_MODE_RH /* recording handlers (fast debug) */,
	DBG_BUS_STORM_MODE_FOC /* FOC: FIN + DRA Rd (slow debug) */,
	DBG_BUS_STORM_MODE_EXT_STORE /* FOC: External Store (slow) */,
	MAX_DBG_BUS_STORM_MODES
};


/*
 * Debug bus target IDs
 */
enum dbg_bus_targets
{
	DBG_BUS_TARGET_ID_INT_BUF /* records debug bus to DBG block internal buffer */,
	DBG_BUS_TARGET_ID_NIG /* records debug bus to the NW */,
	DBG_BUS_TARGET_ID_PCI /* records debug bus to a PCI buffer */,
	MAX_DBG_BUS_TARGETS
};



/*
 * GRC Dump data
 */
struct dbg_grc_data
{
	u8 params_initialized /* Indicates if the GRC parameters were initialized */;
	u8 reserved1;
	u16 reserved2;
	u32 param_val[48] /* Value of each GRC parameter. Array size must match the enum dbg_grc_params. */;
};


/*
 * Debug GRC params
 */
enum dbg_grc_params
{
	DBG_GRC_PARAM_DUMP_TSTORM /* dump Tstorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_MSTORM /* dump Mstorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_USTORM /* dump Ustorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_XSTORM /* dump Xstorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_YSTORM /* dump Ystorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_PSTORM /* dump Pstorm memories (0/1) */,
	DBG_GRC_PARAM_DUMP_REGS /* dump non-memory registers (0/1) */,
	DBG_GRC_PARAM_DUMP_RAM /* dump Storm internal RAMs (0/1) */,
	DBG_GRC_PARAM_DUMP_PBUF /* dump Storm passive buffer (0/1) */,
	DBG_GRC_PARAM_DUMP_IOR /* dump Storm IORs (0/1) */,
	DBG_GRC_PARAM_DUMP_VFC /* dump VFC memories (0/1) */,
	DBG_GRC_PARAM_DUMP_CM_CTX /* dump CM contexts (0/1) */,
	DBG_GRC_PARAM_DUMP_PXP /* dump PXP memories (0/1) */,
	DBG_GRC_PARAM_DUMP_RSS /* dump RSS memories (0/1) */,
	DBG_GRC_PARAM_DUMP_CAU /* dump CAU memories (0/1) */,
	DBG_GRC_PARAM_DUMP_QM /* dump QM memories (0/1) */,
	DBG_GRC_PARAM_DUMP_MCP /* dump MCP memories (0/1) */,
	DBG_GRC_PARAM_RESERVED /* reserved */,
	DBG_GRC_PARAM_DUMP_CFC /* dump CFC memories (0/1) */,
	DBG_GRC_PARAM_DUMP_IGU /* dump IGU memories (0/1) */,
	DBG_GRC_PARAM_DUMP_BRB /* dump BRB memories (0/1) */,
	DBG_GRC_PARAM_DUMP_BTB /* dump BTB memories (0/1) */,
	DBG_GRC_PARAM_DUMP_BMB /* dump BMB memories (0/1) */,
	DBG_GRC_PARAM_DUMP_NIG /* dump NIG memories (0/1) */,
	DBG_GRC_PARAM_DUMP_MULD /* dump MULD memories (0/1) */,
	DBG_GRC_PARAM_DUMP_PRS /* dump PRS memories (0/1) */,
	DBG_GRC_PARAM_DUMP_DMAE /* dump PRS memories (0/1) */,
	DBG_GRC_PARAM_DUMP_TM /* dump TM (timers) memories (0/1) */,
	DBG_GRC_PARAM_DUMP_SDM /* dump SDM memories (0/1) */,
	DBG_GRC_PARAM_DUMP_DIF /* dump DIF memories (0/1) */,
	DBG_GRC_PARAM_DUMP_STATIC /* dump static debug data (0/1) */,
	DBG_GRC_PARAM_UNSTALL /* un-stall Storms after dump (0/1) */,
	DBG_GRC_PARAM_NUM_LCIDS /* number of LCIDs (0..320) */,
	DBG_GRC_PARAM_NUM_LTIDS /* number of LTIDs (0..320) */,
	DBG_GRC_PARAM_EXCLUDE_ALL /* preset: exclude all memories from dump (1 only) */,
	DBG_GRC_PARAM_CRASH /* preset: include memories for crash dump (1 only) */,
	DBG_GRC_PARAM_PARITY_SAFE /* perform dump only if MFW is responding (0/1) */,
	DBG_GRC_PARAM_DUMP_CM /* dump CM memories (0/1) */,
	DBG_GRC_PARAM_DUMP_PHY /* dump PHY memories (0/1) */,
	DBG_GRC_PARAM_NO_MCP /* dont perform MCP commands (0/1) */,
	DBG_GRC_PARAM_NO_FW_VER /* dont read FW/MFW version (0/1) */,
	MAX_DBG_GRC_PARAMS
};


/*
 * Debug reset registers
 */
enum dbg_reset_regs
{
	DBG_RESET_REG_MISCS_PL_UA,
	DBG_RESET_REG_MISCS_PL_HV,
	DBG_RESET_REG_MISCS_PL_HV_2,
	DBG_RESET_REG_MISC_PL_UA,
	DBG_RESET_REG_MISC_PL_HV,
	DBG_RESET_REG_MISC_PL_PDA_VMAIN_1,
	DBG_RESET_REG_MISC_PL_PDA_VMAIN_2,
	DBG_RESET_REG_MISC_PL_PDA_VAUX,
	MAX_DBG_RESET_REGS
};


/*
 * Debug status codes
 */
enum dbg_status
{
	DBG_STATUS_OK,
	DBG_STATUS_APP_VERSION_NOT_SET,
	DBG_STATUS_UNSUPPORTED_APP_VERSION,
	DBG_STATUS_DBG_BLOCK_NOT_RESET,
	DBG_STATUS_INVALID_ARGS,
	DBG_STATUS_OUTPUT_ALREADY_SET,
	DBG_STATUS_INVALID_PCI_BUF_SIZE,
	DBG_STATUS_PCI_BUF_ALLOC_FAILED,
	DBG_STATUS_PCI_BUF_NOT_ALLOCATED,
	DBG_STATUS_TOO_MANY_INPUTS,
	DBG_STATUS_INPUT_OVERLAP,
	DBG_STATUS_HW_ONLY_RECORDING,
	DBG_STATUS_STORM_ALREADY_ENABLED,
	DBG_STATUS_STORM_NOT_ENABLED,
	DBG_STATUS_BLOCK_ALREADY_ENABLED,
	DBG_STATUS_BLOCK_NOT_ENABLED,
	DBG_STATUS_NO_INPUT_ENABLED,
	DBG_STATUS_NO_FILTER_TRIGGER_64B,
	DBG_STATUS_FILTER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_NOT_ENABLED,
	DBG_STATUS_CANT_ADD_CONSTRAINT,
	DBG_STATUS_TOO_MANY_TRIGGER_STATES,
	DBG_STATUS_TOO_MANY_CONSTRAINTS,
	DBG_STATUS_RECORDING_NOT_STARTED,
	DBG_STATUS_DATA_DIDNT_TRIGGER,
	DBG_STATUS_NO_DATA_RECORDED,
	DBG_STATUS_DUMP_BUF_TOO_SMALL,
	DBG_STATUS_DUMP_NOT_CHUNK_ALIGNED,
	DBG_STATUS_UNKNOWN_CHIP,
	DBG_STATUS_VIRT_MEM_ALLOC_FAILED,
	DBG_STATUS_BLOCK_IN_RESET,
	DBG_STATUS_INVALID_TRACE_SIGNATURE,
	DBG_STATUS_INVALID_NVRAM_BUNDLE,
	DBG_STATUS_NVRAM_GET_IMAGE_FAILED,
	DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE,
	DBG_STATUS_NVRAM_READ_FAILED,
	DBG_STATUS_IDLE_CHK_PARSE_FAILED,
	DBG_STATUS_MCP_TRACE_BAD_DATA,
	DBG_STATUS_MCP_TRACE_NO_META,
	DBG_STATUS_MCP_COULD_NOT_HALT,
	DBG_STATUS_MCP_COULD_NOT_RESUME,
	DBG_STATUS_RESERVED2,
	DBG_STATUS_SEMI_FIFO_NOT_EMPTY,
	DBG_STATUS_IGU_FIFO_BAD_DATA,
	DBG_STATUS_MCP_COULD_NOT_MASK_PRTY,
	DBG_STATUS_FW_ASSERTS_PARSE_FAILED,
	DBG_STATUS_REG_FIFO_BAD_DATA,
	DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA,
	DBG_STATUS_DBG_ARRAY_NOT_SET,
	DBG_STATUS_FILTER_BUG,
	DBG_STATUS_NON_MATCHING_LINES,
	DBG_STATUS_INVALID_TRIGGER_DWORD_OFFSET,
	DBG_STATUS_DBG_BUS_IN_USE,
	MAX_DBG_STATUS
};


/*
 * Debug Storms IDs
 */
enum dbg_storms
{
	DBG_TSTORM_ID,
	DBG_MSTORM_ID,
	DBG_USTORM_ID,
	DBG_XSTORM_ID,
	DBG_YSTORM_ID,
	DBG_PSTORM_ID,
	MAX_DBG_STORMS
};


/*
 * Idle Check data
 */
struct idle_chk_data
{
	u32 buf_size /* Idle check buffer size in dwords */;
	u8 buf_size_set /* Indicates if the idle check buffer size was set (0/1) */;
	u8 reserved1;
	u16 reserved2;
};

/*
 * Debug Tools data (per HW function)
 */
struct dbg_tools_data
{
	struct dbg_grc_data grc /* GRC Dump data */;
	struct dbg_bus_data bus /* Debug Bus data */;
	struct idle_chk_data idle_chk /* Idle Check data */;
	u8 mode_enable[40] /* Indicates if a mode is enabled (0/1) */;
	u8 block_in_reset[88] /* Indicates if a block is in reset state (0/1) */;
	u8 chip_id /* Chip ID (from enum chip_ids) */;
	u8 platform_id /* Platform ID */;
	u8 initialized /* Indicates if the data was initialized */;
	u8 use_dmae /* Indicates if DMAE should be used */;
	u32 num_regs_read /* Numbers of registers that were read since last log */;
};


#endif /* __ECORE_HSI_DEBUG_TOOLS__ */
