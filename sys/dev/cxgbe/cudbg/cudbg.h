/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

/*
 * Chelsio Unified Debug Interface header file.
 * Version 1.1
 */
#ifndef _CUDBG_IF_H_
#define _CUDBG_IF_H_

#ifdef __GNUC__
#define ATTRIBUTE_UNUSED __attribute__ ((unused))
#else
#define ATTRIBUTE_UNUSED
#endif

#if defined(CONFIG_CUDBG_DEBUG)
#define cudbg_debug(pdbg_init, format,  ...) do {\
	pdbg_init->print(format, ##__VA_ARGS__); \
} while (0)
#else
#define cudbg_debug(pdbg_init, format,  ...) do { } while (0)
#endif

#define OUT
#define IN
#define INOUT

/* Error codes */

#define CUDBG_STATUS_SUCCESS		     0
#define CUDBG_STATUS_NOSPACE		    -2
#define CUDBG_STATUS_FLASH_WRITE_FAIL	    -3
#define CUDBG_STATUS_FLASH_READ_FAIL	    -4
#define CUDBG_STATUS_UNDEFINED_OUT_BUF	    -5
#define CUDBG_STATUS_UNDEFINED_CBFN	    -6
#define CUDBG_STATUS_UNDEFINED_PRINTF_CBFN  -7
#define CUDBG_STATUS_ADAP_INVALID	    -8
#define CUDBG_STATUS_FLASH_EMPTY	    -9
#define CUDBG_STATUS_NO_ADAPTER		    -10
#define CUDBG_STATUS_NO_SIGNATURE	    -11
#define CUDBG_STATUS_MULTIPLE_REG	    -12
#define CUDBG_STATUS_UNREGISTERED	    -13
#define CUDBG_STATUS_UNDEFINED_ENTITY	    -14
#define CUDBG_STATUS_REG_FAIlED		    -15
#define CUDBG_STATUS_DEVLOG_FAILED	    -16
#define CUDBG_STATUS_SMALL_BUFF		    -17
#define CUDBG_STATUS_CHKSUM_MISSMATCH	    -18
#define CUDBG_STATUS_NO_SCRATCH_MEM	    -19
#define CUDBG_STATUS_OUTBUFF_OVERFLOW	    -20
#define CUDBG_STATUS_INVALID_BUFF	    -21  /* Invalid magic */
#define CUDBG_STATUS_FILE_OPEN_FAIL	    -22
#define CUDBG_STATUS_DEVLOG_INT_FAIL	    -23
#define CUDBG_STATUS_ENTITY_NOT_FOUND	    -24
#define CUDBG_STATUS_DECOMPRESS_FAIL	    -25
#define CUDBG_STATUS_BUFFER_SHORT	    -26
#define CUDBG_METADATA_VERSION_MISMATCH     -27
#define CUDBG_STATUS_NOT_IMPLEMENTED	    -28
#define CUDBG_SYSTEM_ERROR		    -29
#define CUDBG_STATUS_MMAP_FAILED	    -30
#define CUDBG_STATUS_FILE_WRITE_FAILED	    -31
#define CUDBG_STATUS_CCLK_NOT_DEFINED	    -32
#define CUDBG_STATUS_FLASH_FULL            -33
#define CUDBG_STATUS_SECTOR_EMPTY          -34
#define CUDBG_STATUS_ENTITY_NOT_REQUESTED  -35
#define CUDBG_STATUS_NOT_SUPPORTED         -36
#define CUDBG_STATUS_FILE_READ_FAILED      -37
#define CUDBG_STATUS_CORRUPTED             -38
#define CUDBG_STATUS_INVALID_INDEX         -39

#define CUDBG_MAJOR_VERSION		    1
#define CUDBG_MINOR_VERSION		    14
#define CUDBG_BUILD_VERSION		    0

#define CUDBG_FILE_NAME_LEN 256
#define CUDBG_DIR_NAME_LEN  256
#define CUDBG_MAX_BITMAP_LEN 16

static char ATTRIBUTE_UNUSED * err_msg[] = {
	"Success",
	"Unknown",
	"No space",
	"Flash write fail",
	"Flash read fail",
	"Undefined out buf",
	"Callback function undefined",
	"Print callback function undefined",
	"ADAP invalid",
	"Flash empty",
	"No adapter",
	"No signature",
	"Multiple registration",
	"Unregistered",
	"Undefined entity",
	"Reg failed",
	"Devlog failed",
	"Small buff",
	"Checksum mismatch",
	"No scratch memory",
	"Outbuff overflow",
	"Invalid buffer",
	"File open fail",
	"Devlog int fail",
	"Entity not found",
	"Decompress fail",
	"Buffer short",
	"Version mismatch",
	"Not implemented",
	"System error",
	"Mmap failed",
	"File write failed",
	"cclk not defined",
	"Flash full",
	"Sector empty",
	"Entity not requested",
	"Not supported",
	"File read fail",
	"Corrupted",
	"Invalid Index"
};

