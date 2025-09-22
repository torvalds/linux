/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_ring.h"
#include "vi.h"
#include "vi_structs.h"
#include "vid.h"
#include "amdgpu_ucode.h"
#include "amdgpu_atombios.h"
#include "atombios_i2c.h"
#include "clearstate_vi.h"

#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "gca/gfx_8_0_sh_mask.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#include "smu/smu_7_1_3_d.h"

#include "ivsrcid/ivsrcid_vislands30.h"

#define GFX8_NUM_GFX_RINGS     1
#define GFX8_MEC_HPD_SIZE 4096

#define TOPAZ_GB_ADDR_CONFIG_GOLDEN 0x22010001
#define CARRIZO_GB_ADDR_CONFIG_GOLDEN 0x22010001
#define POLARIS11_GB_ADDR_CONFIG_GOLDEN 0x22011002
#define TONGA_GB_ADDR_CONFIG_GOLDEN 0x22011003

#define ARRAY_MODE(x)					((x) << GB_TILE_MODE0__ARRAY_MODE__SHIFT)
#define PIPE_CONFIG(x)					((x) << GB_TILE_MODE0__PIPE_CONFIG__SHIFT)
#define TILE_SPLIT(x)					((x) << GB_TILE_MODE0__TILE_SPLIT__SHIFT)
#define MICRO_TILE_MODE_NEW(x)				((x) << GB_TILE_MODE0__MICRO_TILE_MODE_NEW__SHIFT)
#define SAMPLE_SPLIT(x)					((x) << GB_TILE_MODE0__SAMPLE_SPLIT__SHIFT)
#define BANK_WIDTH(x)					((x) << GB_MACROTILE_MODE0__BANK_WIDTH__SHIFT)
#define BANK_HEIGHT(x)					((x) << GB_MACROTILE_MODE0__BANK_HEIGHT__SHIFT)
#define MACRO_TILE_ASPECT(x)				((x) << GB_MACROTILE_MODE0__MACRO_TILE_ASPECT__SHIFT)
#define NUM_BANKS(x)					((x) << GB_MACROTILE_MODE0__NUM_BANKS__SHIFT)

#define RLC_CGTT_MGCG_OVERRIDE__CPF_MASK            0x00000001L
#define RLC_CGTT_MGCG_OVERRIDE__RLC_MASK            0x00000002L
#define RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK           0x00000004L
#define RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK           0x00000008L
#define RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK           0x00000010L
#define RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK           0x00000020L

/* BPM SERDES CMD */
#define SET_BPM_SERDES_CMD    1
#define CLE_BPM_SERDES_CMD    0

/* BPM Register Address*/
enum {
	BPM_REG_CGLS_EN = 0,        /* Enable/Disable CGLS */
	BPM_REG_CGLS_ON,            /* ON/OFF CGLS: shall be controlled by RLC FW */
	BPM_REG_CGCG_OVERRIDE,      /* Set/Clear CGCG Override */
	BPM_REG_MGCG_OVERRIDE,      /* Set/Clear MGCG Override */
	BPM_REG_FGCG_OVERRIDE,      /* Set/Clear FGCG Override */
	BPM_REG_FGCG_MAX
};

#define RLC_FormatDirectRegListLength        14

MODULE_FIRMWARE("amdgpu/carrizo_ce.bin");
MODULE_FIRMWARE("amdgpu/carrizo_pfp.bin");
MODULE_FIRMWARE("amdgpu/carrizo_me.bin");
MODULE_FIRMWARE("amdgpu/carrizo_mec.bin");
MODULE_FIRMWARE("amdgpu/carrizo_mec2.bin");
MODULE_FIRMWARE("amdgpu/carrizo_rlc.bin");

MODULE_FIRMWARE("amdgpu/stoney_ce.bin");
MODULE_FIRMWARE("amdgpu/stoney_pfp.bin");
MODULE_FIRMWARE("amdgpu/stoney_me.bin");
MODULE_FIRMWARE("amdgpu/stoney_mec.bin");
MODULE_FIRMWARE("amdgpu/stoney_rlc.bin");

MODULE_FIRMWARE("amdgpu/tonga_ce.bin");
MODULE_FIRMWARE("amdgpu/tonga_pfp.bin");
MODULE_FIRMWARE("amdgpu/tonga_me.bin");
MODULE_FIRMWARE("amdgpu/tonga_mec.bin");
MODULE_FIRMWARE("amdgpu/tonga_mec2.bin");
MODULE_FIRMWARE("amdgpu/tonga_rlc.bin");

MODULE_FIRMWARE("amdgpu/topaz_ce.bin");
MODULE_FIRMWARE("amdgpu/topaz_pfp.bin");
MODULE_FIRMWARE("amdgpu/topaz_me.bin");
MODULE_FIRMWARE("amdgpu/topaz_mec.bin");
MODULE_FIRMWARE("amdgpu/topaz_rlc.bin");

MODULE_FIRMWARE("amdgpu/fiji_ce.bin");
MODULE_FIRMWARE("amdgpu/fiji_pfp.bin");
MODULE_FIRMWARE("amdgpu/fiji_me.bin");
MODULE_FIRMWARE("amdgpu/fiji_mec.bin");
MODULE_FIRMWARE("amdgpu/fiji_mec2.bin");
MODULE_FIRMWARE("amdgpu/fiji_rlc.bin");

MODULE_FIRMWARE("amdgpu/polaris10_ce.bin");
MODULE_FIRMWARE("amdgpu/polaris10_ce_2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_pfp.bin");
MODULE_FIRMWARE("amdgpu/polaris10_pfp_2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_me.bin");
MODULE_FIRMWARE("amdgpu/polaris10_me_2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec_2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec2_2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_rlc.bin");

MODULE_FIRMWARE("amdgpu/polaris11_ce.bin");
MODULE_FIRMWARE("amdgpu/polaris11_ce_2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_pfp.bin");
MODULE_FIRMWARE("amdgpu/polaris11_pfp_2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_me.bin");
MODULE_FIRMWARE("amdgpu/polaris11_me_2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec_2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec2_2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_rlc.bin");

MODULE_FIRMWARE("amdgpu/polaris12_ce.bin");
MODULE_FIRMWARE("amdgpu/polaris12_ce_2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_pfp.bin");
MODULE_FIRMWARE("amdgpu/polaris12_pfp_2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_me.bin");
MODULE_FIRMWARE("amdgpu/polaris12_me_2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_mec.bin");
MODULE_FIRMWARE("amdgpu/polaris12_mec_2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_mec2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_mec2_2.bin");
MODULE_FIRMWARE("amdgpu/polaris12_rlc.bin");

MODULE_FIRMWARE("amdgpu/vegam_ce.bin");
MODULE_FIRMWARE("amdgpu/vegam_pfp.bin");
MODULE_FIRMWARE("amdgpu/vegam_me.bin");
MODULE_FIRMWARE("amdgpu/vegam_mec.bin");
MODULE_FIRMWARE("amdgpu/vegam_mec2.bin");
MODULE_FIRMWARE("amdgpu/vegam_rlc.bin");

static const struct amdgpu_gds_reg_offset amdgpu_gds_reg_offset[] =
{
	{mmGDS_VMID0_BASE, mmGDS_VMID0_SIZE, mmGDS_GWS_VMID0, mmGDS_OA_VMID0},
	{mmGDS_VMID1_BASE, mmGDS_VMID1_SIZE, mmGDS_GWS_VMID1, mmGDS_OA_VMID1},
	{mmGDS_VMID2_BASE, mmGDS_VMID2_SIZE, mmGDS_GWS_VMID2, mmGDS_OA_VMID2},
	{mmGDS_VMID3_BASE, mmGDS_VMID3_SIZE, mmGDS_GWS_VMID3, mmGDS_OA_VMID3},
	{mmGDS_VMID4_BASE, mmGDS_VMID4_SIZE, mmGDS_GWS_VMID4, mmGDS_OA_VMID4},
	{mmGDS_VMID5_BASE, mmGDS_VMID5_SIZE, mmGDS_GWS_VMID5, mmGDS_OA_VMID5},
	{mmGDS_VMID6_BASE, mmGDS_VMID6_SIZE, mmGDS_GWS_VMID6, mmGDS_OA_VMID6},
	{mmGDS_VMID7_BASE, mmGDS_VMID7_SIZE, mmGDS_GWS_VMID7, mmGDS_OA_VMID7},
	{mmGDS_VMID8_BASE, mmGDS_VMID8_SIZE, mmGDS_GWS_VMID8, mmGDS_OA_VMID8},
	{mmGDS_VMID9_BASE, mmGDS_VMID9_SIZE, mmGDS_GWS_VMID9, mmGDS_OA_VMID9},
	{mmGDS_VMID10_BASE, mmGDS_VMID10_SIZE, mmGDS_GWS_VMID10, mmGDS_OA_VMID10},
	{mmGDS_VMID11_BASE, mmGDS_VMID11_SIZE, mmGDS_GWS_VMID11, mmGDS_OA_VMID11},
	{mmGDS_VMID12_BASE, mmGDS_VMID12_SIZE, mmGDS_GWS_VMID12, mmGDS_OA_VMID12},
	{mmGDS_VMID13_BASE, mmGDS_VMID13_SIZE, mmGDS_GWS_VMID13, mmGDS_OA_VMID13},
	{mmGDS_VMID14_BASE, mmGDS_VMID14_SIZE, mmGDS_GWS_VMID14, mmGDS_OA_VMID14},
	{mmGDS_VMID15_BASE, mmGDS_VMID15_SIZE, mmGDS_GWS_VMID15, mmGDS_OA_VMID15}
};

static const u32 golden_settings_tonga_a11[] =
{
	mmCB_HW_CONTROL, 0xfffdf3cf, 0x00007208,
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_FIFO_DEPTH_CNTL, 0x000003ff, 0x000000fc,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0000003c,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000002fb,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x0000543b,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0xa9210876,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 tonga_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002A,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF
};

static const u32 tonga_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x40000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU6_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU6_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU6_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU6_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU6_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU7_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU7_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU7_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU7_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU7_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 golden_settings_vegam_a11[] =
{
	mmCB_HW_CONTROL, 0x0001f3cf, 0x00007208,
	mmCB_HW_CONTROL_2, 0x0f000000, 0x0d000000,
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x3a00161a,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x0000002e,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmRLC_CGCG_CGLS_CTRL_3D, 0xffffffff, 0x0001003c,
	mmSQ_CONFIG, 0x07f80000, 0x01180000,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f7,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x32761054,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 vegam_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF,
};

static const u32 golden_settings_polaris11_a11[] =
{
	mmCB_HW_CONTROL, 0x0000f3cf, 0x00007208,
	mmCB_HW_CONTROL_2, 0x0f000000, 0x0f000000,
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmRLC_CGCG_CGLS_CTRL_3D, 0xffffffff, 0x0001003c,
	mmSQ_CONFIG, 0x07f80000, 0x01180000,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f3,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00003210,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 polaris11_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011002,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF,
};

static const u32 golden_settings_polaris10_a11[] =
{
	mmATC_MISC_CG, 0x000c0fc0, 0x000c0200,
	mmCB_HW_CONTROL, 0x0001f3cf, 0x00007208,
	mmCB_HW_CONTROL_2, 0x0f000000, 0x0f000000,
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x0000002a,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmRLC_CGCG_CGLS_CTRL_3D, 0xffffffff, 0x0001003c,
	mmSQ_CONFIG, 0x07f80000, 0x07180000,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f7,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 polaris10_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002A,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF,
};

static const u32 fiji_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x3a00161a,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002e,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmSPI_CONFIG_CNTL_1, 0x0000000f, 0x00000009,
};

static const u32 golden_settings_fiji_a10[] =
{
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000ff,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 fiji_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x40000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 golden_settings_iceland_a11[] =
{
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0xc0000000, 0xc0000000,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0000003c,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f1,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00000010,
};

static const u32 iceland_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF
};

static const u32 iceland_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0xff000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x0f840f87,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x0f840f87,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
};

static const u32 cz_golden_settings_a11[] =
{
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x00000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0000003c,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x00010000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x0000000f, 0x000000f3,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00001302
};

static const u32 cz_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF
};

static const u32 cz_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU6_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU6_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU6_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU6_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU6_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU7_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU7_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU7_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU7_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU7_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 stoney_golden_settings_a11[] =
{
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x0000000f, 0x000000f1,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x10101010,
};

static const u32 stoney_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000000,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x12010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00FF7FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00FF7FAF,
};

static const u32 stoney_mgcg_cgcg_init[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	mmCP_MEM_SLP_CNTL, 0xffffffff, 0x00020201,
	mmRLC_MEM_SLP_CNTL, 0xffffffff, 0x00020201,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
};


static const char * const sq_edc_source_names[] = {
	"SQ_EDC_INFO_SOURCE_INVALID: No EDC error has occurred",
	"SQ_EDC_INFO_SOURCE_INST: EDC source is Instruction Fetch",
	"SQ_EDC_INFO_SOURCE_SGPR: EDC source is SGPR or SQC data return",
	"SQ_EDC_INFO_SOURCE_VGPR: EDC source is VGPR",
	"SQ_EDC_INFO_SOURCE_LDS: EDC source is LDS",
	"SQ_EDC_INFO_SOURCE_GDS: EDC source is GDS",
	"SQ_EDC_INFO_SOURCE_TA: EDC source is TA",
};

static void gfx_v8_0_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v8_0_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v8_0_set_gds_init(struct amdgpu_device *adev);
static void gfx_v8_0_set_rlc_funcs(struct amdgpu_device *adev);
static u32 gfx_v8_0_get_csb_size(struct amdgpu_device *adev);
static void gfx_v8_0_get_cu_info(struct amdgpu_device *adev);
static void gfx_v8_0_ring_emit_ce_meta(struct amdgpu_ring *ring);
static void gfx_v8_0_ring_emit_de_meta(struct amdgpu_ring *ring);

#define CG_ACLK_CNTL__ACLK_DIVIDER_MASK                    0x0000007fL
#define CG_ACLK_CNTL__ACLK_DIVIDER__SHIFT                  0x00000000L

static void gfx_v8_0_init_golden_registers(struct amdgpu_device *adev)
{
	uint32_t data;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		amdgpu_device_program_register_sequence(adev,
							iceland_mgcg_cgcg_init,
							ARRAY_SIZE(iceland_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_iceland_a11,
							ARRAY_SIZE(golden_settings_iceland_a11));
		amdgpu_device_program_register_sequence(adev,
							iceland_golden_common_all,
							ARRAY_SIZE(iceland_golden_common_all));
		break;
	case CHIP_FIJI:
		amdgpu_device_program_register_sequence(adev,
							fiji_mgcg_cgcg_init,
							ARRAY_SIZE(fiji_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_fiji_a10,
							ARRAY_SIZE(golden_settings_fiji_a10));
		amdgpu_device_program_register_sequence(adev,
							fiji_golden_common_all,
							ARRAY_SIZE(fiji_golden_common_all));
		break;

	case CHIP_TONGA:
		amdgpu_device_program_register_sequence(adev,
							tonga_mgcg_cgcg_init,
							ARRAY_SIZE(tonga_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							golden_settings_tonga_a11,
							ARRAY_SIZE(golden_settings_tonga_a11));
		amdgpu_device_program_register_sequence(adev,
							tonga_golden_common_all,
							ARRAY_SIZE(tonga_golden_common_all));
		break;
	case CHIP_VEGAM:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_vegam_a11,
							ARRAY_SIZE(golden_settings_vegam_a11));
		amdgpu_device_program_register_sequence(adev,
							vegam_golden_common_all,
							ARRAY_SIZE(vegam_golden_common_all));
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris11_a11,
							ARRAY_SIZE(golden_settings_polaris11_a11));
		amdgpu_device_program_register_sequence(adev,
							polaris11_golden_common_all,
							ARRAY_SIZE(polaris11_golden_common_all));
		break;
	case CHIP_POLARIS10:
		amdgpu_device_program_register_sequence(adev,
							golden_settings_polaris10_a11,
							ARRAY_SIZE(golden_settings_polaris10_a11));
		amdgpu_device_program_register_sequence(adev,
							polaris10_golden_common_all,
							ARRAY_SIZE(polaris10_golden_common_all));
		data = RREG32_SMC(ixCG_ACLK_CNTL);
		data &= ~CG_ACLK_CNTL__ACLK_DIVIDER_MASK;
		data |= 0x18 << CG_ACLK_CNTL__ACLK_DIVIDER__SHIFT;
		WREG32_SMC(ixCG_ACLK_CNTL, data);
		if ((adev->pdev->device == 0x67DF) && (adev->pdev->revision == 0xc7) &&
		    ((adev->pdev->subsystem_device == 0xb37 && adev->pdev->subsystem_vendor == 0x1002) ||
		     (adev->pdev->subsystem_device == 0x4a8 && adev->pdev->subsystem_vendor == 0x1043) ||
		     (adev->pdev->subsystem_device == 0x9480 && adev->pdev->subsystem_vendor == 0x1680))) {
			amdgpu_atombios_i2c_channel_trans(adev, 0x10, 0x96, 0x1E, 0xDD);
			amdgpu_atombios_i2c_channel_trans(adev, 0x10, 0x96, 0x1F, 0xD0);
		}
		break;
	case CHIP_CARRIZO:
		amdgpu_device_program_register_sequence(adev,
							cz_mgcg_cgcg_init,
							ARRAY_SIZE(cz_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							cz_golden_settings_a11,
							ARRAY_SIZE(cz_golden_settings_a11));
		amdgpu_device_program_register_sequence(adev,
							cz_golden_common_all,
							ARRAY_SIZE(cz_golden_common_all));
		break;
	case CHIP_STONEY:
		amdgpu_device_program_register_sequence(adev,
							stoney_mgcg_cgcg_init,
							ARRAY_SIZE(stoney_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							stoney_golden_settings_a11,
							ARRAY_SIZE(stoney_golden_settings_a11));
		amdgpu_device_program_register_sequence(adev,
							stoney_golden_common_all,
							ARRAY_SIZE(stoney_golden_common_all));
		break;
	default:
		break;
	}
}

static int gfx_v8_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(mmSCRATCH_REG0, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		return r;

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	amdgpu_ring_write(ring, mmSCRATCH_REG0 - PACKET3_SET_UCONFIG_REG_START);
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmSCRATCH_REG0);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

static int gfx_v8_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;

	unsigned int index;
	uint64_t gpu_addr;
	uint32_t tmp;
	long r;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(0xCAFEDEAD);
	memset(&ib, 0, sizeof(ib));

	r = amdgpu_ib_get(adev, NULL, 20, AMDGPU_IB_POOL_DIRECT, &ib);
	if (r)
		goto err1;

	ib.ptr[0] = PACKET3(PACKET3_WRITE_DATA, 3);
	ib.ptr[1] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
	ib.ptr[2] = lower_32_bits(gpu_addr);
	ib.ptr[3] = upper_32_bits(gpu_addr);
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err2;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		goto err2;
	}

	tmp = adev->wb.wb[index];
	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err2:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err1:
	amdgpu_device_wb_free(adev, index);
	return r;
}


static void gfx_v8_0_free_microcode(struct amdgpu_device *adev)
{
	amdgpu_ucode_release(&adev->gfx.pfp_fw);
	amdgpu_ucode_release(&adev->gfx.me_fw);
	amdgpu_ucode_release(&adev->gfx.ce_fw);
	amdgpu_ucode_release(&adev->gfx.rlc_fw);
	amdgpu_ucode_release(&adev->gfx.mec_fw);
	if ((adev->asic_type != CHIP_STONEY) &&
	    (adev->asic_type != CHIP_TOPAZ))
		amdgpu_ucode_release(&adev->gfx.mec2_fw);

	kfree(adev->gfx.rlc.register_list_format);
}

