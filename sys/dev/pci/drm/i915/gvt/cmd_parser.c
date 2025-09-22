/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Ke Yu
 *    Kevin Tian <kevin.tian@intel.com>
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Yulei Zhang <yulei.zhang@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include <linux/slab.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_lrc.h"
#include "gt/intel_ring.h"
#include "gt/intel_gt_requests.h"
#include "gt/shmem_utils.h"
#include "gvt.h"
#include "i915_pvinfo.h"
#include "trace.h"

#include "display/i9xx_plane_regs.h"
#include "display/intel_display.h"
#include "display/intel_sprite_regs.h"
#include "gem/i915_gem_context.h"
#include "gem/i915_gem_pm.h"
#include "gt/intel_context.h"

#define INVALID_OP    (~0U)

#define OP_LEN_MI           9
#define OP_LEN_2D           10
#define OP_LEN_3D_MEDIA     16
#define OP_LEN_MFX_VC       16
#define OP_LEN_VEBOX	    16

#define CMD_TYPE(cmd)	(((cmd) >> 29) & 7)

struct sub_op_bits {
	int hi;
	int low;
};
struct decode_info {
	const char *name;
	int op_len;
	int nr_sub_op;
	const struct sub_op_bits *sub_op;
};

#define   MAX_CMD_BUDGET			0x7fffffff
#define   MI_WAIT_FOR_PLANE_C_FLIP_PENDING      (1<<15)
#define   MI_WAIT_FOR_PLANE_B_FLIP_PENDING      (1<<9)
#define   MI_WAIT_FOR_PLANE_A_FLIP_PENDING      (1<<1)

#define   MI_WAIT_FOR_SPRITE_C_FLIP_PENDING      (1<<20)
#define   MI_WAIT_FOR_SPRITE_B_FLIP_PENDING      (1<<10)
#define   MI_WAIT_FOR_SPRITE_A_FLIP_PENDING      (1<<2)

/* Render Command Map */

/* MI_* command Opcode (28:23) */
#define OP_MI_NOOP                          0x0
#define OP_MI_SET_PREDICATE                 0x1  /* HSW+ */
#define OP_MI_USER_INTERRUPT                0x2
#define OP_MI_WAIT_FOR_EVENT                0x3
#define OP_MI_FLUSH                         0x4
#define OP_MI_ARB_CHECK                     0x5
#define OP_MI_RS_CONTROL                    0x6  /* HSW+ */
#define OP_MI_REPORT_HEAD                   0x7
#define OP_MI_ARB_ON_OFF                    0x8
#define OP_MI_URB_ATOMIC_ALLOC              0x9  /* HSW+ */
#define OP_MI_BATCH_BUFFER_END              0xA
#define OP_MI_SUSPEND_FLUSH                 0xB
#define OP_MI_PREDICATE                     0xC  /* IVB+ */
#define OP_MI_TOPOLOGY_FILTER               0xD  /* IVB+ */
#define OP_MI_SET_APPID                     0xE  /* IVB+ */
#define OP_MI_RS_CONTEXT                    0xF  /* HSW+ */
#define OP_MI_LOAD_SCAN_LINES_INCL          0x12 /* HSW+ */
#define OP_MI_DISPLAY_FLIP                  0x14
#define OP_MI_SEMAPHORE_MBOX                0x16
#define OP_MI_SET_CONTEXT                   0x18
#define OP_MI_MATH                          0x1A
#define OP_MI_URB_CLEAR                     0x19
#define OP_MI_SEMAPHORE_SIGNAL		    0x1B  /* BDW+ */
#define OP_MI_SEMAPHORE_WAIT		    0x1C  /* BDW+ */

#define OP_MI_STORE_DATA_IMM                0x20
#define OP_MI_STORE_DATA_INDEX              0x21
#define OP_MI_LOAD_REGISTER_IMM             0x22
#define OP_MI_UPDATE_GTT                    0x23
#define OP_MI_STORE_REGISTER_MEM            0x24
#define OP_MI_FLUSH_DW                      0x26
#define OP_MI_CLFLUSH                       0x27
#define OP_MI_REPORT_PERF_COUNT             0x28
#define OP_MI_LOAD_REGISTER_MEM             0x29  /* HSW+ */
#define OP_MI_LOAD_REGISTER_REG             0x2A  /* HSW+ */
#define OP_MI_RS_STORE_DATA_IMM             0x2B  /* HSW+ */
#define OP_MI_LOAD_URB_MEM                  0x2C  /* HSW+ */
#define OP_MI_STORE_URM_MEM                 0x2D  /* HSW+ */
#define OP_MI_2E			    0x2E  /* BDW+ */
#define OP_MI_2F			    0x2F  /* BDW+ */
#define OP_MI_BATCH_BUFFER_START            0x31

/* Bit definition for dword 0 */
#define _CMDBIT_BB_START_IN_PPGTT	(1UL << 8)

#define OP_MI_CONDITIONAL_BATCH_BUFFER_END  0x36

#define BATCH_BUFFER_ADDR_MASK ((1UL << 32) - (1U << 2))
#define BATCH_BUFFER_ADDR_HIGH_MASK ((1UL << 16) - (1U))
#define BATCH_BUFFER_ADR_SPACE_BIT(x)	(((x) >> 8) & 1U)
#define BATCH_BUFFER_2ND_LEVEL_BIT(x)   ((x) >> 22 & 1U)

/* 2D command: Opcode (28:22) */
#define OP_2D(x)    ((2<<7) | x)

#define OP_XY_SETUP_BLT                             OP_2D(0x1)
#define OP_XY_SETUP_CLIP_BLT                        OP_2D(0x3)
#define OP_XY_SETUP_MONO_PATTERN_SL_BLT             OP_2D(0x11)
#define OP_XY_PIXEL_BLT                             OP_2D(0x24)
#define OP_XY_SCANLINES_BLT                         OP_2D(0x25)
#define OP_XY_TEXT_BLT                              OP_2D(0x26)
#define OP_XY_TEXT_IMMEDIATE_BLT                    OP_2D(0x31)
#define OP_XY_COLOR_BLT                             OP_2D(0x50)
#define OP_XY_PAT_BLT                               OP_2D(0x51)
#define OP_XY_MONO_PAT_BLT                          OP_2D(0x52)
#define OP_XY_SRC_COPY_BLT                          OP_2D(0x53)
#define OP_XY_MONO_SRC_COPY_BLT                     OP_2D(0x54)
#define OP_XY_FULL_BLT                              OP_2D(0x55)
#define OP_XY_FULL_MONO_SRC_BLT                     OP_2D(0x56)
#define OP_XY_FULL_MONO_PATTERN_BLT                 OP_2D(0x57)
#define OP_XY_FULL_MONO_PATTERN_MONO_SRC_BLT        OP_2D(0x58)
#define OP_XY_MONO_PAT_FIXED_BLT                    OP_2D(0x59)
#define OP_XY_MONO_SRC_COPY_IMMEDIATE_BLT           OP_2D(0x71)
#define OP_XY_PAT_BLT_IMMEDIATE                     OP_2D(0x72)
#define OP_XY_SRC_COPY_CHROMA_BLT                   OP_2D(0x73)
#define OP_XY_FULL_IMMEDIATE_PATTERN_BLT            OP_2D(0x74)
#define OP_XY_FULL_MONO_SRC_IMMEDIATE_PATTERN_BLT   OP_2D(0x75)
#define OP_XY_PAT_CHROMA_BLT                        OP_2D(0x76)
#define OP_XY_PAT_CHROMA_BLT_IMMEDIATE              OP_2D(0x77)

/* 3D/Media Command: Pipeline Type(28:27) Opcode(26:24) Sub Opcode(23:16) */
#define OP_3D_MEDIA(sub_type, opcode, sub_opcode) \
	((3 << 13) | ((sub_type) << 11) | ((opcode) << 8) | (sub_opcode))

#define OP_STATE_PREFETCH                       OP_3D_MEDIA(0x0, 0x0, 0x03)

#define OP_STATE_BASE_ADDRESS                   OP_3D_MEDIA(0x0, 0x1, 0x01)
#define OP_STATE_SIP                            OP_3D_MEDIA(0x0, 0x1, 0x02)
#define OP_3D_MEDIA_0_1_4			OP_3D_MEDIA(0x0, 0x1, 0x04)
#define OP_SWTESS_BASE_ADDRESS			OP_3D_MEDIA(0x0, 0x1, 0x03)

#define OP_3DSTATE_VF_STATISTICS_GM45           OP_3D_MEDIA(0x1, 0x0, 0x0B)

#define OP_PIPELINE_SELECT                      OP_3D_MEDIA(0x1, 0x1, 0x04)

#define OP_MEDIA_VFE_STATE                      OP_3D_MEDIA(0x2, 0x0, 0x0)
#define OP_MEDIA_CURBE_LOAD                     OP_3D_MEDIA(0x2, 0x0, 0x1)
#define OP_MEDIA_INTERFACE_DESCRIPTOR_LOAD      OP_3D_MEDIA(0x2, 0x0, 0x2)
#define OP_MEDIA_GATEWAY_STATE                  OP_3D_MEDIA(0x2, 0x0, 0x3)
#define OP_MEDIA_STATE_FLUSH                    OP_3D_MEDIA(0x2, 0x0, 0x4)
#define OP_MEDIA_POOL_STATE                     OP_3D_MEDIA(0x2, 0x0, 0x5)

#define OP_MEDIA_OBJECT                         OP_3D_MEDIA(0x2, 0x1, 0x0)
#define OP_MEDIA_OBJECT_PRT                     OP_3D_MEDIA(0x2, 0x1, 0x2)
#define OP_MEDIA_OBJECT_WALKER                  OP_3D_MEDIA(0x2, 0x1, 0x3)
#define OP_GPGPU_WALKER                         OP_3D_MEDIA(0x2, 0x1, 0x5)

#define OP_3DSTATE_CLEAR_PARAMS                 OP_3D_MEDIA(0x3, 0x0, 0x04) /* IVB+ */
#define OP_3DSTATE_DEPTH_BUFFER                 OP_3D_MEDIA(0x3, 0x0, 0x05) /* IVB+ */
#define OP_3DSTATE_STENCIL_BUFFER               OP_3D_MEDIA(0x3, 0x0, 0x06) /* IVB+ */
#define OP_3DSTATE_HIER_DEPTH_BUFFER            OP_3D_MEDIA(0x3, 0x0, 0x07) /* IVB+ */
#define OP_3DSTATE_VERTEX_BUFFERS               OP_3D_MEDIA(0x3, 0x0, 0x08)
#define OP_3DSTATE_VERTEX_ELEMENTS              OP_3D_MEDIA(0x3, 0x0, 0x09)
#define OP_3DSTATE_INDEX_BUFFER                 OP_3D_MEDIA(0x3, 0x0, 0x0A)
#define OP_3DSTATE_VF_STATISTICS                OP_3D_MEDIA(0x3, 0x0, 0x0B)
#define OP_3DSTATE_VF                           OP_3D_MEDIA(0x3, 0x0, 0x0C)  /* HSW+ */
#define OP_3DSTATE_CC_STATE_POINTERS            OP_3D_MEDIA(0x3, 0x0, 0x0E)
#define OP_3DSTATE_SCISSOR_STATE_POINTERS       OP_3D_MEDIA(0x3, 0x0, 0x0F)
#define OP_3DSTATE_VS                           OP_3D_MEDIA(0x3, 0x0, 0x10)
#define OP_3DSTATE_GS                           OP_3D_MEDIA(0x3, 0x0, 0x11)
#define OP_3DSTATE_CLIP                         OP_3D_MEDIA(0x3, 0x0, 0x12)
#define OP_3DSTATE_SF                           OP_3D_MEDIA(0x3, 0x0, 0x13)
#define OP_3DSTATE_WM                           OP_3D_MEDIA(0x3, 0x0, 0x14)
#define OP_3DSTATE_CONSTANT_VS                  OP_3D_MEDIA(0x3, 0x0, 0x15)
#define OP_3DSTATE_CONSTANT_GS                  OP_3D_MEDIA(0x3, 0x0, 0x16)
#define OP_3DSTATE_CONSTANT_PS                  OP_3D_MEDIA(0x3, 0x0, 0x17)
#define OP_3DSTATE_SAMPLE_MASK                  OP_3D_MEDIA(0x3, 0x0, 0x18)
#define OP_3DSTATE_CONSTANT_HS                  OP_3D_MEDIA(0x3, 0x0, 0x19) /* IVB+ */
#define OP_3DSTATE_CONSTANT_DS                  OP_3D_MEDIA(0x3, 0x0, 0x1A) /* IVB+ */
#define OP_3DSTATE_HS                           OP_3D_MEDIA(0x3, 0x0, 0x1B) /* IVB+ */
#define OP_3DSTATE_TE                           OP_3D_MEDIA(0x3, 0x0, 0x1C) /* IVB+ */
#define OP_3DSTATE_DS                           OP_3D_MEDIA(0x3, 0x0, 0x1D) /* IVB+ */
#define OP_3DSTATE_STREAMOUT                    OP_3D_MEDIA(0x3, 0x0, 0x1E) /* IVB+ */
#define OP_3DSTATE_SBE                          OP_3D_MEDIA(0x3, 0x0, 0x1F) /* IVB+ */
#define OP_3DSTATE_PS                           OP_3D_MEDIA(0x3, 0x0, 0x20) /* IVB+ */
#define OP_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP OP_3D_MEDIA(0x3, 0x0, 0x21) /* IVB+ */
#define OP_3DSTATE_VIEWPORT_STATE_POINTERS_CC   OP_3D_MEDIA(0x3, 0x0, 0x23) /* IVB+ */
#define OP_3DSTATE_BLEND_STATE_POINTERS         OP_3D_MEDIA(0x3, 0x0, 0x24) /* IVB+ */
#define OP_3DSTATE_DEPTH_STENCIL_STATE_POINTERS OP_3D_MEDIA(0x3, 0x0, 0x25) /* IVB+ */
#define OP_3DSTATE_BINDING_TABLE_POINTERS_VS    OP_3D_MEDIA(0x3, 0x0, 0x26) /* IVB+ */
#define OP_3DSTATE_BINDING_TABLE_POINTERS_HS    OP_3D_MEDIA(0x3, 0x0, 0x27) /* IVB+ */
#define OP_3DSTATE_BINDING_TABLE_POINTERS_DS    OP_3D_MEDIA(0x3, 0x0, 0x28) /* IVB+ */
#define OP_3DSTATE_BINDING_TABLE_POINTERS_GS    OP_3D_MEDIA(0x3, 0x0, 0x29) /* IVB+ */
#define OP_3DSTATE_BINDING_TABLE_POINTERS_PS    OP_3D_MEDIA(0x3, 0x0, 0x2A) /* IVB+ */
#define OP_3DSTATE_SAMPLER_STATE_POINTERS_VS    OP_3D_MEDIA(0x3, 0x0, 0x2B) /* IVB+ */
#define OP_3DSTATE_SAMPLER_STATE_POINTERS_HS    OP_3D_MEDIA(0x3, 0x0, 0x2C) /* IVB+ */
#define OP_3DSTATE_SAMPLER_STATE_POINTERS_DS    OP_3D_MEDIA(0x3, 0x0, 0x2D) /* IVB+ */
#define OP_3DSTATE_SAMPLER_STATE_POINTERS_GS    OP_3D_MEDIA(0x3, 0x0, 0x2E) /* IVB+ */
#define OP_3DSTATE_SAMPLER_STATE_POINTERS_PS    OP_3D_MEDIA(0x3, 0x0, 0x2F) /* IVB+ */
#define OP_3DSTATE_URB_VS                       OP_3D_MEDIA(0x3, 0x0, 0x30) /* IVB+ */
#define OP_3DSTATE_URB_HS                       OP_3D_MEDIA(0x3, 0x0, 0x31) /* IVB+ */
#define OP_3DSTATE_URB_DS                       OP_3D_MEDIA(0x3, 0x0, 0x32) /* IVB+ */
#define OP_3DSTATE_URB_GS                       OP_3D_MEDIA(0x3, 0x0, 0x33) /* IVB+ */
#define OP_3DSTATE_GATHER_CONSTANT_VS           OP_3D_MEDIA(0x3, 0x0, 0x34) /* HSW+ */
#define OP_3DSTATE_GATHER_CONSTANT_GS           OP_3D_MEDIA(0x3, 0x0, 0x35) /* HSW+ */
#define OP_3DSTATE_GATHER_CONSTANT_HS           OP_3D_MEDIA(0x3, 0x0, 0x36) /* HSW+ */
#define OP_3DSTATE_GATHER_CONSTANT_DS           OP_3D_MEDIA(0x3, 0x0, 0x37) /* HSW+ */
#define OP_3DSTATE_GATHER_CONSTANT_PS           OP_3D_MEDIA(0x3, 0x0, 0x38) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTF_VS             OP_3D_MEDIA(0x3, 0x0, 0x39) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTF_PS             OP_3D_MEDIA(0x3, 0x0, 0x3A) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTI_VS             OP_3D_MEDIA(0x3, 0x0, 0x3B) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTI_PS             OP_3D_MEDIA(0x3, 0x0, 0x3C) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTB_VS             OP_3D_MEDIA(0x3, 0x0, 0x3D) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANTB_PS             OP_3D_MEDIA(0x3, 0x0, 0x3E) /* HSW+ */
#define OP_3DSTATE_DX9_LOCAL_VALID_VS           OP_3D_MEDIA(0x3, 0x0, 0x3F) /* HSW+ */
#define OP_3DSTATE_DX9_LOCAL_VALID_PS           OP_3D_MEDIA(0x3, 0x0, 0x40) /* HSW+ */
#define OP_3DSTATE_DX9_GENERATE_ACTIVE_VS       OP_3D_MEDIA(0x3, 0x0, 0x41) /* HSW+ */
#define OP_3DSTATE_DX9_GENERATE_ACTIVE_PS       OP_3D_MEDIA(0x3, 0x0, 0x42) /* HSW+ */
#define OP_3DSTATE_BINDING_TABLE_EDIT_VS        OP_3D_MEDIA(0x3, 0x0, 0x43) /* HSW+ */
#define OP_3DSTATE_BINDING_TABLE_EDIT_GS        OP_3D_MEDIA(0x3, 0x0, 0x44) /* HSW+ */
#define OP_3DSTATE_BINDING_TABLE_EDIT_HS        OP_3D_MEDIA(0x3, 0x0, 0x45) /* HSW+ */
#define OP_3DSTATE_BINDING_TABLE_EDIT_DS        OP_3D_MEDIA(0x3, 0x0, 0x46) /* HSW+ */
#define OP_3DSTATE_BINDING_TABLE_EDIT_PS        OP_3D_MEDIA(0x3, 0x0, 0x47) /* HSW+ */