enum CUDBG_DBG_ENTITY_TYPE {
	CUDBG_ALL	   = 0,
	CUDBG_REG_DUMP	   = 1,
	CUDBG_DEV_LOG	   = 2,
	CUDBG_CIM_LA	   = 3,
	CUDBG_CIM_MA_LA    = 4,
	CUDBG_CIM_QCFG	   = 5,
	CUDBG_CIM_IBQ_TP0  = 6,
	CUDBG_CIM_IBQ_TP1  = 7,
	CUDBG_CIM_IBQ_ULP  = 8,
	CUDBG_CIM_IBQ_SGE0 = 9,
	CUDBG_CIM_IBQ_SGE1 = 10,
	CUDBG_CIM_IBQ_NCSI = 11,
	CUDBG_CIM_OBQ_ULP0 = 12,
	CUDBG_CIM_OBQ_ULP1 = 13,
	CUDBG_CIM_OBQ_ULP2 = 14,
	CUDBG_CIM_OBQ_ULP3 = 15,
	CUDBG_CIM_OBQ_SGE  = 16,
	CUDBG_CIM_OBQ_NCSI = 17,
	CUDBG_EDC0	   = 18,
	CUDBG_EDC1	   = 19,
	CUDBG_MC0	   = 20,
	CUDBG_MC1	   = 21,
	CUDBG_RSS	   = 22,
	CUDBG_RSS_PF_CONF  = 23,
	CUDBG_RSS_KEY	   = 24,
	CUDBG_RSS_VF_CONF  = 25,
	CUDBG_RSS_CONF	   = 26,
	CUDBG_PATH_MTU	   = 27,
	CUDBG_SW_STATE	   = 28,
	CUDBG_WTP	   = 29,
	CUDBG_PM_STATS	   = 30,
	CUDBG_HW_SCHED	   = 31,
	CUDBG_TCP_STATS    = 32,
	CUDBG_TP_ERR_STATS = 33,
	CUDBG_FCOE_STATS   = 34,
	CUDBG_RDMA_STATS   = 35,
	CUDBG_TP_INDIRECT  = 36,
	CUDBG_SGE_INDIRECT = 37,
	CUDBG_CPL_STATS    = 38,
	CUDBG_DDP_STATS    = 39,
	CUDBG_WC_STATS	   = 40,
	CUDBG_ULPRX_LA	   = 41,
	CUDBG_LB_STATS	   = 42,
	CUDBG_TP_LA	   = 43,
	CUDBG_MEMINFO	   = 44,
	CUDBG_CIM_PIF_LA   = 45,
	CUDBG_CLK	   = 46,
	CUDBG_CIM_OBQ_RXQ0 = 47,
	CUDBG_CIM_OBQ_RXQ1 = 48,
	CUDBG_MAC_STATS    = 49,
	CUDBG_PCIE_INDIRECT = 50,
	CUDBG_PM_INDIRECT  = 51,
	CUDBG_FULL	   = 52,
	CUDBG_TX_RATE	   = 53,
	CUDBG_TID_INFO	   = 54,
	CUDBG_PCIE_CONFIG  = 55,
	CUDBG_DUMP_CONTEXT = 56,
	CUDBG_MPS_TCAM	   = 57,
	CUDBG_VPD_DATA	   = 58,
	CUDBG_LE_TCAM	   = 59,
	CUDBG_CCTRL	   = 60,
	CUDBG_MA_INDIRECT  = 61,
	CUDBG_ULPTX_LA	   = 62,
	CUDBG_EXT_ENTITY   = 63,
	CUDBG_UP_CIM_INDIRECT = 64,
	CUDBG_PBT_TABLE    = 65,
	CUDBG_MBOX_LOG     = 66,
	CUDBG_HMA_INDIRECT = 67,
	CUDBG_MAX_ENTITY   = 68,
};

#define ENTITY_FLAG_NULL 0
#define ENTITY_FLAG_REGISTER 1
#define ENTITY_FLAG_BINARY 2
#define ENTITY_FLAG_FW_NO_ATTACH    3