static int gfx_v8_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	int err;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	unsigned int *tmp = NULL, i;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		chip_name = "topaz";
		break;
	case CHIP_TONGA:
		chip_name = "tonga";
		break;
	case CHIP_CARRIZO:
		chip_name = "carrizo";
		break;
	case CHIP_FIJI:
		chip_name = "fiji";
		break;
	case CHIP_STONEY:
		chip_name = "stoney";
		break;
	case CHIP_POLARIS10:
		chip_name = "polaris10";
		break;
	case CHIP_POLARIS11:
		chip_name = "polaris11";
		break;
	case CHIP_POLARIS12:
		chip_name = "polaris12";
		break;
	case CHIP_VEGAM:
		chip_name = "vegam";
		break;
	default:
		BUG();
	}

	if (adev->asic_type >= CHIP_POLARIS10 && adev->asic_type <= CHIP_POLARIS12) {
		err = amdgpu_ucode_request(adev, &adev->gfx.pfp_fw,
					   "amdgpu/%s_pfp_2.bin", chip_name);
		if (err == -ENODEV) {
			err = amdgpu_ucode_request(adev, &adev->gfx.pfp_fw,
						   "amdgpu/%s_pfp.bin", chip_name);
		}
	} else {
		err = amdgpu_ucode_request(adev, &adev->gfx.pfp_fw,
					   "amdgpu/%s_pfp.bin", chip_name);
	}
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
	adev->gfx.pfp_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.pfp_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	if (adev->asic_type >= CHIP_POLARIS10 && adev->asic_type <= CHIP_POLARIS12) {
		err = amdgpu_ucode_request(adev, &adev->gfx.me_fw,
					   "amdgpu/%s_me_2.bin", chip_name);
		if (err == -ENODEV) {
			err = amdgpu_ucode_request(adev, &adev->gfx.me_fw,
						   "amdgpu/%s_me.bin", chip_name);
		}
	} else {
		err = amdgpu_ucode_request(adev, &adev->gfx.me_fw,
					   "amdgpu/%s_me.bin", chip_name);
	}
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
	adev->gfx.me_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);

	adev->gfx.me_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	if (adev->asic_type >= CHIP_POLARIS10 && adev->asic_type <= CHIP_POLARIS12) {
		err = amdgpu_ucode_request(adev, &adev->gfx.ce_fw,
					   "amdgpu/%s_ce_2.bin", chip_name);
		if (err == -ENODEV) {
			err = amdgpu_ucode_request(adev, &adev->gfx.ce_fw,
						   "amdgpu/%s_ce.bin", chip_name);
		}
	} else {
		err = amdgpu_ucode_request(adev, &adev->gfx.ce_fw,
					   "amdgpu/%s_ce.bin", chip_name);
	}
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
	adev->gfx.ce_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.ce_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	/*
	 * Support for MCBP/Virtualization in combination with chained IBs is
	 * formal released on feature version #46
	 */
	if (adev->gfx.ce_feature_version >= 46 &&
	    adev->gfx.pfp_feature_version >= 46) {
		adev->virt.chained_ib_support = true;
		DRM_INFO("Chained IB support enabled!\n");
	} else
		adev->virt.chained_ib_support = false;

	err = amdgpu_ucode_request(adev, &adev->gfx.rlc_fw,
				   "amdgpu/%s_rlc.bin", chip_name);
	if (err)
		goto out;
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_fw_version = le32_to_cpu(rlc_hdr->header.ucode_version);
	adev->gfx.rlc_feature_version = le32_to_cpu(rlc_hdr->ucode_feature_version);

	adev->gfx.rlc.save_and_restore_offset =
			le32_to_cpu(rlc_hdr->save_and_restore_offset);
	adev->gfx.rlc.clear_state_descriptor_offset =
			le32_to_cpu(rlc_hdr->clear_state_descriptor_offset);
	adev->gfx.rlc.avail_scratch_ram_locations =
			le32_to_cpu(rlc_hdr->avail_scratch_ram_locations);
	adev->gfx.rlc.reg_restore_list_size =
			le32_to_cpu(rlc_hdr->reg_restore_list_size);
	adev->gfx.rlc.reg_list_format_start =
			le32_to_cpu(rlc_hdr->reg_list_format_start);
	adev->gfx.rlc.reg_list_format_separate_start =
			le32_to_cpu(rlc_hdr->reg_list_format_separate_start);
	adev->gfx.rlc.starting_offsets_start =
			le32_to_cpu(rlc_hdr->starting_offsets_start);
	adev->gfx.rlc.reg_list_format_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_format_size_bytes);
	adev->gfx.rlc.reg_list_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_size_bytes);

	adev->gfx.rlc.register_list_format =
			kmalloc(adev->gfx.rlc.reg_list_format_size_bytes +
					adev->gfx.rlc.reg_list_size_bytes, GFP_KERNEL);

	if (!adev->gfx.rlc.register_list_format) {
		err = -ENOMEM;
		goto out;
	}

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
	for (i = 0 ; i < (adev->gfx.rlc.reg_list_format_size_bytes >> 2); i++)
		adev->gfx.rlc.register_list_format[i] =	le32_to_cpu(tmp[i]);

	adev->gfx.rlc.register_restore = adev->gfx.rlc.register_list_format + i;

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
	for (i = 0 ; i < (adev->gfx.rlc.reg_list_size_bytes >> 2); i++)
		adev->gfx.rlc.register_restore[i] = le32_to_cpu(tmp[i]);

	if (adev->asic_type >= CHIP_POLARIS10 && adev->asic_type <= CHIP_POLARIS12) {
		err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
					   "amdgpu/%s_mec_2.bin", chip_name);
		if (err == -ENODEV) {
			err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
						   "amdgpu/%s_mec.bin", chip_name);
		}
	} else {
		err = amdgpu_ucode_request(adev, &adev->gfx.mec_fw,
					   "amdgpu/%s_mec.bin", chip_name);
	}
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	adev->gfx.mec_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.mec_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	if ((adev->asic_type != CHIP_STONEY) &&
	    (adev->asic_type != CHIP_TOPAZ)) {
		if (adev->asic_type >= CHIP_POLARIS10 && adev->asic_type <= CHIP_POLARIS12) {
			err = amdgpu_ucode_request(adev, &adev->gfx.mec2_fw,
						   "amdgpu/%s_mec2_2.bin", chip_name);
			if (err == -ENODEV) {
				err = amdgpu_ucode_request(adev, &adev->gfx.mec2_fw,
							   "amdgpu/%s_mec2.bin", chip_name);
			}
		} else {
			err = amdgpu_ucode_request(adev, &adev->gfx.mec2_fw,
						   "amdgpu/%s_mec2.bin", chip_name);
		}
		if (!err) {
			cp_hdr = (const struct gfx_firmware_header_v1_0 *)
				adev->gfx.mec2_fw->data;
			adev->gfx.mec2_fw_version =
				le32_to_cpu(cp_hdr->header.ucode_version);
			adev->gfx.mec2_feature_version =
				le32_to_cpu(cp_hdr->ucode_feature_version);
		} else {
			err = 0;
			adev->gfx.mec2_fw = NULL;
		}
	}

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_PFP];
	info->ucode_id = AMDGPU_UCODE_ID_CP_PFP;
	info->fw = adev->gfx.pfp_fw;
	header = (const struct common_firmware_header *)info->fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_ME];
	info->ucode_id = AMDGPU_UCODE_ID_CP_ME;
	info->fw = adev->gfx.me_fw;
	header = (const struct common_firmware_header *)info->fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_CE];
	info->ucode_id = AMDGPU_UCODE_ID_CP_CE;
	info->fw = adev->gfx.ce_fw;
	header = (const struct common_firmware_header *)info->fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_G];
	info->ucode_id = AMDGPU_UCODE_ID_RLC_G;
	info->fw = adev->gfx.rlc_fw;
	header = (const struct common_firmware_header *)info->fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1];
	info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1;
	info->fw = adev->gfx.mec_fw;
	header = (const struct common_firmware_header *)info->fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

	/* we need account JT in */
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(cp_hdr->jt_size) << 2, PAGE_SIZE);

	if (amdgpu_sriov_vf(adev)) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_STORAGE];
		info->ucode_id = AMDGPU_UCODE_ID_STORAGE;
		info->fw = adev->gfx.mec_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(64 * PAGE_SIZE), PAGE_SIZE);
	}

	if (adev->gfx.mec2_fw) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2;
		info->fw = adev->gfx.mec2_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
	}

out:
	if (err) {
		dev_err(adev->dev, "gfx8: Failed to load firmware %s gfx firmware\n", chip_name);
		amdgpu_ucode_release(&adev->gfx.pfp_fw);
		amdgpu_ucode_release(&adev->gfx.me_fw);
		amdgpu_ucode_release(&adev->gfx.ce_fw);
		amdgpu_ucode_release(&adev->gfx.rlc_fw);
		amdgpu_ucode_release(&adev->gfx.mec_fw);
		amdgpu_ucode_release(&adev->gfx.mec2_fw);
	}
	return err;
}

static void gfx_v8_0_get_csb_buffer(struct amdgpu_device *adev,
				    volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (adev->gfx.rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index -
						PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	buffer[count++] = cpu_to_le32(mmPA_SC_RASTER_CONFIG -
			PACKET3_SET_CONTEXT_REG_START);
	buffer[count++] = cpu_to_le32(adev->gfx.config.rb_config[0][0].raster_config);
	buffer[count++] = cpu_to_le32(adev->gfx.config.rb_config[0][0].raster_config_1);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static int gfx_v8_0_cp_jump_table_num(struct amdgpu_device *adev)
{
	if (adev->asic_type == CHIP_CARRIZO)
		return 5;
	else
		return 4;
}

static int gfx_v8_0_rlc_init(struct amdgpu_device *adev)
{
	const struct cs_section_def *cs_data;
	int r;

	adev->gfx.rlc.cs_data = vi_cs_data;

	cs_data = adev->gfx.rlc.cs_data;

	if (cs_data) {
		/* init clear state block */
		r = amdgpu_gfx_rlc_init_csb(adev);
		if (r)
			return r;
	}

	if ((adev->asic_type == CHIP_CARRIZO) ||
	    (adev->asic_type == CHIP_STONEY)) {
		adev->gfx.rlc.cp_table_size = ALIGN(96 * 5 * 4, 2048) + (64 * 1024); /* JT + GDS */
		r = amdgpu_gfx_rlc_init_cpt(adev);
		if (r)
			return r;
	}

	/* init spm vmid with 0xf */
	if (adev->gfx.rlc.funcs->update_spm_vmid)
		adev->gfx.rlc.funcs->update_spm_vmid(adev, NULL, 0xf);

	return 0;
}

static void gfx_v8_0_mec_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.mec.hpd_eop_obj, NULL, NULL);
}

static int gfx_v8_0_mec_init(struct amdgpu_device *adev)
{
	int r;
	u32 *hpd;
	size_t mec_hpd_size;

	bitmap_zero(adev->gfx.mec_bitmap[0].queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/* take ownership of the relevant compute queues */
	amdgpu_gfx_compute_queue_acquire(adev);

	mec_hpd_size = adev->gfx.num_compute_rings * GFX8_MEC_HPD_SIZE;
	if (mec_hpd_size) {
		r = amdgpu_bo_create_reserved(adev, mec_hpd_size, PAGE_SIZE,
					      AMDGPU_GEM_DOMAIN_VRAM |
					      AMDGPU_GEM_DOMAIN_GTT,
					      &adev->gfx.mec.hpd_eop_obj,
					      &adev->gfx.mec.hpd_eop_gpu_addr,
					      (void **)&hpd);
		if (r) {
			dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
			return r;
		}

		memset(hpd, 0, mec_hpd_size);

		amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);
	}

	return 0;
}

static const u32 vgpr_init_compute_shader[] =
{
	0x7e000209, 0x7e020208,
	0x7e040207, 0x7e060206,
	0x7e080205, 0x7e0a0204,
	0x7e0c0203, 0x7e0e0202,
	0x7e100201, 0x7e120200,
	0x7e140209, 0x7e160208,
	0x7e180207, 0x7e1a0206,
	0x7e1c0205, 0x7e1e0204,
	0x7e200203, 0x7e220202,
	0x7e240201, 0x7e260200,
	0x7e280209, 0x7e2a0208,
	0x7e2c0207, 0x7e2e0206,
	0x7e300205, 0x7e320204,
	0x7e340203, 0x7e360202,
	0x7e380201, 0x7e3a0200,
	0x7e3c0209, 0x7e3e0208,
	0x7e400207, 0x7e420206,
	0x7e440205, 0x7e460204,
	0x7e480203, 0x7e4a0202,
	0x7e4c0201, 0x7e4e0200,
	0x7e500209, 0x7e520208,
	0x7e540207, 0x7e560206,
	0x7e580205, 0x7e5a0204,
	0x7e5c0203, 0x7e5e0202,
	0x7e600201, 0x7e620200,
	0x7e640209, 0x7e660208,
	0x7e680207, 0x7e6a0206,
	0x7e6c0205, 0x7e6e0204,
	0x7e700203, 0x7e720202,
	0x7e740201, 0x7e760200,
	0x7e780209, 0x7e7a0208,
	0x7e7c0207, 0x7e7e0206,
	0xbf8a0000, 0xbf810000,
};

static const u32 sgpr_init_compute_shader[] =
{
	0xbe8a0100, 0xbe8c0102,
	0xbe8e0104, 0xbe900106,
	0xbe920108, 0xbe940100,
	0xbe960102, 0xbe980104,
	0xbe9a0106, 0xbe9c0108,
	0xbe9e0100, 0xbea00102,
	0xbea20104, 0xbea40106,
	0xbea60108, 0xbea80100,
	0xbeaa0102, 0xbeac0104,
	0xbeae0106, 0xbeb00108,
	0xbeb20100, 0xbeb40102,
	0xbeb60104, 0xbeb80106,
	0xbeba0108, 0xbebc0100,
	0xbebe0102, 0xbec00104,
	0xbec20106, 0xbec40108,
	0xbec60100, 0xbec80102,
	0xbee60004, 0xbee70005,
	0xbeea0006, 0xbeeb0007,
	0xbee80008, 0xbee90009,
	0xbefc0000, 0xbf8a0000,
	0xbf810000, 0x00000000,
};

static const u32 vgpr_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0xffffffff,
	mmCOMPUTE_RESOURCE_LIMITS, 0x1000000, /* CU_GROUP_COUNT=1 */
	mmCOMPUTE_NUM_THREAD_X, 256*4,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC1, 0x100004f, /* VGPRS=15 (64 logical VGPRs), SGPRS=1 (16 SGPRs), BULKY=1 */
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sgpr1_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0x0f,
	mmCOMPUTE_RESOURCE_LIMITS, 0x1000000, /* CU_GROUP_COUNT=1 */
	mmCOMPUTE_NUM_THREAD_X, 256*5,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC1, 0x240, /* SGPRS=9 (80 GPRS) */
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sgpr2_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0xf0,
	mmCOMPUTE_RESOURCE_LIMITS, 0x1000000,
	mmCOMPUTE_NUM_THREAD_X, 256*5,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC1, 0x240, /* SGPRS=9 (80 GPRS) */
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sec_ded_counter_registers[] =
{
	mmCPC_EDC_ATC_CNT,
	mmCPC_EDC_SCRATCH_CNT,
	mmCPC_EDC_UCODE_CNT,
	mmCPF_EDC_ATC_CNT,
	mmCPF_EDC_ROQ_CNT,
	mmCPF_EDC_TAG_CNT,
	mmCPG_EDC_ATC_CNT,
	mmCPG_EDC_DMA_CNT,
	mmCPG_EDC_TAG_CNT,
	mmDC_EDC_CSINVOC_CNT,
	mmDC_EDC_RESTORE_CNT,
	mmDC_EDC_STATE_CNT,
	mmGDS_EDC_CNT,
	mmGDS_EDC_GRBM_CNT,
	mmGDS_EDC_OA_DED,
	mmSPI_EDC_CNT,
	mmSQC_ATC_EDC_GATCL1_CNT,
	mmSQC_EDC_CNT,
	mmSQ_EDC_DED_CNT,
	mmSQ_EDC_INFO,
	mmSQ_EDC_SEC_CNT,
	mmTCC_EDC_CNT,
	mmTCP_ATC_EDC_GATCL1_CNT,
	mmTCP_EDC_CNT,
	mmTD_EDC_CNT
};

static int gfx_v8_0_do_edc_gpr_workarounds(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[0];
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	int r, i;
	u32 tmp;
	unsigned total_size, vgpr_offset, sgpr_offset;
	u64 gpu_addr;

	/* only supported on CZ */
	if (adev->asic_type != CHIP_CARRIZO)
		return 0;

	/* bail if the compute ring is not ready */
	if (!ring->sched.ready)
		return 0;

	tmp = RREG32(mmGB_EDC_MODE);
	WREG32(mmGB_EDC_MODE, 0);

	total_size =
		(((ARRAY_SIZE(vgpr_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size +=
		(((ARRAY_SIZE(sgpr1_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size +=
		(((ARRAY_SIZE(sgpr2_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size = ALIGN(total_size, 256);
	vgpr_offset = total_size;
	total_size += ALIGN(sizeof(vgpr_init_compute_shader), 256);
	sgpr_offset = total_size;
	total_size += sizeof(sgpr_init_compute_shader);

	/* allocate an indirect buffer to put the commands in */
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, total_size,
					AMDGPU_IB_POOL_DIRECT, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%d).\n", r);
		return r;
	}

	/* load the compute shaders */
	for (i = 0; i < ARRAY_SIZE(vgpr_init_compute_shader); i++)
		ib.ptr[i + (vgpr_offset / 4)] = vgpr_init_compute_shader[i];

	for (i = 0; i < ARRAY_SIZE(sgpr_init_compute_shader); i++)
		ib.ptr[i + (sgpr_offset / 4)] = sgpr_init_compute_shader[i];

	/* init the ib length to 0 */
	ib.length_dw = 0;

	/* VGPR */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(vgpr_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = vgpr_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = vgpr_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)vgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* SGPR1 */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(sgpr1_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = sgpr1_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = sgpr1_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)sgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* SGPR2 */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(sgpr2_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = sgpr2_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = sgpr2_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)sgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* shedule the ib on the ring */
	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r) {
		DRM_ERROR("amdgpu: ib submit failed (%d).\n", r);
		goto fail;
	}

	/* wait for the GPU to finish processing the IB */
	r = dma_fence_wait(f, false);
	if (r) {
		DRM_ERROR("amdgpu: fence wait failed (%d).\n", r);
		goto fail;
	}

	tmp = REG_SET_FIELD(tmp, GB_EDC_MODE, DED_MODE, 2);
	tmp = REG_SET_FIELD(tmp, GB_EDC_MODE, PROP_FED, 1);
	WREG32(mmGB_EDC_MODE, tmp);

	tmp = RREG32(mmCC_GC_EDC_CONFIG);
	tmp = REG_SET_FIELD(tmp, CC_GC_EDC_CONFIG, DIS_EDC, 0) | 1;
	WREG32(mmCC_GC_EDC_CONFIG, tmp);


	/* read back registers to clear the counters */
	for (i = 0; i < ARRAY_SIZE(sec_ded_counter_registers); i++)
		RREG32(sec_ded_counter_registers[i]);

fail:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);

	return r;
}

static int gfx_v8_0_gpu_early_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config;
	u32 mc_arb_ramcfg;
	u32 dimm00_addr_map, dimm01_addr_map, dimm10_addr_map, dimm11_addr_map;
	u32 tmp;
	int ret;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_cu_per_sh = 6;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TOPAZ_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_FIJI:
		adev->gfx.config.max_shader_engines = 4;
		adev->gfx.config.max_tile_pipes = 16;
		adev->gfx.config.max_cu_per_sh = 16;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 4;
		adev->gfx.config.max_texture_channel_caches = 16;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		ret = amdgpu_atombios_get_gfx_info(adev);
		if (ret)
			return ret;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = POLARIS11_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_POLARIS10:
	case CHIP_VEGAM:
		ret = amdgpu_atombios_get_gfx_info(adev);
		if (ret)
			return ret;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_TONGA:
		adev->gfx.config.max_shader_engines = 4;
		adev->gfx.config.max_tile_pipes = 8;
		adev->gfx.config.max_cu_per_sh = 8;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 8;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_CARRIZO:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_cu_per_sh = 8;
		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CARRIZO_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_STONEY:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 1;
		adev->gfx.config.max_cu_per_sh = 3;
		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 16;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CARRIZO_GB_ADDR_CONFIG_GOLDEN;
		break;
	default:
		adev->gfx.config.max_shader_engines = 2;
		adev->gfx.config.max_tile_pipes = 4;
		adev->gfx.config.max_cu_per_sh = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 4;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	}

	adev->gfx.config.mc_arb_ramcfg = RREG32(mmMC_ARB_RAMCFG);
	mc_arb_ramcfg = adev->gfx.config.mc_arb_ramcfg;

	adev->gfx.config.num_banks = REG_GET_FIELD(mc_arb_ramcfg,
				MC_ARB_RAMCFG, NOOFBANK);
	adev->gfx.config.num_ranks = REG_GET_FIELD(mc_arb_ramcfg,
				MC_ARB_RAMCFG, NOOFRANKS);

	adev->gfx.config.num_tile_pipes = adev->gfx.config.max_tile_pipes;
	adev->gfx.config.mem_max_burst_length_bytes = 256;
	if (adev->flags & AMD_IS_APU) {
		/* Get memory bank mapping mode. */
		tmp = RREG32(mmMC_FUS_DRAM0_BANK_ADDR_MAPPING);
		dimm00_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM0_BANK_ADDR_MAPPING, DIMM0ADDRMAP);
		dimm01_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM0_BANK_ADDR_MAPPING, DIMM1ADDRMAP);

		tmp = RREG32(mmMC_FUS_DRAM1_BANK_ADDR_MAPPING);
		dimm10_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM1_BANK_ADDR_MAPPING, DIMM0ADDRMAP);
		dimm11_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM1_BANK_ADDR_MAPPING, DIMM1ADDRMAP);

		/* Validate settings in case only one DIMM installed. */
		if ((dimm00_addr_map == 0) || (dimm00_addr_map == 3) || (dimm00_addr_map == 4) || (dimm00_addr_map > 12))
			dimm00_addr_map = 0;
		if ((dimm01_addr_map == 0) || (dimm01_addr_map == 3) || (dimm01_addr_map == 4) || (dimm01_addr_map > 12))
			dimm01_addr_map = 0;
		if ((dimm10_addr_map == 0) || (dimm10_addr_map == 3) || (dimm10_addr_map == 4) || (dimm10_addr_map > 12))
			dimm10_addr_map = 0;
		if ((dimm11_addr_map == 0) || (dimm11_addr_map == 3) || (dimm11_addr_map == 4) || (dimm11_addr_map > 12))
			dimm11_addr_map = 0;

		/* If DIMM Addr map is 8GB, ROW size should be 2KB. Otherwise 1KB. */
		/* If ROW size(DIMM1) != ROW size(DMIMM0), ROW size should be larger one. */
		if ((dimm00_addr_map == 11) || (dimm01_addr_map == 11) || (dimm10_addr_map == 11) || (dimm11_addr_map == 11))
			adev->gfx.config.mem_row_size_in_kb = 2;
		else
			adev->gfx.config.mem_row_size_in_kb = 1;
	} else {
		tmp = REG_GET_FIELD(mc_arb_ramcfg, MC_ARB_RAMCFG, NOOFCOLS);
		adev->gfx.config.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
		if (adev->gfx.config.mem_row_size_in_kb > 4)
			adev->gfx.config.mem_row_size_in_kb = 4;
	}

	adev->gfx.config.shader_engine_tile_size = 32;
	adev->gfx.config.num_gpus = 1;
	adev->gfx.config.multi_gpu_tile_size = 64;

	/* fix up row size */
	switch (adev->gfx.config.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 0);
		break;
	case 2:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 1);
		break;
	case 4:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 2);
		break;
	}
	adev->gfx.config.gb_addr_config = gb_addr_config;

	return 0;
}