#define OP_3DSTATE_VF_INSTANCING 		OP_3D_MEDIA(0x3, 0x0, 0x49) /* BDW+ */
#define OP_3DSTATE_VF_SGVS  			OP_3D_MEDIA(0x3, 0x0, 0x4A) /* BDW+ */
#define OP_3DSTATE_VF_TOPOLOGY   		OP_3D_MEDIA(0x3, 0x0, 0x4B) /* BDW+ */
#define OP_3DSTATE_WM_CHROMAKEY   		OP_3D_MEDIA(0x3, 0x0, 0x4C) /* BDW+ */
#define OP_3DSTATE_PS_BLEND   			OP_3D_MEDIA(0x3, 0x0, 0x4D) /* BDW+ */
#define OP_3DSTATE_WM_DEPTH_STENCIL   		OP_3D_MEDIA(0x3, 0x0, 0x4E) /* BDW+ */
#define OP_3DSTATE_PS_EXTRA   			OP_3D_MEDIA(0x3, 0x0, 0x4F) /* BDW+ */
#define OP_3DSTATE_RASTER   			OP_3D_MEDIA(0x3, 0x0, 0x50) /* BDW+ */
#define OP_3DSTATE_SBE_SWIZ   			OP_3D_MEDIA(0x3, 0x0, 0x51) /* BDW+ */
#define OP_3DSTATE_WM_HZ_OP   			OP_3D_MEDIA(0x3, 0x0, 0x52) /* BDW+ */
#define OP_3DSTATE_COMPONENT_PACKING		OP_3D_MEDIA(0x3, 0x0, 0x55) /* SKL+ */

#define OP_3DSTATE_DRAWING_RECTANGLE            OP_3D_MEDIA(0x3, 0x1, 0x00)
#define OP_3DSTATE_SAMPLER_PALETTE_LOAD0        OP_3D_MEDIA(0x3, 0x1, 0x02)
#define OP_3DSTATE_CHROMA_KEY                   OP_3D_MEDIA(0x3, 0x1, 0x04)
#define OP_SNB_3DSTATE_DEPTH_BUFFER             OP_3D_MEDIA(0x3, 0x1, 0x05)
#define OP_3DSTATE_POLY_STIPPLE_OFFSET          OP_3D_MEDIA(0x3, 0x1, 0x06)
#define OP_3DSTATE_POLY_STIPPLE_PATTERN         OP_3D_MEDIA(0x3, 0x1, 0x07)
#define OP_3DSTATE_LINE_STIPPLE                 OP_3D_MEDIA(0x3, 0x1, 0x08)
#define OP_3DSTATE_AA_LINE_PARAMS               OP_3D_MEDIA(0x3, 0x1, 0x0A)
#define OP_3DSTATE_GS_SVB_INDEX                 OP_3D_MEDIA(0x3, 0x1, 0x0B)
#define OP_3DSTATE_SAMPLER_PALETTE_LOAD1        OP_3D_MEDIA(0x3, 0x1, 0x0C)
#define OP_3DSTATE_MULTISAMPLE_BDW		OP_3D_MEDIA(0x3, 0x0, 0x0D)
#define OP_SNB_3DSTATE_STENCIL_BUFFER           OP_3D_MEDIA(0x3, 0x1, 0x0E)
#define OP_SNB_3DSTATE_HIER_DEPTH_BUFFER        OP_3D_MEDIA(0x3, 0x1, 0x0F)
#define OP_SNB_3DSTATE_CLEAR_PARAMS             OP_3D_MEDIA(0x3, 0x1, 0x10)
#define OP_3DSTATE_MONOFILTER_SIZE              OP_3D_MEDIA(0x3, 0x1, 0x11)
#define OP_3DSTATE_PUSH_CONSTANT_ALLOC_VS       OP_3D_MEDIA(0x3, 0x1, 0x12) /* IVB+ */
#define OP_3DSTATE_PUSH_CONSTANT_ALLOC_HS       OP_3D_MEDIA(0x3, 0x1, 0x13) /* IVB+ */
#define OP_3DSTATE_PUSH_CONSTANT_ALLOC_DS       OP_3D_MEDIA(0x3, 0x1, 0x14) /* IVB+ */
#define OP_3DSTATE_PUSH_CONSTANT_ALLOC_GS       OP_3D_MEDIA(0x3, 0x1, 0x15) /* IVB+ */
#define OP_3DSTATE_PUSH_CONSTANT_ALLOC_PS       OP_3D_MEDIA(0x3, 0x1, 0x16) /* IVB+ */
#define OP_3DSTATE_SO_DECL_LIST                 OP_3D_MEDIA(0x3, 0x1, 0x17)
#define OP_3DSTATE_SO_BUFFER                    OP_3D_MEDIA(0x3, 0x1, 0x18)
#define OP_3DSTATE_BINDING_TABLE_POOL_ALLOC     OP_3D_MEDIA(0x3, 0x1, 0x19) /* HSW+ */
#define OP_3DSTATE_GATHER_POOL_ALLOC            OP_3D_MEDIA(0x3, 0x1, 0x1A) /* HSW+ */
#define OP_3DSTATE_DX9_CONSTANT_BUFFER_POOL_ALLOC OP_3D_MEDIA(0x3, 0x1, 0x1B) /* HSW+ */
#define OP_3DSTATE_SAMPLE_PATTERN               OP_3D_MEDIA(0x3, 0x1, 0x1C)
#define OP_PIPE_CONTROL                         OP_3D_MEDIA(0x3, 0x2, 0x00)
#define OP_3DPRIMITIVE                          OP_3D_MEDIA(0x3, 0x3, 0x00)

/* VCCP Command Parser */

/*
 * Below MFX and VBE cmd definition is from vaapi intel driver project (BSD License)
 * git://anongit.freedesktop.org/vaapi/intel-driver
 * src/i965_defines.h
 *
 */

#define OP_MFX(pipeline, op, sub_opa, sub_opb)     \
	(3 << 13 | \
	 (pipeline) << 11 | \
	 (op) << 8 | \
	 (sub_opa) << 5 | \
	 (sub_opb))

#define OP_MFX_PIPE_MODE_SELECT                    OP_MFX(2, 0, 0, 0)  /* ALL */
#define OP_MFX_SURFACE_STATE                       OP_MFX(2, 0, 0, 1)  /* ALL */
#define OP_MFX_PIPE_BUF_ADDR_STATE                 OP_MFX(2, 0, 0, 2)  /* ALL */
#define OP_MFX_IND_OBJ_BASE_ADDR_STATE             OP_MFX(2, 0, 0, 3)  /* ALL */
#define OP_MFX_BSP_BUF_BASE_ADDR_STATE             OP_MFX(2, 0, 0, 4)  /* ALL */
#define OP_2_0_0_5                                 OP_MFX(2, 0, 0, 5)  /* ALL */
#define OP_MFX_STATE_POINTER                       OP_MFX(2, 0, 0, 6)  /* ALL */
#define OP_MFX_QM_STATE                            OP_MFX(2, 0, 0, 7)  /* IVB+ */
#define OP_MFX_FQM_STATE                           OP_MFX(2, 0, 0, 8)  /* IVB+ */
#define OP_MFX_PAK_INSERT_OBJECT                   OP_MFX(2, 0, 2, 8)  /* IVB+ */
#define OP_MFX_STITCH_OBJECT                       OP_MFX(2, 0, 2, 0xA)  /* IVB+ */

#define OP_MFD_IT_OBJECT                           OP_MFX(2, 0, 1, 9) /* ALL */

#define OP_MFX_WAIT                                OP_MFX(1, 0, 0, 0) /* IVB+ */
#define OP_MFX_AVC_IMG_STATE                       OP_MFX(2, 1, 0, 0) /* ALL */
#define OP_MFX_AVC_QM_STATE                        OP_MFX(2, 1, 0, 1) /* ALL */
#define OP_MFX_AVC_DIRECTMODE_STATE                OP_MFX(2, 1, 0, 2) /* ALL */
#define OP_MFX_AVC_SLICE_STATE                     OP_MFX(2, 1, 0, 3) /* ALL */
#define OP_MFX_AVC_REF_IDX_STATE                   OP_MFX(2, 1, 0, 4) /* ALL */
#define OP_MFX_AVC_WEIGHTOFFSET_STATE              OP_MFX(2, 1, 0, 5) /* ALL */
#define OP_MFD_AVC_PICID_STATE                     OP_MFX(2, 1, 1, 5) /* HSW+ */
#define OP_MFD_AVC_DPB_STATE			   OP_MFX(2, 1, 1, 6) /* IVB+ */
#define OP_MFD_AVC_SLICEADDR                       OP_MFX(2, 1, 1, 7) /* IVB+ */
#define OP_MFD_AVC_BSD_OBJECT                      OP_MFX(2, 1, 1, 8) /* ALL */
#define OP_MFC_AVC_PAK_OBJECT                      OP_MFX(2, 1, 2, 9) /* ALL */

#define OP_MFX_VC1_PRED_PIPE_STATE                 OP_MFX(2, 2, 0, 1) /* ALL */
#define OP_MFX_VC1_DIRECTMODE_STATE                OP_MFX(2, 2, 0, 2) /* ALL */
#define OP_MFD_VC1_SHORT_PIC_STATE                 OP_MFX(2, 2, 1, 0) /* IVB+ */
#define OP_MFD_VC1_LONG_PIC_STATE                  OP_MFX(2, 2, 1, 1) /* IVB+ */
#define OP_MFD_VC1_BSD_OBJECT                      OP_MFX(2, 2, 1, 8) /* ALL */

#define OP_MFX_MPEG2_PIC_STATE                     OP_MFX(2, 3, 0, 0) /* ALL */
#define OP_MFX_MPEG2_QM_STATE                      OP_MFX(2, 3, 0, 1) /* ALL */
#define OP_MFD_MPEG2_BSD_OBJECT                    OP_MFX(2, 3, 1, 8) /* ALL */
#define OP_MFC_MPEG2_SLICEGROUP_STATE              OP_MFX(2, 3, 2, 3) /* ALL */
#define OP_MFC_MPEG2_PAK_OBJECT                    OP_MFX(2, 3, 2, 9) /* ALL */

#define OP_MFX_2_6_0_0                             OP_MFX(2, 6, 0, 0) /* IVB+ */
#define OP_MFX_2_6_0_8                             OP_MFX(2, 6, 0, 8) /* IVB+ */
#define OP_MFX_2_6_0_9                             OP_MFX(2, 6, 0, 9) /* IVB+ */

#define OP_MFX_JPEG_PIC_STATE                      OP_MFX(2, 7, 0, 0)
#define OP_MFX_JPEG_HUFF_TABLE_STATE               OP_MFX(2, 7, 0, 2)
#define OP_MFD_JPEG_BSD_OBJECT                     OP_MFX(2, 7, 1, 8)

#define OP_VEB(pipeline, op, sub_opa, sub_opb) \
	(3 << 13 | \
	 (pipeline) << 11 | \
	 (op) << 8 | \
	 (sub_opa) << 5 | \
	 (sub_opb))

#define OP_VEB_SURFACE_STATE                       OP_VEB(2, 4, 0, 0)
#define OP_VEB_STATE                               OP_VEB(2, 4, 0, 2)
#define OP_VEB_DNDI_IECP_STATE                     OP_VEB(2, 4, 0, 3)

struct parser_exec_state;

typedef int (*parser_cmd_handler)(struct parser_exec_state *s);

#define GVT_CMD_HASH_BITS   7

/* which DWords need address fix */
#define ADDR_FIX_1(x1)			(1 << (x1))
#define ADDR_FIX_2(x1, x2)		(ADDR_FIX_1(x1) | ADDR_FIX_1(x2))
#define ADDR_FIX_3(x1, x2, x3)		(ADDR_FIX_1(x1) | ADDR_FIX_2(x2, x3))
#define ADDR_FIX_4(x1, x2, x3, x4)	(ADDR_FIX_1(x1) | ADDR_FIX_3(x2, x3, x4))
#define ADDR_FIX_5(x1, x2, x3, x4, x5)  (ADDR_FIX_1(x1) | ADDR_FIX_4(x2, x3, x4, x5))

#define DWORD_FIELD(dword, end, start) \
	FIELD_GET(GENMASK(end, start), cmd_val(s, dword))

#define OP_LENGTH_BIAS 2
#define CMD_LEN(value)  (value + OP_LENGTH_BIAS)

static int gvt_check_valid_cmd_length(int len, int valid_len)
{
	if (valid_len != len) {
		gvt_err("len is not valid:  len=%u  valid_len=%u\n",
			len, valid_len);
		return -EFAULT;
	}
	return 0;
}

struct cmd_info {
	const char *name;
	u32 opcode;

#define F_LEN_MASK	3U
#define F_LEN_CONST  1U
#define F_LEN_VAR    0U
/* value is const although LEN maybe variable */
#define F_LEN_VAR_FIXED    (1<<1)

/*
 * command has its own ip advance logic
 * e.g. MI_BATCH_START, MI_BATCH_END
 */
#define F_IP_ADVANCE_CUSTOM (1<<2)
	u32 flag;

#define R_RCS	BIT(RCS0)
#define R_VCS1  BIT(VCS0)
#define R_VCS2  BIT(VCS1)
#define R_VCS	(R_VCS1 | R_VCS2)
#define R_BCS	BIT(BCS0)
#define R_VECS	BIT(VECS0)
#define R_ALL (R_RCS | R_VCS | R_BCS | R_VECS)
	/* rings that support this cmd: BLT/RCS/VCS/VECS */
	intel_engine_mask_t rings;

	/* devices that support this cmd: SNB/IVB/HSW/... */
	u16 devices;

	/* which DWords are address that need fix up.
	 * bit 0 means a 32-bit non address operand in command
	 * bit 1 means address operand, which could be 32-bit
	 * or 64-bit depending on different architectures.(
	 * defined by "gmadr_bytes_in_cmd" in intel_gvt.
	 * No matter the address length, each address only takes
	 * one bit in the bitmap.
	 */
	u16 addr_bitmap;

	/* flag == F_LEN_CONST : command length
	 * flag == F_LEN_VAR : length bias bits
	 * Note: length is in DWord
	 */
	u32 len;

	parser_cmd_handler handler;

	/* valid length in DWord */
	u32 valid_len;
};

struct cmd_entry {
	struct hlist_node hlist;
	const struct cmd_info *info;
};

enum {
	RING_BUFFER_INSTRUCTION,
	BATCH_BUFFER_INSTRUCTION,
	BATCH_BUFFER_2ND_LEVEL,
	RING_BUFFER_CTX,
};

enum {
	GTT_BUFFER,
	PPGTT_BUFFER
};

struct parser_exec_state {
	struct intel_vgpu *vgpu;
	const struct intel_engine_cs *engine;

	int buf_type;

	/* batch buffer address type */
	int buf_addr_type;

	/* graphics memory address of ring buffer start */
	unsigned long ring_start;
	unsigned long ring_size;
	unsigned long ring_head;
	unsigned long ring_tail;

	/* instruction graphics memory address */
	unsigned long ip_gma;

	/* mapped va of the instr_gma */
	void *ip_va;
	void *rb_va;