/* file_name matches Linux cxgb4 debugfs entry names. */
struct el {char *name; char *file_name; int bit; u32 flag; };
static struct el ATTRIBUTE_UNUSED entity_list[] = {
	{"all", "all", CUDBG_ALL, ENTITY_FLAG_NULL},
	{"regdump", "regdump", CUDBG_REG_DUMP, 1 << ENTITY_FLAG_REGISTER},
	/* {"reg", CUDBG_REG_DUMP},*/
	{"devlog", "devlog", CUDBG_DEV_LOG, ENTITY_FLAG_NULL},
	{"cimla", "cim_la", CUDBG_CIM_LA, ENTITY_FLAG_NULL},
	{"cimmala", "cim_ma_la", CUDBG_CIM_MA_LA, ENTITY_FLAG_NULL},
	{"cimqcfg", "cim_qcfg", CUDBG_CIM_QCFG, ENTITY_FLAG_NULL},
	{"ibqtp0", "ibq_tp0", CUDBG_CIM_IBQ_TP0, ENTITY_FLAG_NULL},
	{"ibqtp1", "ibq_tp1", CUDBG_CIM_IBQ_TP1, ENTITY_FLAG_NULL},
	{"ibqulp", "ibq_ulp", CUDBG_CIM_IBQ_ULP, ENTITY_FLAG_NULL},
	{"ibqsge0", "ibq_sge0", CUDBG_CIM_IBQ_SGE0, ENTITY_FLAG_NULL},
	{"ibqsge1", "ibq_sge1", CUDBG_CIM_IBQ_SGE1, ENTITY_FLAG_NULL},
	{"ibqncsi", "ibq_ncsi", CUDBG_CIM_IBQ_NCSI, ENTITY_FLAG_NULL},
	{"obqulp0", "obq_ulp0", CUDBG_CIM_OBQ_ULP0, ENTITY_FLAG_NULL},
	/* {"cimobqulp1", CUDBG_CIM_OBQ_ULP1},*/
	{"obqulp1", "obq_ulp1", CUDBG_CIM_OBQ_ULP1, ENTITY_FLAG_NULL},
	{"obqulp2", "obq_ulp2", CUDBG_CIM_OBQ_ULP2, ENTITY_FLAG_NULL},
	{"obqulp3", "obq_ulp3", CUDBG_CIM_OBQ_ULP3, ENTITY_FLAG_NULL},
	{"obqsge", "obq_sge", CUDBG_CIM_OBQ_SGE, ENTITY_FLAG_NULL},
	{"obqncsi", "obq_ncsi", CUDBG_CIM_OBQ_NCSI, ENTITY_FLAG_NULL},
	{"edc0", "edc0", CUDBG_EDC0, (1 << ENTITY_FLAG_BINARY)},
	{"edc1", "edc1", CUDBG_EDC1, (1 << ENTITY_FLAG_BINARY)},
	{"mc0", "mc0", CUDBG_MC0, (1 << ENTITY_FLAG_BINARY)},
	{"mc1", "mc1", CUDBG_MC1, (1 << ENTITY_FLAG_BINARY)},
	{"rss", "rss", CUDBG_RSS, ENTITY_FLAG_NULL},
	{"rss_pf_config", "rss_pf_config", CUDBG_RSS_PF_CONF, ENTITY_FLAG_NULL},
	{"rss_key", "rss_key", CUDBG_RSS_KEY, ENTITY_FLAG_NULL},
	{"rss_vf_config", "rss_vf_config", CUDBG_RSS_VF_CONF, ENTITY_FLAG_NULL},
	{"rss_config", "rss_config", CUDBG_RSS_CONF, ENTITY_FLAG_NULL},
	{"pathmtu", "path_mtus", CUDBG_PATH_MTU, ENTITY_FLAG_NULL},
	{"swstate", "sw_state", CUDBG_SW_STATE, ENTITY_FLAG_NULL},
	{"wtp", "wtp", CUDBG_WTP, ENTITY_FLAG_NULL},
	{"pmstats", "pm_stats", CUDBG_PM_STATS, ENTITY_FLAG_NULL},
	{"hwsched", "hw_sched", CUDBG_HW_SCHED, ENTITY_FLAG_NULL},
	{"tcpstats", "tcp_stats", CUDBG_TCP_STATS, ENTITY_FLAG_NULL},
	{"tperrstats", "tp_err_stats", CUDBG_TP_ERR_STATS, ENTITY_FLAG_NULL},
	{"fcoestats", "fcoe_stats", CUDBG_FCOE_STATS, ENTITY_FLAG_NULL},
	{"rdmastats", "rdma_stats", CUDBG_RDMA_STATS, ENTITY_FLAG_NULL},
	{"tpindirect", "tp_indirect", CUDBG_TP_INDIRECT,
					1 << ENTITY_FLAG_REGISTER},
	{"sgeindirect", "sge_indirect", CUDBG_SGE_INDIRECT,
					1 << ENTITY_FLAG_REGISTER},
	{"cplstats", "cpl_stats", CUDBG_CPL_STATS, ENTITY_FLAG_NULL},
	{"ddpstats", "ddp_stats", CUDBG_DDP_STATS, ENTITY_FLAG_NULL},
	{"wcstats", "wc_stats", CUDBG_WC_STATS, ENTITY_FLAG_NULL},
	{"ulprxla", "ulprx_la", CUDBG_ULPRX_LA, ENTITY_FLAG_NULL},
	{"lbstats", "lb_stats", CUDBG_LB_STATS, ENTITY_FLAG_NULL},
	{"tpla", "tp_la", CUDBG_TP_LA, ENTITY_FLAG_NULL},
	{"meminfo", "meminfo", CUDBG_MEMINFO, ENTITY_FLAG_NULL},
	{"cimpifla", "cim_pif_la", CUDBG_CIM_PIF_LA, ENTITY_FLAG_NULL},
	{"clk", "clk", CUDBG_CLK, ENTITY_FLAG_NULL},
	{"obq_sge_rx_q0", "obq_sge_rx_q0", CUDBG_CIM_OBQ_RXQ0,
					ENTITY_FLAG_NULL},
	{"obq_sge_rx_q1", "obq_sge_rx_q1", CUDBG_CIM_OBQ_RXQ1,
					ENTITY_FLAG_NULL},
	{"macstats", "mac_stats", CUDBG_MAC_STATS, ENTITY_FLAG_NULL},
	{"pcieindirect", "pcie_indirect", CUDBG_PCIE_INDIRECT,
					1 << ENTITY_FLAG_REGISTER},
	{"pmindirect", "pm_indirect", CUDBG_PM_INDIRECT,
				1 << ENTITY_FLAG_REGISTER},
	{"full", "full", CUDBG_FULL, ENTITY_FLAG_NULL},
	{"txrate", "tx_rate", CUDBG_TX_RATE, ENTITY_FLAG_NULL},
	{"tidinfo", "tids", CUDBG_TID_INFO, ENTITY_FLAG_NULL |
				(1 << ENTITY_FLAG_FW_NO_ATTACH)},
	{"pcieconfig", "pcie_config", CUDBG_PCIE_CONFIG, ENTITY_FLAG_NULL},
	{"dumpcontext", "dump_context", CUDBG_DUMP_CONTEXT, ENTITY_FLAG_NULL},
	{"mpstcam", "mps_tcam", CUDBG_MPS_TCAM, ENTITY_FLAG_NULL},
	{"vpddata", "vpd_data", CUDBG_VPD_DATA, ENTITY_FLAG_NULL},
	{"letcam", "le_tcam", CUDBG_LE_TCAM, ENTITY_FLAG_NULL},
	{"cctrl", "cctrl", CUDBG_CCTRL, ENTITY_FLAG_NULL},
	{"maindirect", "ma_indirect", CUDBG_MA_INDIRECT,
				1 << ENTITY_FLAG_REGISTER},
	{"ulptxla", "ulptx_la", CUDBG_ULPTX_LA, ENTITY_FLAG_NULL},
	{"extentity", "ext_entity", CUDBG_EXT_ENTITY, ENTITY_FLAG_NULL},
	{"upcimindirect", "up_cim_indirect", CUDBG_UP_CIM_INDIRECT,
					1 << ENTITY_FLAG_REGISTER},
	{"pbttables", "pbt_tables", CUDBG_PBT_TABLE, ENTITY_FLAG_NULL},
	{"mboxlog", "mboxlog", CUDBG_MBOX_LOG, ENTITY_FLAG_NULL},
	{"hmaindirect", "hma_indirect", CUDBG_HMA_INDIRECT,
				1 << ENTITY_FLAG_REGISTER},
};