static int gfx_v8_0_compute_ring_init(struct amdgpu_device *adev, int ring_id,
					int mec, int pipe, int queue)
{
	int r;
	unsigned irq_type;
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[ring_id];
	unsigned int hw_prio;

	ring = &adev->gfx.compute_ring[ring_id];

	/* mec0 is me1 */
	ring->me = mec + 1;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = adev->doorbell_index.mec_ring0 + ring_id;
	ring->eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr
				+ (ring_id * GFX8_MEC_HPD_SIZE);
	snprintf(ring->name, sizeof(ring->name), "comp_%d.%d.%d", ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP
		+ ((ring->me - 1) * adev->gfx.mec.num_pipe_per_mec)
		+ ring->pipe;

	hw_prio = amdgpu_gfx_is_high_priority_compute_queue(adev, ring) ?
			AMDGPU_RING_PRIO_2 : AMDGPU_RING_PRIO_DEFAULT;
	/* type-2 packets are deprecated on MEC, use type-3 instead */
	r = amdgpu_ring_init(adev, ring, 1024, &adev->gfx.eop_irq, irq_type,
			     hw_prio, NULL);
	if (r)
		return r;


	return 0;
}

static void gfx_v8_0_sq_irq_work_func(struct work_struct *work);

static int gfx_v8_0_sw_init(void *handle)
{
	int i, j, k, r, ring_id;
	int xcc_id = 0;
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_TONGA:
	case CHIP_CARRIZO:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		adev->gfx.mec.num_mec = 2;
		break;
	case CHIP_TOPAZ:
	case CHIP_STONEY:
	default:
		adev->gfx.mec.num_mec = 1;
		break;
	}

	adev->gfx.mec.num_pipe_per_mec = 4;
	adev->gfx.mec.num_queue_per_pipe = 8;

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_CP_END_OF_PIPE, &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_CP_PRIV_REG_FAULT,
			      &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_CP_PRIV_INSTR_FAULT,
			      &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	/* Add CP EDC/ECC irq  */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_CP_ECC_ERROR,
			      &adev->gfx.cp_ecc_error_irq);
	if (r)
		return r;

	/* SQ interrupts. */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_SQ_INTERRUPT_MSG,
			      &adev->gfx.sq_irq);
	if (r) {
		DRM_ERROR("amdgpu_irq_add() for SQ failed: %d\n", r);
		return r;
	}

	INIT_WORK(&adev->gfx.sq_work.work, gfx_v8_0_sq_irq_work_func);

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	r = gfx_v8_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load gfx firmware!\n");
		return r;
	}

	r = adev->gfx.rlc.funcs->init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v8_0_mec_init(adev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the gfx ring */
	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->ring_obj = NULL;
		snprintf(ring->name, sizeof(ring->name), "gfx");
		/* no gfx doorbells on iceland */
		if (adev->asic_type != CHIP_TOPAZ) {
			ring->use_doorbell = true;
			ring->doorbell_index = adev->doorbell_index.gfx_ring0;
		}

		r = amdgpu_ring_init(adev, ring, 1024, &adev->gfx.eop_irq,
				     AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;
	}


	/* set up the compute queues - allocate horizontally across pipes */
	ring_id = 0;
	for (i = 0; i < adev->gfx.mec.num_mec; ++i) {
		for (j = 0; j < adev->gfx.mec.num_queue_per_pipe; j++) {
			for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
				if (!amdgpu_gfx_is_mec_queue_enabled(adev, 0, i,
								     k, j))
					continue;

				r = gfx_v8_0_compute_ring_init(adev,
								ring_id,
								i, k, j);
				if (r)
					return r;

				ring_id++;
			}
		}
	}

	r = amdgpu_gfx_kiq_init(adev, GFX8_MEC_HPD_SIZE, 0);
	if (r) {
		DRM_ERROR("Failed to init KIQ BOs!\n");
		return r;
	}

	r = amdgpu_gfx_kiq_init_ring(adev, xcc_id);
	if (r)
		return r;

	/* create MQD for all compute queues as well as KIQ for SRIOV case */
	r = amdgpu_gfx_mqd_sw_init(adev, sizeof(struct vi_mqd_allocation), 0);
	if (r)
		return r;

	adev->gfx.ce_ram_size = 0x8000;

	r = gfx_v8_0_gpu_early_init(adev);
	if (r)
		return r;

	return 0;
}

static int gfx_v8_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		amdgpu_ring_fini(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	amdgpu_gfx_mqd_sw_fini(adev, 0);
	amdgpu_gfx_kiq_free_ring(&adev->gfx.kiq[0].ring);
	amdgpu_gfx_kiq_fini(adev, 0);

	gfx_v8_0_mec_fini(adev);
	amdgpu_gfx_rlc_fini(adev);
	amdgpu_bo_free_kernel(&adev->gfx.rlc.clear_state_obj,
				&adev->gfx.rlc.clear_state_gpu_addr,
				(void **)&adev->gfx.rlc.cs_ptr);
	if ((adev->asic_type == CHIP_CARRIZO) ||
	    (adev->asic_type == CHIP_STONEY)) {
		amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
				&adev->gfx.rlc.cp_table_gpu_addr,
				(void **)&adev->gfx.rlc.cp_table_ptr);
	}
	gfx_v8_0_free_microcode(adev);

	return 0;
}

static void gfx_v8_0_tiling_mode_table_init(struct amdgpu_device *adev)
{
	uint32_t *modearray, *mod2array;
	const u32 num_tile_mode_states = ARRAY_SIZE(adev->gfx.config.tile_mode_array);
	const u32 num_secondary_tile_mode_states = ARRAY_SIZE(adev->gfx.config.macrotile_mode_array);
	u32 reg_offset;

	modearray = adev->gfx.config.tile_mode_array;
	mod2array = adev->gfx.config.macrotile_mode_array;

	for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
		modearray[reg_offset] = 0;

	for (reg_offset = 0; reg_offset <  num_secondary_tile_mode_states; reg_offset++)
		mod2array[reg_offset] = 0;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_FIJI:
	case CHIP_VEGAM:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_TONGA:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));

		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_POLARIS10:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));

		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_STONEY:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	default:
		dev_warn(adev->dev,
			 "Unknown chip type (%d) in function gfx_v8_0_tiling_mode_table_init() falling through to CHIP_CARRIZO\n",
			 adev->asic_type);
		fallthrough;

	case CHIP_CARRIZO:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	}
}

static void gfx_v8_0_select_se_sh(struct amdgpu_device *adev,
				  u32 se_num, u32 sh_num, u32 instance,
				  int xcc_id)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_INDEX, instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);

	WREG32(mmGRBM_GFX_INDEX, data);
}

static void gfx_v8_0_select_me_pipe_q(struct amdgpu_device *adev,
				  u32 me, u32 pipe, u32 q, u32 vm, u32 xcc_id)
{
	vi_srbm_select(adev, me, pipe, q, vm);
}

static u32 gfx_v8_0_get_rb_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data =  RREG32(mmCC_RB_BACKEND_DISABLE) |
		RREG32(mmGC_USER_RB_BACKEND_DISABLE);

	data = REG_GET_FIELD(data, GC_USER_RB_BACKEND_DISABLE, BACKEND_DISABLE);

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_backends_per_se /
					 adev->gfx.config.max_sh_per_se);

	return (~data) & mask;
}

static void
gfx_v8_0_raster_config(struct amdgpu_device *adev, u32 *rconf, u32 *rconf1)
{
	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_VEGAM:
		*rconf |= RB_MAP_PKR0(2) | RB_MAP_PKR1(2) |
			  RB_XSEL2(1) | PKR_MAP(2) |
			  PKR_XSEL(1) | PKR_YSEL(1) |
			  SE_MAP(2) | SE_XSEL(2) | SE_YSEL(3);
		*rconf1 |= SE_PAIR_MAP(2) | SE_PAIR_XSEL(3) |
			   SE_PAIR_YSEL(2);
		break;
	case CHIP_TONGA:
	case CHIP_POLARIS10:
		*rconf |= RB_MAP_PKR0(2) | RB_XSEL2(1) | SE_MAP(2) |
			  SE_XSEL(1) | SE_YSEL(1);
		*rconf1 |= SE_PAIR_MAP(2) | SE_PAIR_XSEL(2) |
			   SE_PAIR_YSEL(2);
		break;
	case CHIP_TOPAZ:
	case CHIP_CARRIZO:
		*rconf |= RB_MAP_PKR0(2);
		*rconf1 |= 0x0;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		*rconf |= RB_MAP_PKR0(2) | RB_XSEL2(1) | SE_MAP(2) |
			  SE_XSEL(1) | SE_YSEL(1);
		*rconf1 |= 0x0;
		break;
	case CHIP_STONEY:
		*rconf |= 0x0;
		*rconf1 |= 0x0;
		break;
	default:
		DRM_ERROR("unknown asic: 0x%x\n", adev->asic_type);
		break;
	}
}

static void
gfx_v8_0_write_harvested_raster_configs(struct amdgpu_device *adev,
					u32 raster_config, u32 raster_config_1,
					unsigned rb_mask, unsigned num_rb)
{
	unsigned sh_per_se = max_t(unsigned, adev->gfx.config.max_sh_per_se, 1);
	unsigned num_se = max_t(unsigned, adev->gfx.config.max_shader_engines, 1);
	unsigned rb_per_pkr = min_t(unsigned, num_rb / num_se / sh_per_se, 2);
	unsigned rb_per_se = num_rb / num_se;
	unsigned se_mask[4];
	unsigned se;

	se_mask[0] = ((1 << rb_per_se) - 1) & rb_mask;
	se_mask[1] = (se_mask[0] << rb_per_se) & rb_mask;
	se_mask[2] = (se_mask[1] << rb_per_se) & rb_mask;
	se_mask[3] = (se_mask[2] << rb_per_se) & rb_mask;

	WARN_ON(!(num_se == 1 || num_se == 2 || num_se == 4));
	WARN_ON(!(sh_per_se == 1 || sh_per_se == 2));
	WARN_ON(!(rb_per_pkr == 1 || rb_per_pkr == 2));

	if ((num_se > 2) && ((!se_mask[0] && !se_mask[1]) ||
			     (!se_mask[2] && !se_mask[3]))) {
		raster_config_1 &= ~SE_PAIR_MAP_MASK;

		if (!se_mask[0] && !se_mask[1]) {
			raster_config_1 |=
				SE_PAIR_MAP(RASTER_CONFIG_SE_PAIR_MAP_3);
		} else {
			raster_config_1 |=
				SE_PAIR_MAP(RASTER_CONFIG_SE_PAIR_MAP_0);
		}
	}

	for (se = 0; se < num_se; se++) {
		unsigned raster_config_se = raster_config;
		unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
		unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
		int idx = (se / 2) * 2;

		if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
			raster_config_se &= ~SE_MAP_MASK;

			if (!se_mask[idx]) {
				raster_config_se |= SE_MAP(RASTER_CONFIG_SE_MAP_3);
			} else {
				raster_config_se |= SE_MAP(RASTER_CONFIG_SE_MAP_0);
			}
		}

		pkr0_mask &= rb_mask;
		pkr1_mask &= rb_mask;
		if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
			raster_config_se &= ~PKR_MAP_MASK;

			if (!pkr0_mask) {
				raster_config_se |= PKR_MAP(RASTER_CONFIG_PKR_MAP_3);
			} else {
				raster_config_se |= PKR_MAP(RASTER_CONFIG_PKR_MAP_0);
			}
		}

		if (rb_per_se >= 2) {
			unsigned rb0_mask = 1 << (se * rb_per_se);
			unsigned rb1_mask = rb0_mask << 1;

			rb0_mask &= rb_mask;
			rb1_mask &= rb_mask;
			if (!rb0_mask || !rb1_mask) {
				raster_config_se &= ~RB_MAP_PKR0_MASK;

				if (!rb0_mask) {
					raster_config_se |=
						RB_MAP_PKR0(RASTER_CONFIG_RB_MAP_3);
				} else {
					raster_config_se |=
						RB_MAP_PKR0(RASTER_CONFIG_RB_MAP_0);
				}
			}

			if (rb_per_se > 2) {
				rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
				rb1_mask = rb0_mask << 1;
				rb0_mask &= rb_mask;
				rb1_mask &= rb_mask;
				if (!rb0_mask || !rb1_mask) {
					raster_config_se &= ~RB_MAP_PKR1_MASK;

					if (!rb0_mask) {
						raster_config_se |=
							RB_MAP_PKR1(RASTER_CONFIG_RB_MAP_3);
					} else {
						raster_config_se |=
							RB_MAP_PKR1(RASTER_CONFIG_RB_MAP_0);
					}
				}
			}
		}

		/* GRBM_GFX_INDEX has a different offset on VI */
		gfx_v8_0_select_se_sh(adev, se, 0xffffffff, 0xffffffff, 0);
		WREG32(mmPA_SC_RASTER_CONFIG, raster_config_se);
		WREG32(mmPA_SC_RASTER_CONFIG_1, raster_config_1);
	}

	/* GRBM_GFX_INDEX has a different offset on VI */
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
}

static void gfx_v8_0_setup_rb(struct amdgpu_device *adev)
{
	int i, j;
	u32 data;
	u32 raster_config = 0, raster_config_1 = 0;
	u32 active_rbs = 0;
	u32 rb_bitmap_width_per_sh = adev->gfx.config.max_backends_per_se /
					adev->gfx.config.max_sh_per_se;
	unsigned num_rb_pipes;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v8_0_select_se_sh(adev, i, j, 0xffffffff, 0);
			data = gfx_v8_0_get_rb_active_bitmap(adev);
			active_rbs |= data << ((i * adev->gfx.config.max_sh_per_se + j) *
					       rb_bitmap_width_per_sh);
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);

	adev->gfx.config.backend_enable_mask = active_rbs;
	adev->gfx.config.num_rbs = hweight32(active_rbs);

	num_rb_pipes = min_t(unsigned, adev->gfx.config.max_backends_per_se *
			     adev->gfx.config.max_shader_engines, 16);

	gfx_v8_0_raster_config(adev, &raster_config, &raster_config_1);

	if (!adev->gfx.config.backend_enable_mask ||
			adev->gfx.config.num_rbs >= num_rb_pipes) {
		WREG32(mmPA_SC_RASTER_CONFIG, raster_config);
		WREG32(mmPA_SC_RASTER_CONFIG_1, raster_config_1);
	} else {
		gfx_v8_0_write_harvested_raster_configs(adev, raster_config, raster_config_1,
							adev->gfx.config.backend_enable_mask,
							num_rb_pipes);
	}

	/* cache the values for userspace */
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v8_0_select_se_sh(adev, i, j, 0xffffffff, 0);
			adev->gfx.config.rb_config[i][j].rb_backend_disable =
				RREG32(mmCC_RB_BACKEND_DISABLE);
			adev->gfx.config.rb_config[i][j].user_rb_backend_disable =
				RREG32(mmGC_USER_RB_BACKEND_DISABLE);
			adev->gfx.config.rb_config[i][j].raster_config =
				RREG32(mmPA_SC_RASTER_CONFIG);
			adev->gfx.config.rb_config[i][j].raster_config_1 =
				RREG32(mmPA_SC_RASTER_CONFIG_1);
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
	mutex_unlock(&adev->grbm_idx_mutex);
}

#define DEFAULT_SH_MEM_BASES	(0x6000)
/**
 * gfx_v8_0_init_compute_vmid - gart enable
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize compute vmid sh_mem registers
 *
 */
static void gfx_v8_0_init_compute_vmid(struct amdgpu_device *adev)
{
	int i;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;

	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	sh_mem_bases = DEFAULT_SH_MEM_BASES | (DEFAULT_SH_MEM_BASES << 16);

	sh_mem_config = SH_MEM_ADDRESS_MODE_HSA64 <<
			SH_MEM_CONFIG__ADDRESS_MODE__SHIFT |
			SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
			SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT |
			MTYPE_CC << SH_MEM_CONFIG__DEFAULT_MTYPE__SHIFT |
			SH_MEM_CONFIG__PRIVATE_ATC_MASK;

	mutex_lock(&adev->srbm_mutex);
	for (i = adev->vm_manager.first_kfd_vmid; i < AMDGPU_NUM_VMID; i++) {
		vi_srbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32(mmSH_MEM_CONFIG, sh_mem_config);
		WREG32(mmSH_MEM_APE1_BASE, 1);
		WREG32(mmSH_MEM_APE1_LIMIT, 0);
		WREG32(mmSH_MEM_BASES, sh_mem_bases);
	}
	vi_srbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	/* Initialize all compute VMIDs to have no GDS, GWS, or OA
	   access. These should be enabled by FW for target VMIDs. */
	for (i = adev->vm_manager.first_kfd_vmid; i < AMDGPU_NUM_VMID; i++) {
		WREG32(amdgpu_gds_reg_offset[i].mem_base, 0);
		WREG32(amdgpu_gds_reg_offset[i].mem_size, 0);
		WREG32(amdgpu_gds_reg_offset[i].gws, 0);
		WREG32(amdgpu_gds_reg_offset[i].oa, 0);
	}
}

