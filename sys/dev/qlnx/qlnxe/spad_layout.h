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

/****************************************************************************
 * Name:        spad_layout.h
 *
 * Description: Global definitions
 *
 * Created:     01/09/2013
 *
 ****************************************************************************/
/*
 *          Spad Layout                                NVM CFG                         MCP public
 *==========================================================================================================
 *     MCP_REG_SCRATCH                         REG_RD(MISC_REG_GEN_PURP_CR0)       REG_RD(MISC_REG_SHARED_MEM_ADDR)
 *    +------------------+                      +-------------------------+        +-------------------+
 *    |  Num Sections(4B)|Currently 4           |   Num Sections(4B)      |        |   Num Sections(4B)|Currently 6
 *    +------------------+                      +-------------------------+        +-------------------+
 *    | Offsize(Trace)   |4B -+             +-- | Offset(NVM_CFG1)        |        | Offsize(drv_mb)   |
 *  +-| Offsize(NVM_CFG) |4B  |             |   | (Size is fixed)         |        | Offsize(mfw_mb)   |
 *+-|-| Offsize(Public)  |4B  |             +-> +-------------------------+        | Offsize(global)   |
 *| | | Offsize(Private) |4B  |                 |                         |        | Offsize(path)     |
 *| | +------------------+ <--+                 | nvm_cfg1_glob           |        | Offsize(port)     |
 *| | |                  |                      +-------------------------+        | Offsize(func)     |
 *| | |      Trace       |                      | nvm_cfg1_path 0         |        +-------------------+
 *| +>+------------------+                      | nvm_cfg1_path 1         |        | drv_mb   PF0/2/4..|8 Funcs of engine0
 *|   |                  |                      +-------------------------+        | drv_mb   PF1/3/5..|8 Funcs of engine1
 *|   |     NVM_CFG      |                      | nvm_cfg1_port 0         |        +-------------------+
 *+-> +------------------+                      |            ....         |        | mfw_mb   PF0/2/4..|8 Funcs of engine0
 *    |                  |                      | nvm_cfg1_port 3         |        | mfw_mb   PF1/3/5..|8 Funcs of engine1
 *    |   Public Data    |                      +-------------------------+        +-------------------+
 *    +------------------+   8 Funcs of Engine 0| nvm_cfg1_func PF0/2/4/..|        |                   |
 *    |                  |   8 Funcs of Engine 1| nvm_cfg1_func PF1/3/5/..|        | public_global     |
 *    |   Private Data   |                      +-------------------------+        +-------------------+
 *    +------------------+                                                         | public_path 0     |
 *    |       Code       |                                                         | public_path 1     |
 *    |   Static Area    |                                                         +-------------------+
 *    +---            ---+                                                         | public_port 0     |
 *    |       Code       |                                                         |        ....       |
 *    |      PIM Area    |                                                         | public_port 3     |
 *    +------------------+                                                         +-------------------+
 *                                                                                 | public_func 0/2/4.|8 Funcs of engine0
 *                                                                                 | public_func 1/3/5.|8 Funcs of engine1
 *                                                                                 +-------------------+
*/
#ifndef SPAD_LAYOUT_H
#define SPAD_LAYOUT_H

#ifndef MDUMP_PARSE_TOOL

#define PORT_0		0
#define PORT_1		1
#define PORT_2		2
#define PORT_3		3

#include "mcp_public.h"
#include "mfw_hsi.h"
#include "nvm_cfg.h"

#ifdef MFW
#include "mcp_private.h"
#endif

extern struct spad_layout g_spad;

/* TBD - Consider renaming to MCP_STATIC_SPAD_SIZE, since the real size includes another 64kb */
#define MCP_SPAD_SIZE                       0x00028000	/* 160 KB */

#define SPAD_OFFSET(addr) (((u32)addr - (u32)CPU_SPAD_BASE))
#endif /* MDUMP_PARSE_TOOL */

#define TO_OFFSIZE(_offset, _size) \
    (u32)((((u32)(_offset) >> 2) << OFFSIZE_OFFSET_OFFSET) | \
	  (((u32)(_size) >> 2) << OFFSIZE_SIZE_OFFSET))

enum spad_sections {
	SPAD_SECTION_TRACE,
	SPAD_SECTION_NVM_CFG,
	SPAD_SECTION_PUBLIC,
	SPAD_SECTION_PRIVATE,
	SPAD_SECTION_MAX /* Cannot be modified anymore since ROM relying on this size !! */
};

#ifndef MDUMP_PARSE_TOOL
struct spad_layout {
	struct nvm_cfg nvm_cfg;
	struct mcp_public_data public_data;
#ifdef MFW			/* Drivers will not be compiled with this flag. */
	/* Linux should remove this appearance at all. */
	struct mcp_private_data private_data;
#endif
};

#endif /* MDUMP_PARSE_TOOL */


#define STRUCT_OFFSET(f)    (STATIC_INIT_BASE + __builtin_offsetof(struct static_init, f))

/* This section is located at a fixed location in the beginning of the scratchpad,
 * to ensure that the MCP trace is not run over during MFW upgrade.
 * All the rest of data has a floating location which differs from version to version,
 * and is pointed by the mcp_meta_data below.
 * Moreover, the spad_layout section is part of the MFW firmware, and is loaded with it
 * from nvram in order to clear this portion.
 */

struct static_init {