typedef int (*cudbg_print_cb) (char *str, ...);

struct cudbg_init_hdr {
	u8   major_ver;
	u8   minor_ver;
	u8   build_ver;
	u8   res;
	u16  init_struct_size;
};

struct cudbg_flash_hdr {
	u32 signature;
	u8 major_ver;
	u8 minor_ver;
	u8 build_ver;
	u8 res;
	u64 timestamp;
	u64 time_res;
	u32 hdr_len;
	u32 data_len;
	u32 hdr_flags;
	u32 sec_seq_no;
	u32 reserved[22];
};

struct cudbg_param {
	u16			 param_type;
	u16			 reserved;
	union {
		struct {
			u32 memtype;	/* which memory (EDC0, EDC1, MC) */
			u32 start;	/* start of log in firmware memory */
			u32 size;	/* size of log */
		} devlog_param;
		struct {
			struct mbox_cmd_log *log;
			u16 mbox_cmds;
		} mboxlog_param;
		struct {
			u8 caller_string[100];
			u8 os_type;
		} sw_state_param;
		u64 time;
		u8 tcb_bit_param;
		void *adap;
		void *access_lock;
	} u;
};

/* params for tcb_bit_param */
#define CUDBG_TCB_BRIEF_PARAM      0x1
#define CUDBG_TCB_FROM_CARD_PARAM  0x2
#define CUDBG_TCB_AS_SCB_PARAM     0x4