static void gfx_v8_0_init_gds_vmid(struct amdgpu_device *adev)
{
	int vmid;

	/*
	 * Initialize all compute and user-gfx VMIDs to have no GDS, GWS, or OA
	 * access. Compute VMIDs should be enabled by FW for target VMIDs,
	 * the driver can enable them for graphics. VMID0 should maintain
	 * access so that HWS firmware can save/restore entries.
	 */
	for (vmid = 1; vmid < AMDGPU_NUM_VMID; vmid++) {
		WREG32(amdgpu_gds_reg_offset[vmid].mem_base, 0);
		WREG32(amdgpu_gds_reg_offset[vmid].mem_size, 0);
		WREG32(amdgpu_gds_reg_offset[vmid].gws, 0);
		WREG32(amdgpu_gds_reg_offset[vmid].oa, 0);
	}
}

static void gfx_v8_0_config_init(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	default:
		adev->gfx.config.double_offchip_lds_buf = 1;
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		adev->gfx.config.double_offchip_lds_buf = 0;
		break;
	}
}

static void gfx_v8_0_constants_init(struct amdgpu_device *adev)
{
	u32 tmp, sh_static_mem_cfg;
	int i;

	WREG32_FIELD(GRBM_CNTL, READ_TIMEOUT, 0xFF);
	WREG32(mmGB_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmHDP_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmDMIF_ADDR_CALC, adev->gfx.config.gb_addr_config);

	gfx_v8_0_tiling_mode_table_init(adev);
	gfx_v8_0_setup_rb(adev);
	gfx_v8_0_get_cu_info(adev);
	gfx_v8_0_config_init(adev);

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	sh_static_mem_cfg = REG_SET_FIELD(0, SH_STATIC_MEM_CONFIG,
				   SWIZZLE_ENABLE, 1);
	sh_static_mem_cfg = REG_SET_FIELD(sh_static_mem_cfg, SH_STATIC_MEM_CONFIG,
				   ELEMENT_SIZE, 1);
	sh_static_mem_cfg = REG_SET_FIELD(sh_static_mem_cfg, SH_STATIC_MEM_CONFIG,
				   INDEX_STRIDE, 3);
	WREG32(mmSH_STATIC_MEM_CONFIG, sh_static_mem_cfg);

	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->vm_manager.id_mgr[0].num_ids; i++) {
		vi_srbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		if (i == 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, DEFAULT_MTYPE, MTYPE_UC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, APE1_MTYPE, MTYPE_UC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			WREG32(mmSH_MEM_CONFIG, tmp);
			WREG32(mmSH_MEM_BASES, 0);
		} else {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, DEFAULT_MTYPE, MTYPE_NC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, APE1_MTYPE, MTYPE_UC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			WREG32(mmSH_MEM_CONFIG, tmp);
			tmp = adev->gmc.shared_aperture_start >> 48;
			WREG32(mmSH_MEM_BASES, tmp);
		}

		WREG32(mmSH_MEM_APE1_BASE, 1);
		WREG32(mmSH_MEM_APE1_LIMIT, 0);
	}
	vi_srbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	gfx_v8_0_init_compute_vmid(adev);
	gfx_v8_0_init_gds_vmid(adev);

	mutex_lock(&adev->grbm_idx_mutex);
	/*
	 * making sure that the following register writes will be broadcasted
	 * to all the shaders
	 */
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);

	WREG32(mmPA_SC_FIFO_SIZE,
		   (adev->gfx.config.sc_prim_fifo_size_frontend <<
			PA_SC_FIFO_SIZE__SC_FRONTEND_PRIM_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_prim_fifo_size_backend <<
			PA_SC_FIFO_SIZE__SC_BACKEND_PRIM_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_hiz_tile_fifo_size <<
			PA_SC_FIFO_SIZE__SC_HIZ_TILE_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_earlyz_tile_fifo_size <<
			PA_SC_FIFO_SIZE__SC_EARLYZ_TILE_FIFO_SIZE__SHIFT));

	tmp = RREG32(mmSPI_ARB_PRIORITY);
	tmp = REG_SET_FIELD(tmp, SPI_ARB_PRIORITY, PIPE_ORDER_TS0, 2);
	tmp = REG_SET_FIELD(tmp, SPI_ARB_PRIORITY, PIPE_ORDER_TS1, 2);
	tmp = REG_SET_FIELD(tmp, SPI_ARB_PRIORITY, PIPE_ORDER_TS2, 2);
	tmp = REG_SET_FIELD(tmp, SPI_ARB_PRIORITY, PIPE_ORDER_TS3, 2);
	WREG32(mmSPI_ARB_PRIORITY, tmp);

	mutex_unlock(&adev->grbm_idx_mutex);

}

static void gfx_v8_0_wait_for_rlc_serdes(struct amdgpu_device *adev)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v8_0_select_se_sh(adev, i, j, 0xffffffff, 0);
			for (k = 0; k < adev->usec_timeout; k++) {
				if (RREG32(mmRLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
			if (k == adev->usec_timeout) {
				gfx_v8_0_select_se_sh(adev, 0xffffffff,
						      0xffffffff, 0xffffffff, 0);
				mutex_unlock(&adev->grbm_idx_mutex);
				DRM_INFO("Timeout wait for RLC serdes %u,%u\n",
					 i, j);
				return;
			}
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = RLC_SERDES_NONCU_MASTER_BUSY__SE_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__GC_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC0_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC1_MASTER_BUSY_MASK;
	for (k = 0; k < adev->usec_timeout; k++) {
		if ((RREG32(mmRLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v8_0_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{
	u32 tmp = RREG32(mmCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE, enable ? 1 : 0);

	WREG32(mmCP_INT_CNTL_RING0, tmp);
}

static void gfx_v8_0_init_csb(struct amdgpu_device *adev)
{
	adev->gfx.rlc.funcs->get_csb_buffer(adev, adev->gfx.rlc.cs_ptr);
	/* csib */
	WREG32(mmRLC_CSIB_ADDR_HI,
			adev->gfx.rlc.clear_state_gpu_addr >> 32);
	WREG32(mmRLC_CSIB_ADDR_LO,
			adev->gfx.rlc.clear_state_gpu_addr & 0xfffffffc);
	WREG32(mmRLC_CSIB_LENGTH,
			adev->gfx.rlc.clear_state_size);
}

static void gfx_v8_0_parse_ind_reg_list(int *register_list_format,
				int ind_offset,
				int list_size,
				int *unique_indices,
				int *indices_count,
				int max_indices,
				int *ind_start_offsets,
				int *offset_count,
				int max_offset)
{
	int indices;
	bool new_entry = true;

	for (; ind_offset < list_size; ind_offset++) {

		if (new_entry) {
			new_entry = false;
			ind_start_offsets[*offset_count] = ind_offset;
			*offset_count = *offset_count + 1;
			BUG_ON(*offset_count >= max_offset);
		}

		if (register_list_format[ind_offset] == 0xFFFFFFFF) {
			new_entry = true;
			continue;
		}

		ind_offset += 2;

		/* look for the matching indice */
		for (indices = 0;
			indices < *indices_count;
			indices++) {
			if (unique_indices[indices] ==
				register_list_format[ind_offset])
				break;
		}

		if (indices >= *indices_count) {
			unique_indices[*indices_count] =
				register_list_format[ind_offset];
			indices = *indices_count;
			*indices_count = *indices_count + 1;
			BUG_ON(*indices_count >= max_indices);
		}

		register_list_format[ind_offset] = indices;
	}
}

static int gfx_v8_0_init_save_restore_list(struct amdgpu_device *adev)
{
	int i, temp, data;
	int unique_indices[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int indices_count = 0;
	int indirect_start_offsets[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int offset_count = 0;

	int list_size;
	unsigned int *register_list_format =
		kmemdup(adev->gfx.rlc.register_list_format,
			adev->gfx.rlc.reg_list_format_size_bytes, GFP_KERNEL);
	if (!register_list_format)
		return -ENOMEM;

	gfx_v8_0_parse_ind_reg_list(register_list_format,
				RLC_FormatDirectRegListLength,
				adev->gfx.rlc.reg_list_format_size_bytes >> 2,
				unique_indices,
				&indices_count,
				ARRAY_SIZE(unique_indices),
				indirect_start_offsets,
				&offset_count,
				ARRAY_SIZE(indirect_start_offsets));

	/* save and restore list */
	WREG32_FIELD(RLC_SRM_CNTL, AUTO_INCR_ADDR, 1);

	WREG32(mmRLC_SRM_ARAM_ADDR, 0);
	for (i = 0; i < adev->gfx.rlc.reg_list_size_bytes >> 2; i++)
		WREG32(mmRLC_SRM_ARAM_DATA, adev->gfx.rlc.register_restore[i]);

	/* indirect list */
	WREG32(mmRLC_GPM_SCRATCH_ADDR, adev->gfx.rlc.reg_list_format_start);
	for (i = 0; i < adev->gfx.rlc.reg_list_format_size_bytes >> 2; i++)
		WREG32(mmRLC_GPM_SCRATCH_DATA, register_list_format[i]);

	list_size = adev->gfx.rlc.reg_list_size_bytes >> 2;
	list_size = list_size >> 1;
	WREG32(mmRLC_GPM_SCRATCH_ADDR, adev->gfx.rlc.reg_restore_list_size);
	WREG32(mmRLC_GPM_SCRATCH_DATA, list_size);

	/* starting offsets starts */
	WREG32(mmRLC_GPM_SCRATCH_ADDR,
		adev->gfx.rlc.starting_offsets_start);
	for (i = 0; i < ARRAY_SIZE(indirect_start_offsets); i++)
		WREG32(mmRLC_GPM_SCRATCH_DATA,
				indirect_start_offsets[i]);

	/* unique indices */
	temp = mmRLC_SRM_INDEX_CNTL_ADDR_0;
	data = mmRLC_SRM_INDEX_CNTL_DATA_0;
	for (i = 0; i < ARRAY_SIZE(unique_indices); i++) {
		if (unique_indices[i] != 0) {
			WREG32(temp + i, unique_indices[i] & 0x3FFFF);
			WREG32(data + i, unique_indices[i] >> 20);
		}
	}
	kfree(register_list_format);

	return 0;
}

static void gfx_v8_0_enable_save_restore_machine(struct amdgpu_device *adev)
{
	WREG32_FIELD(RLC_SRM_CNTL, SRM_ENABLE, 1);
}

static void gfx_v8_0_init_power_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	WREG32_FIELD(CP_RB_WPTR_POLL_CNTL, IDLE_POLL_COUNT, 0x60);

	data = REG_SET_FIELD(0, RLC_PG_DELAY, POWER_UP_DELAY, 0x10);
	data = REG_SET_FIELD(data, RLC_PG_DELAY, POWER_DOWN_DELAY, 0x10);
	data = REG_SET_FIELD(data, RLC_PG_DELAY, CMD_PROPAGATE_DELAY, 0x10);
	data = REG_SET_FIELD(data, RLC_PG_DELAY, MEM_SLEEP_DELAY, 0x10);
	WREG32(mmRLC_PG_DELAY, data);

	WREG32_FIELD(RLC_PG_DELAY_2, SERDES_CMD_DELAY, 0x3);
	WREG32_FIELD(RLC_AUTO_PG_CTRL, GRBM_REG_SAVE_GFX_IDLE_THRESHOLD, 0x55f0);

}

static void cz_enable_sck_slow_down_on_power_up(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, SMU_CLK_SLOWDOWN_ON_PU_ENABLE, enable ? 1 : 0);
}

static void cz_enable_sck_slow_down_on_power_down(struct amdgpu_device *adev,
						  bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, SMU_CLK_SLOWDOWN_ON_PD_ENABLE, enable ? 1 : 0);
}

static void cz_enable_cp_power_gating(struct amdgpu_device *adev, bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, CP_PG_DISABLE, enable ? 0 : 1);
}

static void gfx_v8_0_init_pg(struct amdgpu_device *adev)
{
	if ((adev->asic_type == CHIP_CARRIZO) ||
	    (adev->asic_type == CHIP_STONEY)) {
		gfx_v8_0_init_csb(adev);
		gfx_v8_0_init_save_restore_list(adev);
		gfx_v8_0_enable_save_restore_machine(adev);
		WREG32(mmRLC_JUMP_TABLE_RESTORE, adev->gfx.rlc.cp_table_gpu_addr >> 8);
		gfx_v8_0_init_power_gating(adev);
		WREG32(mmRLC_PG_ALWAYS_ON_CU_MASK, adev->gfx.cu_info.ao_cu_mask);
	} else if ((adev->asic_type == CHIP_POLARIS11) ||
		   (adev->asic_type == CHIP_POLARIS12) ||
		   (adev->asic_type == CHIP_VEGAM)) {
		gfx_v8_0_init_csb(adev);
		gfx_v8_0_init_save_restore_list(adev);
		gfx_v8_0_enable_save_restore_machine(adev);
		gfx_v8_0_init_power_gating(adev);
	}

}

static void gfx_v8_0_rlc_stop(struct amdgpu_device *adev)
{
	WREG32_FIELD(RLC_CNTL, RLC_ENABLE_F32, 0);

	gfx_v8_0_enable_gui_idle_interrupt(adev, false);
	gfx_v8_0_wait_for_rlc_serdes(adev);
}

static void gfx_v8_0_rlc_reset(struct amdgpu_device *adev)
{
	WREG32_FIELD(GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	udelay(50);

	WREG32_FIELD(GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v8_0_rlc_start(struct amdgpu_device *adev)
{
	WREG32_FIELD(RLC_CNTL, RLC_ENABLE_F32, 1);

	/* carrizo do enable cp interrupt after cp inited */
	if (!(adev->flags & AMD_IS_APU))
		gfx_v8_0_enable_gui_idle_interrupt(adev, true);

	udelay(50);
}

static int gfx_v8_0_rlc_resume(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		gfx_v8_0_init_csb(adev);
		return 0;
	}

	adev->gfx.rlc.funcs->stop(adev);
	adev->gfx.rlc.funcs->reset(adev);
	gfx_v8_0_init_pg(adev);
	adev->gfx.rlc.funcs->start(adev);

	return 0;
}

static void gfx_v8_0_cp_gfx_enable(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32(mmCP_ME_CNTL);

	if (enable) {
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, 0);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, 0);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, 0);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, 1);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, 1);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, 1);
	}
	WREG32(mmCP_ME_CNTL, tmp);
	udelay(50);
}

static u32 gfx_v8_0_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = vi_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}
	/* pa_sc_raster_config/pa_sc_raster_config1 */
	count += 4;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

static int gfx_v8_0_cp_gfx_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.gfx_ring[0];
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	int r, i;

	/* init the CP */
	WREG32(mmCP_MAX_CONTEXT, adev->gfx.config.max_hw_contexts - 1);
	WREG32(mmCP_ENDIAN_SWAP, 0);
	WREG32(mmCP_DEVICE_ID, 1);

	gfx_v8_0_cp_gfx_enable(adev, true);

	r = amdgpu_ring_alloc(ring, gfx_v8_0_get_csb_size(adev) + 4);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* clear state buffer */
	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, 0x80000000);
	amdgpu_ring_write(ring, 0x80000000);

	for (sect = vi_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				amdgpu_ring_write(ring,
				       PACKET3(PACKET3_SET_CONTEXT_REG,
					       ext->reg_count));
				amdgpu_ring_write(ring,
				       ext->reg_index - PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					amdgpu_ring_write(ring, ext->extent[i]);
			}
		}
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	amdgpu_ring_write(ring, mmPA_SC_RASTER_CONFIG - PACKET3_SET_CONTEXT_REG_START);
	amdgpu_ring_write(ring, adev->gfx.config.rb_config[0][0].raster_config);
	amdgpu_ring_write(ring, adev->gfx.config.rb_config[0][0].raster_config_1);

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	/* init the CE partitions */
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	amdgpu_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	amdgpu_ring_write(ring, 0x8000);
	amdgpu_ring_write(ring, 0x8000);

	amdgpu_ring_commit(ring);

	return 0;
}
static void gfx_v8_0_set_cpg_door_bell(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	u32 tmp;
	/* no gfx doorbells on iceland */
	if (adev->asic_type == CHIP_TOPAZ)
		return;

	tmp = RREG32(mmCP_RB_DOORBELL_CONTROL);

	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
						DOORBELL_HIT, 0);
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
					    DOORBELL_EN, 1);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL, DOORBELL_EN, 0);
	}

	WREG32(mmCP_RB_DOORBELL_CONTROL, tmp);

	if (adev->flags & AMD_IS_APU)
		return;

	tmp = REG_SET_FIELD(0, CP_RB_DOORBELL_RANGE_LOWER,
					DOORBELL_RANGE_LOWER,
					adev->doorbell_index.gfx_ring0);
	WREG32(mmCP_RB_DOORBELL_RANGE_LOWER, tmp);

	WREG32(mmCP_RB_DOORBELL_RANGE_UPPER,
		CP_RB_DOORBELL_RANGE_UPPER__DOORBELL_RANGE_UPPER_MASK);
}

static int gfx_v8_0_cp_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr, rptr_addr, wptr_gpu_addr;

	/* Set the write pointer delay */
	WREG32(mmCP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32(mmCP_RB_VMID, 0);

	/* Set ring buffer size */
	ring = &adev->gfx.gfx_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = REG_SET_FIELD(0, CP_RB0_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, RB_BLKSZ, rb_bufsz - 2);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, MTYPE, 3);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, MIN_IB_AVAILSZ, 1);
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, BUF_SWAP, 1);
#endif
	WREG32(mmCP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(mmCP_RB0_CNTL, tmp | CP_RB0_CNTL__RB_RPTR_WR_ENA_MASK);
	ring->wptr = 0;
	WREG32(mmCP_RB0_WPTR, lower_32_bits(ring->wptr));

	/* set the wb address wether it's enabled or not */
	rptr_addr = ring->rptr_gpu_addr;
	WREG32(mmCP_RB0_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32(mmCP_RB0_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & 0xFF);

	wptr_gpu_addr = ring->wptr_gpu_addr;
	WREG32(mmCP_RB_WPTR_POLL_ADDR_LO, lower_32_bits(wptr_gpu_addr));
	WREG32(mmCP_RB_WPTR_POLL_ADDR_HI, upper_32_bits(wptr_gpu_addr));
	mdelay(1);
	WREG32(mmCP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32(mmCP_RB0_BASE, rb_addr);
	WREG32(mmCP_RB0_BASE_HI, upper_32_bits(rb_addr));

	gfx_v8_0_set_cpg_door_bell(adev, ring);
	/* start the ring */
	amdgpu_ring_clear_ring(ring);
	gfx_v8_0_cp_gfx_start(adev);

	return 0;
}

static void gfx_v8_0_cp_compute_enable(struct amdgpu_device *adev, bool enable)
{
	if (enable) {
		WREG32(mmCP_MEC_CNTL, 0);
	} else {
		WREG32(mmCP_MEC_CNTL, (CP_MEC_CNTL__MEC_ME1_HALT_MASK | CP_MEC_CNTL__MEC_ME2_HALT_MASK));
		adev->gfx.kiq[0].ring.sched.ready = false;
	}
	udelay(50);
}

/* KIQ functions */
static void gfx_v8_0_kiq_setting(struct amdgpu_ring *ring)
{
	uint32_t tmp;
	struct amdgpu_device *adev = ring->adev;

	/* tell RLC which is KIQ queue */
	tmp = RREG32(mmRLC_CP_SCHEDULERS);
	tmp &= 0xffffff00;
	tmp |= (ring->me << 5) | (ring->pipe << 3) | (ring->queue);
	WREG32(mmRLC_CP_SCHEDULERS, tmp);
	tmp |= 0x80;
	WREG32(mmRLC_CP_SCHEDULERS, tmp);
}

static int gfx_v8_0_kiq_kcq_enable(struct amdgpu_device *adev)
{
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq[0].ring;
	uint64_t queue_mask = 0;
	int r, i;

	for (i = 0; i < AMDGPU_MAX_COMPUTE_QUEUES; ++i) {
		if (!test_bit(i, adev->gfx.mec_bitmap[0].queue_bitmap))
			continue;

		/* This situation may be hit in the future if a new HW
		 * generation exposes more than 64 queues. If so, the
		 * definition of queue_mask needs updating */
		if (WARN_ON(i >= (sizeof(queue_mask)*8))) {
			DRM_ERROR("Invalid KCQ enabled: %d\n", i);
			break;
		}

		queue_mask |= (1ull << i);
	}

	r = amdgpu_ring_alloc(kiq_ring, (8 * adev->gfx.num_compute_rings) + 8);
	if (r) {
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);
		return r;
	}
	/* set resources */
	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_SET_RESOURCES, 6));
	amdgpu_ring_write(kiq_ring, 0);	/* vmid_mask:0 queue_type:0 (KIQ) */
	amdgpu_ring_write(kiq_ring, lower_32_bits(queue_mask));	/* queue mask lo */
	amdgpu_ring_write(kiq_ring, upper_32_bits(queue_mask));	/* queue mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask lo */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* oac mask */
	amdgpu_ring_write(kiq_ring, 0);	/* gds heap base:0, gds heap size:0 */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];
		uint64_t mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
		uint64_t wptr_addr = ring->wptr_gpu_addr;

		/* map queues */
		amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
		/* Q_sel:0, vmid:0, vidmem: 1, engine:0, num_Q:1*/
		amdgpu_ring_write(kiq_ring,
				  PACKET3_MAP_QUEUES_NUM_QUEUES(1));
		amdgpu_ring_write(kiq_ring,
				  PACKET3_MAP_QUEUES_DOORBELL_OFFSET(ring->doorbell_index) |
				  PACKET3_MAP_QUEUES_QUEUE(ring->queue) |
				  PACKET3_MAP_QUEUES_PIPE(ring->pipe) |
				  PACKET3_MAP_QUEUES_ME(ring->me == 1 ? 0 : 1)); /* doorbell */
		amdgpu_ring_write(kiq_ring, lower_32_bits(mqd_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(mqd_addr));
		amdgpu_ring_write(kiq_ring, lower_32_bits(wptr_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(wptr_addr));
	}

	amdgpu_ring_commit(kiq_ring);

	return 0;
}