	u32 num_sections;						/* 0xe20000 */
	offsize_t sections[SPAD_SECTION_MAX];				/* 0xe20004 */
#define SECTION(_sec_) *((offsize_t*)(STRUCT_OFFSET(sections[_sec_])))

#ifdef SECURE_BOOT
	u32 tim_sha256[8];	/* Used by E5 ROM. Do not relocate */
	u32 rom_status_code; 	/* Used by E5 ROM. Do not relocate */
	u32 secure_running_mfw;	/* Instead of the one after the trace_buffer */ /* Used by E5 ROM. Do not relocate */
#define SECURE_RUNNING_MFW *((u32*)(STRUCT_OFFSET(secure_running_mfw)))
#endif

	struct mcp_trace trace;						/* 0xe20014 */

#ifdef MFW
#define MCP_TRACE_P ((struct mcp_trace*)(STRUCT_OFFSET(trace)))
	u8 trace_buffer[MCP_TRACE_SIZE];				/* 0xe20030 */
#define MCP_TRACE_BUF ((u8*)(STRUCT_OFFSET(trace_buffer)))
	/* running_mfw has the same definition as in nvm_map.h.
	 * This bit indicate both the running dir, and the running bundle.
	 * It is set once when the LIM is loaded.
	 */
	u32 running_mfw;						/* 0xe20830 */
#define RUNNING_MFW *((u32*)(STRUCT_OFFSET(running_mfw)))
	u32 build_time;							/* 0xe20834 */
#define MFW_BUILD_TIME *((u32*)(STRUCT_OFFSET(build_time)))
	u32 reset_type;							/* 0xe20838 */
#define RESET_TYPE *((u32*)(STRUCT_OFFSET(reset_type)))
	u32 mfw_secure_mode;						/* 0xe2083c */
#define MFW_SECURE_MODE *((u32*)(STRUCT_OFFSET(mfw_secure_mode)))
	u16 pme_status_pf_bitmap;					/* 0xe20840 */
#define PME_STATUS_PF_BITMAP *((u16*)(STRUCT_OFFSET(pme_status_pf_bitmap)))
	u16 pme_enable_pf_bitmap;					
#define PME_ENABLE_PF_BITMAP *((u16*)(STRUCT_OFFSET(pme_enable_pf_bitmap)))
	u32 mim_nvm_addr;						/* 0xe20844 */
	u32 mim_start_addr;						/* 0xe20848 */
	u32 ah_pcie_link_params; /* 0xe20850 Stores PCIe link configuration at start, so they can be used later also for Hot-Reset, without the need to re-reading them from nvm cfg. */
#define AH_PCIE_LINK_PARAMS_LINK_SPEED_MASK	(0x000000ff)
#define AH_PCIE_LINK_PARAMS_LINK_SPEED_OFFSET	(0)
#define AH_PCIE_LINK_PARAMS_LINK_WIDTH_MASK	(0x0000ff00)
#define AH_PCIE_LINK_PARAMS_LINK_WIDTH_OFFSET	(8)
#define AH_PCIE_LINK_PARAMS_ASPM_MODE_MASK	(0x00ff0000)
#define AH_PCIE_LINK_PARAMS_ASPM_MODE_OFFSET	(16)
#define AH_PCIE_LINK_PARAMS_ASPM_CAP_MASK	(0xff000000)
#define AH_PCIE_LINK_PARAMS_ASPM_CAP_OFFSET	(24)
#define AH_PCIE_LINK_PARAMS *((u32*)(STRUCT_OFFSET(ah_pcie_link_params)))
	
	u32 flags;							/* 0xe20850 */
#define M_GLOB_FLAGS		*((u32*)(STRUCT_OFFSET(flags)))
#define FLAGS_VAUX_REQUIRED		(1 << 0)
#define FLAGS_WAIT_AVS_READY		(1 << 1)
#define FLAGS_FAILURE_ISSUED		(1 << 2)
#define FLAGS_FAILURE_DETECTED		(1 << 3)
#define FLAGS_VAUX			(1 << 4)
#define FLAGS_PERST_ASSERT_OCCURED	(1 << 5)
#define FLAGS_HOT_RESET_STEP2		(1 << 6)
#define FLAGS_MSIX_SYNC_ALLOWED		(1 << 7)
#define FLAGS_PROGRAM_PCI_COMPLETED	(1 << 8)
#define FLAGS_SMBUS_AUX_MODE		(1 << 9)
#define FLAGS_PEND_SMBUS_VMAIN_TO_AUX	(1 << 10)
#define FLAGS_NVM_CFG_EFUSE_FAILURE	(1 << 11)
#define FLAGS_POWER_TRANSITION		(1 << 12)
#define FLAGS_MCTP_CHECK_PUMA_TIMEOUT	(1 << 13)
#define FLAGS_MCTP_TX_PLDM_UPDATE	(1 << 14)
#define FLAGS_MCTP_SENSOR_EVENT	    (1 << 15)
#define FLAGS_PMBUS_ERROR           (1 << 28)
#define FLAGS_OS_DRV_LOADED 		(1 << 29)
#define FLAGS_OVER_TEMP_OCCUR		(1 << 30)
#define FLAGS_FAN_FAIL_OCCUR		(1 << 31)
	u32 rsrv_persist[4]; /* Persist reserved for MFW upgrades */	/* 0xe20854 */
#endif /* MFW */
};

#ifndef MDUMP_PARSE_TOOL
#define NVM_CFG1(x)             g_spad.nvm_cfg.cfg1.x
#define NVM_GLOB(x)		NVM_CFG1(glob).x
#define NVM_GLOB_VAL(n, m, o)   ((NVM_GLOB(n) & m) >> o)
#endif /* MDUMP_PARSE_TOOL */

#endif				/* SPAD_LAYOUT_H */