/*
 * * What is OFFLINE_VIEW_ONLY mode?
 *
 * cudbg frame work will be used only to interpret previously collected
 * data store in a file (i.e NOT hw flash)
 */

struct cudbg_init {
	struct cudbg_init_hdr	 header;
	struct adapter		 *adap;		 /* Pointer to adapter structure
						    with filled fields */
	cudbg_print_cb		 print;		 /* Platform dependent print
						    function */
	u32			 verbose:1;	 /* Turn on verbose print */
	u32			 use_flash:1;	 /* Use flash to collect or view
						    debug */
	u32			 full_mode:1;	 /* If set, cudbg will pull in
						    common code */
	u32			 no_compress:1;  /* Dont compress will storing
						    the collected debug */
	u32			 info:1;	 /* Show just the info, Dont
						    interpret */
	u32			 reserved:27;
	u8			 dbg_bitmap[CUDBG_MAX_BITMAP_LEN];
						/* Bit map to select the dbg
						    data type to be collected
						    or viewed */
};


/********************************* Helper functions *************************/
static inline void set_dbg_bitmap(u8 *bitmap, enum CUDBG_DBG_ENTITY_TYPE type)
{
	int index = type / 8;
	int bit = type % 8;

	bitmap[index] |= (1 << bit);
}

static inline void reset_dbg_bitmap(u8 *bitmap, enum CUDBG_DBG_ENTITY_TYPE type)
{
	int index = type / 8;
	int bit = type % 8;

	bitmap[index] &= ~(1 << bit);
}

/********************************* End of Helper functions
 * *************************/

/* API Prototypes */

/**
 *  cudbg_alloc_handle - Allocates and initializes a handle that represents
 *  cudbg state.  Needs to called first before calling any other function.
 *
 *  returns a pointer to memory that has a cudbg_init structure at the begining
 *  and enough space after that for internal book keeping.
 */

void *cudbg_alloc_handle(void);
static inline struct cudbg_init *cudbg_get_init(void *handle)
{
	return (handle);
}

/**
 *  cudbg_collect - Collect and store debug information.
 *  ## Parameters ##
 *  @handle : A pointer returned by cudbg_alloc_handle.
 *  @outbuf : pointer to output buffer, to store the collected information
 *	      or to use it as a scratch buffer in case HW flash is used to
 *	      store the debug information.
 *  @outbuf_size : Size of output buffer.
 *  ##	Return ##
 *  If the function succeeds, the return value will be size of debug information
 *  collected and stored.
 *  -ve value represent error.
 */
int cudbg_collect(void *handle, void *outbuf, u32 *outbuf_size);

/**
 *  cudbg_free_handle - Release cudbg resources.
 *  ## Parameters ##
 *  @handle : A pointer returned by cudbg_alloc_handle.
 */

void cudbg_free_handle(IN void *handle);

/**
 *  cudbg_read_flash_data - Read cudbg “flash” header from adapter flash.
 *  			    This will be used by the consumer mainly to
 *  			    know the size of the data in flash.
 *  ## Parameters ##
 *  @handle : A pointer returned by cudbg_hello.
 *  @data : A pointer to data/header buffer
 */

int cudbg_read_flash_details(void *handle, struct cudbg_flash_hdr *data);

/**
 *  cudbg_read_flash_data - Read cudbg dump contents stored in flash.
 *  ## Parameters ##
 *  @handle : A pointer returned by cudbg_hello.
 *  @data_buf : A pointer to data buffer.
 *  @data_buf_size : Data buffer size.
 */

int cudbg_read_flash_data(void *handle, void *data_buf, u32 data_buf_size);

#endif /* _CUDBG_IF_H_ */