static int gfx_v8_0_deactivate_hqd(struct amdgpu_device *adev, u32 req)
{
	int i, r = 0;

	if (RREG32(mmCP_HQD_ACTIVE) & CP_HQD_ACTIVE__ACTIVE_MASK) {
		WREG32_FIELD(CP_HQD_DEQUEUE_REQUEST, DEQUEUE_REQ, req);
		for (i = 0; i < adev->usec_timeout; i++) {
			if (!(RREG32(mmCP_HQD_ACTIVE) & CP_HQD_ACTIVE__ACTIVE_MASK))
				break;
			udelay(1);
		}
		if (i == adev->usec_timeout)
			r = -ETIMEDOUT;
	}
	WREG32(mmCP_HQD_DEQUEUE_REQUEST, 0);
	WREG32(mmCP_HQD_PQ_RPTR, 0);
	WREG32(mmCP_HQD_PQ_WPTR, 0);

	return r;
}

static void gfx_v8_0_mqd_set_priority(struct amdgpu_ring *ring, struct vi_mqd *mqd)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) {
		if (amdgpu_gfx_is_high_priority_compute_queue(adev, ring)) {
			mqd->cp_hqd_pipe_priority = AMDGPU_GFX_PIPE_PRIO_HIGH;
			mqd->cp_hqd_queue_priority =
				AMDGPU_GFX_QUEUE_PRIORITY_MAXIMUM;
		}
	}
}

static int gfx_v8_0_mqd_init(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct vi_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000003;
	mqd->dynamic_cu_mask_addr_lo = lower_32_bits(ring->mqd_gpu_addr
						     + offsetof(struct vi_mqd_allocation, dynamic_cu_mask));
	mqd->dynamic_cu_mask_addr_hi = upper_32_bits(ring->mqd_gpu_addr
						     + offsetof(struct vi_mqd_allocation, dynamic_cu_mask));
	eop_base_addr = ring->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32(mmCP_HQD_EOP_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(GFX8_MEC_HPD_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = REG_SET_FIELD(RREG32(mmCP_HQD_PQ_DOORBELL_CONTROL),
			    CP_HQD_PQ_DOORBELL_CONTROL,
			    DOORBELL_EN,
			    ring->use_doorbell ? 1 : 0);

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = RREG32(mmCP_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = RREG32(mmCP_HQD_PQ_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
			    (order_base_2(ring->ring_size / 4) - 1));
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			(order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1));
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ENDIAN_SWAP, 1);
#endif
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ROQ_PQ_IB_FLIP, 0);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = ring->rptr_gpu_addr;
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = ring->wptr_gpu_addr;
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	tmp = 0;
	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		tmp = RREG32(mmCP_HQD_PQ_DOORBELL_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				DOORBELL_OFFSET, ring->doorbell_index);

		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_HIT, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_hqd_pq_wptr = ring->wptr;
	mqd->cp_hqd_pq_rptr = RREG32(mmCP_HQD_PQ_RPTR);

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = RREG32(mmCP_HQD_PERSISTENT_STATE);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MTYPE */
	tmp = RREG32(mmCP_HQD_IB_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 3);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MTYPE, 3);
	mqd->cp_hqd_ib_control = tmp;

	tmp = RREG32(mmCP_HQD_IQ_TIMER);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IQ_TIMER, MTYPE, 3);
	mqd->cp_hqd_iq_timer = tmp;

	tmp = RREG32(mmCP_HQD_CTX_SAVE_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_CTX_SAVE_CONTROL, MTYPE, 3);
	mqd->cp_hqd_ctx_save_control = tmp;

	/* defaults */
	mqd->cp_hqd_eop_rptr = RREG32(mmCP_HQD_EOP_RPTR);
	mqd->cp_hqd_eop_wptr = RREG32(mmCP_HQD_EOP_WPTR);
	mqd->cp_hqd_ctx_save_base_addr_lo = RREG32(mmCP_HQD_CTX_SAVE_BASE_ADDR_LO);
	mqd->cp_hqd_ctx_save_base_addr_hi = RREG32(mmCP_HQD_CTX_SAVE_BASE_ADDR_HI);
	mqd->cp_hqd_cntl_stack_offset = RREG32(mmCP_HQD_CNTL_STACK_OFFSET);
	mqd->cp_hqd_cntl_stack_size = RREG32(mmCP_HQD_CNTL_STACK_SIZE);
	mqd->cp_hqd_wg_state_offset = RREG32(mmCP_HQD_WG_STATE_OFFSET);
	mqd->cp_hqd_ctx_save_size = RREG32(mmCP_HQD_CTX_SAVE_SIZE);
	mqd->cp_hqd_eop_done_events = RREG32(mmCP_HQD_EOP_EVENTS);
	mqd->cp_hqd_error = RREG32(mmCP_HQD_ERROR);
	mqd->cp_hqd_eop_wptr_mem = RREG32(mmCP_HQD_EOP_WPTR_MEM);
	mqd->cp_hqd_eop_dones = RREG32(mmCP_HQD_EOP_DONES);

	/* set static priority for a queue/ring */
	gfx_v8_0_mqd_set_priority(ring, mqd);
	mqd->cp_hqd_quantum = RREG32(mmCP_HQD_QUANTUM);

	/* map_queues packet doesn't need activate the queue,
	 * so only kiq need set this field.
	 */
	if (ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
		mqd->cp_hqd_active = 1;

	return 0;
}

static int gfx_v8_0_mqd_commit(struct amdgpu_device *adev,
			struct vi_mqd *mqd)
{
	uint32_t mqd_reg;
	uint32_t *mqd_data;

	/* HQD registers extend from mmCP_MQD_BASE_ADDR to mmCP_HQD_ERROR */
	mqd_data = &mqd->cp_mqd_base_addr_lo;

	/* disable wptr polling */
	WREG32_FIELD(CP_PQ_WPTR_POLL_CNTL, EN, 0);

	/* program all HQD registers */
	for (mqd_reg = mmCP_HQD_VMID; mqd_reg <= mmCP_HQD_EOP_CONTROL; mqd_reg++)
		WREG32(mqd_reg, mqd_data[mqd_reg - mmCP_MQD_BASE_ADDR]);

	/* Tonga errata: EOP RPTR/WPTR should be left unmodified.
	 * This is safe since EOP RPTR==WPTR for any inactive HQD
	 * on ASICs that do not support context-save.
	 * EOP writes/reads can start anywhere in the ring.
	 */
	if (adev->asic_type != CHIP_TONGA) {
		WREG32(mmCP_HQD_EOP_RPTR, mqd->cp_hqd_eop_rptr);
		WREG32(mmCP_HQD_EOP_WPTR, mqd->cp_hqd_eop_wptr);
		WREG32(mmCP_HQD_EOP_WPTR_MEM, mqd->cp_hqd_eop_wptr_mem);
	}

	for (mqd_reg = mmCP_HQD_EOP_EVENTS; mqd_reg <= mmCP_HQD_ERROR; mqd_reg++)
		WREG32(mqd_reg, mqd_data[mqd_reg - mmCP_MQD_BASE_ADDR]);

	/* activate the HQD */
	for (mqd_reg = mmCP_MQD_BASE_ADDR; mqd_reg <= mmCP_HQD_ACTIVE; mqd_reg++)
		WREG32(mqd_reg, mqd_data[mqd_reg - mmCP_MQD_BASE_ADDR]);

	return 0;
}

static int gfx_v8_0_kiq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct vi_mqd *mqd = ring->mqd_ptr;

	gfx_v8_0_kiq_setting(ring);

	if (amdgpu_in_reset(adev)) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.kiq[0].mqd_backup)
			memcpy(mqd, adev->gfx.kiq[0].mqd_backup, sizeof(struct vi_mqd_allocation));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);
		mutex_lock(&adev->srbm_mutex);
		vi_srbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v8_0_mqd_commit(adev, mqd);
		vi_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	} else {
		memset((void *)mqd, 0, sizeof(struct vi_mqd_allocation));
		((struct vi_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct vi_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		if (amdgpu_sriov_vf(adev) && adev->in_suspend)
			amdgpu_ring_clear_ring(ring);
		mutex_lock(&adev->srbm_mutex);
		vi_srbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v8_0_mqd_init(ring);
		gfx_v8_0_mqd_commit(adev, mqd);
		vi_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.kiq[0].mqd_backup)
			memcpy(adev->gfx.kiq[0].mqd_backup, mqd, sizeof(struct vi_mqd_allocation));
	}

	return 0;
}

static int gfx_v8_0_kcq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct vi_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = ring - &adev->gfx.compute_ring[0];

	if (!amdgpu_in_reset(adev) && !adev->in_suspend) {
		memset((void *)mqd, 0, sizeof(struct vi_mqd_allocation));
		((struct vi_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct vi_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		mutex_lock(&adev->srbm_mutex);
		vi_srbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v8_0_mqd_init(ring);
		vi_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(struct vi_mqd_allocation));
	} else {
		/* restore MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(struct vi_mqd_allocation));
		/* reset ring buffer */
		ring->wptr = 0;
		atomic64_set((atomic64_t *)ring->wptr_cpu_addr, 0);
		amdgpu_ring_clear_ring(ring);
	}
	return 0;
}

static void gfx_v8_0_set_mec_doorbell_range(struct amdgpu_device *adev)
{
	if (adev->asic_type > CHIP_TONGA) {
		WREG32(mmCP_MEC_DOORBELL_RANGE_LOWER, adev->doorbell_index.kiq << 2);
		WREG32(mmCP_MEC_DOORBELL_RANGE_UPPER, adev->doorbell_index.mec_ring7 << 2);
	}
	/* enable doorbells */
	WREG32_FIELD(CP_PQ_STATUS, DOORBELL_ENABLE, 1);
}

static int gfx_v8_0_kiq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->gfx.kiq[0].ring;

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_kmap(ring->mqd_obj, &ring->mqd_ptr);
	if (unlikely(r != 0)) {
		amdgpu_bo_unreserve(ring->mqd_obj);
		return r;
	}

	gfx_v8_0_kiq_init_queue(ring);
	amdgpu_bo_kunmap(ring->mqd_obj);
	ring->mqd_ptr = NULL;
	amdgpu_bo_unreserve(ring->mqd_obj);
	return 0;
}

static int gfx_v8_0_kcq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = NULL;
	int r = 0, i;

	gfx_v8_0_cp_compute_enable(adev, true);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;
		r = amdgpu_bo_kmap(ring->mqd_obj, &ring->mqd_ptr);
		if (!r) {
			r = gfx_v8_0_kcq_init_queue(ring);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}

	gfx_v8_0_set_mec_doorbell_range(adev);

	r = gfx_v8_0_kiq_kcq_enable(adev);
	if (r)
		goto done;

done:
	return r;
}

static int gfx_v8_0_cp_test_all_rings(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *ring;

	/* collect all the ring_tests here, gfx, kiq, compute */
	ring = &adev->gfx.gfx_ring[0];
	r = amdgpu_ring_test_helper(ring);
	if (r)
		return r;

	ring = &adev->gfx.kiq[0].ring;
	r = amdgpu_ring_test_helper(ring);
	if (r)
		return r;

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		amdgpu_ring_test_helper(ring);
	}

	return 0;
}

static int gfx_v8_0_cp_resume(struct amdgpu_device *adev)
{
	int r;

	if (!(adev->flags & AMD_IS_APU))
		gfx_v8_0_enable_gui_idle_interrupt(adev, false);

	r = gfx_v8_0_kiq_resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_cp_gfx_resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_kcq_resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_cp_test_all_rings(adev);
	if (r)
		return r;

	gfx_v8_0_enable_gui_idle_interrupt(adev, true);

	return 0;
}

static void gfx_v8_0_cp_enable(struct amdgpu_device *adev, bool enable)
{
	gfx_v8_0_cp_gfx_enable(adev, enable);
	gfx_v8_0_cp_compute_enable(adev, enable);
}

static int gfx_v8_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfx_v8_0_init_golden_registers(adev);
	gfx_v8_0_constants_init(adev);

	r = adev->gfx.rlc.funcs->resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_cp_resume(adev);

	return r;
}

static int gfx_v8_0_kcq_disable(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq[0].ring;

	r = amdgpu_ring_alloc(kiq_ring, 6 * adev->gfx.num_compute_rings);
	if (r)
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_UNMAP_QUEUES, 4));
		amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
						PACKET3_UNMAP_QUEUES_ACTION(1) | /* RESET_QUEUES */
						PACKET3_UNMAP_QUEUES_QUEUE_SEL(0) |
						PACKET3_UNMAP_QUEUES_ENGINE_SEL(0) |
						PACKET3_UNMAP_QUEUES_NUM_QUEUES(1));
		amdgpu_ring_write(kiq_ring, PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(ring->doorbell_index));
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
	}
	/* Submit unmap queue packet */
	amdgpu_ring_commit(kiq_ring);
	/*
	 * Ring test will do a basic scratch register change check. Just run
	 * this to ensure that unmap queues that is submitted before got
	 * processed successfully before returning.
	 */
	r = amdgpu_ring_test_helper(kiq_ring);
	if (r)
		DRM_ERROR("KCQ disable failed\n");

	return r;
}

static bool gfx_v8_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (REG_GET_FIELD(RREG32(mmGRBM_STATUS), GRBM_STATUS, GUI_ACTIVE)
		|| RREG32(mmGRBM_STATUS2) != 0x8)
		return false;
	else
		return true;
}

static bool gfx_v8_0_rlc_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (RREG32(mmGRBM_STATUS2) != 0x8)
		return false;
	else
		return true;
}

static int gfx_v8_0_wait_for_rlc_idle(void *handle)
{
	unsigned int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v8_0_rlc_is_idle(handle))
			return 0;

		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v8_0_wait_for_idle(void *handle)
{
	unsigned int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v8_0_is_idle(handle))
			return 0;

		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v8_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);

	amdgpu_irq_put(adev, &adev->gfx.cp_ecc_error_irq, 0);

	amdgpu_irq_put(adev, &adev->gfx.sq_irq, 0);

	/* disable KCQ to avoid CPC touch memory not valid anymore */
	gfx_v8_0_kcq_disable(adev);

	if (amdgpu_sriov_vf(adev)) {
		pr_debug("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}
	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);
	if (!gfx_v8_0_wait_for_idle(adev))
		gfx_v8_0_cp_enable(adev, false);
	else
		pr_err("cp is busy, skip halt cp\n");
	if (!gfx_v8_0_wait_for_rlc_idle(adev))
		adev->gfx.rlc.funcs->stop(adev);
	else
		pr_err("rlc is busy, skip halt rlc\n");
	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	return 0;
}

static int gfx_v8_0_suspend(void *handle)
{
	return gfx_v8_0_hw_fini(handle);
}

static int gfx_v8_0_resume(void *handle)
{
	return gfx_v8_0_hw_init(handle);
}

static bool gfx_v8_0_check_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	/* GRBM_STATUS */
	tmp = RREG32(mmGRBM_STATUS);
	if (tmp & (GRBM_STATUS__PA_BUSY_MASK | GRBM_STATUS__SC_BUSY_MASK |
		   GRBM_STATUS__BCI_BUSY_MASK | GRBM_STATUS__SX_BUSY_MASK |
		   GRBM_STATUS__TA_BUSY_MASK | GRBM_STATUS__VGT_BUSY_MASK |
		   GRBM_STATUS__DB_BUSY_MASK | GRBM_STATUS__CB_BUSY_MASK |
		   GRBM_STATUS__GDS_BUSY_MASK | GRBM_STATUS__SPI_BUSY_MASK |
		   GRBM_STATUS__IA_BUSY_MASK | GRBM_STATUS__IA_BUSY_NO_DMA_MASK |
		   GRBM_STATUS__CP_BUSY_MASK | GRBM_STATUS__CP_COHERENCY_BUSY_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP, 1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_GFX, 1);
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_GRBM, 1);
	}

	/* GRBM_STATUS2 */
	tmp = RREG32(mmGRBM_STATUS2);
	if (REG_GET_FIELD(tmp, GRBM_STATUS2, RLC_BUSY))
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);

	if (REG_GET_FIELD(tmp, GRBM_STATUS2, CPF_BUSY) ||
	    REG_GET_FIELD(tmp, GRBM_STATUS2, CPC_BUSY) ||
	    REG_GET_FIELD(tmp, GRBM_STATUS2, CPG_BUSY)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET,
						SOFT_RESET_CPF, 1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET,
						SOFT_RESET_CPC, 1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET,
						SOFT_RESET_CPG, 1);
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET,
						SOFT_RESET_GRBM, 1);
	}

	/* SRBM_STATUS */
	tmp = RREG32(mmSRBM_STATUS);
	if (REG_GET_FIELD(tmp, SRBM_STATUS, GRBM_RQ_PENDING))
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_GRBM, 1);
	if (REG_GET_FIELD(tmp, SRBM_STATUS, SEM_BUSY))
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_SEM, 1);

	if (grbm_soft_reset || srbm_soft_reset) {
		adev->gfx.grbm_soft_reset = grbm_soft_reset;
		adev->gfx.srbm_soft_reset = srbm_soft_reset;
		return true;
	} else {
		adev->gfx.grbm_soft_reset = 0;
		adev->gfx.srbm_soft_reset = 0;
		return false;
	}
}

static int gfx_v8_0_pre_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 grbm_soft_reset = 0;

	if ((!adev->gfx.grbm_soft_reset) &&
	    (!adev->gfx.srbm_soft_reset))
		return 0;

	grbm_soft_reset = adev->gfx.grbm_soft_reset;

	/* stop the rlc */
	adev->gfx.rlc.funcs->stop(adev);

	if (REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CP) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_GFX))
		/* Disable GFX parsing/prefetching */
		gfx_v8_0_cp_gfx_enable(adev, false);

	if (REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CP) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPF) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPC) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPG)) {
		int i;

		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

			mutex_lock(&adev->srbm_mutex);
			vi_srbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
			gfx_v8_0_deactivate_hqd(adev, 2);
			vi_srbm_select(adev, 0, 0, 0, 0);
			mutex_unlock(&adev->srbm_mutex);
		}
		/* Disable MEC parsing/prefetching */
		gfx_v8_0_cp_compute_enable(adev, false);
	}

	return 0;
}

static int gfx_v8_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if ((!adev->gfx.grbm_soft_reset) &&
	    (!adev->gfx.srbm_soft_reset))
		return 0;

	grbm_soft_reset = adev->gfx.grbm_soft_reset;
	srbm_soft_reset = adev->gfx.srbm_soft_reset;

	if (grbm_soft_reset || srbm_soft_reset) {
		tmp = RREG32(mmGMCON_DEBUG);
		tmp = REG_SET_FIELD(tmp, GMCON_DEBUG, GFX_STALL, 1);
		tmp = REG_SET_FIELD(tmp, GMCON_DEBUG, GFX_CLEAR, 1);
		WREG32(mmGMCON_DEBUG, tmp);
		udelay(50);
	}

	if (grbm_soft_reset) {
		tmp = RREG32(mmGRBM_SOFT_RESET);
		tmp |= grbm_soft_reset;
		dev_info(adev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmGRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmGRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~grbm_soft_reset;
		WREG32(mmGRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmGRBM_SOFT_RESET);
	}

	if (srbm_soft_reset) {
		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);
	}

	if (grbm_soft_reset || srbm_soft_reset) {
		tmp = RREG32(mmGMCON_DEBUG);
		tmp = REG_SET_FIELD(tmp, GMCON_DEBUG, GFX_STALL, 0);
		tmp = REG_SET_FIELD(tmp, GMCON_DEBUG, GFX_CLEAR, 0);
		WREG32(mmGMCON_DEBUG, tmp);
	}

	/* Wait a little for things to settle down */
	udelay(50);

	return 0;
}