	void *ret_bb_va;
	/* next instruction when return from  batch buffer to ring buffer */
	unsigned long ret_ip_gma_ring;

	/* next instruction when return from 2nd batch buffer to batch buffer */
	unsigned long ret_ip_gma_bb;

	/* batch buffer address type (GTT or PPGTT)
	 * used when ret from 2nd level batch buffer
	 */
	int saved_buf_addr_type;
	bool is_ctx_wa;
	bool is_init_ctx;

	const struct cmd_info *info;

	struct intel_vgpu_workload *workload;
};

#define gmadr_dw_number(s)	\
	(s->vgpu->gvt->device_info.gmadr_bytes_in_cmd >> 2)

static unsigned long bypass_scan_mask = 0;

/* ring ALL, type = 0 */
static const struct sub_op_bits sub_op_mi[] = {
	{31, 29},
	{28, 23},
};

static const struct decode_info decode_info_mi = {
	"MI",
	OP_LEN_MI,
	ARRAY_SIZE(sub_op_mi),
	sub_op_mi,
};

/* ring RCS, command type 2 */
static const struct sub_op_bits sub_op_2d[] = {
	{31, 29},
	{28, 22},
};

static const struct decode_info decode_info_2d = {
	"2D",
	OP_LEN_2D,
	ARRAY_SIZE(sub_op_2d),
	sub_op_2d,
};

/* ring RCS, command type 3 */
static const struct sub_op_bits sub_op_3d_media[] = {
	{31, 29},
	{28, 27},
	{26, 24},
	{23, 16},
};

static const struct decode_info decode_info_3d_media = {
	"3D_Media",
	OP_LEN_3D_MEDIA,
	ARRAY_SIZE(sub_op_3d_media),
	sub_op_3d_media,
};

/* ring VCS, command type 3 */
static const struct sub_op_bits sub_op_mfx_vc[] = {
	{31, 29},
	{28, 27},
	{26, 24},
	{23, 21},
	{20, 16},
};

static const struct decode_info decode_info_mfx_vc = {
	"MFX_VC",
	OP_LEN_MFX_VC,
	ARRAY_SIZE(sub_op_mfx_vc),
	sub_op_mfx_vc,
};

/* ring VECS, command type 3 */
static const struct sub_op_bits sub_op_vebox[] = {
	{31, 29},
	{28, 27},
	{26, 24},
	{23, 21},
	{20, 16},
};

static const struct decode_info decode_info_vebox = {
	"VEBOX",
	OP_LEN_VEBOX,
	ARRAY_SIZE(sub_op_vebox),
	sub_op_vebox,
};

static const struct decode_info *ring_decode_info[I915_NUM_ENGINES][8] = {
	[RCS0] = {
		&decode_info_mi,
		NULL,
		NULL,
		&decode_info_3d_media,
		NULL,
		NULL,
		NULL,
		NULL,
	},

	[VCS0] = {
		&decode_info_mi,
		NULL,
		NULL,
		&decode_info_mfx_vc,
		NULL,
		NULL,
		NULL,
		NULL,
	},

	[BCS0] = {
		&decode_info_mi,
		NULL,
		&decode_info_2d,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	},

	[VECS0] = {
		&decode_info_mi,
		NULL,
		NULL,
		&decode_info_vebox,
		NULL,
		NULL,
		NULL,
		NULL,
	},

	[VCS1] = {
		&decode_info_mi,
		NULL,
		NULL,
		&decode_info_mfx_vc,
		NULL,
		NULL,
		NULL,
		NULL,
	},
};

static inline u32 get_opcode(u32 cmd, const struct intel_engine_cs *engine)
{
	const struct decode_info *d_info;

	d_info = ring_decode_info[engine->id][CMD_TYPE(cmd)];
	if (d_info == NULL)
		return INVALID_OP;

	return cmd >> (32 - d_info->op_len);
}

static inline const struct cmd_info *
find_cmd_entry(struct intel_gvt *gvt, unsigned int opcode,
	       const struct intel_engine_cs *engine)
{
	struct cmd_entry *e;

	hash_for_each_possible(gvt->cmd_table, e, hlist, opcode) {
		if (opcode == e->info->opcode &&
		    e->info->rings & engine->mask)
			return e->info;
	}
	return NULL;
}

static inline const struct cmd_info *
get_cmd_info(struct intel_gvt *gvt, u32 cmd,
	     const struct intel_engine_cs *engine)
{
	u32 opcode;

	opcode = get_opcode(cmd, engine);
	if (opcode == INVALID_OP)
		return NULL;

	return find_cmd_entry(gvt, opcode, engine);
}

static inline u32 sub_op_val(u32 cmd, u32 hi, u32 low)
{
	return (cmd >> low) & ((1U << (hi - low + 1)) - 1);
}

static inline void print_opcode(u32 cmd, const struct intel_engine_cs *engine)
{
	const struct decode_info *d_info;
	int i;

	d_info = ring_decode_info[engine->id][CMD_TYPE(cmd)];
	if (d_info == NULL)
		return;

	gvt_dbg_cmd("opcode=0x%x %s sub_ops:",
			cmd >> (32 - d_info->op_len), d_info->name);

	for (i = 0; i < d_info->nr_sub_op; i++)
		pr_err("0x%x ", sub_op_val(cmd, d_info->sub_op[i].hi,
					d_info->sub_op[i].low));

	pr_err("\n");
}

static inline u32 *cmd_ptr(struct parser_exec_state *s, int index)
{
	return s->ip_va + (index << 2);
}

static inline u32 cmd_val(struct parser_exec_state *s, int index)
{
	return *cmd_ptr(s, index);
}

static inline bool is_init_ctx(struct parser_exec_state *s)
{
	return (s->buf_type == RING_BUFFER_CTX && s->is_init_ctx);
}

static void parser_exec_state_dump(struct parser_exec_state *s)
{
	int cnt = 0;
	int i;

	gvt_dbg_cmd("  vgpu%d RING%s: ring_start(%08lx) ring_end(%08lx)"
		    " ring_head(%08lx) ring_tail(%08lx)\n",
		    s->vgpu->id, s->engine->name,
		    s->ring_start, s->ring_start + s->ring_size,
		    s->ring_head, s->ring_tail);

	gvt_dbg_cmd("  %s %s ip_gma(%08lx) ",
			s->buf_type == RING_BUFFER_INSTRUCTION ?
			"RING_BUFFER" : ((s->buf_type == RING_BUFFER_CTX) ?
				"CTX_BUFFER" : "BATCH_BUFFER"),
			s->buf_addr_type == GTT_BUFFER ?
			"GTT" : "PPGTT", s->ip_gma);

	if (s->ip_va == NULL) {
		gvt_dbg_cmd(" ip_va(NULL)");
		return;
	}

	gvt_dbg_cmd("  ip_va=%p: %08x %08x %08x %08x\n",
			s->ip_va, cmd_val(s, 0), cmd_val(s, 1),
			cmd_val(s, 2), cmd_val(s, 3));

	print_opcode(cmd_val(s, 0), s->engine);

	s->ip_va = (u32 *)((((u64)s->ip_va) >> 12) << 12);

	while (cnt < 1024) {
		gvt_dbg_cmd("ip_va=%p: ", s->ip_va);
		for (i = 0; i < 8; i++)
			gvt_dbg_cmd("%08x ", cmd_val(s, i));
		gvt_dbg_cmd("\n");

		s->ip_va += 8 * sizeof(u32);
		cnt += 8;
	}
}

static inline void update_ip_va(struct parser_exec_state *s)
{
	unsigned long len = 0;

	if (WARN_ON(s->ring_head == s->ring_tail))
		return;

	if (s->buf_type == RING_BUFFER_INSTRUCTION ||
			s->buf_type == RING_BUFFER_CTX) {
		unsigned long ring_top = s->ring_start + s->ring_size;

		if (s->ring_head > s->ring_tail) {
			if (s->ip_gma >= s->ring_head && s->ip_gma < ring_top)
				len = (s->ip_gma - s->ring_head);
			else if (s->ip_gma >= s->ring_start &&
					s->ip_gma <= s->ring_tail)
				len = (ring_top - s->ring_head) +
					(s->ip_gma - s->ring_start);
		} else
			len = (s->ip_gma - s->ring_head);

		s->ip_va = s->rb_va + len;
	} else {/* shadow batch buffer */
		s->ip_va = s->ret_bb_va;
	}
}

static inline int ip_gma_set(struct parser_exec_state *s,
		unsigned long ip_gma)
{
	WARN_ON(!IS_ALIGNED(ip_gma, 4));

	s->ip_gma = ip_gma;
	update_ip_va(s);
	return 0;
}

static inline int ip_gma_advance(struct parser_exec_state *s,
		unsigned int dw_len)
{
	s->ip_gma += (dw_len << 2);

	if (s->buf_type == RING_BUFFER_INSTRUCTION) {
		if (s->ip_gma >= s->ring_start + s->ring_size)
			s->ip_gma -= s->ring_size;
		update_ip_va(s);
	} else {
		s->ip_va += (dw_len << 2);
	}

	return 0;
}

static inline int get_cmd_length(const struct cmd_info *info, u32 cmd)
{
	if ((info->flag & F_LEN_MASK) == F_LEN_CONST)
		return info->len;
	else
		return (cmd & ((1U << info->len) - 1)) + 2;
	return 0;
}

static inline int cmd_length(struct parser_exec_state *s)
{
	return get_cmd_length(s->info, cmd_val(s, 0));
}

/* do not remove this, some platform may need clflush here */
#define patch_value(s, addr, val) do { \
	*addr = val; \
} while (0)

static inline bool is_mocs_mmio(unsigned int offset)
{
	return ((offset >= 0xc800) && (offset <= 0xcff8)) ||
		((offset >= 0xb020) && (offset <= 0xb0a0));
}

static int is_cmd_update_pdps(unsigned int offset,
			      struct parser_exec_state *s)
{
	u32 base = s->workload->engine->mmio_base;
	return i915_mmio_reg_equal(_MMIO(offset), GEN8_RING_PDP_UDW(base, 0));
}

static int cmd_pdp_mmio_update_handler(struct parser_exec_state *s,
				       unsigned int offset, unsigned int index)
{
	struct intel_vgpu *vgpu = s->vgpu;
	struct intel_vgpu_mm *shadow_mm = s->workload->shadow_mm;
	struct intel_vgpu_mm *mm;
	u64 pdps[GEN8_3LVL_PDPES];

	if (shadow_mm->ppgtt_mm.root_entry_type ==
	    GTT_TYPE_PPGTT_ROOT_L4_ENTRY) {
		pdps[0] = (u64)cmd_val(s, 2) << 32;
		pdps[0] |= cmd_val(s, 4);

		mm = intel_vgpu_find_ppgtt_mm(vgpu, pdps);
		if (!mm) {
			gvt_vgpu_err("failed to get the 4-level shadow vm\n");
			return -EINVAL;
		}
		intel_vgpu_mm_get(mm);
		list_add_tail(&mm->ppgtt_mm.link,
			      &s->workload->lri_shadow_mm);
		*cmd_ptr(s, 2) = upper_32_bits(mm->ppgtt_mm.shadow_pdps[0]);
		*cmd_ptr(s, 4) = lower_32_bits(mm->ppgtt_mm.shadow_pdps[0]);
	} else {
		/* Currently all guests use PML4 table and now can't
		 * have a guest with 3-level table but uses LRI for
		 * PPGTT update. So this is simply un-testable. */
		GEM_BUG_ON(1);
		gvt_vgpu_err("invalid shared shadow vm type\n");
		return -EINVAL;
	}
	return 0;
}

static int cmd_reg_handler(struct parser_exec_state *s,
	unsigned int offset, unsigned int index, char *cmd)
{
	struct intel_vgpu *vgpu = s->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	u32 ctx_sr_ctl;
	u32 *vreg, vreg_old;

	if (offset + 4 > gvt->device_info.mmio_size) {
		gvt_vgpu_err("%s access to (%x) outside of MMIO range\n",
				cmd, offset);
		return -EFAULT;
	}

	if (is_init_ctx(s)) {
		struct intel_gvt_mmio_info *mmio_info;

		intel_gvt_mmio_set_cmd_accessible(gvt, offset);
		mmio_info = intel_gvt_find_mmio_info(gvt, offset);
		if (mmio_info && mmio_info->write)
			intel_gvt_mmio_set_cmd_write_patch(gvt, offset);
		return 0;
	}

	if (!intel_gvt_mmio_is_cmd_accessible(gvt, offset)) {
		gvt_vgpu_err("%s access to non-render register (%x)\n",
				cmd, offset);
		return -EBADRQC;
	}

	if (!strncmp(cmd, "srm", 3) ||
			!strncmp(cmd, "lrm", 3)) {
		if (offset == i915_mmio_reg_offset(GEN8_L3SQCREG4) ||
		    offset == 0x21f0 ||
		    (IS_BROADWELL(gvt->gt->i915) &&
		     offset == i915_mmio_reg_offset(INSTPM)))
			return 0;
		else {
			gvt_vgpu_err("%s access to register (%x)\n",
					cmd, offset);
			return -EPERM;
		}
	}

	if (!strncmp(cmd, "lrr-src", 7) ||
			!strncmp(cmd, "lrr-dst", 7)) {
		if (IS_BROADWELL(gvt->gt->i915) && offset == 0x215c)
			return 0;
		else {
			gvt_vgpu_err("not allowed cmd %s reg (%x)\n", cmd, offset);
			return -EPERM;
		}
	}

	if (!strncmp(cmd, "pipe_ctrl", 9)) {
		/* TODO: add LRI POST logic here */
		return 0;
	}

	if (strncmp(cmd, "lri", 3))
		return -EPERM;

	/* below are all lri handlers */
	vreg = &vgpu_vreg(s->vgpu, offset);

	if (is_cmd_update_pdps(offset, s) &&
	    cmd_pdp_mmio_update_handler(s, offset, index))
		return -EINVAL;

	if (offset == i915_mmio_reg_offset(DERRMR) ||
		offset == i915_mmio_reg_offset(FORCEWAKE_MT)) {
		/* Writing to HW VGT_PVINFO_PAGE offset will be discarded */
		patch_value(s, cmd_ptr(s, index), VGT_PVINFO_PAGE);
	}

	if (is_mocs_mmio(offset))
		*vreg = cmd_val(s, index + 1);

	vreg_old = *vreg;

	if (intel_gvt_mmio_is_cmd_write_patch(gvt, offset)) {
		u32 cmdval_new, cmdval;
		struct intel_gvt_mmio_info *mmio_info;

		cmdval = cmd_val(s, index + 1);

		mmio_info = intel_gvt_find_mmio_info(gvt, offset);
		if (!mmio_info) {
			cmdval_new = cmdval;
		} else {
			u64 ro_mask = mmio_info->ro_mask;
			int ret;

			if (likely(!ro_mask))
				ret = mmio_info->write(s->vgpu, offset,
						&cmdval, 4);
			else {
				gvt_vgpu_err("try to write RO reg %x\n",
						offset);
				ret = -EBADRQC;
			}
			if (ret)
				return ret;
			cmdval_new = *vreg;
		}
		if (cmdval_new != cmdval)
			patch_value(s, cmd_ptr(s, index+1), cmdval_new);
	}

	/* only patch cmd. restore vreg value if changed in mmio write handler*/
	*vreg = vreg_old;

	/* TODO
	 * In order to let workload with inhibit context to generate
	 * correct image data into memory, vregs values will be loaded to
	 * hw via LRIs in the workload with inhibit context. But as
	 * indirect context is loaded prior to LRIs in workload, we don't
	 * want reg values specified in indirect context overwritten by
	 * LRIs in workloads. So, when scanning an indirect context, we
	 * update reg values in it into vregs, so LRIs in workload with
	 * inhibit context will restore with correct values
	 */
	if (GRAPHICS_VER(s->engine->i915) == 9 &&
	    intel_gvt_mmio_is_sr_in_ctx(gvt, offset) &&
	    !strncmp(cmd, "lri", 3)) {
		intel_gvt_read_gpa(s->vgpu,
			s->workload->ring_context_gpa + 12, &ctx_sr_ctl, 4);
		/* check inhibit context */
		if (ctx_sr_ctl & 1) {
			u32 data = cmd_val(s, index + 1);

			if (intel_gvt_mmio_has_mode_mask(s->vgpu->gvt, offset))
				intel_vgpu_mask_mmio_write(vgpu,
							offset, &data, 4);
			else
				vgpu_vreg(vgpu, offset) = data;
		}
	}

	return 0;
}

#define cmd_reg(s, i) \
	(cmd_val(s, i) & GENMASK(22, 2))

#define cmd_reg_inhibit(s, i) \
	(cmd_val(s, i) & GENMASK(22, 18))

#define cmd_gma(s, i) \
	(cmd_val(s, i) & GENMASK(31, 2))

#define cmd_gma_hi(s, i) \
	(cmd_val(s, i) & GENMASK(15, 0))