static int gfx_v8_0_post_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 grbm_soft_reset = 0;

	if ((!adev->gfx.grbm_soft_reset) &&
	    (!adev->gfx.srbm_soft_reset))
		return 0;

	grbm_soft_reset = adev->gfx.grbm_soft_reset;

	if (REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CP) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPF) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPC) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CPG)) {
		int i;

		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

			mutex_lock(&adev->srbm_mutex);
			vi_srbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
			gfx_v8_0_deactivate_hqd(adev, 2);
			vi_srbm_select(adev, 0, 0, 0, 0);
			mutex_unlock(&adev->srbm_mutex);
		}
		gfx_v8_0_kiq_resume(adev);
		gfx_v8_0_kcq_resume(adev);
	}

	if (REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_CP) ||
	    REG_GET_FIELD(grbm_soft_reset, GRBM_SOFT_RESET, SOFT_RESET_GFX))
		gfx_v8_0_cp_gfx_resume(adev);

	gfx_v8_0_cp_test_all_rings(adev);

	adev->gfx.rlc.funcs->start(adev);

	return 0;
}

/**
 * gfx_v8_0_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @adev: amdgpu_device pointer
 *
 * Fetches a GPU clock counter snapshot.
 * Returns the 64 bit clock counter snapshot.
 */
static uint64_t gfx_v8_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32(mmRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(mmRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32(mmRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	return clock;
}

static void gfx_v8_0_ring_emit_gds_switch(struct amdgpu_ring *ring,
					  uint32_t vmid,
					  uint32_t gds_base, uint32_t gds_size,
					  uint32_t gws_base, uint32_t gws_size,
					  uint32_t oa_base, uint32_t oa_size)
{
	/* GDS Base */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].mem_base);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gds_base);

	/* GDS Size */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].mem_size);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gds_size);

	/* GWS */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].gws);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gws_size << GDS_GWS_VMID0__SIZE__SHIFT | gws_base);

	/* OA */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].oa);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, (1 << (oa_size + oa_base)) - (1 << oa_base));
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t address)
{
	WREG32(mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK));
	return RREG32(mmSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev, uint32_t simd,
			   uint32_t wave, uint32_t thread,
			   uint32_t regno, uint32_t num, uint32_t *out)
{
	WREG32(mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__THREAD_ID__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32(mmSQ_IND_DATA);
}

static void gfx_v8_0_read_wave_data(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd, uint32_t wave, uint32_t *dst, int *no_fields)
{
	/* type 0 wave data */
	dst[(*no_fields)++] = 0;
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_HW_ID);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW1);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TRAPSTS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TBA_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TBA_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TMA_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TMA_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_DBG0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_M0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_MODE);
}

static void gfx_v8_0_read_wave_sgprs(struct amdgpu_device *adev, uint32_t xcc_id, uint32_t simd,
				     uint32_t wave, uint32_t start,
				     uint32_t size, uint32_t *dst)
{
	wave_read_regs(
		adev, simd, wave, 0,
		start + SQIND_WAVE_SGPRS_OFFSET, size, dst);
}


static const struct amdgpu_gfx_funcs gfx_v8_0_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v8_0_get_gpu_clock_counter,
	.select_se_sh = &gfx_v8_0_select_se_sh,
	.read_wave_data = &gfx_v8_0_read_wave_data,
	.read_wave_sgprs = &gfx_v8_0_read_wave_sgprs,
	.select_me_pipe_q = &gfx_v8_0_select_me_pipe_q
};

static int gfx_v8_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfx.xcc_mask = 1;
	adev->gfx.num_gfx_rings = GFX8_NUM_GFX_RINGS;
	adev->gfx.num_compute_rings = min(amdgpu_gfx_get_num_kcq(adev),
					  AMDGPU_MAX_COMPUTE_RINGS);
	adev->gfx.funcs = &gfx_v8_0_gfx_funcs;
	gfx_v8_0_set_ring_funcs(adev);
	gfx_v8_0_set_irq_funcs(adev);
	gfx_v8_0_set_gds_init(adev);
	gfx_v8_0_set_rlc_funcs(adev);

	return 0;
}

static int gfx_v8_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	/* requires IBs so do in late init after IB pool is initialized */
	r = gfx_v8_0_do_edc_gpr_workarounds(adev);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.cp_ecc_error_irq, 0);
	if (r) {
		DRM_ERROR("amdgpu_irq_get() failed to get IRQ for EDC, r: %d.\n", r);
		return r;
	}

	r = amdgpu_irq_get(adev, &adev->gfx.sq_irq, 0);
	if (r) {
		DRM_ERROR(
			"amdgpu_irq_get() failed to get IRQ for SQ, r: %d.\n",
			r);
		return r;
	}

	return 0;
}

static void gfx_v8_0_enable_gfx_static_mg_power_gating(struct amdgpu_device *adev,
						       bool enable)
{
	if ((adev->asic_type == CHIP_POLARIS11) ||
	    (adev->asic_type == CHIP_POLARIS12) ||
	    (adev->asic_type == CHIP_VEGAM))
		/* Send msg to SMU via Powerplay */
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_GFX, enable);

	WREG32_FIELD(RLC_PG_CNTL, STATIC_PER_CU_PG_ENABLE, enable ? 1 : 0);
}

static void gfx_v8_0_enable_gfx_dynamic_mg_power_gating(struct amdgpu_device *adev,
							bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, DYN_PER_CU_PG_ENABLE, enable ? 1 : 0);
}

static void polaris11_enable_gfx_quick_mg_power_gating(struct amdgpu_device *adev,
		bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, QUICK_PG_ENABLE, enable ? 1 : 0);
}

static void cz_enable_gfx_cg_power_gating(struct amdgpu_device *adev,
					  bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, GFX_POWER_GATING_ENABLE, enable ? 1 : 0);
}

static void cz_enable_gfx_pipeline_power_gating(struct amdgpu_device *adev,
						bool enable)
{
	WREG32_FIELD(RLC_PG_CNTL, GFX_PIPELINE_PG_ENABLE, enable ? 1 : 0);

	/* Read any GFX register to wake up GFX. */
	if (!enable)
		RREG32(mmDB_RENDER_CONTROL);
}

static void cz_update_gfx_cg_power_gating(struct amdgpu_device *adev,
					  bool enable)
{
	if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) && enable) {
		cz_enable_gfx_cg_power_gating(adev, true);
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PIPELINE)
			cz_enable_gfx_pipeline_power_gating(adev, true);
	} else {
		cz_enable_gfx_cg_power_gating(adev, false);
		cz_enable_gfx_pipeline_power_gating(adev, false);
	}
}

static int gfx_v8_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_PG_STATE_GATE);

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_SMG |
				AMD_PG_SUPPORT_RLC_SMU_HS |
				AMD_PG_SUPPORT_CP |
				AMD_PG_SUPPORT_GFX_DMG))
		amdgpu_gfx_rlc_enter_safe_mode(adev, 0);
	switch (adev->asic_type) {
	case CHIP_CARRIZO:
	case CHIP_STONEY:

		if (adev->pg_flags & AMD_PG_SUPPORT_RLC_SMU_HS) {
			cz_enable_sck_slow_down_on_power_up(adev, true);
			cz_enable_sck_slow_down_on_power_down(adev, true);
		} else {
			cz_enable_sck_slow_down_on_power_up(adev, false);
			cz_enable_sck_slow_down_on_power_down(adev, false);
		}
		if (adev->pg_flags & AMD_PG_SUPPORT_CP)
			cz_enable_cp_power_gating(adev, true);
		else
			cz_enable_cp_power_gating(adev, false);

		cz_update_gfx_cg_power_gating(adev, enable);

		if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_SMG) && enable)
			gfx_v8_0_enable_gfx_static_mg_power_gating(adev, true);
		else
			gfx_v8_0_enable_gfx_static_mg_power_gating(adev, false);

		if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_DMG) && enable)
			gfx_v8_0_enable_gfx_dynamic_mg_power_gating(adev, true);
		else
			gfx_v8_0_enable_gfx_dynamic_mg_power_gating(adev, false);
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_SMG) && enable)
			gfx_v8_0_enable_gfx_static_mg_power_gating(adev, true);
		else
			gfx_v8_0_enable_gfx_static_mg_power_gating(adev, false);

		if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_DMG) && enable)
			gfx_v8_0_enable_gfx_dynamic_mg_power_gating(adev, true);
		else
			gfx_v8_0_enable_gfx_dynamic_mg_power_gating(adev, false);

		if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_QUICK_MG) && enable)
			polaris11_enable_gfx_quick_mg_power_gating(adev, true);
		else
			polaris11_enable_gfx_quick_mg_power_gating(adev, false);
		break;
	default:
		break;
	}
	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_SMG |
				AMD_PG_SUPPORT_RLC_SMU_HS |
				AMD_PG_SUPPORT_CP |
				AMD_PG_SUPPORT_GFX_DMG))
		amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
	return 0;
}

static void gfx_v8_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_GFX_MGCG */
	data = RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__CPF_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_MGCG;

	/* AMD_CG_SUPPORT_GFX_CGLG */
	data = RREG32(mmRLC_CGCG_CGLS_CTRL);
	if (data & RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGCG;

	/* AMD_CG_SUPPORT_GFX_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGLS;

	/* AMD_CG_SUPPORT_GFX_CGTS */
	data = RREG32(mmCGTS_SM_CTRL_REG);
	if (!(data & CGTS_SM_CTRL_REG__OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_CGTS;

	/* AMD_CG_SUPPORT_GFX_CGTS_LS */
	if (!(data & CGTS_SM_CTRL_REG__LS_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_CGTS_LS;

	/* AMD_CG_SUPPORT_GFX_RLC_LS */
	data = RREG32(mmRLC_MEM_SLP_CNTL);
	if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_RLC_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_CP_LS */
	data = RREG32(mmCP_MEM_SLP_CNTL);
	if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CP_LS | AMD_CG_SUPPORT_GFX_MGLS;
}

static void gfx_v8_0_send_serdes_cmd(struct amdgpu_device *adev,
				     uint32_t reg_addr, uint32_t cmd)
{
	uint32_t data;

	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);

	WREG32(mmRLC_SERDES_WR_CU_MASTER_MASK, 0xffffffff);
	WREG32(mmRLC_SERDES_WR_NONCU_MASTER_MASK, 0xffffffff);

	data = RREG32(mmRLC_SERDES_WR_CTRL);
	if (adev->asic_type == CHIP_STONEY)
		data &= ~(RLC_SERDES_WR_CTRL__WRITE_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__READ_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__P1_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__P2_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__RDDATA_RESET_MASK |
			  RLC_SERDES_WR_CTRL__POWER_DOWN_MASK |
			  RLC_SERDES_WR_CTRL__POWER_UP_MASK |
			  RLC_SERDES_WR_CTRL__SHORT_FORMAT_MASK |
			  RLC_SERDES_WR_CTRL__SRBM_OVERRIDE_MASK);
	else
		data &= ~(RLC_SERDES_WR_CTRL__WRITE_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__READ_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__P1_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__P2_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__RDDATA_RESET_MASK |
			  RLC_SERDES_WR_CTRL__POWER_DOWN_MASK |
			  RLC_SERDES_WR_CTRL__POWER_UP_MASK |
			  RLC_SERDES_WR_CTRL__SHORT_FORMAT_MASK |
			  RLC_SERDES_WR_CTRL__BPM_DATA_MASK |
			  RLC_SERDES_WR_CTRL__REG_ADDR_MASK |
			  RLC_SERDES_WR_CTRL__SRBM_OVERRIDE_MASK);
	data |= (RLC_SERDES_WR_CTRL__RSVD_BPM_ADDR_MASK |
		 (cmd << RLC_SERDES_WR_CTRL__BPM_DATA__SHIFT) |
		 (reg_addr << RLC_SERDES_WR_CTRL__REG_ADDR__SHIFT) |
		 (0xff << RLC_SERDES_WR_CTRL__BPM_ADDR__SHIFT));

	WREG32(mmRLC_SERDES_WR_CTRL, data);
}

#define MSG_ENTER_RLC_SAFE_MODE     1
#define MSG_EXIT_RLC_SAFE_MODE      0
#define RLC_GPR_REG2__REQ_MASK 0x00000001
#define RLC_GPR_REG2__REQ__SHIFT 0
#define RLC_GPR_REG2__MESSAGE__SHIFT 0x00000001
#define RLC_GPR_REG2__MESSAGE_MASK 0x0000001e

static bool gfx_v8_0_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_setting;

	rlc_setting = RREG32(mmRLC_CNTL);
	if (!(rlc_setting & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return false;

	return true;
}

static void gfx_v8_0_set_safe_mode(struct amdgpu_device *adev, int xcc_id)
{
	uint32_t data;
	unsigned i;
	data = RREG32(mmRLC_CNTL);
	data |= RLC_SAFE_MODE__CMD_MASK;
	data &= ~RLC_SAFE_MODE__MESSAGE_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
	WREG32(mmRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if ((RREG32(mmRLC_GPM_STAT) &
		     (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
		      RLC_GPM_STAT__GFX_POWER_STATUS_MASK)) ==
		    (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
		     RLC_GPM_STAT__GFX_POWER_STATUS_MASK))
			break;
		udelay(1);
	}
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32(mmRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v8_0_unset_safe_mode(struct amdgpu_device *adev, int xcc_id)
{
	uint32_t data;
	unsigned i;

	data = RREG32(mmRLC_CNTL);
	data |= RLC_SAFE_MODE__CMD_MASK;
	data &= ~RLC_SAFE_MODE__MESSAGE_MASK;
	WREG32(mmRLC_SAFE_MODE, data);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32(mmRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v8_0_update_spm_vmid(struct amdgpu_device *adev, struct amdgpu_ring *ring, unsigned vmid)
{
	u32 data;

	amdgpu_gfx_off_ctrl(adev, false);

	if (amdgpu_sriov_is_pp_one_vf(adev))
		data = RREG32_NO_KIQ(mmRLC_SPM_VMID);
	else
		data = RREG32(mmRLC_SPM_VMID);

	data &= ~RLC_SPM_VMID__RLC_SPM_VMID_MASK;
	data |= (vmid & RLC_SPM_VMID__RLC_SPM_VMID_MASK) << RLC_SPM_VMID__RLC_SPM_VMID__SHIFT;

	if (amdgpu_sriov_is_pp_one_vf(adev))
		WREG32_NO_KIQ(mmRLC_SPM_VMID, data);
	else
		WREG32(mmRLC_SPM_VMID, data);

	amdgpu_gfx_off_ctrl(adev, true);
}

static const struct amdgpu_rlc_funcs iceland_rlc_funcs = {
	.is_rlc_enabled = gfx_v8_0_is_rlc_enabled,
	.set_safe_mode = gfx_v8_0_set_safe_mode,
	.unset_safe_mode = gfx_v8_0_unset_safe_mode,
	.init = gfx_v8_0_rlc_init,
	.get_csb_size = gfx_v8_0_get_csb_size,
	.get_csb_buffer = gfx_v8_0_get_csb_buffer,
	.get_cp_table_num = gfx_v8_0_cp_jump_table_num,
	.resume = gfx_v8_0_rlc_resume,
	.stop = gfx_v8_0_rlc_stop,
	.reset = gfx_v8_0_rlc_reset,
	.start = gfx_v8_0_rlc_start,
	.update_spm_vmid = gfx_v8_0_update_spm_vmid
};

static void gfx_v8_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t temp, data;

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	/* It is disabled by HW by default */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS)
				/* 1 - RLC memory Light sleep */
				WREG32_FIELD(RLC_MEM_SLP_CNTL, RLC_MEM_LS_EN, 1);

			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS)
				WREG32_FIELD(CP_MEM_SLP_CNTL, CP_MEM_LS_EN, 1);
		}

		/* 3 - RLC_CGTT_MGCG_OVERRIDE */
		temp = data = RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		if (adev->flags & AMD_IS_APU)
			data &= ~(RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK);
		else
			data &= ~(RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK);

		if (temp != data)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 4 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 5 - clear mgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_MGCG_OVERRIDE, CLE_BPM_SERDES_CMD);

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGTS) {
			/* 6 - Enable CGTS(Tree Shade) MGCG /MGLS */
			temp = data = RREG32(mmCGTS_SM_CTRL_REG);
			data &= ~(CGTS_SM_CTRL_REG__SM_MODE_MASK);
			data |= (0x2 << CGTS_SM_CTRL_REG__SM_MODE__SHIFT);
			data |= CGTS_SM_CTRL_REG__SM_MODE_ENABLE_MASK;
			data &= ~CGTS_SM_CTRL_REG__OVERRIDE_MASK;
			if ((adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) &&
			    (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGTS_LS))
				data &= ~CGTS_SM_CTRL_REG__LS_OVERRIDE_MASK;
			data |= CGTS_SM_CTRL_REG__ON_MONITOR_ADD_EN_MASK;
			data |= (0x96 << CGTS_SM_CTRL_REG__ON_MONITOR_ADD__SHIFT);
			if (temp != data)
				WREG32(mmCGTS_SM_CTRL_REG, data);
		}
		udelay(50);

		/* 7 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);
	} else {
		/* 1 - MGCG_OVERRIDE[0] for CP and MGCG_OVERRIDE[1] for RLC */
		temp = data = RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data |= (RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK |
				RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK);
		if (temp != data)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 2 - disable MGLS in RLC */
		data = RREG32(mmRLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK) {
			data &= ~RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
			WREG32(mmRLC_MEM_SLP_CNTL, data);
		}

		/* 3 - disable MGLS in CP */
		data = RREG32(mmCP_MEM_SLP_CNTL);
		if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK) {
			data &= ~CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
			WREG32(mmCP_MEM_SLP_CNTL, data);
		}

		/* 4 - Disable CGTS(Tree Shade) MGCG and MGLS */
		temp = data = RREG32(mmCGTS_SM_CTRL_REG);
		data |= (CGTS_SM_CTRL_REG__OVERRIDE_MASK |
				CGTS_SM_CTRL_REG__LS_OVERRIDE_MASK);
		if (temp != data)
			WREG32(mmCGTS_SM_CTRL_REG, data);

		/* 5 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 6 - set mgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_MGCG_OVERRIDE, SET_BPM_SERDES_CMD);

		udelay(50);

		/* 7- wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
}

static void gfx_v8_0_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t temp, temp1, data, data1;

	temp = data = RREG32(mmRLC_CGCG_CGLS_CTRL);

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {
		temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data1 &= ~RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK;
		if (temp1 != data1)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);

		/* : wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 2 - clear cgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGCG_OVERRIDE, CLE_BPM_SERDES_CMD);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 3 - write cmd to set CGLS */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGLS_EN, SET_BPM_SERDES_CMD);

		/* 4 - enable cgcg */
		data |= RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS) {
			/* enable cgls*/
			data |= RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;

			temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
			data1 &= ~RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK;

			if (temp1 != data1)
				WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);
		} else {
			data &= ~RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		}

		if (temp != data)
			WREG32(mmRLC_CGCG_CGLS_CTRL, data);

		/* 5 enable cntx_empty_int_enable/cntx_busy_int_enable/
		 * Cmp_busy/GFX_Idle interrupts
		 */
		gfx_v8_0_enable_gui_idle_interrupt(adev, true);
	} else {
		/* disable cntx_empty_int_enable & GFX Idle interrupt */
		gfx_v8_0_enable_gui_idle_interrupt(adev, false);

		/* TEST CGCG */
		temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data1 |= (RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK |
				RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK);
		if (temp1 != data1)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);

		/* read gfx register to wake up cgcg */
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* write cmd to Set CGCG Override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGCG_OVERRIDE, SET_BPM_SERDES_CMD);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* write cmd to Clear CGLS */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGLS_EN, CLE_BPM_SERDES_CMD);

		/* disable cgcg, cgls should be disabled too. */
		data &= ~(RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK |
			  RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK);
		if (temp != data)
			WREG32(mmRLC_CGCG_CGLS_CTRL, data);
		/* enable interrupts again for PG */
		gfx_v8_0_enable_gui_idle_interrupt(adev, true);
	}

	gfx_v8_0_wait_for_rlc_serdes(adev);

	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
}
static int gfx_v8_0_update_gfx_clock_gating(struct amdgpu_device *adev,
					    bool enable)
{
	if (enable) {
		/* CGCG/CGLS should be enabled after MGCG/MGLS/TS(CG/LS)
		 * ===  MGCG + MGLS + TS(CG/LS) ===
		 */
		gfx_v8_0_update_medium_grain_clock_gating(adev, enable);
		gfx_v8_0_update_coarse_grain_clock_gating(adev, enable);
	} else {
		/* CGCG/CGLS should be disabled before MGCG/MGLS/TS(CG/LS)
		 * ===  CGCG + CGLS ===
		 */
		gfx_v8_0_update_coarse_grain_clock_gating(adev, enable);
		gfx_v8_0_update_medium_grain_clock_gating(adev, enable);
	}
	return 0;
}

static int gfx_v8_0_tonga_update_gfx_clock_gating(struct amdgpu_device *adev,
					  enum amd_clockgating_state state)
{
	uint32_t msg_id, pp_state = 0;
	uint32_t pp_support_state = 0;

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_CGLS)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_CG,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_MGCG | AMD_CG_SUPPORT_GFX_MGLS)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_MG,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	return 0;
}

static int gfx_v8_0_polaris_update_gfx_clock_gating(struct amdgpu_device *adev,
					  enum amd_clockgating_state state)
{

	uint32_t msg_id, pp_state = 0;
	uint32_t pp_support_state = 0;

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_CGLS)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_CG,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_3D_CGCG | AMD_CG_SUPPORT_GFX_3D_CGLS)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGLS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}
		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_3D,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_MGCG | AMD_CG_SUPPORT_GFX_MGLS)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			pp_support_state = PP_STATE_SUPPORT_LS;
			pp_state = PP_STATE_LS;
		}

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG) {
			pp_support_state |= PP_STATE_SUPPORT_CG;
			pp_state |= PP_STATE_CG;
		}

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_MG,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS) {
		pp_support_state = PP_STATE_SUPPORT_LS;

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_LS;

		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
				PP_BLOCK_GFX_RLC,
				pp_support_state,
				pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
		pp_support_state = PP_STATE_SUPPORT_LS;

		if (state == AMD_CG_STATE_UNGATE)
			pp_state = 0;
		else
			pp_state = PP_STATE_LS;
		msg_id = PP_CG_MSG_ID(PP_GROUP_GFX,
			PP_BLOCK_GFX_CP,
			pp_support_state,
			pp_state);
		amdgpu_dpm_set_clockgating_by_smu(adev, msg_id);
	}

	return 0;
}

static int gfx_v8_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		gfx_v8_0_update_gfx_clock_gating(adev,
						 state == AMD_CG_STATE_GATE);
		break;
	case CHIP_TONGA:
		gfx_v8_0_tonga_update_gfx_clock_gating(adev, state);
		break;
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		gfx_v8_0_polaris_update_gfx_clock_gating(adev, state);
		break;
	default:
		break;
	}
	return 0;
}

static u64 gfx_v8_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	return *ring->rptr_cpu_addr;
}

static u64 gfx_v8_0_ring_get_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		/* XXX check if swapping is necessary on BE */
		return *ring->wptr_cpu_addr;
	else
		return RREG32(mmCP_RB0_WPTR);
}

static void gfx_v8_0_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32(mmCP_RB0_WPTR, lower_32_bits(ring->wptr));
		(void)RREG32(mmCP_RB0_WPTR);
	}
}

static void gfx_v8_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 ref_and_mask, reg_mem_engine;

	if ((ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) ||
	    (ring->funcs->type == AMDGPU_RING_TYPE_KIQ)) {
		switch (ring->me) {
		case 1:
			ref_and_mask = GPU_HDP_FLUSH_DONE__CP2_MASK << ring->pipe;
			break;
		case 2:
			ref_and_mask = GPU_HDP_FLUSH_DONE__CP6_MASK << ring->pipe;
			break;
		default:
			return;
		}
		reg_mem_engine = 0;
	} else {
		ref_and_mask = GPU_HDP_FLUSH_DONE__CP0_MASK;
		reg_mem_engine = WAIT_REG_MEM_ENGINE(1); /* pfp */
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_OPERATION(1) | /* write, wait, write */
				 WAIT_REG_MEM_FUNCTION(3) |  /* == */
				 reg_mem_engine));
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_REQ);
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_DONE);
	amdgpu_ring_write(ring, ref_and_mask);
	amdgpu_ring_write(ring, ref_and_mask);
	amdgpu_ring_write(ring, 0x20); /* poll interval */
}

static void gfx_v8_0_ring_emit_vgt_flush(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE, 0));
	amdgpu_ring_write(ring, EVENT_TYPE(VS_PARTIAL_FLUSH) |
		EVENT_INDEX(4));

	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE, 0));
	amdgpu_ring_write(ring, EVENT_TYPE(VGT_FLUSH) |
		EVENT_INDEX(0));
}

static void gfx_v8_0_ring_emit_ib_gfx(struct amdgpu_ring *ring,
					struct amdgpu_job *job,
					struct amdgpu_ib *ib,
					uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 header, control = 0;

	if (ib->flags & AMDGPU_IB_FLAG_CE)
		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	else
		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vmid << 24);

	if (amdgpu_sriov_vf(ring->adev) && (ib->flags & AMDGPU_IB_FLAG_PREEMPT)) {
		control |= INDIRECT_BUFFER_PRE_ENB(1);

		if (!(ib->flags & AMDGPU_IB_FLAG_CE) && vmid)
			gfx_v8_0_ring_emit_de_meta(ring);
	}

	amdgpu_ring_write(ring, header);
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	amdgpu_ring_write(ring, control);
}

static void gfx_v8_0_ring_emit_ib_compute(struct amdgpu_ring *ring,
					  struct amdgpu_job *job,
					  struct amdgpu_ib *ib,
					  uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 control = INDIRECT_BUFFER_VALID | ib->length_dw | (vmid << 24);

	/* Currently, there is a high possibility to get wave ID mismatch
	 * between ME and GDS, leading to a hw deadlock, because ME generates
	 * different wave IDs than the GDS expects. This situation happens
	 * randomly when at least 5 compute pipes use GDS ordered append.
	 * The wave IDs generated by ME are also wrong after suspend/resume.
	 * Those are probably bugs somewhere else in the kernel driver.
	 *
	 * Writing GDS_COMPUTE_MAX_WAVE_ID resets wave ID counters in ME and
	 * GDS to 0 for this ring (me/pipe).
	 */
	if (ib->flags & AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		amdgpu_ring_write(ring, mmGDS_COMPUTE_MAX_WAVE_ID - PACKET3_SET_CONFIG_REG_START);
		amdgpu_ring_write(ring, ring->adev->gds.gds_compute_max_wave_id);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
				(2 << 0) |
#endif
				(ib->gpu_addr & 0xFFFFFFFC));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	amdgpu_ring_write(ring, control);
}

static void gfx_v8_0_ring_emit_fence_gfx(struct amdgpu_ring *ring, u64 addr,
					 u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;
	bool exec = flags & AMDGPU_FENCE_FLAG_EXEC;

	/* Workaround for cache flush problems. First send a dummy EOP
	 * event down the pipe with seq one below.
	 */
	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	amdgpu_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EOP_TC_WB_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xffff) |
				DATA_SEL(1) | INT_SEL(0));
	amdgpu_ring_write(ring, lower_32_bits(seq - 1));
	amdgpu_ring_write(ring, upper_32_bits(seq - 1));

	/* Then send the real EOP event down the pipe:
	 * EVENT_WRITE_EOP - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	amdgpu_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EOP_TC_WB_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5) |
				 (exec ? EOP_EXEC : 0)));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xffff) |
			  DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));

}

static void gfx_v8_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_MEM_SPACE(1) | /* memory */
				 WAIT_REG_MEM_FUNCTION(3) | /* equal */
				 WAIT_REG_MEM_ENGINE(usepfp))); /* pfp or me */
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, 0xffffffff);
	amdgpu_ring_write(ring, 4); /* poll interval */
}

static void gfx_v8_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vmid, uint64_t pd_addr)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);

	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for the invalidate to complete */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_OPERATION(0) | /* wait */
				 WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* ref */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, 0x20); /* poll interval */

	/* compute doesn't have PFP */
	if (usepfp) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);
	}
}

static u64 gfx_v8_0_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	return *ring->wptr_cpu_addr;
}

static void gfx_v8_0_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
	WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
}

static void gfx_v8_0_ring_emit_fence_compute(struct amdgpu_ring *ring,
					     u64 addr, u64 seq,
					     unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 5));
	amdgpu_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EOP_TC_WB_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
}

static void gfx_v8_0_ring_emit_fence_kiq(struct amdgpu_ring *ring, u64 addr,
					 u64 seq, unsigned int flags)
{
	/* we only allocate 32bit for each seq wb address */
	BUG_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	/* write fence seq to the "addr" */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(5) | WR_CONFIRM));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* set register to trigger INT */
		amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
		amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
					 WRITE_DATA_DST_SEL(0) | WR_CONFIRM));
		amdgpu_ring_write(ring, mmCPC_INT_STATUS);
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, 0x20000000); /* src_id is 178 */
	}
}

static void gfx_v8_ring_emit_sb(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v8_ring_emit_cntxcntl(struct amdgpu_ring *ring, uint32_t flags)
{
	uint32_t dw2 = 0;

	if (amdgpu_sriov_vf(ring->adev))
		gfx_v8_0_ring_emit_ce_meta(ring);

	dw2 |= 0x80000000; /* set load_enable otherwise this package is just NOPs */
	if (flags & AMDGPU_HAVE_CTX_SWITCH) {
		gfx_v8_0_ring_emit_vgt_flush(ring);
		/* set load_global_config & load_global_uconfig */
		dw2 |= 0x8001;
		/* set load_cs_sh_regs */
		dw2 |= 0x01000000;
		/* set load_per_context_state & load_gfx_sh_regs for GFX */
		dw2 |= 0x10002;

		/* set load_ce_ram if preamble presented */
		if (AMDGPU_PREAMBLE_IB_PRESENT & flags)
			dw2 |= 0x10000000;
	} else {
		/* still load_ce_ram if this is the first time preamble presented
		 * although there is no context switch happens.
		 */
		if (AMDGPU_PREAMBLE_IB_PRESENT_FIRST & flags)
			dw2 |= 0x10000000;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, dw2);
	amdgpu_ring_write(ring, 0);
}

static unsigned gfx_v8_0_ring_emit_init_cond_exec(struct amdgpu_ring *ring,
						  uint64_t addr)
{
	unsigned ret;

	amdgpu_ring_write(ring, PACKET3(PACKET3_COND_EXEC, 3));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	/* discard following DWs if *cond_exec_gpu_addr==0 */
	amdgpu_ring_write(ring, 0);
	ret = ring->wptr & ring->buf_mask;
	/* patch dummy value later */
	amdgpu_ring_write(ring, 0);
	return ret;
}

static void gfx_v8_0_ring_emit_rreg(struct amdgpu_ring *ring, uint32_t reg,
				    uint32_t reg_val_offs)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring, PACKET3(PACKET3_COPY_DATA, 4));
	amdgpu_ring_write(ring, 0 |	/* src: register*/
				(5 << 8) |	/* dst: memory */
				(1 << 20));	/* write confirm */
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, lower_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
	amdgpu_ring_write(ring, upper_32_bits(adev->wb.gpu_addr +
				reg_val_offs * 4));
}

static void gfx_v8_0_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				  uint32_t val)
{
	uint32_t cmd;

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		cmd = WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM;
		break;
	case AMDGPU_RING_TYPE_KIQ:
		cmd = 1 << 16; /* no inc addr */
		break;
	default:
		cmd = WR_CONFIRM;
		break;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, cmd);
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v8_0_wait_reg_mem(struct amdgpu_ring *ring, int eng_sel,
				  int mem_space, int opt, uint32_t addr0,
				  uint32_t addr1, uint32_t ref, uint32_t mask,
				  uint32_t inv)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring,
			  /* memory (1) or register (0) */
			  (WAIT_REG_MEM_MEM_SPACE(mem_space) |
			   WAIT_REG_MEM_OPERATION(opt) | /* wait */
			   WAIT_REG_MEM_FUNCTION(3) |  /* equal */
			   WAIT_REG_MEM_ENGINE(eng_sel)));

	if (mem_space)
		BUG_ON(addr0 & 0x3); /* Dword align */
	amdgpu_ring_write(ring, addr0);
	amdgpu_ring_write(ring, addr1);
	amdgpu_ring_write(ring, ref);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, inv); /* poll interval */
}

static void gfx_v8_0_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask)
{
	gfx_v8_0_wait_reg_mem(ring, 0, 0, 0, reg, 0, val, mask, 0x20);
}

static void gfx_v8_0_ring_soft_recovery(struct amdgpu_ring *ring, unsigned vmid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t value = 0;

	value = REG_SET_FIELD(value, SQ_CMD, CMD, 0x03);
	value = REG_SET_FIELD(value, SQ_CMD, MODE, 0x01);
	value = REG_SET_FIELD(value, SQ_CMD, CHECK_VMID, 1);
	value = REG_SET_FIELD(value, SQ_CMD, VM_ID, vmid);
	WREG32(mmSQ_CMD, value);
}

static void gfx_v8_0_set_gfx_eop_interrupt_state(struct amdgpu_device *adev,
						 enum amdgpu_interrupt_state state)
{
	WREG32_FIELD(CP_INT_CNTL_RING0, TIME_STAMP_INT_ENABLE,
		     state == AMDGPU_IRQ_STATE_DISABLE ? 0 : 1);
}

static void gfx_v8_0_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
						     int me, int pipe,
						     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = mmCP_ME1_PIPE0_INT_CNTL;
			break;
		case 1:
			mec_int_cntl_reg = mmCP_ME1_PIPE1_INT_CNTL;
			break;
		case 2:
			mec_int_cntl_reg = mmCP_ME1_PIPE2_INT_CNTL;
			break;
		case 3:
			mec_int_cntl_reg = mmCP_ME1_PIPE3_INT_CNTL;
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl &= ~CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK;
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl |= CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK;
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	default:
		break;
	}
}

static int gfx_v8_0_set_priv_reg_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	WREG32_FIELD(CP_INT_CNTL_RING0, PRIV_REG_INT_ENABLE,
		     state == AMDGPU_IRQ_STATE_DISABLE ? 0 : 1);

	return 0;
}

static int gfx_v8_0_set_priv_inst_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	WREG32_FIELD(CP_INT_CNTL_RING0, PRIV_INSTR_INT_ENABLE,
		     state == AMDGPU_IRQ_STATE_DISABLE ? 0 : 1);

	return 0;
}

static int gfx_v8_0_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP:
		gfx_v8_0_set_gfx_eop_interrupt_state(adev, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 3, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 3, state);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v8_0_set_cp_ecc_int_state(struct amdgpu_device *adev,
					 struct amdgpu_irq_src *source,
					 unsigned int type,
					 enum amdgpu_interrupt_state state)
{
	int enable_flag;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		enable_flag = 0;
		break;

	case AMDGPU_IRQ_STATE_ENABLE:
		enable_flag = 1;
		break;

	default:
		return -EINVAL;
	}

	WREG32_FIELD(CP_INT_CNTL, CP_ECC_ERROR_INT_ENABLE, enable_flag);
	WREG32_FIELD(CP_INT_CNTL_RING0, CP_ECC_ERROR_INT_ENABLE, enable_flag);
	WREG32_FIELD(CP_INT_CNTL_RING1, CP_ECC_ERROR_INT_ENABLE, enable_flag);
	WREG32_FIELD(CP_INT_CNTL_RING2, CP_ECC_ERROR_INT_ENABLE, enable_flag);
	WREG32_FIELD(CPC_INT_CNTL, CP_ECC_ERROR_INT_ENABLE, enable_flag);
	WREG32_FIELD(CP_ME1_PIPE0_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME1_PIPE1_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME1_PIPE2_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME1_PIPE3_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME2_PIPE0_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME2_PIPE1_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME2_PIPE2_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);
	WREG32_FIELD(CP_ME2_PIPE3_INT_CNTL, CP_ECC_ERROR_INT_ENABLE,
		     enable_flag);

	return 0;
}

static int gfx_v8_0_set_sq_int_state(struct amdgpu_device *adev,
				     struct amdgpu_irq_src *source,
				     unsigned int type,
				     enum amdgpu_interrupt_state state)
{
	int enable_flag;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		enable_flag = 1;
		break;

	case AMDGPU_IRQ_STATE_ENABLE:
		enable_flag = 0;
		break;

	default:
		return -EINVAL;
	}

	WREG32_FIELD(SQ_INTERRUPT_MSG_CTRL, STALL,
		     enable_flag);

	return 0;
}

static int gfx_v8_0_eop_irq(struct amdgpu_device *adev,
			    struct amdgpu_irq_src *source,
			    struct amdgpu_iv_entry *entry)
{
	int i;
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;

	DRM_DEBUG("IH: CP EOP\n");
	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		amdgpu_fence_process(&adev->gfx.gfx_ring[0]);
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			/* Per-queue interrupt is supported for MEC starting from VI.
			  * The interrupt can only be enabled/disabled per pipe instead of per queue.
			  */
			if ((ring->me == me_id) && (ring->pipe == pipe_id) && (ring->queue == queue_id))
				amdgpu_fence_process(ring);
		}
		break;
	}
	return 0;
}

static void gfx_v8_0_fault(struct amdgpu_device *adev,
			   struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i;

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		drm_sched_fault(&adev->gfx.gfx_ring[0].sched);
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			if (ring->me == me_id && ring->pipe == pipe_id &&
			    ring->queue == queue_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	}
}

static int gfx_v8_0_priv_reg_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	gfx_v8_0_fault(adev, entry);
	return 0;
}

static int gfx_v8_0_priv_inst_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	gfx_v8_0_fault(adev, entry);
	return 0;
}

static int gfx_v8_0_cp_ecc_error_irq(struct amdgpu_device *adev,
				     struct amdgpu_irq_src *source,
				     struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("CP EDC/ECC error detected.");
	return 0;
}

static void gfx_v8_0_parse_sq_irq(struct amdgpu_device *adev, unsigned ih_data,
				  bool from_wq)
{
	u32 enc, se_id, sh_id, cu_id;
	char type[20];
	int sq_edc_source = -1;

	enc = REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_CMN, ENCODING);
	se_id = REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_CMN, SE_ID);

	switch (enc) {
		case 0:
			DRM_INFO("SQ general purpose intr detected:"
					"se_id %d, immed_overflow %d, host_reg_overflow %d,"
					"host_cmd_overflow %d, cmd_timestamp %d,"
					"reg_timestamp %d, thread_trace_buff_full %d,"
					"wlt %d, thread_trace %d.\n",
					se_id,
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, IMMED_OVERFLOW),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, HOST_REG_OVERFLOW),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, HOST_CMD_OVERFLOW),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, CMD_TIMESTAMP),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, REG_TIMESTAMP),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, THREAD_TRACE_BUF_FULL),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, WLT),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_AUTO, THREAD_TRACE)
					);
			break;
		case 1:
		case 2:

			cu_id = REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, CU_ID);
			sh_id = REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, SH_ID);

			/*
			 * This function can be called either directly from ISR
			 * or from BH in which case we can access SQ_EDC_INFO
			 * instance
			 */
			if (from_wq) {
				mutex_lock(&adev->grbm_idx_mutex);
				gfx_v8_0_select_se_sh(adev, se_id, sh_id, cu_id, 0);

				sq_edc_source = REG_GET_FIELD(RREG32(mmSQ_EDC_INFO), SQ_EDC_INFO, SOURCE);

				gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
				mutex_unlock(&adev->grbm_idx_mutex);
			}

			if (enc == 1)
				snprintf(type, sizeof(type), "instruction intr");
			else
				snprintf(type, sizeof(type), "EDC/ECC error");

			DRM_INFO(
				"SQ %s detected: "
					"se_id %d, sh_id %d, cu_id %d, simd_id %d, wave_id %d, vm_id %d "
					"trap %s, sq_ed_info.source %s.\n",
					type, se_id, sh_id, cu_id,
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, SIMD_ID),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, WAVE_ID),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, VM_ID),
					REG_GET_FIELD(ih_data, SQ_INTERRUPT_WORD_WAVE, PRIV) ? "true" : "false",
					(sq_edc_source != -1) ? sq_edc_source_names[sq_edc_source] : "unavailable"
				);
			break;
		default:
			DRM_ERROR("SQ invalid encoding type\n.");
	}
}