static int cmd_handler_lri(struct parser_exec_state *s)
{
	int i, ret = 0;
	int cmd_len = cmd_length(s);

	for (i = 1; i < cmd_len; i += 2) {
		if (IS_BROADWELL(s->engine->i915) && s->engine->id != RCS0) {
			if (s->engine->id == BCS0 &&
			    cmd_reg(s, i) == i915_mmio_reg_offset(DERRMR))
				ret |= 0;
			else
				ret |= cmd_reg_inhibit(s, i) ? -EBADRQC : 0;
		}
		if (ret)
			break;
		ret |= cmd_reg_handler(s, cmd_reg(s, i), i, "lri");
		if (ret)
			break;
	}
	return ret;
}

static int cmd_handler_lrr(struct parser_exec_state *s)
{
	int i, ret = 0;
	int cmd_len = cmd_length(s);

	for (i = 1; i < cmd_len; i += 2) {
		if (IS_BROADWELL(s->engine->i915))
			ret |= ((cmd_reg_inhibit(s, i) ||
				 (cmd_reg_inhibit(s, i + 1)))) ?
				-EBADRQC : 0;
		if (ret)
			break;
		ret |= cmd_reg_handler(s, cmd_reg(s, i), i, "lrr-src");
		if (ret)
			break;
		ret |= cmd_reg_handler(s, cmd_reg(s, i + 1), i, "lrr-dst");
		if (ret)
			break;
	}
	return ret;
}

static inline int cmd_address_audit(struct parser_exec_state *s,
		unsigned long guest_gma, int op_size, bool index_mode);

static int cmd_handler_lrm(struct parser_exec_state *s)
{
	struct intel_gvt *gvt = s->vgpu->gvt;
	int gmadr_bytes = gvt->device_info.gmadr_bytes_in_cmd;
	unsigned long gma;
	int i, ret = 0;
	int cmd_len = cmd_length(s);

	for (i = 1; i < cmd_len;) {
		if (IS_BROADWELL(s->engine->i915))
			ret |= (cmd_reg_inhibit(s, i)) ? -EBADRQC : 0;
		if (ret)
			break;
		ret |= cmd_reg_handler(s, cmd_reg(s, i), i, "lrm");
		if (ret)
			break;
		if (cmd_val(s, 0) & (1 << 22)) {
			gma = cmd_gma(s, i + 1);
			if (gmadr_bytes == 8)
				gma |= (cmd_gma_hi(s, i + 2)) << 32;
			ret |= cmd_address_audit(s, gma, sizeof(u32), false);
			if (ret)
				break;
		}
		i += gmadr_dw_number(s) + 1;
	}
	return ret;
}

static int cmd_handler_srm(struct parser_exec_state *s)
{
	int gmadr_bytes = s->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	unsigned long gma;
	int i, ret = 0;
	int cmd_len = cmd_length(s);

	for (i = 1; i < cmd_len;) {
		ret |= cmd_reg_handler(s, cmd_reg(s, i), i, "srm");
		if (ret)
			break;
		if (cmd_val(s, 0) & (1 << 22)) {
			gma = cmd_gma(s, i + 1);
			if (gmadr_bytes == 8)
				gma |= (cmd_gma_hi(s, i + 2)) << 32;
			ret |= cmd_address_audit(s, gma, sizeof(u32), false);
			if (ret)
				break;
		}
		i += gmadr_dw_number(s) + 1;
	}
	return ret;
}

struct cmd_interrupt_event {
	int pipe_control_notify;
	int mi_flush_dw;
	int mi_user_interrupt;
};

static const struct cmd_interrupt_event cmd_interrupt_events[] = {
	[RCS0] = {
		.pipe_control_notify = RCS_PIPE_CONTROL,
		.mi_flush_dw = INTEL_GVT_EVENT_RESERVED,
		.mi_user_interrupt = RCS_MI_USER_INTERRUPT,
	},
	[BCS0] = {
		.pipe_control_notify = INTEL_GVT_EVENT_RESERVED,
		.mi_flush_dw = BCS_MI_FLUSH_DW,
		.mi_user_interrupt = BCS_MI_USER_INTERRUPT,
	},
	[VCS0] = {
		.pipe_control_notify = INTEL_GVT_EVENT_RESERVED,
		.mi_flush_dw = VCS_MI_FLUSH_DW,
		.mi_user_interrupt = VCS_MI_USER_INTERRUPT,
	},
	[VCS1] = {
		.pipe_control_notify = INTEL_GVT_EVENT_RESERVED,
		.mi_flush_dw = VCS2_MI_FLUSH_DW,
		.mi_user_interrupt = VCS2_MI_USER_INTERRUPT,
	},
	[VECS0] = {
		.pipe_control_notify = INTEL_GVT_EVENT_RESERVED,
		.mi_flush_dw = VECS_MI_FLUSH_DW,
		.mi_user_interrupt = VECS_MI_USER_INTERRUPT,
	},
};

static int cmd_handler_pipe_control(struct parser_exec_state *s)
{
	int gmadr_bytes = s->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	unsigned long gma;
	bool index_mode = false;
	unsigned int post_sync;
	int ret = 0;
	u32 hws_pga, val;

	post_sync = (cmd_val(s, 1) & PIPE_CONTROL_POST_SYNC_OP_MASK) >> 14;

	/* LRI post sync */
	if (cmd_val(s, 1) & PIPE_CONTROL_MMIO_WRITE)
		ret = cmd_reg_handler(s, cmd_reg(s, 2), 1, "pipe_ctrl");
	/* post sync */
	else if (post_sync) {
		if (post_sync == 2)
			ret = cmd_reg_handler(s, 0x2350, 1, "pipe_ctrl");
		else if (post_sync == 3)
			ret = cmd_reg_handler(s, 0x2358, 1, "pipe_ctrl");
		else if (post_sync == 1) {
			/* check ggtt*/
			if ((cmd_val(s, 1) & PIPE_CONTROL_GLOBAL_GTT_IVB)) {
				gma = cmd_val(s, 2) & GENMASK(31, 3);
				if (gmadr_bytes == 8)
					gma |= (cmd_gma_hi(s, 3)) << 32;
				/* Store Data Index */
				if (cmd_val(s, 1) & (1 << 21))
					index_mode = true;
				ret |= cmd_address_audit(s, gma, sizeof(u64),
						index_mode);
				if (ret)
					return ret;
				if (index_mode) {
					hws_pga = s->vgpu->hws_pga[s->engine->id];
					gma = hws_pga + gma;
					patch_value(s, cmd_ptr(s, 2), gma);
					val = cmd_val(s, 1) & (~(1 << 21));
					patch_value(s, cmd_ptr(s, 1), val);
				}
			}
		}
	}

	if (ret)
		return ret;

	if (cmd_val(s, 1) & PIPE_CONTROL_NOTIFY)
		set_bit(cmd_interrupt_events[s->engine->id].pipe_control_notify,
			s->workload->pending_events);
	return 0;
}

static int cmd_handler_mi_user_interrupt(struct parser_exec_state *s)
{
	set_bit(cmd_interrupt_events[s->engine->id].mi_user_interrupt,
		s->workload->pending_events);
	patch_value(s, cmd_ptr(s, 0), MI_NOOP);
	return 0;
}

static int cmd_advance_default(struct parser_exec_state *s)
{
	return ip_gma_advance(s, cmd_length(s));
}

static int cmd_handler_mi_batch_buffer_end(struct parser_exec_state *s)
{
	int ret;

	if (s->buf_type == BATCH_BUFFER_2ND_LEVEL) {
		s->buf_type = BATCH_BUFFER_INSTRUCTION;
		ret = ip_gma_set(s, s->ret_ip_gma_bb);
		s->buf_addr_type = s->saved_buf_addr_type;
	} else if (s->buf_type == RING_BUFFER_CTX) {
		ret = ip_gma_set(s, s->ring_tail);
	} else {
		s->buf_type = RING_BUFFER_INSTRUCTION;
		s->buf_addr_type = GTT_BUFFER;
		if (s->ret_ip_gma_ring >= s->ring_start + s->ring_size)
			s->ret_ip_gma_ring -= s->ring_size;
		ret = ip_gma_set(s, s->ret_ip_gma_ring);
	}
	return ret;
}

struct mi_display_flip_command_info {
	int pipe;
	int plane;
	int event;
	i915_reg_t stride_reg;
	i915_reg_t ctrl_reg;
	i915_reg_t surf_reg;
	u64 stride_val;
	u64 tile_val;
	u64 surf_val;
	bool async_flip;
};

struct plane_code_mapping {
	int pipe;
	int plane;
	int event;
};

static int gen8_decode_mi_display_flip(struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	struct drm_i915_private *dev_priv = s->engine->i915;
	struct plane_code_mapping gen8_plane_code[] = {
		[0] = {PIPE_A, PLANE_A, PRIMARY_A_FLIP_DONE},
		[1] = {PIPE_B, PLANE_A, PRIMARY_B_FLIP_DONE},
		[2] = {PIPE_A, PLANE_B, SPRITE_A_FLIP_DONE},
		[3] = {PIPE_B, PLANE_B, SPRITE_B_FLIP_DONE},
		[4] = {PIPE_C, PLANE_A, PRIMARY_C_FLIP_DONE},
		[5] = {PIPE_C, PLANE_B, SPRITE_C_FLIP_DONE},
	};
	u32 dword0, dword1, dword2;
	u32 v;

	dword0 = cmd_val(s, 0);
	dword1 = cmd_val(s, 1);
	dword2 = cmd_val(s, 2);

	v = (dword0 & GENMASK(21, 19)) >> 19;
	if (drm_WARN_ON(&dev_priv->drm, v >= ARRAY_SIZE(gen8_plane_code)))
		return -EBADRQC;

	info->pipe = gen8_plane_code[v].pipe;
	info->plane = gen8_plane_code[v].plane;
	info->event = gen8_plane_code[v].event;
	info->stride_val = (dword1 & GENMASK(15, 6)) >> 6;
	info->tile_val = (dword1 & 0x1);
	info->surf_val = (dword2 & GENMASK(31, 12)) >> 12;
	info->async_flip = ((dword2 & GENMASK(1, 0)) == 0x1);

	if (info->plane == PLANE_A) {
		info->ctrl_reg = DSPCNTR(dev_priv, info->pipe);
		info->stride_reg = DSPSTRIDE(dev_priv, info->pipe);
		info->surf_reg = DSPSURF(dev_priv, info->pipe);
	} else if (info->plane == PLANE_B) {
		info->ctrl_reg = SPRCTL(info->pipe);
		info->stride_reg = SPRSTRIDE(info->pipe);
		info->surf_reg = SPRSURF(info->pipe);
	} else {
		drm_WARN_ON(&dev_priv->drm, 1);
		return -EBADRQC;
	}
	return 0;
}

static int skl_decode_mi_display_flip(struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	struct drm_i915_private *dev_priv = s->engine->i915;
	struct intel_vgpu *vgpu = s->vgpu;
	u32 dword0 = cmd_val(s, 0);
	u32 dword1 = cmd_val(s, 1);
	u32 dword2 = cmd_val(s, 2);
	u32 plane = (dword0 & GENMASK(12, 8)) >> 8;

	info->plane = PRIMARY_PLANE;

	switch (plane) {
	case MI_DISPLAY_FLIP_SKL_PLANE_1_A:
		info->pipe = PIPE_A;
		info->event = PRIMARY_A_FLIP_DONE;
		break;
	case MI_DISPLAY_FLIP_SKL_PLANE_1_B:
		info->pipe = PIPE_B;
		info->event = PRIMARY_B_FLIP_DONE;
		break;
	case MI_DISPLAY_FLIP_SKL_PLANE_1_C:
		info->pipe = PIPE_C;
		info->event = PRIMARY_C_FLIP_DONE;
		break;

	case MI_DISPLAY_FLIP_SKL_PLANE_2_A:
		info->pipe = PIPE_A;
		info->event = SPRITE_A_FLIP_DONE;
		info->plane = SPRITE_PLANE;
		break;
	case MI_DISPLAY_FLIP_SKL_PLANE_2_B:
		info->pipe = PIPE_B;
		info->event = SPRITE_B_FLIP_DONE;
		info->plane = SPRITE_PLANE;
		break;
	case MI_DISPLAY_FLIP_SKL_PLANE_2_C:
		info->pipe = PIPE_C;
		info->event = SPRITE_C_FLIP_DONE;
		info->plane = SPRITE_PLANE;
		break;

	default:
		gvt_vgpu_err("unknown plane code %d\n", plane);
		return -EBADRQC;
	}

	info->stride_val = (dword1 & GENMASK(15, 6)) >> 6;
	info->tile_val = (dword1 & GENMASK(2, 0));
	info->surf_val = (dword2 & GENMASK(31, 12)) >> 12;
	info->async_flip = ((dword2 & GENMASK(1, 0)) == 0x1);

	info->ctrl_reg = DSPCNTR(dev_priv, info->pipe);
	info->stride_reg = DSPSTRIDE(dev_priv, info->pipe);
	info->surf_reg = DSPSURF(dev_priv, info->pipe);

	return 0;
}

static int gen8_check_mi_display_flip(struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	u32 stride, tile;

	if (!info->async_flip)
		return 0;

	if (GRAPHICS_VER(s->engine->i915) >= 9) {
		stride = vgpu_vreg_t(s->vgpu, info->stride_reg) & GENMASK(9, 0);
		tile = (vgpu_vreg_t(s->vgpu, info->ctrl_reg) &
				GENMASK(12, 10)) >> 10;
	} else {
		stride = (vgpu_vreg_t(s->vgpu, info->stride_reg) &
				GENMASK(15, 6)) >> 6;
		tile = (vgpu_vreg_t(s->vgpu, info->ctrl_reg) & (1 << 10)) >> 10;
	}

	if (stride != info->stride_val)
		gvt_dbg_cmd("cannot change stride during async flip\n");

	if (tile != info->tile_val)
		gvt_dbg_cmd("cannot change tile during async flip\n");

	return 0;
}

static int gen8_update_plane_mmio_from_mi_display_flip(
		struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	struct drm_i915_private *dev_priv = s->engine->i915;
	struct intel_vgpu *vgpu = s->vgpu;

	set_mask_bits(&vgpu_vreg_t(vgpu, info->surf_reg), GENMASK(31, 12),
		      info->surf_val << 12);
	if (GRAPHICS_VER(dev_priv) >= 9) {
		set_mask_bits(&vgpu_vreg_t(vgpu, info->stride_reg), GENMASK(9, 0),
			      info->stride_val);
		set_mask_bits(&vgpu_vreg_t(vgpu, info->ctrl_reg), GENMASK(12, 10),
			      info->tile_val << 10);
	} else {
		set_mask_bits(&vgpu_vreg_t(vgpu, info->stride_reg), GENMASK(15, 6),
			      info->stride_val << 6);
		set_mask_bits(&vgpu_vreg_t(vgpu, info->ctrl_reg), GENMASK(10, 10),
			      info->tile_val << 10);
	}

	if (info->plane == PLANE_PRIMARY)
		vgpu_vreg_t(vgpu, PIPE_FLIPCOUNT_G4X(dev_priv, info->pipe))++;

	if (info->async_flip)
		intel_vgpu_trigger_virtual_event(vgpu, info->event);
	else
		set_bit(info->event, vgpu->irq.flip_done_event[info->pipe]);

	return 0;
}

static int decode_mi_display_flip(struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	if (IS_BROADWELL(s->engine->i915))
		return gen8_decode_mi_display_flip(s, info);
	if (GRAPHICS_VER(s->engine->i915) >= 9)
		return skl_decode_mi_display_flip(s, info);

	return -ENODEV;
}

static int check_mi_display_flip(struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	return gen8_check_mi_display_flip(s, info);
}

static int update_plane_mmio_from_mi_display_flip(
		struct parser_exec_state *s,
		struct mi_display_flip_command_info *info)
{
	return gen8_update_plane_mmio_from_mi_display_flip(s, info);
}

static int cmd_handler_mi_display_flip(struct parser_exec_state *s)
{
	struct mi_display_flip_command_info info;
	struct intel_vgpu *vgpu = s->vgpu;
	int ret;
	int i;
	int len = cmd_length(s);
	u32 valid_len = CMD_LEN(1);

	/* Flip Type == Stereo 3D Flip */
	if (DWORD_FIELD(2, 1, 0) == 2)
		valid_len++;
	ret = gvt_check_valid_cmd_length(cmd_length(s),
			valid_len);
	if (ret)
		return ret;

	ret = decode_mi_display_flip(s, &info);
	if (ret) {
		gvt_vgpu_err("fail to decode MI display flip command\n");
		return ret;
	}

	ret = check_mi_display_flip(s, &info);
	if (ret) {
		gvt_vgpu_err("invalid MI display flip command\n");
		return ret;
	}

	ret = update_plane_mmio_from_mi_display_flip(s, &info);
	if (ret) {
		gvt_vgpu_err("fail to update plane mmio\n");
		return ret;
	}

	for (i = 0; i < len; i++)
		patch_value(s, cmd_ptr(s, i), MI_NOOP);
	return 0;
}

static bool is_wait_for_flip_pending(u32 cmd)
{
	return cmd & (MI_WAIT_FOR_PLANE_A_FLIP_PENDING |
			MI_WAIT_FOR_PLANE_B_FLIP_PENDING |
			MI_WAIT_FOR_PLANE_C_FLIP_PENDING |
			MI_WAIT_FOR_SPRITE_A_FLIP_PENDING |
			MI_WAIT_FOR_SPRITE_B_FLIP_PENDING |
			MI_WAIT_FOR_SPRITE_C_FLIP_PENDING);
}

static int cmd_handler_mi_wait_for_event(struct parser_exec_state *s)
{
	u32 cmd = cmd_val(s, 0);

	if (!is_wait_for_flip_pending(cmd))
		return 0;

	patch_value(s, cmd_ptr(s, 0), MI_NOOP);
	return 0;
}

static unsigned long get_gma_bb_from_cmd(struct parser_exec_state *s, int index)
{
	unsigned long addr;
	unsigned long gma_high, gma_low;
	struct intel_vgpu *vgpu = s->vgpu;
	int gmadr_bytes = vgpu->gvt->device_info.gmadr_bytes_in_cmd;

	if (WARN_ON(gmadr_bytes != 4 && gmadr_bytes != 8)) {
		gvt_vgpu_err("invalid gma bytes %d\n", gmadr_bytes);
		return INTEL_GVT_INVALID_ADDR;
	}

	gma_low = cmd_val(s, index) & BATCH_BUFFER_ADDR_MASK;
	if (gmadr_bytes == 4) {
		addr = gma_low;
	} else {
		gma_high = cmd_val(s, index + 1) & BATCH_BUFFER_ADDR_HIGH_MASK;
		addr = (((unsigned long)gma_high) << 32) | gma_low;
	}
	return addr;
}

static inline int cmd_address_audit(struct parser_exec_state *s,
		unsigned long guest_gma, int op_size, bool index_mode)
{
	struct intel_vgpu *vgpu = s->vgpu;
	u32 max_surface_size = vgpu->gvt->device_info.max_surface_size;
	int i;
	int ret;

	if (op_size > max_surface_size) {
		gvt_vgpu_err("command address audit fail name %s\n",
			s->info->name);
		return -EFAULT;
	}

	if (index_mode)	{
		if (guest_gma >= I915_GTT_PAGE_SIZE) {
			ret = -EFAULT;
			goto err;
		}
	} else if (!intel_gvt_ggtt_validate_range(vgpu, guest_gma, op_size)) {
		ret = -EFAULT;
		goto err;
	}

	return 0;

err:
	gvt_vgpu_err("cmd_parser: Malicious %s detected, addr=0x%lx, len=%d!\n",
			s->info->name, guest_gma, op_size);

	pr_err("cmd dump: ");
	for (i = 0; i < cmd_length(s); i++) {
		if (!(i % 4))
			pr_err("\n%08x ", cmd_val(s, i));
		else
			pr_err("%08x ", cmd_val(s, i));
	}
	pr_err("\nvgpu%d: aperture 0x%llx - 0x%llx, hidden 0x%llx - 0x%llx\n",
			vgpu->id,
			vgpu_aperture_gmadr_base(vgpu),
			vgpu_aperture_gmadr_end(vgpu),
			vgpu_hidden_gmadr_base(vgpu),
			vgpu_hidden_gmadr_end(vgpu));
	return ret;
}

static int cmd_handler_mi_store_data_imm(struct parser_exec_state *s)
{
	int gmadr_bytes = s->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	int op_size = (cmd_length(s) - 3) * sizeof(u32);
	int core_id = (cmd_val(s, 2) & (1 << 0)) ? 1 : 0;
	unsigned long gma, gma_low, gma_high;
	u32 valid_len = CMD_LEN(2);
	int ret = 0;

	/* check ppggt */
	if (!(cmd_val(s, 0) & (1 << 22)))
		return 0;

	/* check if QWORD */
	if (DWORD_FIELD(0, 21, 21))
		valid_len++;
	ret = gvt_check_valid_cmd_length(cmd_length(s),
			valid_len);
	if (ret)
		return ret;

	gma = cmd_val(s, 2) & GENMASK(31, 2);

	if (gmadr_bytes == 8) {
		gma_low = cmd_val(s, 1) & GENMASK(31, 2);
		gma_high = cmd_val(s, 2) & GENMASK(15, 0);
		gma = (gma_high << 32) | gma_low;
		core_id = (cmd_val(s, 1) & (1 << 0)) ? 1 : 0;
	}
	ret = cmd_address_audit(s, gma + op_size * core_id, op_size, false);
	return ret;
}

static inline int unexpected_cmd(struct parser_exec_state *s)
{
	struct intel_vgpu *vgpu = s->vgpu;

	gvt_vgpu_err("Unexpected %s in command buffer!\n", s->info->name);

	return -EBADRQC;
}