static void gfx_v8_0_sq_irq_work_func(struct work_struct *work)
{

	struct amdgpu_device *adev = container_of(work, struct amdgpu_device, gfx.sq_work.work);
	struct sq_work *sq_work = container_of(work, struct sq_work, work);

	gfx_v8_0_parse_sq_irq(adev, sq_work->ih_data, true);
}

static int gfx_v8_0_sq_irq(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *source,
			   struct amdgpu_iv_entry *entry)
{
	unsigned ih_data = entry->src_data[0];

	/*
	 * Try to submit work so SQ_EDC_INFO can be accessed from
	 * BH. If previous work submission hasn't finished yet
	 * just print whatever info is possible directly from the ISR.
	 */
	if (work_pending(&adev->gfx.sq_work.work)) {
		gfx_v8_0_parse_sq_irq(adev, ih_data, false);
	} else {
		adev->gfx.sq_work.ih_data = ih_data;
		schedule_work(&adev->gfx.sq_work.work);
	}

	return 0;
}

static void gfx_v8_0_emit_mem_sync(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	amdgpu_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
			  PACKET3_TC_ACTION_ENA |
			  PACKET3_SH_KCACHE_ACTION_ENA |
			  PACKET3_SH_ICACHE_ACTION_ENA |
			  PACKET3_TC_WB_ACTION_ENA);  /* CP_COHER_CNTL */
	amdgpu_ring_write(ring, 0xffffffff);  /* CP_COHER_SIZE */
	amdgpu_ring_write(ring, 0);  /* CP_COHER_BASE */
	amdgpu_ring_write(ring, 0x0000000A); /* poll interval */
}

static void gfx_v8_0_emit_mem_sync_compute(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_ACQUIRE_MEM, 5));
	amdgpu_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
			  PACKET3_TC_ACTION_ENA |
			  PACKET3_SH_KCACHE_ACTION_ENA |
			  PACKET3_SH_ICACHE_ACTION_ENA |
			  PACKET3_TC_WB_ACTION_ENA);  /* CP_COHER_CNTL */
	amdgpu_ring_write(ring, 0xffffffff);	/* CP_COHER_SIZE */
	amdgpu_ring_write(ring, 0xff);		/* CP_COHER_SIZE_HI */
	amdgpu_ring_write(ring, 0);		/* CP_COHER_BASE */
	amdgpu_ring_write(ring, 0);		/* CP_COHER_BASE_HI */
	amdgpu_ring_write(ring, 0x0000000A);	/* poll interval */
}


/* mmSPI_WCL_PIPE_PERCENT_CS[0-7]_DEFAULT values are same */
#define mmSPI_WCL_PIPE_PERCENT_CS_DEFAULT	0x0000007f
static void gfx_v8_0_emit_wave_limit_cs(struct amdgpu_ring *ring,
					uint32_t pipe, bool enable)
{
	uint32_t val;
	uint32_t wcl_cs_reg;

	val = enable ? 0x1 : mmSPI_WCL_PIPE_PERCENT_CS_DEFAULT;

	switch (pipe) {
	case 0:
		wcl_cs_reg = mmSPI_WCL_PIPE_PERCENT_CS0;
		break;
	case 1:
		wcl_cs_reg = mmSPI_WCL_PIPE_PERCENT_CS1;
		break;
	case 2:
		wcl_cs_reg = mmSPI_WCL_PIPE_PERCENT_CS2;
		break;
	case 3:
		wcl_cs_reg = mmSPI_WCL_PIPE_PERCENT_CS3;
		break;
	default:
		DRM_DEBUG("invalid pipe %d\n", pipe);
		return;
	}

	amdgpu_ring_emit_wreg(ring, wcl_cs_reg, val);

}

#define mmSPI_WCL_PIPE_PERCENT_GFX_DEFAULT	0x07ffffff
static void gfx_v8_0_emit_wave_limit(struct amdgpu_ring *ring, bool enable)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t val;
	int i;

	/* mmSPI_WCL_PIPE_PERCENT_GFX is 7 bit multiplier register to limit
	 * number of gfx waves. Setting 5 bit will make sure gfx only gets
	 * around 25% of gpu resources.
	 */
	val = enable ? 0x1f : mmSPI_WCL_PIPE_PERCENT_GFX_DEFAULT;
	amdgpu_ring_emit_wreg(ring, mmSPI_WCL_PIPE_PERCENT_GFX, val);

	/* Restrict waves for normal/low priority compute queues as well
	 * to get best QoS for high priority compute jobs.
	 *
	 * amdgpu controls only 1st ME(0-3 CS pipes).
	 */
	for (i = 0; i < adev->gfx.mec.num_pipe_per_mec; i++) {
		if (i != ring->pipe)
			gfx_v8_0_emit_wave_limit_cs(ring, i, enable);

	}

}

static int gfx_v8_0_reset_kgq(struct amdgpu_ring *ring, unsigned int vmid)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[0];
	struct amdgpu_ring *kiq_ring = &kiq->ring;
	unsigned long flags;
	u32 tmp;
	int r;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	if (!kiq->pmf || !kiq->pmf->kiq_unmap_queues)
		return -EINVAL;

	spin_lock_irqsave(&kiq->ring_lock, flags);

	if (amdgpu_ring_alloc(kiq_ring, 5)) {
		spin_unlock_irqrestore(&kiq->ring_lock, flags);
		return -ENOMEM;
	}

	tmp = REG_SET_FIELD(0, CP_VMID_RESET, RESET_REQUEST, 1 << vmid);
	gfx_v8_0_ring_emit_wreg(kiq_ring, mmCP_VMID_RESET, tmp);
	amdgpu_ring_commit(kiq_ring);

	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_ring_test_ring(kiq_ring);
	if (r)
		return r;

	if (amdgpu_ring_alloc(ring, 7 + 12 + 5))
		return -ENOMEM;
	gfx_v8_0_ring_emit_fence_gfx(ring, ring->fence_drv.gpu_addr,
				     ring->fence_drv.sync_seq, AMDGPU_FENCE_FLAG_EXEC);
	gfx_v8_0_ring_emit_reg_wait(ring, mmCP_VMID_RESET, 0, 0xffff);
	gfx_v8_0_ring_emit_wreg(ring, mmCP_VMID_RESET, 0);

	return amdgpu_ring_test_ring(ring);
}

static const struct amd_ip_funcs gfx_v8_0_ip_funcs = {
	.name = "gfx_v8_0",
	.early_init = gfx_v8_0_early_init,
	.late_init = gfx_v8_0_late_init,
	.sw_init = gfx_v8_0_sw_init,
	.sw_fini = gfx_v8_0_sw_fini,
	.hw_init = gfx_v8_0_hw_init,
	.hw_fini = gfx_v8_0_hw_fini,
	.suspend = gfx_v8_0_suspend,
	.resume = gfx_v8_0_resume,
	.is_idle = gfx_v8_0_is_idle,
	.wait_for_idle = gfx_v8_0_wait_for_idle,
	.check_soft_reset = gfx_v8_0_check_soft_reset,
	.pre_soft_reset = gfx_v8_0_pre_soft_reset,
	.soft_reset = gfx_v8_0_soft_reset,
	.post_soft_reset = gfx_v8_0_post_soft_reset,
	.set_clockgating_state = gfx_v8_0_set_clockgating_state,
	.set_powergating_state = gfx_v8_0_set_powergating_state,
	.get_clockgating_state = gfx_v8_0_get_clockgating_state,
	.dump_ip_state = NULL,
	.print_ip_state = NULL,
};

static const struct amdgpu_ring_funcs gfx_v8_0_ring_funcs_gfx = {
	.type = AMDGPU_RING_TYPE_GFX,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = false,
	.get_rptr = gfx_v8_0_ring_get_rptr,
	.get_wptr = gfx_v8_0_ring_get_wptr_gfx,
	.set_wptr = gfx_v8_0_ring_set_wptr_gfx,
	.emit_frame_size = /* maximum 215dw if count 16 IBs in */
		5 +  /* COND_EXEC */
		7 +  /* PIPELINE_SYNC */
		VI_FLUSH_GPU_TLB_NUM_WREG * 5 + 9 + /* VM_FLUSH */
		12 +  /* FENCE for VM_FLUSH */
		20 + /* GDS switch */
		4 + /* double SWITCH_BUFFER,
		       the first COND_EXEC jump to the place just
			   prior to this double SWITCH_BUFFER  */
		5 + /* COND_EXEC */
		7 +	 /*	HDP_flush */
		4 +	 /*	VGT_flush */
		14 + /*	CE_META */
		31 + /*	DE_META */
		3 + /* CNTX_CTRL */
		5 + /* HDP_INVL */
		12 + 12 + /* FENCE x2 */
		2 + /* SWITCH_BUFFER */
		5, /* SURFACE_SYNC */
	.emit_ib_size =	4, /* gfx_v8_0_ring_emit_ib_gfx */
	.emit_ib = gfx_v8_0_ring_emit_ib_gfx,
	.emit_fence = gfx_v8_0_ring_emit_fence_gfx,
	.emit_pipeline_sync = gfx_v8_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v8_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v8_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v8_0_ring_emit_hdp_flush,
	.test_ring = gfx_v8_0_ring_test_ring,
	.test_ib = gfx_v8_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_switch_buffer = gfx_v8_ring_emit_sb,
	.emit_cntxcntl = gfx_v8_ring_emit_cntxcntl,
	.init_cond_exec = gfx_v8_0_ring_emit_init_cond_exec,
	.emit_wreg = gfx_v8_0_ring_emit_wreg,
	.soft_recovery = gfx_v8_0_ring_soft_recovery,
	.emit_mem_sync = gfx_v8_0_emit_mem_sync,
	.reset = gfx_v8_0_reset_kgq,
};

static const struct amdgpu_ring_funcs gfx_v8_0_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = false,
	.get_rptr = gfx_v8_0_ring_get_rptr,
	.get_wptr = gfx_v8_0_ring_get_wptr_compute,
	.set_wptr = gfx_v8_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v8_0_ring_emit_gds_switch */
		7 + /* gfx_v8_0_ring_emit_hdp_flush */
		5 + /* hdp_invalidate */
		7 + /* gfx_v8_0_ring_emit_pipeline_sync */
		VI_FLUSH_GPU_TLB_NUM_WREG * 5 + 7 + /* gfx_v8_0_ring_emit_vm_flush */
		7 + 7 + 7 + /* gfx_v8_0_ring_emit_fence_compute x3 for user fence, vm fence */
		7 + /* gfx_v8_0_emit_mem_sync_compute */
		5 + /* gfx_v8_0_emit_wave_limit for updating mmSPI_WCL_PIPE_PERCENT_GFX register */
		15, /* for updating 3 mmSPI_WCL_PIPE_PERCENT_CS registers */
	.emit_ib_size =	7, /* gfx_v8_0_ring_emit_ib_compute */
	.emit_ib = gfx_v8_0_ring_emit_ib_compute,
	.emit_fence = gfx_v8_0_ring_emit_fence_compute,
	.emit_pipeline_sync = gfx_v8_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v8_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v8_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v8_0_ring_emit_hdp_flush,
	.test_ring = gfx_v8_0_ring_test_ring,
	.test_ib = gfx_v8_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_wreg = gfx_v8_0_ring_emit_wreg,
	.soft_recovery = gfx_v8_0_ring_soft_recovery,
	.emit_mem_sync = gfx_v8_0_emit_mem_sync_compute,
	.emit_wave_limit = gfx_v8_0_emit_wave_limit,
};

static const struct amdgpu_ring_funcs gfx_v8_0_ring_funcs_kiq = {
	.type = AMDGPU_RING_TYPE_KIQ,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = false,
	.get_rptr = gfx_v8_0_ring_get_rptr,
	.get_wptr = gfx_v8_0_ring_get_wptr_compute,
	.set_wptr = gfx_v8_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v8_0_ring_emit_gds_switch */
		7 + /* gfx_v8_0_ring_emit_hdp_flush */
		5 + /* hdp_invalidate */
		7 + /* gfx_v8_0_ring_emit_pipeline_sync */
		17 + /* gfx_v8_0_ring_emit_vm_flush */
		7 + 7 + 7, /* gfx_v8_0_ring_emit_fence_kiq x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v8_0_ring_emit_ib_compute */
	.emit_fence = gfx_v8_0_ring_emit_fence_kiq,
	.test_ring = gfx_v8_0_ring_test_ring,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_rreg = gfx_v8_0_ring_emit_rreg,
	.emit_wreg = gfx_v8_0_ring_emit_wreg,
};

static void gfx_v8_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	adev->gfx.kiq[0].ring.funcs = &gfx_v8_0_ring_funcs_kiq;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		adev->gfx.gfx_ring[i].funcs = &gfx_v8_0_ring_funcs_gfx;

	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		adev->gfx.compute_ring[i].funcs = &gfx_v8_0_ring_funcs_compute;
}

static const struct amdgpu_irq_src_funcs gfx_v8_0_eop_irq_funcs = {
	.set = gfx_v8_0_set_eop_interrupt_state,
	.process = gfx_v8_0_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_priv_reg_irq_funcs = {
	.set = gfx_v8_0_set_priv_reg_fault_state,
	.process = gfx_v8_0_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_priv_inst_irq_funcs = {
	.set = gfx_v8_0_set_priv_inst_fault_state,
	.process = gfx_v8_0_priv_inst_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_cp_ecc_error_irq_funcs = {
	.set = gfx_v8_0_set_cp_ecc_int_state,
	.process = gfx_v8_0_cp_ecc_error_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_sq_irq_funcs = {
	.set = gfx_v8_0_set_sq_int_state,
	.process = gfx_v8_0_sq_irq,
};

static void gfx_v8_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v8_0_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v8_0_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v8_0_priv_inst_irq_funcs;

	adev->gfx.cp_ecc_error_irq.num_types = 1;
	adev->gfx.cp_ecc_error_irq.funcs = &gfx_v8_0_cp_ecc_error_irq_funcs;

	adev->gfx.sq_irq.num_types = 1;
	adev->gfx.sq_irq.funcs = &gfx_v8_0_sq_irq_funcs;
}

static void gfx_v8_0_set_rlc_funcs(struct amdgpu_device *adev)
{
	adev->gfx.rlc.funcs = &iceland_rlc_funcs;
}

static void gfx_v8_0_set_gds_init(struct amdgpu_device *adev)
{
	/* init asci gds info */
	adev->gds.gds_size = RREG32(mmGDS_VMID0_SIZE);
	adev->gds.gws_size = 64;
	adev->gds.oa_size = 16;
	adev->gds.gds_compute_max_wave_id = RREG32(mmGDS_COMPUTE_MAX_WAVE_ID);
}

static void gfx_v8_0_set_user_cu_inactive_bitmap(struct amdgpu_device *adev,
						 u32 bitmap)
{
	u32 data;

	if (!bitmap)
		return;

	data = bitmap << GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;
	data &= GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;

	WREG32(mmGC_USER_SHADER_ARRAY_CONFIG, data);
}

static u32 gfx_v8_0_get_cu_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data =  RREG32(mmCC_GC_SHADER_ARRAY_CONFIG) |
		RREG32(mmGC_USER_SHADER_ARRAY_CONFIG);

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_cu_per_sh);

	return ~REG_GET_FIELD(data, CC_GC_SHADER_ARRAY_CONFIG, INACTIVE_CUS) & mask;
}

static void gfx_v8_0_get_cu_info(struct amdgpu_device *adev)
{
	int i, j, k, counter, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0;
	struct amdgpu_cu_info *cu_info = &adev->gfx.cu_info;
	unsigned disable_masks[4 * 2];
	u32 ao_cu_num;

	memset(cu_info, 0, sizeof(*cu_info));

	if (adev->flags & AMD_IS_APU)
		ao_cu_num = 2;
	else
		ao_cu_num = adev->gfx.config.max_cu_per_sh;

	amdgpu_gfx_parse_disable_cu(disable_masks, 4, 2);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			ao_bitmap = 0;
			counter = 0;
			gfx_v8_0_select_se_sh(adev, i, j, 0xffffffff, 0);
			if (i < 4 && j < 2)
				gfx_v8_0_set_user_cu_inactive_bitmap(
					adev, disable_masks[i * 2 + j]);
			bitmap = gfx_v8_0_get_cu_active_bitmap(adev);
			cu_info->bitmap[0][i][j] = bitmap;

			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k ++) {
				if (bitmap & mask) {
					if (counter < ao_cu_num)
						ao_bitmap |= mask;
					counter ++;
				}
				mask <<= 1;
			}
			active_cu_number += counter;
			if (i < 2 && j < 2)
				ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
			cu_info->ao_cu_bitmap[i][j] = ao_bitmap;
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
	cu_info->simd_per_cu = NUM_SIMD_PER_CU;
	cu_info->max_waves_per_simd = 10;
	cu_info->max_scratch_slots_per_cu = 32;
	cu_info->wave_front_size = 64;
	cu_info->lds_size = 64;
}

const struct amdgpu_ip_block_version gfx_v8_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 8,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v8_0_ip_funcs,
};

const struct amdgpu_ip_block_version gfx_v8_1_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 8,
	.minor = 1,
	.rev = 0,
	.funcs = &gfx_v8_0_ip_funcs,
};

static void gfx_v8_0_ring_emit_ce_meta(struct amdgpu_ring *ring)
{
	uint64_t ce_payload_addr;
	int cnt_ce;
	union {
		struct vi_ce_ib_state regular;
		struct vi_ce_ib_state_chained_ib chained;
	} ce_payload = {};

	if (ring->adev->virt.chained_ib_support) {
		ce_payload_addr = amdgpu_csa_vaddr(ring->adev) +
			offsetof(struct vi_gfx_meta_data_chained_ib, ce_payload);
		cnt_ce = (sizeof(ce_payload.chained) >> 2) + 4 - 2;
	} else {
		ce_payload_addr = amdgpu_csa_vaddr(ring->adev) +
			offsetof(struct vi_gfx_meta_data, ce_payload);
		cnt_ce = (sizeof(ce_payload.regular) >> 2) + 4 - 2;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt_ce));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(2) |
				WRITE_DATA_DST_SEL(8) |
				WR_CONFIRM) |
				WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(ce_payload_addr));
	amdgpu_ring_write(ring, upper_32_bits(ce_payload_addr));
	amdgpu_ring_write_multiple(ring, (void *)&ce_payload, cnt_ce - 2);
}

static void gfx_v8_0_ring_emit_de_meta(struct amdgpu_ring *ring)
{
	uint64_t de_payload_addr, gds_addr, csa_addr;
	int cnt_de;
	union {
		struct vi_de_ib_state regular;
		struct vi_de_ib_state_chained_ib chained;
	} de_payload = {};

	csa_addr = amdgpu_csa_vaddr(ring->adev);
	gds_addr = csa_addr + 4096;
	if (ring->adev->virt.chained_ib_support) {
		de_payload.chained.gds_backup_addrlo = lower_32_bits(gds_addr);
		de_payload.chained.gds_backup_addrhi = upper_32_bits(gds_addr);
		de_payload_addr = csa_addr + offsetof(struct vi_gfx_meta_data_chained_ib, de_payload);
		cnt_de = (sizeof(de_payload.chained) >> 2) + 4 - 2;
	} else {
		de_payload.regular.gds_backup_addrlo = lower_32_bits(gds_addr);
		de_payload.regular.gds_backup_addrhi = upper_32_bits(gds_addr);
		de_payload_addr = csa_addr + offsetof(struct vi_gfx_meta_data, de_payload);
		cnt_de = (sizeof(de_payload.regular) >> 2) + 4 - 2;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt_de));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				WRITE_DATA_DST_SEL(8) |
				WR_CONFIRM) |
				WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(de_payload_addr));
	amdgpu_ring_write(ring, upper_32_bits(de_payload_addr));
	amdgpu_ring_write_multiple(ring, (void *)&de_payload, cnt_de - 2);
}