static int cmd_handler_mi_semaphore_wait(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_report_perf_count(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_op_2e(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_op_2f(struct parser_exec_state *s)
{
	int gmadr_bytes = s->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	int op_size = (1 << ((cmd_val(s, 0) & GENMASK(20, 19)) >> 19)) *
			sizeof(u32);
	unsigned long gma, gma_high;
	u32 valid_len = CMD_LEN(1);
	int ret = 0;

	if (!(cmd_val(s, 0) & (1 << 22)))
		return ret;

	/* check inline data */
	if (cmd_val(s, 0) & BIT(18))
		valid_len = CMD_LEN(9);
	ret = gvt_check_valid_cmd_length(cmd_length(s),
			valid_len);
	if (ret)
		return ret;

	gma = cmd_val(s, 1) & GENMASK(31, 2);
	if (gmadr_bytes == 8) {
		gma_high = cmd_val(s, 2) & GENMASK(15, 0);
		gma = (gma_high << 32) | gma;
	}
	ret = cmd_address_audit(s, gma, op_size, false);
	return ret;
}

static int cmd_handler_mi_store_data_index(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_clflush(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_conditional_batch_buffer_end(
		struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_update_gtt(struct parser_exec_state *s)
{
	return unexpected_cmd(s);
}

static int cmd_handler_mi_flush_dw(struct parser_exec_state *s)
{
	int gmadr_bytes = s->vgpu->gvt->device_info.gmadr_bytes_in_cmd;
	unsigned long gma;
	bool index_mode = false;
	int ret = 0;
	u32 hws_pga, val;
	u32 valid_len = CMD_LEN(2);

	ret = gvt_check_valid_cmd_length(cmd_length(s),
			valid_len);
	if (ret) {
		/* Check again for Qword */
		ret = gvt_check_valid_cmd_length(cmd_length(s),
			++valid_len);
		return ret;
	}

	/* Check post-sync and ppgtt bit */
	if (((cmd_val(s, 0) >> 14) & 0x3) && (cmd_val(s, 1) & (1 << 2))) {
		gma = cmd_val(s, 1) & GENMASK(31, 3);
		if (gmadr_bytes == 8)
			gma |= (cmd_val(s, 2) & GENMASK(15, 0)) << 32;
		/* Store Data Index */
		if (cmd_val(s, 0) & (1 << 21))
			index_mode = true;
		ret = cmd_address_audit(s, gma, sizeof(u64), index_mode);
		if (ret)
			return ret;
		if (index_mode) {
			hws_pga = s->vgpu->hws_pga[s->engine->id];
			gma = hws_pga + gma;
			patch_value(s, cmd_ptr(s, 1), gma);
			val = cmd_val(s, 0) & (~(1 << 21));
			patch_value(s, cmd_ptr(s, 0), val);
		}
	}
	/* Check notify bit */
	if ((cmd_val(s, 0) & (1 << 8)))
		set_bit(cmd_interrupt_events[s->engine->id].mi_flush_dw,
			s->workload->pending_events);
	return ret;
}

static void addr_type_update_snb(struct parser_exec_state *s)
{
	if ((s->buf_type == RING_BUFFER_INSTRUCTION) &&
			(BATCH_BUFFER_ADR_SPACE_BIT(cmd_val(s, 0)) == 1)) {
		s->buf_addr_type = PPGTT_BUFFER;
	}
}


static int copy_gma_to_hva(struct intel_vgpu *vgpu, struct intel_vgpu_mm *mm,
		unsigned long gma, unsigned long end_gma, void *va)
{
	unsigned long copy_len, offset;
	unsigned long len = 0;
	unsigned long gpa;

	while (gma != end_gma) {
		gpa = intel_vgpu_gma_to_gpa(mm, gma);
		if (gpa == INTEL_GVT_INVALID_ADDR) {
			gvt_vgpu_err("invalid gma address: %lx\n", gma);
			return -EFAULT;
		}

		offset = gma & (I915_GTT_PAGE_SIZE - 1);

		copy_len = (end_gma - gma) >= (I915_GTT_PAGE_SIZE - offset) ?
			I915_GTT_PAGE_SIZE - offset : end_gma - gma;

		intel_gvt_read_gpa(vgpu, gpa, va + len, copy_len);

		len += copy_len;
		gma += copy_len;
	}
	return len;
}


/*
 * Check whether a batch buffer needs to be scanned. Currently
 * the only criteria is based on privilege.
 */
static int batch_buffer_needs_scan(struct parser_exec_state *s)
{
	/* Decide privilege based on address space */
	if (cmd_val(s, 0) & BIT(8) &&
	    !(s->vgpu->scan_nonprivbb & s->engine->mask))
		return 0;

	return 1;
}

static const char *repr_addr_type(unsigned int type)
{
	return type == PPGTT_BUFFER ? "ppgtt" : "ggtt";
}

static int find_bb_size(struct parser_exec_state *s,
			unsigned long *bb_size,
			unsigned long *bb_end_cmd_offset)
{
	unsigned long gma = 0;
	const struct cmd_info *info;
	u32 cmd_len = 0;
	bool bb_end = false;
	struct intel_vgpu *vgpu = s->vgpu;
	u32 cmd;
	struct intel_vgpu_mm *mm = (s->buf_addr_type == GTT_BUFFER) ?
		s->vgpu->gtt.ggtt_mm : s->workload->shadow_mm;

	*bb_size = 0;
	*bb_end_cmd_offset = 0;

	/* get the start gm address of the batch buffer */
	gma = get_gma_bb_from_cmd(s, 1);
	if (gma == INTEL_GVT_INVALID_ADDR)
		return -EFAULT;

	cmd = cmd_val(s, 0);
	info = get_cmd_info(s->vgpu->gvt, cmd, s->engine);
	if (info == NULL) {
		gvt_vgpu_err("unknown cmd 0x%x, opcode=0x%x, addr_type=%s, ring %s, workload=%p\n",
			     cmd, get_opcode(cmd, s->engine),
			     repr_addr_type(s->buf_addr_type),
			     s->engine->name, s->workload);
		return -EBADRQC;
	}
	do {
		if (copy_gma_to_hva(s->vgpu, mm,
				    gma, gma + 4, &cmd) < 0)
			return -EFAULT;
		info = get_cmd_info(s->vgpu->gvt, cmd, s->engine);
		if (info == NULL) {
			gvt_vgpu_err("unknown cmd 0x%x, opcode=0x%x, addr_type=%s, ring %s, workload=%p\n",
				     cmd, get_opcode(cmd, s->engine),
				     repr_addr_type(s->buf_addr_type),
				     s->engine->name, s->workload);
			return -EBADRQC;
		}

		if (info->opcode == OP_MI_BATCH_BUFFER_END) {
			bb_end = true;
		} else if (info->opcode == OP_MI_BATCH_BUFFER_START) {
			if (BATCH_BUFFER_2ND_LEVEL_BIT(cmd) == 0)
				/* chained batch buffer */
				bb_end = true;
		}

		if (bb_end)
			*bb_end_cmd_offset = *bb_size;

		cmd_len = get_cmd_length(info, cmd) << 2;
		*bb_size += cmd_len;
		gma += cmd_len;
	} while (!bb_end);

	return 0;
}

static int audit_bb_end(struct parser_exec_state *s, void *va)
{
	struct intel_vgpu *vgpu = s->vgpu;
	u32 cmd = *(u32 *)va;
	const struct cmd_info *info;

	info = get_cmd_info(s->vgpu->gvt, cmd, s->engine);
	if (info == NULL) {
		gvt_vgpu_err("unknown cmd 0x%x, opcode=0x%x, addr_type=%s, ring %s, workload=%p\n",
			     cmd, get_opcode(cmd, s->engine),
			     repr_addr_type(s->buf_addr_type),
			     s->engine->name, s->workload);
		return -EBADRQC;
	}

	if ((info->opcode == OP_MI_BATCH_BUFFER_END) ||
	    ((info->opcode == OP_MI_BATCH_BUFFER_START) &&
	     (BATCH_BUFFER_2ND_LEVEL_BIT(cmd) == 0)))
		return 0;

	return -EBADRQC;
}

static int perform_bb_shadow(struct parser_exec_state *s)
{
	struct intel_vgpu *vgpu = s->vgpu;
	struct intel_vgpu_shadow_bb *bb;
	unsigned long gma = 0;
	unsigned long bb_size;
	unsigned long bb_end_cmd_offset;
	int ret = 0;
	struct intel_vgpu_mm *mm = (s->buf_addr_type == GTT_BUFFER) ?
		s->vgpu->gtt.ggtt_mm : s->workload->shadow_mm;
	unsigned long start_offset = 0;

	/* get the start gm address of the batch buffer */
	gma = get_gma_bb_from_cmd(s, 1);
	if (gma == INTEL_GVT_INVALID_ADDR)
		return -EFAULT;

	ret = find_bb_size(s, &bb_size, &bb_end_cmd_offset);
	if (ret)
		return ret;

	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb)
		return -ENOMEM;

	bb->ppgtt = (s->buf_addr_type == GTT_BUFFER) ? false : true;

	/* the start_offset stores the batch buffer's start gma's
	 * offset relative to page boundary. so for non-privileged batch
	 * buffer, the shadowed gem object holds exactly the same page
	 * layout as original gem object. This is for the convience of
	 * replacing the whole non-privilged batch buffer page to this
	 * shadowed one in PPGTT at the same gma address. (this replacing
	 * action is not implemented yet now, but may be necessary in
	 * future).
	 * for prileged batch buffer, we just change start gma address to
	 * that of shadowed page.
	 */
	if (bb->ppgtt)
		start_offset = gma & ~I915_GTT_PAGE_MASK;

	bb->obj = i915_gem_object_create_shmem(s->engine->i915,
					       round_up(bb_size + start_offset,
							PAGE_SIZE));
	if (IS_ERR(bb->obj)) {
		ret = PTR_ERR(bb->obj);
		goto err_free_bb;
	}

	bb->va = i915_gem_object_pin_map(bb->obj, I915_MAP_WB);
	if (IS_ERR(bb->va)) {
		ret = PTR_ERR(bb->va);
		goto err_free_obj;
	}

	ret = copy_gma_to_hva(s->vgpu, mm,
			      gma, gma + bb_size,
			      bb->va + start_offset);
	if (ret < 0) {
		gvt_vgpu_err("fail to copy guest ring buffer\n");
		ret = -EFAULT;
		goto err_unmap;
	}

	ret = audit_bb_end(s, bb->va + start_offset + bb_end_cmd_offset);
	if (ret)
		goto err_unmap;

	i915_gem_object_unlock(bb->obj);
	INIT_LIST_HEAD(&bb->list);
	list_add(&bb->list, &s->workload->shadow_bb);

	bb->bb_start_cmd_va = s->ip_va;

	if ((s->buf_type == BATCH_BUFFER_INSTRUCTION) && (!s->is_ctx_wa))
		bb->bb_offset = s->ip_va - s->rb_va;
	else
		bb->bb_offset = 0;

	/*
	 * ip_va saves the virtual address of the shadow batch buffer, while
	 * ip_gma saves the graphics address of the original batch buffer.
	 * As the shadow batch buffer is just a copy from the originial one,
	 * it should be right to use shadow batch buffer'va and original batch
	 * buffer's gma in pair. After all, we don't want to pin the shadow
	 * buffer here (too early).
	 */
	s->ip_va = bb->va + start_offset;
	s->ip_gma = gma;
	return 0;
err_unmap:
	i915_gem_object_unpin_map(bb->obj);
err_free_obj:
	i915_gem_object_put(bb->obj);
err_free_bb:
	kfree(bb);
	return ret;
}

static int cmd_handler_mi_batch_buffer_start(struct parser_exec_state *s)
{
	bool second_level;
	int ret = 0;
	struct intel_vgpu *vgpu = s->vgpu;

	if (s->buf_type == BATCH_BUFFER_2ND_LEVEL) {
		gvt_vgpu_err("Found MI_BATCH_BUFFER_START in 2nd level BB\n");
		return -EFAULT;
	}

	second_level = BATCH_BUFFER_2ND_LEVEL_BIT(cmd_val(s, 0)) == 1;
	if (second_level && (s->buf_type != BATCH_BUFFER_INSTRUCTION)) {
		gvt_vgpu_err("Jumping to 2nd level BB from RB is not allowed\n");
		return -EFAULT;
	}

	s->saved_buf_addr_type = s->buf_addr_type;
	addr_type_update_snb(s);
	if (s->buf_type == RING_BUFFER_INSTRUCTION) {
		s->ret_ip_gma_ring = s->ip_gma + cmd_length(s) * sizeof(u32);
		s->buf_type = BATCH_BUFFER_INSTRUCTION;
	} else if (second_level) {
		s->buf_type = BATCH_BUFFER_2ND_LEVEL;
		s->ret_ip_gma_bb = s->ip_gma + cmd_length(s) * sizeof(u32);
		s->ret_bb_va = s->ip_va + cmd_length(s) * sizeof(u32);
	}

	if (batch_buffer_needs_scan(s)) {
		ret = perform_bb_shadow(s);
		if (ret < 0)
			gvt_vgpu_err("invalid shadow batch buffer\n");
	} else {
		/* emulate a batch buffer end to do return right */
		ret = cmd_handler_mi_batch_buffer_end(s);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static int mi_noop_index;

static const struct cmd_info cmd_info[] = {
	{"MI_NOOP", OP_MI_NOOP, F_LEN_CONST, R_ALL, D_ALL, 0, 1, NULL},

	{"MI_SET_PREDICATE", OP_MI_SET_PREDICATE, F_LEN_CONST, R_ALL, D_ALL,
		0, 1, NULL},

	{"MI_USER_INTERRUPT", OP_MI_USER_INTERRUPT, F_LEN_CONST, R_ALL, D_ALL,
		0, 1, cmd_handler_mi_user_interrupt},

	{"MI_WAIT_FOR_EVENT", OP_MI_WAIT_FOR_EVENT, F_LEN_CONST, R_RCS | R_BCS,
		D_ALL, 0, 1, cmd_handler_mi_wait_for_event},

	{"MI_FLUSH", OP_MI_FLUSH, F_LEN_CONST, R_ALL, D_ALL, 0, 1, NULL},

	{"MI_ARB_CHECK", OP_MI_ARB_CHECK, F_LEN_CONST, R_ALL, D_ALL, 0, 1,
		NULL},

	{"MI_RS_CONTROL", OP_MI_RS_CONTROL, F_LEN_CONST, R_RCS, D_ALL, 0, 1,
		NULL},

	{"MI_REPORT_HEAD", OP_MI_REPORT_HEAD, F_LEN_CONST, R_ALL, D_ALL, 0, 1,
		NULL},

	{"MI_ARB_ON_OFF", OP_MI_ARB_ON_OFF, F_LEN_CONST, R_ALL, D_ALL, 0, 1,
		NULL},

	{"MI_URB_ATOMIC_ALLOC", OP_MI_URB_ATOMIC_ALLOC, F_LEN_CONST, R_RCS,
		D_ALL, 0, 1, NULL},

	{"MI_BATCH_BUFFER_END", OP_MI_BATCH_BUFFER_END,
		F_IP_ADVANCE_CUSTOM | F_LEN_CONST, R_ALL, D_ALL, 0, 1,
		cmd_handler_mi_batch_buffer_end},

	{"MI_SUSPEND_FLUSH", OP_MI_SUSPEND_FLUSH, F_LEN_CONST, R_ALL, D_ALL,
		0, 1, NULL},

	{"MI_PREDICATE", OP_MI_PREDICATE, F_LEN_CONST, R_RCS, D_ALL, 0, 1,
		NULL},

	{"MI_TOPOLOGY_FILTER", OP_MI_TOPOLOGY_FILTER, F_LEN_CONST, R_ALL,
		D_ALL, 0, 1, NULL},

	{"MI_SET_APPID", OP_MI_SET_APPID, F_LEN_CONST, R_ALL, D_ALL, 0, 1,
		NULL},

	{"MI_RS_CONTEXT", OP_MI_RS_CONTEXT, F_LEN_CONST, R_RCS, D_ALL, 0, 1,
		NULL},

	{"MI_DISPLAY_FLIP", OP_MI_DISPLAY_FLIP, F_LEN_VAR,
		R_RCS | R_BCS, D_ALL, 0, 8, cmd_handler_mi_display_flip},

	{"MI_SEMAPHORE_MBOX", OP_MI_SEMAPHORE_MBOX, F_LEN_VAR | F_LEN_VAR_FIXED,
		R_ALL, D_ALL, 0, 8, NULL, CMD_LEN(1)},

	{"MI_MATH", OP_MI_MATH, F_LEN_VAR, R_ALL, D_ALL, 0, 8, NULL},

	{"MI_URB_CLEAR", OP_MI_URB_CLEAR, F_LEN_VAR | F_LEN_VAR_FIXED, R_RCS,
		D_ALL, 0, 8, NULL, CMD_LEN(0)},

	{"MI_SEMAPHORE_SIGNAL", OP_MI_SEMAPHORE_SIGNAL,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_BDW_PLUS, 0, 8,
		NULL, CMD_LEN(0)},

	{"MI_SEMAPHORE_WAIT", OP_MI_SEMAPHORE_WAIT,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_BDW_PLUS, ADDR_FIX_1(2),
		8, cmd_handler_mi_semaphore_wait, CMD_LEN(2)},

	{"MI_STORE_DATA_IMM", OP_MI_STORE_DATA_IMM, F_LEN_VAR, R_ALL, D_BDW_PLUS,
		ADDR_FIX_1(1), 10, cmd_handler_mi_store_data_imm},

	{"MI_STORE_DATA_INDEX", OP_MI_STORE_DATA_INDEX, F_LEN_VAR, R_ALL, D_ALL,
		0, 8, cmd_handler_mi_store_data_index},

	{"MI_LOAD_REGISTER_IMM", OP_MI_LOAD_REGISTER_IMM, F_LEN_VAR, R_ALL,
		D_ALL, 0, 8, cmd_handler_lri},

	{"MI_UPDATE_GTT", OP_MI_UPDATE_GTT, F_LEN_VAR, R_ALL, D_BDW_PLUS, 0, 10,
		cmd_handler_mi_update_gtt},

	{"MI_STORE_REGISTER_MEM", OP_MI_STORE_REGISTER_MEM,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_ALL, ADDR_FIX_1(2), 8,
		cmd_handler_srm, CMD_LEN(2)},

	{"MI_FLUSH_DW", OP_MI_FLUSH_DW, F_LEN_VAR, R_ALL, D_ALL, 0, 6,
		cmd_handler_mi_flush_dw},

	{"MI_CLFLUSH", OP_MI_CLFLUSH, F_LEN_VAR, R_ALL, D_ALL, ADDR_FIX_1(1),
		10, cmd_handler_mi_clflush},

	{"MI_REPORT_PERF_COUNT", OP_MI_REPORT_PERF_COUNT,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_ALL, ADDR_FIX_1(1), 6,
		cmd_handler_mi_report_perf_count, CMD_LEN(2)},

	{"MI_LOAD_REGISTER_MEM", OP_MI_LOAD_REGISTER_MEM,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_ALL, ADDR_FIX_1(2), 8,
		cmd_handler_lrm, CMD_LEN(2)},

	{"MI_LOAD_REGISTER_REG", OP_MI_LOAD_REGISTER_REG,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_ALL, 0, 8,
		cmd_handler_lrr, CMD_LEN(1)},

	{"MI_RS_STORE_DATA_IMM", OP_MI_RS_STORE_DATA_IMM,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_RCS, D_ALL, 0,
		8, NULL, CMD_LEN(2)},

	{"MI_LOAD_URB_MEM", OP_MI_LOAD_URB_MEM, F_LEN_VAR | F_LEN_VAR_FIXED,
		R_RCS, D_ALL, ADDR_FIX_1(2), 8, NULL, CMD_LEN(2)},

	{"MI_STORE_URM_MEM", OP_MI_STORE_URM_MEM, F_LEN_VAR, R_RCS, D_ALL,
		ADDR_FIX_1(2), 8, NULL},

	{"MI_OP_2E", OP_MI_2E, F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_BDW_PLUS,
		ADDR_FIX_2(1, 2), 8, cmd_handler_mi_op_2e, CMD_LEN(3)},

	{"MI_OP_2F", OP_MI_2F, F_LEN_VAR, R_ALL, D_BDW_PLUS, ADDR_FIX_1(1),
		8, cmd_handler_mi_op_2f},

	{"MI_BATCH_BUFFER_START", OP_MI_BATCH_BUFFER_START,
		F_IP_ADVANCE_CUSTOM, R_ALL, D_ALL, 0, 8,
		cmd_handler_mi_batch_buffer_start},

	{"MI_CONDITIONAL_BATCH_BUFFER_END", OP_MI_CONDITIONAL_BATCH_BUFFER_END,
		F_LEN_VAR | F_LEN_VAR_FIXED, R_ALL, D_ALL, ADDR_FIX_1(2), 8,
		cmd_handler_mi_conditional_batch_buffer_end, CMD_LEN(2)},

	{"MI_LOAD_SCAN_LINES_INCL", OP_MI_LOAD_SCAN_LINES_INCL, F_LEN_CONST,
		R_RCS | R_BCS, D_ALL, 0, 2, NULL},

	{"XY_SETUP_BLT", OP_XY_SETUP_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_2(4, 7), 8, NULL},

	{"XY_SETUP_CLIP_BLT", OP_XY_SETUP_CLIP_BLT, F_LEN_VAR, R_BCS, D_ALL,
		0, 8, NULL},

	{"XY_SETUP_MONO_PATTERN_SL_BLT", OP_XY_SETUP_MONO_PATTERN_SL_BLT,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_1(4), 8, NULL},

	{"XY_PIXEL_BLT", OP_XY_PIXEL_BLT, F_LEN_VAR, R_BCS, D_ALL, 0, 8, NULL},

	{"XY_SCANLINES_BLT", OP_XY_SCANLINES_BLT, F_LEN_VAR, R_BCS, D_ALL,
		0, 8, NULL},

	{"XY_TEXT_BLT", OP_XY_TEXT_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_1(3), 8, NULL},

	{"XY_TEXT_IMMEDIATE_BLT", OP_XY_TEXT_IMMEDIATE_BLT, F_LEN_VAR, R_BCS,
		D_ALL, 0, 8, NULL},

	{"XY_COLOR_BLT", OP_XY_COLOR_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_1(4), 8, NULL},

	{"XY_PAT_BLT", OP_XY_PAT_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_2(4, 5), 8, NULL},

	{"XY_MONO_PAT_BLT", OP_XY_MONO_PAT_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_1(4), 8, NULL},

	{"XY_SRC_COPY_BLT", OP_XY_SRC_COPY_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_2(4, 7), 8, NULL},

	{"XY_MONO_SRC_COPY_BLT", OP_XY_MONO_SRC_COPY_BLT, F_LEN_VAR, R_BCS,
		D_ALL, ADDR_FIX_2(4, 5), 8, NULL},

	{"XY_FULL_BLT", OP_XY_FULL_BLT, F_LEN_VAR, R_BCS, D_ALL, 0, 8, NULL},

	{"XY_FULL_MONO_SRC_BLT", OP_XY_FULL_MONO_SRC_BLT, F_LEN_VAR, R_BCS,
		D_ALL, ADDR_FIX_3(4, 5, 8), 8, NULL},

	{"XY_FULL_MONO_PATTERN_BLT", OP_XY_FULL_MONO_PATTERN_BLT, F_LEN_VAR,
		R_BCS, D_ALL, ADDR_FIX_2(4, 7), 8, NULL},

	{"XY_FULL_MONO_PATTERN_MONO_SRC_BLT",
		OP_XY_FULL_MONO_PATTERN_MONO_SRC_BLT,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_2(4, 5), 8, NULL},

	{"XY_MONO_PAT_FIXED_BLT", OP_XY_MONO_PAT_FIXED_BLT, F_LEN_VAR, R_BCS,
		D_ALL, ADDR_FIX_1(4), 8, NULL},

	{"XY_MONO_SRC_COPY_IMMEDIATE_BLT", OP_XY_MONO_SRC_COPY_IMMEDIATE_BLT,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_1(4), 8, NULL},

	{"XY_PAT_BLT_IMMEDIATE", OP_XY_PAT_BLT_IMMEDIATE, F_LEN_VAR, R_BCS,
		D_ALL, ADDR_FIX_1(4), 8, NULL},

	{"XY_SRC_COPY_CHROMA_BLT", OP_XY_SRC_COPY_CHROMA_BLT, F_LEN_VAR, R_BCS,
		D_ALL, ADDR_FIX_2(4, 7), 8, NULL},

	{"XY_FULL_IMMEDIATE_PATTERN_BLT", OP_XY_FULL_IMMEDIATE_PATTERN_BLT,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_2(4, 7), 8, NULL},

	{"XY_FULL_MONO_SRC_IMMEDIATE_PATTERN_BLT",
		OP_XY_FULL_MONO_SRC_IMMEDIATE_PATTERN_BLT,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_2(4, 5), 8, NULL},

	{"XY_PAT_CHROMA_BLT", OP_XY_PAT_CHROMA_BLT, F_LEN_VAR, R_BCS, D_ALL,
		ADDR_FIX_2(4, 5), 8, NULL},

	{"XY_PAT_CHROMA_BLT_IMMEDIATE", OP_XY_PAT_CHROMA_BLT_IMMEDIATE,
		F_LEN_VAR, R_BCS, D_ALL, ADDR_FIX_1(4), 8, NULL},

	{"3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP",
		OP_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_VIEWPORT_STATE_POINTERS_CC",
		OP_3DSTATE_VIEWPORT_STATE_POINTERS_CC,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BLEND_STATE_POINTERS",
		OP_3DSTATE_BLEND_STATE_POINTERS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DEPTH_STENCIL_STATE_POINTERS",
		OP_3DSTATE_DEPTH_STENCIL_STATE_POINTERS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_POINTERS_VS",
		OP_3DSTATE_BINDING_TABLE_POINTERS_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_POINTERS_HS",
		OP_3DSTATE_BINDING_TABLE_POINTERS_HS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_POINTERS_DS",
		OP_3DSTATE_BINDING_TABLE_POINTERS_DS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_POINTERS_GS",
		OP_3DSTATE_BINDING_TABLE_POINTERS_GS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_POINTERS_PS",
		OP_3DSTATE_BINDING_TABLE_POINTERS_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_STATE_POINTERS_VS",
		OP_3DSTATE_SAMPLER_STATE_POINTERS_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_STATE_POINTERS_HS",
		OP_3DSTATE_SAMPLER_STATE_POINTERS_HS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_STATE_POINTERS_DS",
		OP_3DSTATE_SAMPLER_STATE_POINTERS_DS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_STATE_POINTERS_GS",
		OP_3DSTATE_SAMPLER_STATE_POINTERS_GS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_STATE_POINTERS_PS",
		OP_3DSTATE_SAMPLER_STATE_POINTERS_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_URB_VS", OP_3DSTATE_URB_VS, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"3DSTATE_URB_HS", OP_3DSTATE_URB_HS, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"3DSTATE_URB_DS", OP_3DSTATE_URB_DS, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"3DSTATE_URB_GS", OP_3DSTATE_URB_GS, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"3DSTATE_GATHER_CONSTANT_VS", OP_3DSTATE_GATHER_CONSTANT_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_GATHER_CONSTANT_GS", OP_3DSTATE_GATHER_CONSTANT_GS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_GATHER_CONSTANT_HS", OP_3DSTATE_GATHER_CONSTANT_HS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_GATHER_CONSTANT_DS", OP_3DSTATE_GATHER_CONSTANT_DS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_GATHER_CONSTANT_PS", OP_3DSTATE_GATHER_CONSTANT_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_CONSTANTF_VS", OP_3DSTATE_DX9_CONSTANTF_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 11, NULL},

	{"3DSTATE_DX9_CONSTANTF_PS", OP_3DSTATE_DX9_CONSTANTF_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 11, NULL},

	{"3DSTATE_DX9_CONSTANTI_VS", OP_3DSTATE_DX9_CONSTANTI_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_CONSTANTI_PS", OP_3DSTATE_DX9_CONSTANTI_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_CONSTANTB_VS", OP_3DSTATE_DX9_CONSTANTB_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_CONSTANTB_PS", OP_3DSTATE_DX9_CONSTANTB_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_LOCAL_VALID_VS", OP_3DSTATE_DX9_LOCAL_VALID_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_LOCAL_VALID_PS", OP_3DSTATE_DX9_LOCAL_VALID_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_GENERATE_ACTIVE_VS", OP_3DSTATE_DX9_GENERATE_ACTIVE_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DX9_GENERATE_ACTIVE_PS", OP_3DSTATE_DX9_GENERATE_ACTIVE_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_BINDING_TABLE_EDIT_VS", OP_3DSTATE_BINDING_TABLE_EDIT_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 9, NULL},

	{"3DSTATE_BINDING_TABLE_EDIT_GS", OP_3DSTATE_BINDING_TABLE_EDIT_GS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 9, NULL},

	{"3DSTATE_BINDING_TABLE_EDIT_HS", OP_3DSTATE_BINDING_TABLE_EDIT_HS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 9, NULL},

	{"3DSTATE_BINDING_TABLE_EDIT_DS", OP_3DSTATE_BINDING_TABLE_EDIT_DS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 9, NULL},

	{"3DSTATE_BINDING_TABLE_EDIT_PS", OP_3DSTATE_BINDING_TABLE_EDIT_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 9, NULL},

	{"3DSTATE_VF_INSTANCING", OP_3DSTATE_VF_INSTANCING, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_VF_SGVS", OP_3DSTATE_VF_SGVS, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0, 8,
		NULL},

	{"3DSTATE_VF_TOPOLOGY", OP_3DSTATE_VF_TOPOLOGY, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_WM_CHROMAKEY", OP_3DSTATE_WM_CHROMAKEY, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_PS_BLEND", OP_3DSTATE_PS_BLEND, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0,
		8, NULL},

	{"3DSTATE_WM_DEPTH_STENCIL", OP_3DSTATE_WM_DEPTH_STENCIL, F_LEN_VAR,
		R_RCS, D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_PS_EXTRA", OP_3DSTATE_PS_EXTRA, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0,
		8, NULL},

	{"3DSTATE_RASTER", OP_3DSTATE_RASTER, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0, 8,
		NULL},

	{"3DSTATE_SBE_SWIZ", OP_3DSTATE_SBE_SWIZ, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0, 8,
		NULL},

	{"3DSTATE_WM_HZ_OP", OP_3DSTATE_WM_HZ_OP, F_LEN_VAR, R_RCS, D_BDW_PLUS, 0, 8,
		NULL},

	{"3DSTATE_VERTEX_BUFFERS", OP_3DSTATE_VERTEX_BUFFERS, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_VERTEX_ELEMENTS", OP_3DSTATE_VERTEX_ELEMENTS, F_LEN_VAR,
		R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_INDEX_BUFFER", OP_3DSTATE_INDEX_BUFFER, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, ADDR_FIX_1(2), 8, NULL},

	{"3DSTATE_VF_STATISTICS", OP_3DSTATE_VF_STATISTICS, F_LEN_CONST,
		R_RCS, D_ALL, 0, 1, NULL},

	{"3DSTATE_VF", OP_3DSTATE_VF, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_CC_STATE_POINTERS", OP_3DSTATE_CC_STATE_POINTERS, F_LEN_VAR,
		R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SCISSOR_STATE_POINTERS", OP_3DSTATE_SCISSOR_STATE_POINTERS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_GS", OP_3DSTATE_GS, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_CLIP", OP_3DSTATE_CLIP, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_WM", OP_3DSTATE_WM, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_CONSTANT_GS", OP_3DSTATE_CONSTANT_GS, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_CONSTANT_PS", OP_3DSTATE_CONSTANT_PS, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_SAMPLE_MASK", OP_3DSTATE_SAMPLE_MASK, F_LEN_VAR, R_RCS,
		D_ALL, 0, 8, NULL},

	{"3DSTATE_CONSTANT_HS", OP_3DSTATE_CONSTANT_HS, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_CONSTANT_DS", OP_3DSTATE_CONSTANT_DS, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_HS", OP_3DSTATE_HS, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_TE", OP_3DSTATE_TE, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DS", OP_3DSTATE_DS, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_STREAMOUT", OP_3DSTATE_STREAMOUT, F_LEN_VAR, R_RCS,
		D_ALL, 0, 8, NULL},

	{"3DSTATE_SBE", OP_3DSTATE_SBE, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PS", OP_3DSTATE_PS, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_DRAWING_RECTANGLE", OP_3DSTATE_DRAWING_RECTANGLE, F_LEN_VAR,
		R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_PALETTE_LOAD0", OP_3DSTATE_SAMPLER_PALETTE_LOAD0,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_CHROMA_KEY", OP_3DSTATE_CHROMA_KEY, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"3DSTATE_DEPTH_BUFFER", OP_3DSTATE_DEPTH_BUFFER, F_LEN_VAR, R_RCS,
		D_ALL, ADDR_FIX_1(2), 8, NULL},

	{"3DSTATE_POLY_STIPPLE_OFFSET", OP_3DSTATE_POLY_STIPPLE_OFFSET,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_POLY_STIPPLE_PATTERN", OP_3DSTATE_POLY_STIPPLE_PATTERN,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_LINE_STIPPLE", OP_3DSTATE_LINE_STIPPLE, F_LEN_VAR, R_RCS,
		D_ALL, 0, 8, NULL},

	{"3DSTATE_AA_LINE_PARAMS", OP_3DSTATE_AA_LINE_PARAMS, F_LEN_VAR, R_RCS,
		D_ALL, 0, 8, NULL},

	{"3DSTATE_GS_SVB_INDEX", OP_3DSTATE_GS_SVB_INDEX, F_LEN_VAR, R_RCS,
		D_ALL, 0, 8, NULL},

	{"3DSTATE_SAMPLER_PALETTE_LOAD1", OP_3DSTATE_SAMPLER_PALETTE_LOAD1,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_MULTISAMPLE", OP_3DSTATE_MULTISAMPLE_BDW, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"3DSTATE_STENCIL_BUFFER", OP_3DSTATE_STENCIL_BUFFER, F_LEN_VAR, R_RCS,
		D_ALL, ADDR_FIX_1(2), 8, NULL},

	{"3DSTATE_HIER_DEPTH_BUFFER", OP_3DSTATE_HIER_DEPTH_BUFFER, F_LEN_VAR,
		R_RCS, D_ALL, ADDR_FIX_1(2), 8, NULL},

	{"3DSTATE_CLEAR_PARAMS", OP_3DSTATE_CLEAR_PARAMS, F_LEN_VAR,
		R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PUSH_CONSTANT_ALLOC_VS", OP_3DSTATE_PUSH_CONSTANT_ALLOC_VS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PUSH_CONSTANT_ALLOC_HS", OP_3DSTATE_PUSH_CONSTANT_ALLOC_HS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PUSH_CONSTANT_ALLOC_DS", OP_3DSTATE_PUSH_CONSTANT_ALLOC_DS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PUSH_CONSTANT_ALLOC_GS", OP_3DSTATE_PUSH_CONSTANT_ALLOC_GS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_PUSH_CONSTANT_ALLOC_PS", OP_3DSTATE_PUSH_CONSTANT_ALLOC_PS,
		F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_MONOFILTER_SIZE", OP_3DSTATE_MONOFILTER_SIZE, F_LEN_VAR,
		R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SO_DECL_LIST", OP_3DSTATE_SO_DECL_LIST, F_LEN_VAR, R_RCS,
		D_ALL, 0, 9, NULL},

	{"3DSTATE_SO_BUFFER", OP_3DSTATE_SO_BUFFER, F_LEN_VAR, R_RCS, D_BDW_PLUS,
		ADDR_FIX_2(2, 4), 8, NULL},

	{"3DSTATE_BINDING_TABLE_POOL_ALLOC",
		OP_3DSTATE_BINDING_TABLE_POOL_ALLOC,
		F_LEN_VAR, R_RCS, D_BDW_PLUS, ADDR_FIX_1(1), 8, NULL},

	{"3DSTATE_GATHER_POOL_ALLOC", OP_3DSTATE_GATHER_POOL_ALLOC,
		F_LEN_VAR, R_RCS, D_BDW_PLUS, ADDR_FIX_1(1), 8, NULL},

	{"3DSTATE_DX9_CONSTANT_BUFFER_POOL_ALLOC",
		OP_3DSTATE_DX9_CONSTANT_BUFFER_POOL_ALLOC,
		F_LEN_VAR, R_RCS, D_BDW_PLUS, ADDR_FIX_1(1), 8, NULL},

	{"3DSTATE_SAMPLE_PATTERN", OP_3DSTATE_SAMPLE_PATTERN, F_LEN_VAR, R_RCS,
		D_BDW_PLUS, 0, 8, NULL},

	{"PIPE_CONTROL", OP_PIPE_CONTROL, F_LEN_VAR, R_RCS, D_ALL,
		ADDR_FIX_1(2), 8, cmd_handler_pipe_control},

	{"3DPRIMITIVE", OP_3DPRIMITIVE, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"PIPELINE_SELECT", OP_PIPELINE_SELECT, F_LEN_CONST, R_RCS, D_ALL, 0,
		1, NULL},

	{"STATE_PREFETCH", OP_STATE_PREFETCH, F_LEN_VAR, R_RCS, D_ALL,
		ADDR_FIX_1(1), 8, NULL},

	{"STATE_SIP", OP_STATE_SIP, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"STATE_BASE_ADDRESS", OP_STATE_BASE_ADDRESS, F_LEN_VAR, R_RCS, D_BDW_PLUS,
		ADDR_FIX_5(1, 3, 4, 5, 6), 8, NULL},

	{"OP_3D_MEDIA_0_1_4", OP_3D_MEDIA_0_1_4, F_LEN_VAR, R_RCS, D_ALL,
		ADDR_FIX_1(1), 8, NULL},

	{"OP_SWTESS_BASE_ADDRESS", OP_SWTESS_BASE_ADDRESS,
		F_LEN_VAR, R_RCS, D_ALL, ADDR_FIX_2(1, 2), 3, NULL},

	{"3DSTATE_VS", OP_3DSTATE_VS, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_SF", OP_3DSTATE_SF, F_LEN_VAR, R_RCS, D_ALL, 0, 8, NULL},

	{"3DSTATE_CONSTANT_VS", OP_3DSTATE_CONSTANT_VS, F_LEN_VAR, R_RCS, D_BDW_PLUS,
		0, 8, NULL},

	{"3DSTATE_COMPONENT_PACKING", OP_3DSTATE_COMPONENT_PACKING, F_LEN_VAR, R_RCS,
		D_SKL_PLUS, 0, 8, NULL},

	{"MEDIA_INTERFACE_DESCRIPTOR_LOAD", OP_MEDIA_INTERFACE_DESCRIPTOR_LOAD,
		F_LEN_VAR, R_RCS, D_ALL, 0, 16, NULL},

	{"MEDIA_GATEWAY_STATE", OP_MEDIA_GATEWAY_STATE, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"MEDIA_STATE_FLUSH", OP_MEDIA_STATE_FLUSH, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"MEDIA_POOL_STATE", OP_MEDIA_POOL_STATE, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"MEDIA_OBJECT", OP_MEDIA_OBJECT, F_LEN_VAR, R_RCS, D_ALL, 0, 16, NULL},

	{"MEDIA_CURBE_LOAD", OP_MEDIA_CURBE_LOAD, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"MEDIA_OBJECT_PRT", OP_MEDIA_OBJECT_PRT, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"MEDIA_OBJECT_WALKER", OP_MEDIA_OBJECT_WALKER, F_LEN_VAR, R_RCS, D_ALL,
		0, 16, NULL},

	{"GPGPU_WALKER", OP_GPGPU_WALKER, F_LEN_VAR, R_RCS, D_ALL,
		0, 8, NULL},

	{"MEDIA_VFE_STATE", OP_MEDIA_VFE_STATE, F_LEN_VAR, R_RCS, D_ALL, 0, 16,
		NULL},

	{"3DSTATE_VF_STATISTICS_GM45", OP_3DSTATE_VF_STATISTICS_GM45,
		F_LEN_CONST, R_ALL, D_ALL, 0, 1, NULL},

	{"MFX_PIPE_MODE_SELECT", OP_MFX_PIPE_MODE_SELECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_SURFACE_STATE", OP_MFX_SURFACE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_PIPE_BUF_ADDR_STATE", OP_MFX_PIPE_BUF_ADDR_STATE, F_LEN_VAR,
		R_VCS, D_BDW_PLUS, 0, 12, NULL},

	{"MFX_IND_OBJ_BASE_ADDR_STATE", OP_MFX_IND_OBJ_BASE_ADDR_STATE,
		F_LEN_VAR, R_VCS, D_BDW_PLUS, 0, 12, NULL},

	{"MFX_BSP_BUF_BASE_ADDR_STATE", OP_MFX_BSP_BUF_BASE_ADDR_STATE,
		F_LEN_VAR, R_VCS, D_BDW_PLUS, ADDR_FIX_3(1, 3, 5), 12, NULL},

	{"OP_2_0_0_5", OP_2_0_0_5, F_LEN_VAR, R_VCS, D_BDW_PLUS, 0, 12, NULL},

	{"MFX_STATE_POINTER", OP_MFX_STATE_POINTER, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_QM_STATE", OP_MFX_QM_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_FQM_STATE", OP_MFX_FQM_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_PAK_INSERT_OBJECT", OP_MFX_PAK_INSERT_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_STITCH_OBJECT", OP_MFX_STITCH_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_IT_OBJECT", OP_MFD_IT_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_WAIT", OP_MFX_WAIT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 6, NULL},

	{"MFX_AVC_IMG_STATE", OP_MFX_AVC_IMG_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_AVC_QM_STATE", OP_MFX_AVC_QM_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_AVC_DIRECTMODE_STATE", OP_MFX_AVC_DIRECTMODE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_AVC_SLICE_STATE", OP_MFX_AVC_SLICE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_AVC_REF_IDX_STATE", OP_MFX_AVC_REF_IDX_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_AVC_WEIGHTOFFSET_STATE", OP_MFX_AVC_WEIGHTOFFSET_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_AVC_PICID_STATE", OP_MFD_AVC_PICID_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},
	{"MFD_AVC_DPB_STATE", OP_MFD_AVC_DPB_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_AVC_BSD_OBJECT", OP_MFD_AVC_BSD_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_AVC_SLICEADDR", OP_MFD_AVC_SLICEADDR, F_LEN_VAR,
		R_VCS, D_ALL, ADDR_FIX_1(2), 12, NULL},

	{"MFC_AVC_PAK_OBJECT", OP_MFC_AVC_PAK_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_VC1_PRED_PIPE_STATE", OP_MFX_VC1_PRED_PIPE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_VC1_DIRECTMODE_STATE", OP_MFX_VC1_DIRECTMODE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_VC1_SHORT_PIC_STATE", OP_MFD_VC1_SHORT_PIC_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_VC1_LONG_PIC_STATE", OP_MFD_VC1_LONG_PIC_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_VC1_BSD_OBJECT", OP_MFD_VC1_BSD_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFC_MPEG2_SLICEGROUP_STATE", OP_MFC_MPEG2_SLICEGROUP_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFC_MPEG2_PAK_OBJECT", OP_MFC_MPEG2_PAK_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_MPEG2_PIC_STATE", OP_MFX_MPEG2_PIC_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_MPEG2_QM_STATE", OP_MFX_MPEG2_QM_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_MPEG2_BSD_OBJECT", OP_MFD_MPEG2_BSD_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_2_6_0_0", OP_MFX_2_6_0_0, F_LEN_VAR, R_VCS, D_ALL,
		0, 16, NULL},

	{"MFX_2_6_0_9", OP_MFX_2_6_0_9, F_LEN_VAR, R_VCS, D_ALL, 0, 16, NULL},

	{"MFX_2_6_0_8", OP_MFX_2_6_0_8, F_LEN_VAR, R_VCS, D_ALL, 0, 16, NULL},

	{"MFX_JPEG_PIC_STATE", OP_MFX_JPEG_PIC_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFX_JPEG_HUFF_TABLE_STATE", OP_MFX_JPEG_HUFF_TABLE_STATE, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"MFD_JPEG_BSD_OBJECT", OP_MFD_JPEG_BSD_OBJECT, F_LEN_VAR,
		R_VCS, D_ALL, 0, 12, NULL},

	{"VEBOX_STATE", OP_VEB_STATE, F_LEN_VAR, R_VECS, D_ALL, 0, 12, NULL},

	{"VEBOX_SURFACE_STATE", OP_VEB_SURFACE_STATE, F_LEN_VAR, R_VECS, D_ALL,
		0, 12, NULL},

	{"VEB_DI_IECP", OP_VEB_DNDI_IECP_STATE, F_LEN_VAR, R_VECS, D_BDW_PLUS,
		0, 12, NULL},
};

static void add_cmd_entry(struct intel_gvt *gvt, struct cmd_entry *e)
{
	hash_add(gvt->cmd_table, &e->hlist, e->info->opcode);
}

/* call the cmd handler, and advance ip */
static int cmd_parser_exec(struct parser_exec_state *s)
{
	struct intel_vgpu *vgpu = s->vgpu;
	const struct cmd_info *info;
	u32 cmd;
	int ret = 0;

	cmd = cmd_val(s, 0);

	/* fastpath for MI_NOOP */
	if (cmd == MI_NOOP)
		info = &cmd_info[mi_noop_index];
	else
		info = get_cmd_info(s->vgpu->gvt, cmd, s->engine);

	if (info == NULL) {
		gvt_vgpu_err("unknown cmd 0x%x, opcode=0x%x, addr_type=%s, ring %s, workload=%p\n",
			     cmd, get_opcode(cmd, s->engine),
			     repr_addr_type(s->buf_addr_type),
			     s->engine->name, s->workload);
		return -EBADRQC;
	}

	s->info = info;

	trace_gvt_command(vgpu->id, s->engine->id, s->ip_gma, s->ip_va,
			  cmd_length(s), s->buf_type, s->buf_addr_type,
			  s->workload, info->name);

	if ((info->flag & F_LEN_MASK) == F_LEN_VAR_FIXED) {
		ret = gvt_check_valid_cmd_length(cmd_length(s),
						 info->valid_len);
		if (ret)
			return ret;
	}

	if (info->handler) {
		ret = info->handler(s);
		if (ret < 0) {
			gvt_vgpu_err("%s handler error\n", info->name);
			return ret;
		}
	}

	if (!(info->flag & F_IP_ADVANCE_CUSTOM)) {
		ret = cmd_advance_default(s);
		if (ret) {
			gvt_vgpu_err("%s IP advance error\n", info->name);
			return ret;
		}
	}
	return 0;
}

static inline bool gma_out_of_range(unsigned long gma,
		unsigned long gma_head, unsigned int gma_tail)
{
	if (gma_tail >= gma_head)
		return (gma < gma_head) || (gma > gma_tail);
	else
		return (gma > gma_tail) && (gma < gma_head);
}

/* Keep the consistent return type, e.g EBADRQC for unknown
 * cmd, EFAULT for invalid address, EPERM for nonpriv. later
 * works as the input of VM healthy status.
 */
static int command_scan(struct parser_exec_state *s,
		unsigned long rb_head, unsigned long rb_tail,
		unsigned long rb_start, unsigned long rb_len)
{

	unsigned long gma_head, gma_tail, gma_bottom;
	int ret = 0;
	struct intel_vgpu *vgpu = s->vgpu;

	gma_head = rb_start + rb_head;
	gma_tail = rb_start + rb_tail;
	gma_bottom = rb_start +  rb_len;

	while (s->ip_gma != gma_tail) {
		if (s->buf_type == RING_BUFFER_INSTRUCTION ||
				s->buf_type == RING_BUFFER_CTX) {
			if (!(s->ip_gma >= rb_start) ||
				!(s->ip_gma < gma_bottom)) {
				gvt_vgpu_err("ip_gma %lx out of ring scope."
					"(base:0x%lx, bottom: 0x%lx)\n",
					s->ip_gma, rb_start,
					gma_bottom);
				parser_exec_state_dump(s);
				return -EFAULT;
			}
			if (gma_out_of_range(s->ip_gma, gma_head, gma_tail)) {
				gvt_vgpu_err("ip_gma %lx out of range."
					"base 0x%lx head 0x%lx tail 0x%lx\n",
					s->ip_gma, rb_start,
					rb_head, rb_tail);
				parser_exec_state_dump(s);
				break;
			}
		}
		ret = cmd_parser_exec(s);
		if (ret) {
			gvt_vgpu_err("cmd parser error\n");
			parser_exec_state_dump(s);
			break;
		}
	}

	return ret;
}

static int scan_workload(struct intel_vgpu_workload *workload)
{
	unsigned long gma_head, gma_tail;
	struct parser_exec_state s;
	int ret = 0;

	/* ring base is page aligned */
	if (WARN_ON(!IS_ALIGNED(workload->rb_start, I915_GTT_PAGE_SIZE)))
		return -EINVAL;

	gma_head = workload->rb_start + workload->rb_head;
	gma_tail = workload->rb_start + workload->rb_tail;

	s.buf_type = RING_BUFFER_INSTRUCTION;
	s.buf_addr_type = GTT_BUFFER;
	s.vgpu = workload->vgpu;
	s.engine = workload->engine;
	s.ring_start = workload->rb_start;
	s.ring_size = _RING_CTL_BUF_SIZE(workload->rb_ctl);
	s.ring_head = gma_head;
	s.ring_tail = gma_tail;
	s.rb_va = workload->shadow_ring_buffer_va;
	s.workload = workload;
	s.is_ctx_wa = false;

	if (bypass_scan_mask & workload->engine->mask || gma_head == gma_tail)
		return 0;

	ret = ip_gma_set(&s, gma_head);
	if (ret)
		goto out;

	ret = command_scan(&s, workload->rb_head, workload->rb_tail,
		workload->rb_start, _RING_CTL_BUF_SIZE(workload->rb_ctl));

out:
	return ret;
}

static int scan_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{

	unsigned long gma_head, gma_tail, ring_size, ring_tail;
	struct parser_exec_state s;
	int ret = 0;
	struct intel_vgpu_workload *workload = container_of(wa_ctx,
				struct intel_vgpu_workload,
				wa_ctx);

	/* ring base is page aligned */
	if (WARN_ON(!IS_ALIGNED(wa_ctx->indirect_ctx.guest_gma,
					I915_GTT_PAGE_SIZE)))
		return -EINVAL;

	ring_tail = wa_ctx->indirect_ctx.size + 3 * sizeof(u32);
	ring_size = round_up(wa_ctx->indirect_ctx.size + CACHELINE_BYTES,
			PAGE_SIZE);
	gma_head = wa_ctx->indirect_ctx.guest_gma;
	gma_tail = wa_ctx->indirect_ctx.guest_gma + ring_tail;

	s.buf_type = RING_BUFFER_INSTRUCTION;
	s.buf_addr_type = GTT_BUFFER;
	s.vgpu = workload->vgpu;
	s.engine = workload->engine;
	s.ring_start = wa_ctx->indirect_ctx.guest_gma;
	s.ring_size = ring_size;
	s.ring_head = gma_head;
	s.ring_tail = gma_tail;
	s.rb_va = wa_ctx->indirect_ctx.shadow_va;
	s.workload = workload;
	s.is_ctx_wa = true;

	ret = ip_gma_set(&s, gma_head);
	if (ret)
		goto out;

	ret = command_scan(&s, 0, ring_tail,
		wa_ctx->indirect_ctx.guest_gma, ring_size);
out:
	return ret;
}

static int shadow_workload_ring_buffer(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	struct intel_vgpu_submission *s = &vgpu->submission;
	unsigned long gma_head, gma_tail, gma_top, guest_rb_size;
	void *shadow_ring_buffer_va;
	int ret;

	guest_rb_size = _RING_CTL_BUF_SIZE(workload->rb_ctl);

	/* calculate workload ring buffer size */
	workload->rb_len = (workload->rb_tail + guest_rb_size -
			workload->rb_head) % guest_rb_size;

	gma_head = workload->rb_start + workload->rb_head;
	gma_tail = workload->rb_start + workload->rb_tail;
	gma_top = workload->rb_start + guest_rb_size;

	if (workload->rb_len > s->ring_scan_buffer_size[workload->engine->id]) {
		void *p;

		/* realloc the new ring buffer if needed */
		p = krealloc(s->ring_scan_buffer[workload->engine->id],
			     workload->rb_len, GFP_KERNEL);
		if (!p) {
			gvt_vgpu_err("fail to re-alloc ring scan buffer\n");
			return -ENOMEM;
		}
		s->ring_scan_buffer[workload->engine->id] = p;
		s->ring_scan_buffer_size[workload->engine->id] = workload->rb_len;
	}

	shadow_ring_buffer_va = s->ring_scan_buffer[workload->engine->id];

	/* get shadow ring buffer va */
	workload->shadow_ring_buffer_va = shadow_ring_buffer_va;

	/* head > tail --> copy head <-> top */
	if (gma_head > gma_tail) {
		ret = copy_gma_to_hva(vgpu, vgpu->gtt.ggtt_mm,
				      gma_head, gma_top, shadow_ring_buffer_va);
		if (ret < 0) {
			gvt_vgpu_err("fail to copy guest ring buffer\n");
			return ret;
		}
		shadow_ring_buffer_va += ret;
		gma_head = workload->rb_start;
	}

	/* copy head or start <-> tail */
	ret = copy_gma_to_hva(vgpu, vgpu->gtt.ggtt_mm, gma_head, gma_tail,
				shadow_ring_buffer_va);
	if (ret < 0) {
		gvt_vgpu_err("fail to copy guest ring buffer\n");
		return ret;
	}
	return 0;
}

int intel_gvt_scan_and_shadow_ringbuffer(struct intel_vgpu_workload *workload)
{
	int ret;
	struct intel_vgpu *vgpu = workload->vgpu;

	ret = shadow_workload_ring_buffer(workload);
	if (ret) {
		gvt_vgpu_err("fail to shadow workload ring_buffer\n");
		return ret;
	}

	ret = scan_workload(workload);
	if (ret) {
		gvt_vgpu_err("scan workload error\n");
		return ret;
	}
	return 0;
}

static int shadow_indirect_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	int ctx_size = wa_ctx->indirect_ctx.size;
	unsigned long guest_gma = wa_ctx->indirect_ctx.guest_gma;
	struct intel_vgpu_workload *workload = container_of(wa_ctx,
					struct intel_vgpu_workload,
					wa_ctx);
	struct intel_vgpu *vgpu = workload->vgpu;
	struct drm_i915_gem_object *obj;
	int ret = 0;
	void *map;

	obj = i915_gem_object_create_shmem(workload->engine->i915,
					   roundup(ctx_size + CACHELINE_BYTES,
						   PAGE_SIZE));
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	/* get the va of the shadow batch buffer */
	map = i915_gem_object_pin_map(obj, I915_MAP_WB);
	if (IS_ERR(map)) {
		gvt_vgpu_err("failed to vmap shadow indirect ctx\n");
		ret = PTR_ERR(map);
		goto put_obj;
	}

	i915_gem_object_lock(obj, NULL);
	ret = i915_gem_object_set_to_cpu_domain(obj, false);
	i915_gem_object_unlock(obj);
	if (ret) {
		gvt_vgpu_err("failed to set shadow indirect ctx to CPU\n");
		goto unmap_src;
	}

	ret = copy_gma_to_hva(workload->vgpu,
				workload->vgpu->gtt.ggtt_mm,
				guest_gma, guest_gma + ctx_size,
				map);
	if (ret < 0) {
		gvt_vgpu_err("fail to copy guest indirect ctx\n");
		goto unmap_src;
	}

	wa_ctx->indirect_ctx.obj = obj;
	wa_ctx->indirect_ctx.shadow_va = map;
	return 0;

unmap_src:
	i915_gem_object_unpin_map(obj);
put_obj:
	i915_gem_object_put(obj);
	return ret;
}

static int combine_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	u32 per_ctx_start[CACHELINE_DWORDS] = {};
	unsigned char *bb_start_sva;

	if (!wa_ctx->per_ctx.valid)
		return 0;

	per_ctx_start[0] = 0x18800001;
	per_ctx_start[1] = wa_ctx->per_ctx.guest_gma;

	bb_start_sva = (unsigned char *)wa_ctx->indirect_ctx.shadow_va +
				wa_ctx->indirect_ctx.size;

	memcpy(bb_start_sva, per_ctx_start, CACHELINE_BYTES);

	return 0;
}

int intel_gvt_scan_and_shadow_wa_ctx(struct intel_shadow_wa_ctx *wa_ctx)
{
	int ret;
	struct intel_vgpu_workload *workload = container_of(wa_ctx,
					struct intel_vgpu_workload,
					wa_ctx);
	struct intel_vgpu *vgpu = workload->vgpu;

	if (wa_ctx->indirect_ctx.size == 0)
		return 0;

	ret = shadow_indirect_ctx(wa_ctx);
	if (ret) {
		gvt_vgpu_err("fail to shadow indirect ctx\n");
		return ret;
	}

	combine_wa_ctx(wa_ctx);

	ret = scan_wa_ctx(wa_ctx);
	if (ret) {
		gvt_vgpu_err("scan wa ctx error\n");
		return ret;
	}

	return 0;
}

/* generate dummy contexts by sending empty requests to HW, and let
 * the HW to fill Engine Contexts. This dummy contexts are used for
 * initialization purpose (update reg whitelist), so referred to as
 * init context here
 */
void intel_gvt_update_reg_whitelist(struct intel_vgpu *vgpu)
{
	const unsigned long start = LRC_STATE_PN * PAGE_SIZE;
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	if (gvt->is_reg_whitelist_updated)
		return;

	/* scan init ctx to update cmd accessible list */
	for_each_engine(engine, gvt->gt, id) {
		struct parser_exec_state s;
		void *vaddr;
		int ret;

		if (!engine->default_state)
			continue;

		vaddr = shmem_pin_map(engine->default_state);
		if (!vaddr) {
			gvt_err("failed to map %s->default state\n",
				engine->name);
			return;
		}

		s.buf_type = RING_BUFFER_CTX;
		s.buf_addr_type = GTT_BUFFER;
		s.vgpu = vgpu;
		s.engine = engine;
		s.ring_start = 0;
		s.ring_size = engine->context_size - start;
		s.ring_head = 0;
		s.ring_tail = s.ring_size;
		s.rb_va = vaddr + start;
		s.workload = NULL;
		s.is_ctx_wa = false;
		s.is_init_ctx = true;

		/* skipping the first RING_CTX_SIZE(0x50) dwords */
		ret = ip_gma_set(&s, RING_CTX_SIZE);
		if (ret == 0) {
			ret = command_scan(&s, 0, s.ring_size, 0, s.ring_size);
			if (ret)
				gvt_err("Scan init ctx error\n");
		}

		shmem_unpin_map(engine->default_state, vaddr);
		if (ret)
			return;
	}

	gvt->is_reg_whitelist_updated = true;
}

int intel_gvt_scan_engine_context(struct intel_vgpu_workload *workload)
{
	struct intel_vgpu *vgpu = workload->vgpu;
	unsigned long gma_head, gma_tail, gma_start, ctx_size;
	struct parser_exec_state s;
	int ring_id = workload->engine->id;
	struct intel_context *ce = vgpu->submission.shadow[ring_id];
	int ret;

	GEM_BUG_ON(atomic_read(&ce->pin_count) < 0);

	ctx_size = workload->engine->context_size - PAGE_SIZE;

	/* Only ring contxt is loaded to HW for inhibit context, no need to
	 * scan engine context
	 */
	if (is_inhibit_context(ce))
		return 0;

	gma_start = i915_ggtt_offset(ce->state) + LRC_STATE_PN*PAGE_SIZE;
	gma_head = 0;
	gma_tail = ctx_size;

	s.buf_type = RING_BUFFER_CTX;
	s.buf_addr_type = GTT_BUFFER;
	s.vgpu = workload->vgpu;
	s.engine = workload->engine;
	s.ring_start = gma_start;
	s.ring_size = ctx_size;
	s.ring_head = gma_start + gma_head;
	s.ring_tail = gma_start + gma_tail;
	s.rb_va = ce->lrc_reg_state;
	s.workload = workload;
	s.is_ctx_wa = false;
	s.is_init_ctx = false;

	/* don't scan the first RING_CTX_SIZE(0x50) dwords, as it's ring
	 * context
	 */
	ret = ip_gma_set(&s, gma_start + gma_head + RING_CTX_SIZE);
	if (ret)
		goto out;

	ret = command_scan(&s, gma_head, gma_tail,
		gma_start, ctx_size);
out:
	if (ret)
		gvt_vgpu_err("scan shadow ctx error\n");

	return ret;
}

static int init_cmd_table(struct intel_gvt *gvt)
{
	unsigned int gen_type = intel_gvt_get_device_type(gvt);
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_info); i++) {
		struct cmd_entry *e;

		if (!(cmd_info[i].devices & gen_type))
			continue;

		e = kzalloc(sizeof(*e), GFP_KERNEL);
		if (!e)
			return -ENOMEM;

		e->info = &cmd_info[i];
		if (cmd_info[i].opcode == OP_MI_NOOP)
			mi_noop_index = i;

		INIT_HLIST_NODE(&e->hlist);
		add_cmd_entry(gvt, e);
		gvt_dbg_cmd("add %-30s op %04x flag %x devs %02x rings %02x\n",
			    e->info->name, e->info->opcode, e->info->flag,
			    e->info->devices, e->info->rings);
	}

	return 0;
}

static void clean_cmd_table(struct intel_gvt *gvt)
{
	struct hlist_node *tmp;
	struct cmd_entry *e;
	int i;

	hash_for_each_safe(gvt->cmd_table, i, tmp, e, hlist)
		kfree(e);

	hash_init(gvt->cmd_table);
}

void intel_gvt_clean_cmd_parser(struct intel_gvt *gvt)
{
	clean_cmd_table(gvt);
}

int intel_gvt_init_cmd_parser(struct intel_gvt *gvt)
{
	int ret;

	ret = init_cmd_table(gvt);
	if (ret) {
		intel_gvt_clean_cmd_parser(gvt);
		return ret;
	}
	return 0;
}
