/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <drm/drm_vblank.h>
#include <drm/radeon_drm.h>

#include "atom.h"
#include "clearstate_si.h"
#include "evergreen.h"
#include "r600.h"
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_audio.h"
#include "radeon_ucode.h"
#include "si_blit_shaders.h"
#include "si.h"
#include "sid.h"


MODULE_FIRMWARE("radeon/TAHITI_pfp.bin");
MODULE_FIRMWARE("radeon/TAHITI_me.bin");
MODULE_FIRMWARE("radeon/TAHITI_ce.bin");
MODULE_FIRMWARE("radeon/TAHITI_mc.bin");
MODULE_FIRMWARE("radeon/TAHITI_mc2.bin");
MODULE_FIRMWARE("radeon/TAHITI_rlc.bin");
MODULE_FIRMWARE("radeon/TAHITI_smc.bin");

MODULE_FIRMWARE("radeon/tahiti_pfp.bin");
MODULE_FIRMWARE("radeon/tahiti_me.bin");
MODULE_FIRMWARE("radeon/tahiti_ce.bin");
MODULE_FIRMWARE("radeon/tahiti_mc.bin");
MODULE_FIRMWARE("radeon/tahiti_rlc.bin");
MODULE_FIRMWARE("radeon/tahiti_smc.bin");

MODULE_FIRMWARE("radeon/PITCAIRN_pfp.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_me.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_ce.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_mc.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_mc2.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_rlc.bin");
MODULE_FIRMWARE("radeon/PITCAIRN_smc.bin");

MODULE_FIRMWARE("radeon/pitcairn_pfp.bin");
MODULE_FIRMWARE("radeon/pitcairn_me.bin");
MODULE_FIRMWARE("radeon/pitcairn_ce.bin");
MODULE_FIRMWARE("radeon/pitcairn_mc.bin");
MODULE_FIRMWARE("radeon/pitcairn_rlc.bin");
MODULE_FIRMWARE("radeon/pitcairn_smc.bin");
MODULE_FIRMWARE("radeon/pitcairn_k_smc.bin");

MODULE_FIRMWARE("radeon/VERDE_pfp.bin");
MODULE_FIRMWARE("radeon/VERDE_me.bin");
MODULE_FIRMWARE("radeon/VERDE_ce.bin");
MODULE_FIRMWARE("radeon/VERDE_mc.bin");
MODULE_FIRMWARE("radeon/VERDE_mc2.bin");
MODULE_FIRMWARE("radeon/VERDE_rlc.bin");
MODULE_FIRMWARE("radeon/VERDE_smc.bin");

MODULE_FIRMWARE("radeon/verde_pfp.bin");
MODULE_FIRMWARE("radeon/verde_me.bin");
MODULE_FIRMWARE("radeon/verde_ce.bin");
MODULE_FIRMWARE("radeon/verde_mc.bin");
MODULE_FIRMWARE("radeon/verde_rlc.bin");
MODULE_FIRMWARE("radeon/verde_smc.bin");
MODULE_FIRMWARE("radeon/verde_k_smc.bin");

MODULE_FIRMWARE("radeon/OLAND_pfp.bin");
MODULE_FIRMWARE("radeon/OLAND_me.bin");
MODULE_FIRMWARE("radeon/OLAND_ce.bin");
MODULE_FIRMWARE("radeon/OLAND_mc.bin");
MODULE_FIRMWARE("radeon/OLAND_mc2.bin");
MODULE_FIRMWARE("radeon/OLAND_rlc.bin");
MODULE_FIRMWARE("radeon/OLAND_smc.bin");

MODULE_FIRMWARE("radeon/oland_pfp.bin");
MODULE_FIRMWARE("radeon/oland_me.bin");
MODULE_FIRMWARE("radeon/oland_ce.bin");
MODULE_FIRMWARE("radeon/oland_mc.bin");
MODULE_FIRMWARE("radeon/oland_rlc.bin");
MODULE_FIRMWARE("radeon/oland_smc.bin");
MODULE_FIRMWARE("radeon/oland_k_smc.bin");

MODULE_FIRMWARE("radeon/HAINAN_pfp.bin");
MODULE_FIRMWARE("radeon/HAINAN_me.bin");
MODULE_FIRMWARE("radeon/HAINAN_ce.bin");
MODULE_FIRMWARE("radeon/HAINAN_mc.bin");
MODULE_FIRMWARE("radeon/HAINAN_mc2.bin");
MODULE_FIRMWARE("radeon/HAINAN_rlc.bin");
MODULE_FIRMWARE("radeon/HAINAN_smc.bin");

MODULE_FIRMWARE("radeon/hainan_pfp.bin");
MODULE_FIRMWARE("radeon/hainan_me.bin");
MODULE_FIRMWARE("radeon/hainan_ce.bin");
MODULE_FIRMWARE("radeon/hainan_mc.bin");
MODULE_FIRMWARE("radeon/hainan_rlc.bin");
MODULE_FIRMWARE("radeon/hainan_smc.bin");
MODULE_FIRMWARE("radeon/hainan_k_smc.bin");
MODULE_FIRMWARE("radeon/banks_k_2_smc.bin");

MODULE_FIRMWARE("radeon/si58_mc.bin");

static u32 si_get_cu_active_bitmap(struct radeon_device *rdev, u32 se, u32 sh);
static void si_pcie_gen3_enable(struct radeon_device *rdev);
static void si_program_aspm(struct radeon_device *rdev);
extern void sumo_rlc_fini(struct radeon_device *rdev);
extern int sumo_rlc_init(struct radeon_device *rdev);
static void si_enable_gui_idle_interrupt(struct radeon_device *rdev,
					 bool enable);
static void si_init_pg(struct radeon_device *rdev);
static void si_init_cg(struct radeon_device *rdev);
static void si_fini_pg(struct radeon_device *rdev);
static void si_fini_cg(struct radeon_device *rdev);
static void si_rlc_stop(struct radeon_device *rdev);

static const u32 crtc_offsets[] = {
	EVERGREEN_CRTC0_REGISTER_OFFSET,
	EVERGREEN_CRTC1_REGISTER_OFFSET,
	EVERGREEN_CRTC2_REGISTER_OFFSET,
	EVERGREEN_CRTC3_REGISTER_OFFSET,
	EVERGREEN_CRTC4_REGISTER_OFFSET,
	EVERGREEN_CRTC5_REGISTER_OFFSET
};

static const u32 si_disp_int_status[] = {
	DISP_INTERRUPT_STATUS,
	DISP_INTERRUPT_STATUS_CONTINUE,
	DISP_INTERRUPT_STATUS_CONTINUE2,
	DISP_INTERRUPT_STATUS_CONTINUE3,
	DISP_INTERRUPT_STATUS_CONTINUE4,
	DISP_INTERRUPT_STATUS_CONTINUE5
};

#define DC_HPDx_CONTROL(x)        (DC_HPD1_CONTROL     + (x * 0xc))
#define DC_HPDx_INT_CONTROL(x)    (DC_HPD1_INT_CONTROL + (x * 0xc))
#define DC_HPDx_INT_STATUS_REG(x) (DC_HPD1_INT_STATUS  + (x * 0xc))

static const u32 verde_rlc_save_restore_register_list[] = {
	(0x8000 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x98f4 >> 2),
	0x00000000,
	(0x8000 << 16) | (0xe80 >> 2),
	0x00000000,
	(0x8040 << 16) | (0xe80 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x8040 << 16) | (0x89bc >> 2),
	0x00000000,
	(0x8000 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x8c1c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x98f0 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xe7c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9148 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9148 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9150 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x897c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8d8c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac54 >> 2),
	0X00000000,
	0x3,
	(0x9c00 << 16) | (0x98f8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9910 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9914 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9918 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x991c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9920 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9924 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9928 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x992c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9930 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9934 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9938 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x993c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9940 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9944 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9948 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x994c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9950 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9954 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9958 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x995c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9960 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9964 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9968 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x996c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9970 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9974 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9978 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x997c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9980 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9984 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9988 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x998c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c00 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c04 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c08 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9b7c >> 2),
	0x00000000,
	(0x8000 << 16) | (0xe84 >> 2),
	0x00000000,
	(0x8040 << 16) | (0xe84 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x89c0 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x914c >> 2),
	0x00000000,
	(0x8040 << 16) | (0x914c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x8c20 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9354 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9354 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9060 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9364 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9100 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x913c >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e0 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e4 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e0 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e4 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x90e8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8bcc >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8b24 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88c4 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e50 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8c0c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e58 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8e5c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9508 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x950c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9494 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac0c >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac10 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xae00 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0xac08 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88d4 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88c8 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x88cc >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x89b0 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8b10 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x8a14 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9830 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9834 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9838 >> 2),
	0x00000000,
	(0x9c00 << 16) | (0x9a10 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8000 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8001 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8001 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8040 << 16) | (0x9874 >> 2),
	0x00000000,
	(0x8041 << 16) | (0x9870 >> 2),
	0x00000000,
	(0x8041 << 16) | (0x9874 >> 2),
	0x00000000,
	0x00000000
};

static const u32 tahiti_golden_rlc_registers[] = {
	0xc424, 0xffffffff, 0x00601005,
	0xc47c, 0xffffffff, 0x10104040,
	0xc488, 0xffffffff, 0x0100000a,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000f4,
	0xf4a8, 0xffffffff, 0x00000000
};

static const u32 tahiti_golden_registers[] = {
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x277c, 0x00000003, 0x000007ff,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x2a00126a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x00000200, 0x000002fb,
	0xac10, 0xffffffff, 0x0000543b,
	0xac0c, 0xffffffff, 0xa9210876,
	0x88d0, 0xffffffff, 0x000fff40,
	0x88d4, 0x0000001f, 0x00000010,
	0x1410, 0x20000000, 0x20fffed8,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 tahiti_golden_registers2[] = {
	0xc64, 0x00000001, 0x00000001
};

static const u32 pitcairn_golden_rlc_registers[] = {
	0xc424, 0xffffffff, 0x00601004,
	0xc47c, 0xffffffff, 0x10102020,
	0xc488, 0xffffffff, 0x01000020,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000a4
};

static const u32 pitcairn_golden_registers[] = {
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x2a00126a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f7,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x32761054,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 verde_golden_rlc_registers[] = {
	0xc424, 0xffffffff, 0x033f1005,
	0xc47c, 0xffffffff, 0x10808020,
	0xc488, 0xffffffff, 0x00800008,
	0xc314, 0xffffffff, 0x00001000,
	0xc30c, 0xffffffff, 0x80010014
};

static const u32 verde_golden_registers[] = {
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x240c, 0x000007ff, 0x00000000,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8a14, 0xf000001f, 0x00000007,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x28350, 0x3f3f3fff, 0x0000124a,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9100, 0x07ffffff, 0x03000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e88, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x8e84, 0x01ff1f3f, 0x00000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x00000003,
	0xac14, 0x000003ff, 0x00000003,
	0xac14, 0x000003ff, 0x00000003,
	0xac10, 0xffffffff, 0x00000000,
	0xac10, 0xffffffff, 0x00000000,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00001032,
	0xac0c, 0xffffffff, 0x00001032,
	0xac0c, 0xffffffff, 0x00001032,
	0x88d4, 0x0000001f, 0x00000010,
	0x88d4, 0x0000001f, 0x00000010,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 oland_golden_rlc_registers[] = {
	0xc424, 0xffffffff, 0x00601005,
	0xc47c, 0xffffffff, 0x10104040,
	0xc488, 0xffffffff, 0x0100000a,
	0xc314, 0xffffffff, 0x00000800,
	0xc30c, 0xffffffff, 0x800000f4
};

static const u32 oland_golden_registers[] = {
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xc78, 0x00000080, 0x00000000,
	0xd030, 0x000300c0, 0x00800040,
	0xd830, 0x000300c0, 0x00800040,
	0x5bb0, 0x000000f0, 0x00000070,
	0x5bc0, 0x00200000, 0x50100000,
	0x7030, 0x31000311, 0x00000011,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x00000082,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x07ffffff, 0x03000000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f3,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00003210,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 hainan_golden_registers[] = {
	0x9a10, 0x00010000, 0x00018208,
	0x9830, 0xffffffff, 0x00000000,
	0x9834, 0xf00fffff, 0x00000400,
	0x9838, 0x0002021c, 0x00020200,
	0xd0c0, 0xff000fff, 0x00000100,
	0xd030, 0x000300c0, 0x00800040,
	0xd8c0, 0xff000fff, 0x00000100,
	0xd830, 0x000300c0, 0x00800040,
	0x2ae4, 0x00073ffe, 0x000022a2,
	0x240c, 0x000007ff, 0x00000000,
	0x8a14, 0xf000001f, 0x00000007,
	0x8b24, 0xffffffff, 0x00ffffff,
	0x8b10, 0x0000ff0f, 0x00000000,
	0x28a4c, 0x07ffffff, 0x4e000000,
	0x28350, 0x3f3f3fff, 0x00000000,
	0x30, 0x000000ff, 0x0040,
	0x34, 0x00000040, 0x00004040,
	0x9100, 0x03e00000, 0x03600000,
	0x9060, 0x0000007f, 0x00000020,
	0x9508, 0x00010000, 0x00010000,
	0xac14, 0x000003ff, 0x000000f1,
	0xac10, 0xffffffff, 0x00000000,
	0xac0c, 0xffffffff, 0x00003210,
	0x88d4, 0x0000001f, 0x00000010,
	0x15c0, 0x000c0fc0, 0x000c0400
};

static const u32 hainan_golden_registers2[] = {
	0x98f8, 0xffffffff, 0x02010001
};

static const u32 tahiti_mgcg_cgcg_init[] = {
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x91d8, 0xffffffff, 0x00070006,
	0x91dc, 0xffffffff, 0x00090008,
	0x91e0, 0xffffffff, 0x0000000c,
	0x91e4, 0xffffffff, 0x000b000a,
	0x91e8, 0xffffffff, 0x000e000d,
	0x91ec, 0xffffffff, 0x00080007,
	0x91f0, 0xffffffff, 0x000a0009,
	0x91f4, 0xffffffff, 0x0000000d,
	0x91f8, 0xffffffff, 0x000c000b,
	0x91fc, 0xffffffff, 0x000f000e,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9264, 0xffffffff, 0x000e000d,
	0x9268, 0xffffffff, 0x0010000f,
	0x926c, 0xffffffff, 0x00000013,
	0x9270, 0xffffffff, 0x00120011,
	0x9274, 0xffffffff, 0x00150014,
	0x9278, 0xffffffff, 0x000f000e,
	0x927c, 0xffffffff, 0x00110010,
	0x9280, 0xffffffff, 0x00000014,
	0x9284, 0xffffffff, 0x00130012,
	0x9288, 0xffffffff, 0x00160015,
	0x928c, 0xffffffff, 0x0010000f,
	0x9290, 0xffffffff, 0x00120011,
	0x9294, 0xffffffff, 0x00000015,
	0x9298, 0xffffffff, 0x00140013,
	0x929c, 0xffffffff, 0x00170016,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 pitcairn_mgcg_cgcg_init[] = {
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 verde_mgcg_cgcg_init[] = {
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x9200, 0xffffffff, 0x00090008,
	0x9204, 0xffffffff, 0x000b000a,
	0x9208, 0xffffffff, 0x000c000f,
	0x920c, 0xffffffff, 0x000e000d,
	0x9210, 0xffffffff, 0x00110010,
	0x9214, 0xffffffff, 0x000a0009,
	0x9218, 0xffffffff, 0x000c000b,
	0x921c, 0xffffffff, 0x0000000f,
	0x9220, 0xffffffff, 0x000e000d,
	0x9224, 0xffffffff, 0x00110010,
	0x9228, 0xffffffff, 0x000b000a,
	0x922c, 0xffffffff, 0x000d000c,
	0x9230, 0xffffffff, 0x00000010,
	0x9234, 0xffffffff, 0x000f000e,
	0x9238, 0xffffffff, 0x00120011,
	0x923c, 0xffffffff, 0x000c000b,
	0x9240, 0xffffffff, 0x000e000d,
	0x9244, 0xffffffff, 0x00000011,
	0x9248, 0xffffffff, 0x0010000f,
	0x924c, 0xffffffff, 0x00130012,
	0x9250, 0xffffffff, 0x000d000c,
	0x9254, 0xffffffff, 0x000f000e,
	0x9258, 0xffffffff, 0x00100013,
	0x925c, 0xffffffff, 0x00120011,
	0x9260, 0xffffffff, 0x00150014,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 oland_mgcg_cgcg_init[] = {
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x102c, 0x00000101, 0x00000000,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x55e4, 0xff000fff, 0x00000100,
	0x55e8, 0x00000001, 0x00000001,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static const u32 hainan_mgcg_cgcg_init[] = {
	0xc400, 0xffffffff, 0xfffffffc,
	0x802c, 0xffffffff, 0xe0000000,
	0x9a60, 0xffffffff, 0x00000100,
	0x92a4, 0xffffffff, 0x00000100,
	0xc164, 0xffffffff, 0x00000100,
	0x9774, 0xffffffff, 0x00000100,
	0x8984, 0xffffffff, 0x06000100,
	0x8a18, 0xffffffff, 0x00000100,
	0x92a0, 0xffffffff, 0x00000100,
	0xc380, 0xffffffff, 0x00000100,
	0x8b28, 0xffffffff, 0x00000100,
	0x9144, 0xffffffff, 0x00000100,
	0x8d88, 0xffffffff, 0x00000100,
	0x8d8c, 0xffffffff, 0x00000100,
	0x9030, 0xffffffff, 0x00000100,
	0x9034, 0xffffffff, 0x00000100,
	0x9038, 0xffffffff, 0x00000100,
	0x903c, 0xffffffff, 0x00000100,
	0xad80, 0xffffffff, 0x00000100,
	0xac54, 0xffffffff, 0x00000100,
	0x897c, 0xffffffff, 0x06000100,
	0x9868, 0xffffffff, 0x00000100,
	0x9510, 0xffffffff, 0x00000100,
	0xaf04, 0xffffffff, 0x00000100,
	0xae04, 0xffffffff, 0x00000100,
	0x949c, 0xffffffff, 0x00000100,
	0x802c, 0xffffffff, 0xe0000000,
	0x9160, 0xffffffff, 0x00010000,
	0x9164, 0xffffffff, 0x00030002,
	0x9168, 0xffffffff, 0x00040007,
	0x916c, 0xffffffff, 0x00060005,
	0x9170, 0xffffffff, 0x00090008,
	0x9174, 0xffffffff, 0x00020001,
	0x9178, 0xffffffff, 0x00040003,
	0x917c, 0xffffffff, 0x00000007,
	0x9180, 0xffffffff, 0x00060005,
	0x9184, 0xffffffff, 0x00090008,
	0x9188, 0xffffffff, 0x00030002,
	0x918c, 0xffffffff, 0x00050004,
	0x9190, 0xffffffff, 0x00000008,
	0x9194, 0xffffffff, 0x00070006,
	0x9198, 0xffffffff, 0x000a0009,
	0x919c, 0xffffffff, 0x00040003,
	0x91a0, 0xffffffff, 0x00060005,
	0x91a4, 0xffffffff, 0x00000009,
	0x91a8, 0xffffffff, 0x00080007,
	0x91ac, 0xffffffff, 0x000b000a,
	0x91b0, 0xffffffff, 0x00050004,
	0x91b4, 0xffffffff, 0x00070006,
	0x91b8, 0xffffffff, 0x0008000b,
	0x91bc, 0xffffffff, 0x000a0009,
	0x91c0, 0xffffffff, 0x000d000c,
	0x91c4, 0xffffffff, 0x00060005,
	0x91c8, 0xffffffff, 0x00080007,
	0x91cc, 0xffffffff, 0x0000000b,
	0x91d0, 0xffffffff, 0x000a0009,
	0x91d4, 0xffffffff, 0x000d000c,
	0x9150, 0xffffffff, 0x96940200,
	0x8708, 0xffffffff, 0x00900100,
	0xc478, 0xffffffff, 0x00000080,
	0xc404, 0xffffffff, 0x0020003f,
	0x30, 0xffffffff, 0x0000001c,
	0x34, 0x000f0000, 0x000f0000,
	0x160c, 0xffffffff, 0x00000100,
	0x1024, 0xffffffff, 0x00000100,
	0x20a8, 0xffffffff, 0x00000104,
	0x264c, 0x000c0000, 0x000c0000,
	0x2648, 0x000c0000, 0x000c0000,
	0x2f50, 0x00000001, 0x00000001,
	0x30cc, 0xc0000fff, 0x00000104,
	0xc1e4, 0x00000001, 0x00000001,
	0xd0c0, 0xfffffff0, 0x00000100,
	0xd8c0, 0xfffffff0, 0x00000100
};

static u32 verde_pg_init[] = {
	0x353c, 0xffffffff, 0x40000,
	0x3538, 0xffffffff, 0x200010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x7007,
	0x3538, 0xffffffff, 0x300010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x400000,
	0x3538, 0xffffffff, 0x100010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x120200,
	0x3538, 0xffffffff, 0x500010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x1e1e16,
	0x3538, 0xffffffff, 0x600010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x171f1e,
	0x3538, 0xffffffff, 0x700010ff,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x353c, 0xffffffff, 0x0,
	0x3538, 0xffffffff, 0x9ff,
	0x3500, 0xffffffff, 0x0,
	0x3504, 0xffffffff, 0x10000800,
	0x3504, 0xffffffff, 0xf,
	0x3504, 0xffffffff, 0xf,
	0x3500, 0xffffffff, 0x4,
	0x3504, 0xffffffff, 0x1000051e,
	0x3504, 0xffffffff, 0xffff,
	0x3504, 0xffffffff, 0xffff,
	0x3500, 0xffffffff, 0x8,
	0x3504, 0xffffffff, 0x80500,
	0x3500, 0xffffffff, 0x12,
	0x3504, 0xffffffff, 0x9050c,
	0x3500, 0xffffffff, 0x1d,
	0x3504, 0xffffffff, 0xb052c,
	0x3500, 0xffffffff, 0x2a,
	0x3504, 0xffffffff, 0x1053e,
	0x3500, 0xffffffff, 0x2d,
	0x3504, 0xffffffff, 0x10546,
	0x3500, 0xffffffff, 0x30,
	0x3504, 0xffffffff, 0xa054e,
	0x3500, 0xffffffff, 0x3c,
	0x3504, 0xffffffff, 0x1055f,
	0x3500, 0xffffffff, 0x3f,
	0x3504, 0xffffffff, 0x10567,
	0x3500, 0xffffffff, 0x42,
	0x3504, 0xffffffff, 0x1056f,
	0x3500, 0xffffffff, 0x45,
	0x3504, 0xffffffff, 0x10572,
	0x3500, 0xffffffff, 0x48,
	0x3504, 0xffffffff, 0x20575,
	0x3500, 0xffffffff, 0x4c,
	0x3504, 0xffffffff, 0x190801,
	0x3500, 0xffffffff, 0x67,
	0x3504, 0xffffffff, 0x1082a,
	0x3500, 0xffffffff, 0x6a,
	0x3504, 0xffffffff, 0x1b082d,
	0x3500, 0xffffffff, 0x87,
	0x3504, 0xffffffff, 0x310851,
	0x3500, 0xffffffff, 0xba,
	0x3504, 0xffffffff, 0x891,
	0x3500, 0xffffffff, 0xbc,
	0x3504, 0xffffffff, 0x893,
	0x3500, 0xffffffff, 0xbe,
	0x3504, 0xffffffff, 0x20895,
	0x3500, 0xffffffff, 0xc2,
	0x3504, 0xffffffff, 0x20899,
	0x3500, 0xffffffff, 0xc6,
	0x3504, 0xffffffff, 0x2089d,
	0x3500, 0xffffffff, 0xca,
	0x3504, 0xffffffff, 0x8a1,
	0x3500, 0xffffffff, 0xcc,
	0x3504, 0xffffffff, 0x8a3,
	0x3500, 0xffffffff, 0xce,
	0x3504, 0xffffffff, 0x308a5,
	0x3500, 0xffffffff, 0xd3,
	0x3504, 0xffffffff, 0x6d08cd,
	0x3500, 0xffffffff, 0x142,
	0x3504, 0xffffffff, 0x2000095a,
	0x3504, 0xffffffff, 0x1,
	0x3500, 0xffffffff, 0x144,
	0x3504, 0xffffffff, 0x301f095b,
	0x3500, 0xffffffff, 0x165,
	0x3504, 0xffffffff, 0xc094d,
	0x3500, 0xffffffff, 0x173,
	0x3504, 0xffffffff, 0xf096d,
	0x3500, 0xffffffff, 0x184,
	0x3504, 0xffffffff, 0x15097f,
	0x3500, 0xffffffff, 0x19b,
	0x3504, 0xffffffff, 0xc0998,
	0x3500, 0xffffffff, 0x1a9,
	0x3504, 0xffffffff, 0x409a7,
	0x3500, 0xffffffff, 0x1af,
	0x3504, 0xffffffff, 0xcdc,
	0x3500, 0xffffffff, 0x1b1,
	0x3504, 0xffffffff, 0x800,
	0x3508, 0xffffffff, 0x6c9b2000,
	0x3510, 0xfc00, 0x2000,
	0x3544, 0xffffffff, 0xfc0,
	0x28d4, 0x00000100, 0x100
};

static void si_init_golden_registers(struct radeon_device *rdev)
{
	switch (rdev->family) {
	case CHIP_TAHITI:
		radeon_program_register_sequence(rdev,
						 tahiti_golden_registers,
						 (const u32)ARRAY_SIZE(tahiti_golden_registers));
		radeon_program_register_sequence(rdev,
						 tahiti_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(tahiti_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 tahiti_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(tahiti_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 tahiti_golden_registers2,
						 (const u32)ARRAY_SIZE(tahiti_golden_registers2));
		break;
	case CHIP_PITCAIRN:
		radeon_program_register_sequence(rdev,
						 pitcairn_golden_registers,
						 (const u32)ARRAY_SIZE(pitcairn_golden_registers));
		radeon_program_register_sequence(rdev,
						 pitcairn_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(pitcairn_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 pitcairn_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(pitcairn_mgcg_cgcg_init));
		break;
	case CHIP_VERDE:
		radeon_program_register_sequence(rdev,
						 verde_golden_registers,
						 (const u32)ARRAY_SIZE(verde_golden_registers));
		radeon_program_register_sequence(rdev,
						 verde_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(verde_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 verde_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(verde_mgcg_cgcg_init));
		radeon_program_register_sequence(rdev,
						 verde_pg_init,
						 (const u32)ARRAY_SIZE(verde_pg_init));
		break;
	case CHIP_OLAND:
		radeon_program_register_sequence(rdev,
						 oland_golden_registers,
						 (const u32)ARRAY_SIZE(oland_golden_registers));
		radeon_program_register_sequence(rdev,
						 oland_golden_rlc_registers,
						 (const u32)ARRAY_SIZE(oland_golden_rlc_registers));
		radeon_program_register_sequence(rdev,
						 oland_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(oland_mgcg_cgcg_init));
		break;
	case CHIP_HAINAN:
		radeon_program_register_sequence(rdev,
						 hainan_golden_registers,
						 (const u32)ARRAY_SIZE(hainan_golden_registers));
		radeon_program_register_sequence(rdev,
						 hainan_golden_registers2,
						 (const u32)ARRAY_SIZE(hainan_golden_registers2));
		radeon_program_register_sequence(rdev,
						 hainan_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(hainan_mgcg_cgcg_init));
		break;
	default:
		break;
	}
}

/**
 * si_get_allowed_info_register - fetch the register for the info ioctl
 *
 * @rdev: radeon_device pointer
 * @reg: register offset in bytes
 * @val: register value
 *
 * Returns 0 for success or -EINVAL for an invalid register
 *
 */
int si_get_allowed_info_register(struct radeon_device *rdev,
				 u32 reg, u32 *val)
{
	switch (reg) {
	case GRBM_STATUS:
	case GRBM_STATUS2:
	case GRBM_STATUS_SE0:
	case GRBM_STATUS_SE1:
	case SRBM_STATUS:
	case SRBM_STATUS2:
	case (DMA_STATUS_REG + DMA0_REGISTER_OFFSET):
	case (DMA_STATUS_REG + DMA1_REGISTER_OFFSET):
	case UVD_STATUS:
		*val = RREG32(reg);
		return 0;
	default:
		return -EINVAL;
	}
}

#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)

/**
 * si_get_xclk - get the xclk
 *
 * @rdev: radeon_device pointer
 *
 * Returns the reference clock used by the gfx engine
 * (SI).
 */
u32 si_get_xclk(struct radeon_device *rdev)
{
	u32 reference_clock = rdev->clock.spll.reference_freq;
	u32 tmp;

	tmp = RREG32(CG_CLKPIN_CNTL_2);
	if (tmp & MUX_TCLK_TO_XCLK)
		return TCLK;

	tmp = RREG32(CG_CLKPIN_CNTL);
	if (tmp & XTALIN_DIVIDE)
		return reference_clock / 4;

	return reference_clock;
}

/* get temperature in millidegrees */
int si_get_temp(struct radeon_device *rdev)
{
	u32 temp;
	int actual_temp = 0;

	temp = (RREG32(CG_MULT_THERMAL_STATUS) & CTF_TEMP_MASK) >>
		CTF_TEMP_SHIFT;

	if (temp & 0x200)
		actual_temp = 255;
	else
		actual_temp = temp & 0x1ff;

	actual_temp = (actual_temp * 1000);

	return actual_temp;
}

#define TAHITI_IO_MC_REGS_SIZE 36

static const u32 tahiti_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a77400}
};

static const u32 pitcairn_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a47400}
};

static const u32 verde_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a37400}
};

static const u32 oland_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a17730}
};

static const u32 hainan_io_mc_regs[TAHITI_IO_MC_REGS_SIZE][2] = {
	{0x0000006f, 0x03044000},
	{0x00000070, 0x0480c018},
	{0x00000071, 0x00000040},
	{0x00000072, 0x01000000},
	{0x00000074, 0x000000ff},
	{0x00000075, 0x00143400},
	{0x00000076, 0x08ec0800},
	{0x00000077, 0x040000cc},
	{0x00000079, 0x00000000},
	{0x0000007a, 0x21000409},
	{0x0000007c, 0x00000000},
	{0x0000007d, 0xe8000000},
	{0x0000007e, 0x044408a8},
	{0x0000007f, 0x00000003},
	{0x00000080, 0x00000000},
	{0x00000081, 0x01000000},
	{0x00000082, 0x02000000},
	{0x00000083, 0x00000000},
	{0x00000084, 0xe3f3e4f4},
	{0x00000085, 0x00052024},
	{0x00000087, 0x00000000},
	{0x00000088, 0x66036603},
	{0x00000089, 0x01000000},
	{0x0000008b, 0x1c0a0000},
	{0x0000008c, 0xff010000},
	{0x0000008e, 0xffffefff},
	{0x0000008f, 0xfff3efff},
	{0x00000090, 0xfff3efbf},
	{0x00000094, 0x00101101},
	{0x00000095, 0x00000fff},
	{0x00000096, 0x00116fff},
	{0x00000097, 0x60010000},
	{0x00000098, 0x10010000},
	{0x00000099, 0x00006000},
	{0x0000009a, 0x00001000},
	{0x0000009f, 0x00a07730}
};

/* ucode loading */
int si_mc_load_microcode(struct radeon_device *rdev)
{
	const __be32 *fw_data = NULL;
	const __le32 *new_fw_data = NULL;
	u32 running;
	u32 *io_mc_regs = NULL;
	const __le32 *new_io_mc_regs = NULL;
	int i, regs_size, ucode_size;

	if (!rdev->mc_fw)
		return -EINVAL;

	if (rdev->new_fw) {
		const struct mc_firmware_header_v1_0 *hdr =
			(const struct mc_firmware_header_v1_0 *)rdev->mc_fw->data;

		radeon_ucode_print_mc_hdr(&hdr->header);
		regs_size = le32_to_cpu(hdr->io_debug_size_bytes) / (4 * 2);
		new_io_mc_regs = (const __le32 *)
			(rdev->mc_fw->data + le32_to_cpu(hdr->io_debug_array_offset_bytes));
		ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		new_fw_data = (const __le32 *)
			(rdev->mc_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	} else {
		ucode_size = rdev->mc_fw->size / 4;

		switch (rdev->family) {
		case CHIP_TAHITI:
			io_mc_regs = (u32 *)&tahiti_io_mc_regs;
			regs_size = TAHITI_IO_MC_REGS_SIZE;
			break;
		case CHIP_PITCAIRN:
			io_mc_regs = (u32 *)&pitcairn_io_mc_regs;
			regs_size = TAHITI_IO_MC_REGS_SIZE;
			break;
		case CHIP_VERDE:
		default:
			io_mc_regs = (u32 *)&verde_io_mc_regs;
			regs_size = TAHITI_IO_MC_REGS_SIZE;
			break;
		case CHIP_OLAND:
			io_mc_regs = (u32 *)&oland_io_mc_regs;
			regs_size = TAHITI_IO_MC_REGS_SIZE;
			break;
		case CHIP_HAINAN:
			io_mc_regs = (u32 *)&hainan_io_mc_regs;
			regs_size = TAHITI_IO_MC_REGS_SIZE;
			break;
		}
		fw_data = (const __be32 *)rdev->mc_fw->data;
	}

	running = RREG32(MC_SEQ_SUP_CNTL) & RUN_MASK;

	if (running == 0) {
		/* reset the engine and set to writable */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000010);

		/* load mc io regs */
		for (i = 0; i < regs_size; i++) {
			if (rdev->new_fw) {
				WREG32(MC_SEQ_IO_DEBUG_INDEX, le32_to_cpup(new_io_mc_regs++));
				WREG32(MC_SEQ_IO_DEBUG_DATA, le32_to_cpup(new_io_mc_regs++));
			} else {
				WREG32(MC_SEQ_IO_DEBUG_INDEX, io_mc_regs[(i << 1)]);
				WREG32(MC_SEQ_IO_DEBUG_DATA, io_mc_regs[(i << 1) + 1]);
			}
		}
		/* load the MC ucode */
		for (i = 0; i < ucode_size; i++) {
			if (rdev->new_fw)
				WREG32(MC_SEQ_SUP_PGM, le32_to_cpup(new_fw_data++));
			else
				WREG32(MC_SEQ_SUP_PGM, be32_to_cpup(fw_data++));
		}

		/* put the engine back into the active state */
		WREG32(MC_SEQ_SUP_CNTL, 0x00000008);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000004);
		WREG32(MC_SEQ_SUP_CNTL, 0x00000001);

		/* wait for training to complete */
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D0)
				break;
			udelay(1);
		}
		for (i = 0; i < rdev->usec_timeout; i++) {
			if (RREG32(MC_SEQ_TRAIN_WAKEUP_CNTL) & TRAIN_DONE_D1)
				break;
			udelay(1);
		}
	}

	return 0;
}

static int si_init_microcode(struct radeon_device *rdev)
{
	const char *chip_name;
	const char *new_chip_name;
	size_t pfp_req_size, me_req_size, ce_req_size, rlc_req_size, mc_req_size;
	size_t smc_req_size, mc2_req_size;
	char fw_name[30];
	int err;
	int new_fw = 0;
	bool new_smc = false;
	bool si58_fw = false;
	bool banks2_fw = false;

	DRM_DEBUG("\n");

	switch (rdev->family) {
	case CHIP_TAHITI:
		chip_name = "TAHITI";
		new_chip_name = "tahiti";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		mc2_req_size = TAHITI_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(TAHITI_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_PITCAIRN:
		chip_name = "PITCAIRN";
		if ((rdev->pdev->revision == 0x81) &&
		    ((rdev->pdev->device == 0x6810) ||
		     (rdev->pdev->device == 0x6811)))
			new_smc = true;
		new_chip_name = "pitcairn";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		mc2_req_size = PITCAIRN_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(PITCAIRN_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_VERDE:
		chip_name = "VERDE";
		if (((rdev->pdev->device == 0x6820) &&
		     ((rdev->pdev->revision == 0x81) ||
		      (rdev->pdev->revision == 0x83))) ||
		    ((rdev->pdev->device == 0x6821) &&
		     ((rdev->pdev->revision == 0x83) ||
		      (rdev->pdev->revision == 0x87))) ||
		    ((rdev->pdev->revision == 0x87) &&
		     ((rdev->pdev->device == 0x6823) ||
		      (rdev->pdev->device == 0x682b))))
			new_smc = true;
		new_chip_name = "verde";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = SI_MC_UCODE_SIZE * 4;
		mc2_req_size = VERDE_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(VERDE_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_OLAND:
		chip_name = "OLAND";
		if (((rdev->pdev->revision == 0x81) &&
		     ((rdev->pdev->device == 0x6600) ||
		      (rdev->pdev->device == 0x6604) ||
		      (rdev->pdev->device == 0x6605) ||
		      (rdev->pdev->device == 0x6610))) ||
		    ((rdev->pdev->revision == 0x83) &&
		     (rdev->pdev->device == 0x6610)))
			new_smc = true;
		new_chip_name = "oland";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = mc2_req_size = OLAND_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(OLAND_SMC_UCODE_SIZE, 4);
		break;
	case CHIP_HAINAN:
		chip_name = "HAINAN";
		if (((rdev->pdev->revision == 0x81) &&
		     (rdev->pdev->device == 0x6660)) ||
		    ((rdev->pdev->revision == 0x83) &&
		     ((rdev->pdev->device == 0x6660) ||
		      (rdev->pdev->device == 0x6663) ||
		      (rdev->pdev->device == 0x6665) ||
		      (rdev->pdev->device == 0x6667))))
			new_smc = true;
		else if ((rdev->pdev->revision == 0xc3) &&
			 (rdev->pdev->device == 0x6665))
			banks2_fw = true;
		new_chip_name = "hainan";
		pfp_req_size = SI_PFP_UCODE_SIZE * 4;
		me_req_size = SI_PM4_UCODE_SIZE * 4;
		ce_req_size = SI_CE_UCODE_SIZE * 4;
		rlc_req_size = SI_RLC_UCODE_SIZE * 4;
		mc_req_size = mc2_req_size = OLAND_MC_UCODE_SIZE * 4;
		smc_req_size = ALIGN(HAINAN_SMC_UCODE_SIZE, 4);
		break;
	default:
		BUG();
	}

	/* this memory configuration requires special firmware */
	if (((RREG32(MC_SEQ_MISC0) & 0xff000000) >> 24) == 0x58)
		si58_fw = true;

	DRM_INFO("Loading %s Microcode\n", new_chip_name);

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", new_chip_name);
	err = request_firmware(&rdev->pfp_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_pfp.bin", chip_name);
		err = request_firmware(&rdev->pfp_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->pfp_fw->size != pfp_req_size) {
			pr_err("si_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->pfp_fw->size, fw_name);
			err = -EINVAL;
			goto out;
		}
	} else {
		err = radeon_ucode_validate(rdev->pfp_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", new_chip_name);
	err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_me.bin", chip_name);
		err = request_firmware(&rdev->me_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->me_fw->size != me_req_size) {
			pr_err("si_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->me_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->me_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", new_chip_name);
	err = request_firmware(&rdev->ce_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_ce.bin", chip_name);
		err = request_firmware(&rdev->ce_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->ce_fw->size != ce_req_size) {
			pr_err("si_cp: Bogus length %zu in firmware \"%s\"\n",
			       rdev->ce_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->ce_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", new_chip_name);
	err = request_firmware(&rdev->rlc_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_rlc.bin", chip_name);
		err = request_firmware(&rdev->rlc_fw, fw_name, rdev->dev);
		if (err)
			goto out;
		if (rdev->rlc_fw->size != rlc_req_size) {
			pr_err("si_rlc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->rlc_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->rlc_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	if (si58_fw)
		snprintf(fw_name, sizeof(fw_name), "radeon/si58_mc.bin");
	else
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", new_chip_name);
	err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc2.bin", chip_name);
		err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
		if (err) {
			snprintf(fw_name, sizeof(fw_name), "radeon/%s_mc.bin", chip_name);
			err = request_firmware(&rdev->mc_fw, fw_name, rdev->dev);
			if (err)
				goto out;
		}
		if ((rdev->mc_fw->size != mc_req_size) &&
		    (rdev->mc_fw->size != mc2_req_size)) {
			pr_err("si_mc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->mc_fw->size, fw_name);
			err = -EINVAL;
		}
		DRM_INFO("%s: %zu bytes\n", fw_name, rdev->mc_fw->size);
	} else {
		err = radeon_ucode_validate(rdev->mc_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	if (banks2_fw)
		snprintf(fw_name, sizeof(fw_name), "radeon/banks_k_2_smc.bin");
	else if (new_smc)
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_k_smc.bin", new_chip_name);
	else
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_smc.bin", new_chip_name);
	err = request_firmware(&rdev->smc_fw, fw_name, rdev->dev);
	if (err) {
		snprintf(fw_name, sizeof(fw_name), "radeon/%s_smc.bin", chip_name);
		err = request_firmware(&rdev->smc_fw, fw_name, rdev->dev);
		if (err) {
			pr_err("smc: error loading firmware \"%s\"\n", fw_name);
			release_firmware(rdev->smc_fw);
			rdev->smc_fw = NULL;
			err = 0;
		} else if (rdev->smc_fw->size != smc_req_size) {
			pr_err("si_smc: Bogus length %zu in firmware \"%s\"\n",
			       rdev->smc_fw->size, fw_name);
			err = -EINVAL;
		}
	} else {
		err = radeon_ucode_validate(rdev->smc_fw);
		if (err) {
			pr_err("si_cp: validation failed for firmware \"%s\"\n",
			       fw_name);
			goto out;
		} else {
			new_fw++;
		}
	}

	if (new_fw == 0) {
		rdev->new_fw = false;
	} else if (new_fw < 6) {
		pr_err("si_fw: mixing new and old firmware!\n");
		err = -EINVAL;
	} else {
		rdev->new_fw = true;
	}
out:
	if (err) {
		if (err != -EINVAL)
			pr_err("si_cp: Failed to load firmware \"%s\"\n",
			       fw_name);
		release_firmware(rdev->pfp_fw);
		rdev->pfp_fw = NULL;
		release_firmware(rdev->me_fw);
		rdev->me_fw = NULL;
		release_firmware(rdev->ce_fw);
		rdev->ce_fw = NULL;
		release_firmware(rdev->rlc_fw);
		rdev->rlc_fw = NULL;
		release_firmware(rdev->mc_fw);
		rdev->mc_fw = NULL;
		release_firmware(rdev->smc_fw);
		rdev->smc_fw = NULL;
	}
	return err;
}

/* watermark setup */
static u32 dce6_line_buffer_adjust(struct radeon_device *rdev,
				   struct radeon_crtc *radeon_crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *other_mode)
{
	u32 tmp, buffer_alloc, i;
	u32 pipe_offset = radeon_crtc->crtc_id * 0x20;
	/*
	 * Line Buffer Setup
	 * There are 3 line buffers, each one shared by 2 display controllers.
	 * DC_LB_MEMORY_SPLIT controls how that line buffer is shared between
	 * the display controllers.  The paritioning is done via one of four
	 * preset allocations specified in bits 21:20:
	 *  0 - half lb
	 *  2 - whole lb, other crtc must be disabled
	 */
	/* this can get tricky if we have two large displays on a paired group
	 * of crtcs.  Ideally for multiple large displays we'd assign them to
	 * non-linked crtcs for maximum line buffer allocation.
	 */
	if (radeon_crtc->base.enabled && mode) {
		if (other_mode) {
			tmp = 0; /* 1/2 */
			buffer_alloc = 1;
		} else {
			tmp = 2; /* whole */
			buffer_alloc = 2;
		}
	} else {
		tmp = 0;
		buffer_alloc = 0;
	}

	WREG32(DC_LB_MEMORY_SPLIT + radeon_crtc->crtc_offset,
	       DC_LB_MEMORY_CONFIG(tmp));

	WREG32(PIPE0_DMIF_BUFFER_CONTROL + pipe_offset,
	       DMIF_BUFFERS_ALLOCATED(buffer_alloc));
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(PIPE0_DMIF_BUFFER_CONTROL + pipe_offset) &
		    DMIF_BUFFERS_ALLOCATED_COMPLETED)
			break;
		udelay(1);
	}

	if (radeon_crtc->base.enabled && mode) {
		switch (tmp) {
		case 0:
		default:
			return 4096 * 2;
		case 2:
			return 8192 * 2;
		}
	}

	/* controller not enabled, so no lb used */
	return 0;
}

static u32 si_get_number_of_dram_channels(struct radeon_device *rdev)
{
	u32 tmp = RREG32(MC_SHARED_CHMAP);

	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		return 1;
	case 1:
		return 2;
	case 2:
		return 4;
	case 3:
		return 8;
	case 4:
		return 3;
	case 5:
		return 6;
	case 6:
		return 10;
	case 7:
		return 12;
	case 8:
		return 16;
	}
}

struct dce6_wm_params {
	u32 dram_channels; /* number of dram channels */
	u32 yclk;          /* bandwidth per dram data pin in kHz */
	u32 sclk;          /* engine clock in kHz */
	u32 disp_clk;      /* display clock in kHz */
	u32 src_width;     /* viewport width */
	u32 active_time;   /* active display time in ns */
	u32 blank_time;    /* blank time in ns */
	bool interlaced;    /* mode is interlaced */
	fixed20_12 vsc;    /* vertical scale ratio */
	u32 num_heads;     /* number of active crtcs */
	u32 bytes_per_pixel; /* bytes per pixel display + overlay */
	u32 lb_size;       /* line buffer allocated to pipe */
	u32 vtaps;         /* vertical scaler taps */
};

static u32 dce6_dram_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate raw DRAM Bandwidth */
	fixed20_12 dram_efficiency; /* 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	dram_efficiency.full = dfixed_const(7);
	dram_efficiency.full = dfixed_div(dram_efficiency, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, dram_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	/* Calculate DRAM Bandwidth and the part allocated to display. */
	fixed20_12 disp_dram_allocation; /* 0.3 to 0.7 */
	fixed20_12 yclk, dram_channels, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	yclk.full = dfixed_const(wm->yclk);
	yclk.full = dfixed_div(yclk, a);
	dram_channels.full = dfixed_const(wm->dram_channels * 4);
	a.full = dfixed_const(10);
	disp_dram_allocation.full = dfixed_const(3); /* XXX worse case value 0.3 */
	disp_dram_allocation.full = dfixed_div(disp_dram_allocation, a);
	bandwidth.full = dfixed_mul(dram_channels, yclk);
	bandwidth.full = dfixed_mul(bandwidth, disp_dram_allocation);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_data_return_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display Data return Bandwidth */
	fixed20_12 return_efficiency; /* 0.8 */
	fixed20_12 sclk, bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(10);
	return_efficiency.full = dfixed_const(8);
	return_efficiency.full = dfixed_div(return_efficiency, a);
	a.full = dfixed_const(32);
	bandwidth.full = dfixed_mul(a, sclk);
	bandwidth.full = dfixed_mul(bandwidth, return_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_get_dmif_bytes_per_request(struct dce6_wm_params *wm)
{
	return 32;
}

static u32 dce6_dmif_request_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the DMIF Request Bandwidth */
	fixed20_12 disp_clk_request_efficiency; /* 0.8 */
	fixed20_12 disp_clk, sclk, bandwidth;
	fixed20_12 a, b1, b2;
	u32 min_bandwidth;

	a.full = dfixed_const(1000);
	disp_clk.full = dfixed_const(wm->disp_clk);
	disp_clk.full = dfixed_div(disp_clk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm) / 2);
	b1.full = dfixed_mul(a, disp_clk);

	a.full = dfixed_const(1000);
	sclk.full = dfixed_const(wm->sclk);
	sclk.full = dfixed_div(sclk, a);
	a.full = dfixed_const(dce6_get_dmif_bytes_per_request(wm));
	b2.full = dfixed_mul(a, sclk);

	a.full = dfixed_const(10);
	disp_clk_request_efficiency.full = dfixed_const(8);
	disp_clk_request_efficiency.full = dfixed_div(disp_clk_request_efficiency, a);

	min_bandwidth = min(dfixed_trunc(b1), dfixed_trunc(b2));

	a.full = dfixed_const(min_bandwidth);
	bandwidth.full = dfixed_mul(a, disp_clk_request_efficiency);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_available_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the Available bandwidth. Display can use this temporarily but not in average. */
	u32 dram_bandwidth = dce6_dram_bandwidth(wm);
	u32 data_return_bandwidth = dce6_data_return_bandwidth(wm);
	u32 dmif_req_bandwidth = dce6_dmif_request_bandwidth(wm);

	return min(dram_bandwidth, min(data_return_bandwidth, dmif_req_bandwidth));
}

static u32 dce6_average_bandwidth(struct dce6_wm_params *wm)
{
	/* Calculate the display mode Average Bandwidth
	 * DisplayMode should contain the source and destination dimensions,
	 * timing, etc.
	 */
	fixed20_12 bpp;
	fixed20_12 line_time;
	fixed20_12 src_width;
	fixed20_12 bandwidth;
	fixed20_12 a;

	a.full = dfixed_const(1000);
	line_time.full = dfixed_const(wm->active_time + wm->blank_time);
	line_time.full = dfixed_div(line_time, a);
	bpp.full = dfixed_const(wm->bytes_per_pixel);
	src_width.full = dfixed_const(wm->src_width);
	bandwidth.full = dfixed_mul(src_width, bpp);
	bandwidth.full = dfixed_mul(bandwidth, wm->vsc);
	bandwidth.full = dfixed_div(bandwidth, line_time);

	return dfixed_trunc(bandwidth);
}

static u32 dce6_latency_watermark(struct dce6_wm_params *wm)
{
	/* First calcualte the latency in ns */
	u32 mc_latency = 2000; /* 2000 ns. */
	u32 available_bandwidth = dce6_available_bandwidth(wm);
	u32 worst_chunk_return_time = (512 * 8 * 1000) / available_bandwidth;
	u32 cursor_line_pair_return_time = (128 * 4 * 1000) / available_bandwidth;
	u32 dc_latency = 40000000 / wm->disp_clk; /* dc pipe latency */
	u32 other_heads_data_return_time = ((wm->num_heads + 1) * worst_chunk_return_time) +
		(wm->num_heads * cursor_line_pair_return_time);
	u32 latency = mc_latency + other_heads_data_return_time + dc_latency;
	u32 max_src_lines_per_dst_line, lb_fill_bw, line_fill_time;
	u32 tmp, dmif_size = 12288;
	fixed20_12 a, b, c;

	if (wm->num_heads == 0)
		return 0;

	a.full = dfixed_const(2);
	b.full = dfixed_const(1);
	if ((wm->vsc.full > a.full) ||
	    ((wm->vsc.full > b.full) && (wm->vtaps >= 3)) ||
	    (wm->vtaps >= 5) ||
	    ((wm->vsc.full >= a.full) && wm->interlaced))
		max_src_lines_per_dst_line = 4;
	else
		max_src_lines_per_dst_line = 2;

	a.full = dfixed_const(available_bandwidth);
	b.full = dfixed_const(wm->num_heads);
	a.full = dfixed_div(a, b);
	tmp = div_u64((u64) dmif_size * (u64) wm->disp_clk, mc_latency + 512);
	tmp = min(dfixed_trunc(a), tmp);

	lb_fill_bw = min(tmp, wm->disp_clk * wm->bytes_per_pixel / 1000);

	a.full = dfixed_const(max_src_lines_per_dst_line * wm->src_width * wm->bytes_per_pixel);
	b.full = dfixed_const(1000);
	c.full = dfixed_const(lb_fill_bw);
	b.full = dfixed_div(c, b);
	a.full = dfixed_div(a, b);
	line_fill_time = dfixed_trunc(a);

	if (line_fill_time < wm->active_time)
		return latency;
	else
		return latency + (line_fill_time - wm->active_time);

}

static bool dce6_average_bandwidth_vs_dram_bandwidth_for_display(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_dram_bandwidth_for_display(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_average_bandwidth_vs_available_bandwidth(struct dce6_wm_params *wm)
{
	if (dce6_average_bandwidth(wm) <=
	    (dce6_available_bandwidth(wm) / wm->num_heads))
		return true;
	else
		return false;
};

static bool dce6_check_latency_hiding(struct dce6_wm_params *wm)
{
	u32 lb_partitions = wm->lb_size / wm->src_width;
	u32 line_time = wm->active_time + wm->blank_time;
	u32 latency_tolerant_lines;
	u32 latency_hiding;
	fixed20_12 a;

	a.full = dfixed_const(1);
	if (wm->vsc.full > a.full)
		latency_tolerant_lines = 1;
	else {
		if (lb_partitions <= (wm->vtaps + 1))
			latency_tolerant_lines = 1;
		else
			latency_tolerant_lines = 2;
	}

	latency_hiding = (latency_tolerant_lines * line_time + wm->blank_time);

	if (dce6_latency_watermark(wm) <= latency_hiding)
		return true;
	else
		return false;
}

static void dce6_program_watermarks(struct radeon_device *rdev,
					 struct radeon_crtc *radeon_crtc,
					 u32 lb_size, u32 num_heads)
{
	struct drm_display_mode *mode = &radeon_crtc->base.mode;
	struct dce6_wm_params wm_low, wm_high;
	u32 dram_channels;
	u32 active_time;
	u32 line_time = 0;
	u32 latency_watermark_a = 0, latency_watermark_b = 0;
	u32 priority_a_mark = 0, priority_b_mark = 0;
	u32 priority_a_cnt = PRIORITY_OFF;
	u32 priority_b_cnt = PRIORITY_OFF;
	u32 tmp, arb_control3;
	fixed20_12 a, b, c;

	if (radeon_crtc->base.enabled && num_heads && mode) {
		active_time = (u32) div_u64((u64)mode->crtc_hdisplay * 1000000,
					    (u32)mode->clock);
		line_time = (u32) div_u64((u64)mode->crtc_htotal * 1000000,
					  (u32)mode->clock);
		line_time = min(line_time, (u32)65535);
		priority_a_cnt = 0;
		priority_b_cnt = 0;

		if (rdev->family == CHIP_ARUBA)
			dram_channels = evergreen_get_number_of_dram_channels(rdev);
		else
			dram_channels = si_get_number_of_dram_channels(rdev);

		/* watermark for high clocks */
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			wm_high.yclk =
				radeon_dpm_get_mclk(rdev, false) * 10;
			wm_high.sclk =
				radeon_dpm_get_sclk(rdev, false) * 10;
		} else {
			wm_high.yclk = rdev->pm.current_mclk * 10;
			wm_high.sclk = rdev->pm.current_sclk * 10;
		}

		wm_high.disp_clk = mode->clock;
		wm_high.src_width = mode->crtc_hdisplay;
		wm_high.active_time = active_time;
		wm_high.blank_time = line_time - wm_high.active_time;
		wm_high.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_high.interlaced = true;
		wm_high.vsc = radeon_crtc->vsc;
		wm_high.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm_high.vtaps = 2;
		wm_high.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_high.lb_size = lb_size;
		wm_high.dram_channels = dram_channels;
		wm_high.num_heads = num_heads;

		/* watermark for low clocks */
		if ((rdev->pm.pm_method == PM_METHOD_DPM) && rdev->pm.dpm_enabled) {
			wm_low.yclk =
				radeon_dpm_get_mclk(rdev, true) * 10;
			wm_low.sclk =
				radeon_dpm_get_sclk(rdev, true) * 10;
		} else {
			wm_low.yclk = rdev->pm.current_mclk * 10;
			wm_low.sclk = rdev->pm.current_sclk * 10;
		}

		wm_low.disp_clk = mode->clock;
		wm_low.src_width = mode->crtc_hdisplay;
		wm_low.active_time = active_time;
		wm_low.blank_time = line_time - wm_low.active_time;
		wm_low.interlaced = false;
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			wm_low.interlaced = true;
		wm_low.vsc = radeon_crtc->vsc;
		wm_low.vtaps = 1;
		if (radeon_crtc->rmx_type != RMX_OFF)
			wm_low.vtaps = 2;
		wm_low.bytes_per_pixel = 4; /* XXX: get this from fb config */
		wm_low.lb_size = lb_size;
		wm_low.dram_channels = dram_channels;
		wm_low.num_heads = num_heads;

		/* set for high clocks */
		latency_watermark_a = min(dce6_latency_watermark(&wm_high), (u32)65535);
		/* set for low clocks */
		latency_watermark_b = min(dce6_latency_watermark(&wm_low), (u32)65535);

		/* possibly force display priority to high */
		/* should really do this at mode validation time... */
		if (!dce6_average_bandwidth_vs_dram_bandwidth_for_display(&wm_high) ||
		    !dce6_average_bandwidth_vs_available_bandwidth(&wm_high) ||
		    !dce6_check_latency_hiding(&wm_high) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
			priority_a_cnt |= PRIORITY_ALWAYS_ON;
			priority_b_cnt |= PRIORITY_ALWAYS_ON;
		}
		if (!dce6_average_bandwidth_vs_dram_bandwidth_for_display(&wm_low) ||
		    !dce6_average_bandwidth_vs_available_bandwidth(&wm_low) ||
		    !dce6_check_latency_hiding(&wm_low) ||
		    (rdev->disp_priority == 2)) {
			DRM_DEBUG_KMS("force priority to high\n");
			priority_a_cnt |= PRIORITY_ALWAYS_ON;
			priority_b_cnt |= PRIORITY_ALWAYS_ON;
		}

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_a);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_a_mark = dfixed_trunc(c);
		priority_a_cnt |= priority_a_mark & PRIORITY_MARK_MASK;

		a.full = dfixed_const(1000);
		b.full = dfixed_const(mode->clock);
		b.full = dfixed_div(b, a);
		c.full = dfixed_const(latency_watermark_b);
		c.full = dfixed_mul(c, b);
		c.full = dfixed_mul(c, radeon_crtc->hsc);
		c.full = dfixed_div(c, a);
		a.full = dfixed_const(16);
		c.full = dfixed_div(c, a);
		priority_b_mark = dfixed_trunc(c);
		priority_b_cnt |= priority_b_mark & PRIORITY_MARK_MASK;

		/* Save number of lines the linebuffer leads before the scanout */
		radeon_crtc->lb_vblank_lead_lines = DIV_ROUND_UP(lb_size, mode->crtc_hdisplay);
	}

	/* select wm A */
	arb_control3 = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp = arb_control3;
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(1);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_a) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* select wm B */
	tmp = RREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset);
	tmp &= ~LATENCY_WATERMARK_MASK(3);
	tmp |= LATENCY_WATERMARK_MASK(2);
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, tmp);
	WREG32(DPG_PIPE_LATENCY_CONTROL + radeon_crtc->crtc_offset,
	       (LATENCY_LOW_WATERMARK(latency_watermark_b) |
		LATENCY_HIGH_WATERMARK(line_time)));
	/* restore original selection */
	WREG32(DPG_PIPE_ARBITRATION_CONTROL3 + radeon_crtc->crtc_offset, arb_control3);

	/* write the priority marks */
	WREG32(PRIORITY_A_CNT + radeon_crtc->crtc_offset, priority_a_cnt);
	WREG32(PRIORITY_B_CNT + radeon_crtc->crtc_offset, priority_b_cnt);

	/* save values for DPM */
	radeon_crtc->line_time = line_time;
	radeon_crtc->wm_high = latency_watermark_a;
	radeon_crtc->wm_low = latency_watermark_b;
}

void dce6_bandwidth_update(struct radeon_device *rdev)
{
	struct drm_display_mode *mode0 = NULL;
	struct drm_display_mode *mode1 = NULL;
	u32 num_heads = 0, lb_size;
	int i;

	if (!rdev->mode_info.mode_config_initialized)
		return;

	radeon_update_display_priority(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->mode_info.crtcs[i]->base.enabled)
			num_heads++;
	}
	for (i = 0; i < rdev->num_crtc; i += 2) {
		mode0 = &rdev->mode_info.crtcs[i]->base.mode;
		mode1 = &rdev->mode_info.crtcs[i+1]->base.mode;
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i], mode0, mode1);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i], lb_size, num_heads);
		lb_size = dce6_line_buffer_adjust(rdev, rdev->mode_info.crtcs[i+1], mode1, mode0);
		dce6_program_watermarks(rdev, rdev->mode_info.crtcs[i+1], lb_size, num_heads);
	}
}

/*
 * Core functions
 */
static void si_tiling_mode_table_init(struct radeon_device *rdev)
{
	u32 *tile = rdev->config.si.tile_mode_array;
	const u32 num_tile_mode_states =
			ARRAY_SIZE(rdev->config.si.tile_mode_array);
	u32 reg_offset, split_equal_to_row_size;

	switch (rdev->config.si.mem_row_size_in_kb) {
	case 1:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_1KB;
		break;
	case 2:
	default:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_2KB;
		break;
	case 4:
		split_equal_to_row_size = ADDR_SURF_TILE_SPLIT_4KB;
		break;
	}

	for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
		tile[reg_offset] = 0;

	switch(rdev->family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		/* non-AA compressed depth or any compressed stencil */
		tile[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 2xAA/4xAA compressed depth only */
		tile[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 8xAA compressed depth only */
		tile[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 2xAA/4xAA compressed depth with stencil (for depth buffer) */
		tile[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Maps w/ a dimension less than the 2D macro-tile dimensions (for mipmapped depth textures) */
		tile[4] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Uncompressed 16bpp depth - and stencil buffer allocated with it */
		tile[5] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Uncompressed 32bpp depth - and stencil buffer allocated with it */
		tile[6] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* Uncompressed 8bpp stencil without depth (drivers typically do not use) */
		tile[7] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 1D and 1D Array Surfaces */
		tile[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Displayable maps. */
		tile[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Display 8bpp. */
		tile[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Display 16bpp. */
		tile[11] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Display 32bpp. */
		tile[12] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* Thin. */
		tile[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin 8 bpp. */
		tile[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* Thin 16 bpp. */
		tile[15] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* Thin 32 bpp. */
		tile[16] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* Thin 64 bpp. */
		tile[17] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));
		/* 8 bpp PRT. */
		tile[21] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 16 bpp PRT */
		tile[22] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 32 bpp PRT */
		tile[23] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 64 bpp PRT */
		tile[24] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 128 bpp PRT */
		tile[25] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
			   NUM_BANKS(ADDR_SURF_8_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), tile[reg_offset]);
		break;

	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_HAINAN:
		/* non-AA compressed depth or any compressed stencil */
		tile[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 2xAA/4xAA compressed depth only */
		tile[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 8xAA compressed depth only */
		tile[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 2xAA/4xAA compressed depth with stencil (for depth buffer) */
		tile[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* Maps w/ a dimension less than the 2D macro-tile dimensions (for mipmapped depth textures) */
		tile[4] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Uncompressed 16bpp depth - and stencil buffer allocated with it */
		tile[5] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Uncompressed 32bpp depth - and stencil buffer allocated with it */
		tile[6] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Uncompressed 8bpp stencil without depth (drivers typically do not use) */
		tile[7] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DEPTH_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 1D and 1D Array Surfaces */
		tile[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Displayable maps. */
		tile[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Display 8bpp. */
		tile[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* Display 16bpp. */
		tile[11] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Display 32bpp. */
		tile[12] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_DISPLAY_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin. */
		tile[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin 8 bpp. */
		tile[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin 16 bpp. */
		tile[15] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin 32 bpp. */
		tile[16] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* Thin 64 bpp. */
		tile[17] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P4_8x16) |
			   TILE_SPLIT(split_equal_to_row_size) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 8 bpp PRT. */
		tile[21] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 16 bpp PRT */
		tile[22] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4));
		/* 32 bpp PRT */
		tile[23] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 64 bpp PRT */
		tile[24] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
			   NUM_BANKS(ADDR_SURF_16_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2));
		/* 128 bpp PRT */
		tile[25] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
			   MICRO_TILE_MODE(ADDR_SURF_THIN_MICRO_TILING) |
			   PIPE_CONFIG(ADDR_SURF_P8_32x32_8x16) |
			   TILE_SPLIT(ADDR_SURF_TILE_SPLIT_1KB) |
			   NUM_BANKS(ADDR_SURF_8_BANK) |
			   BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
			   BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
			   MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(GB_TILE_MODE0 + (reg_offset * 4), tile[reg_offset]);
		break;

	default:
		DRM_ERROR("unknown asic: 0x%x\n", rdev->family);
	}
}

static void si_select_se_sh(struct radeon_device *rdev,
			    u32 se_num, u32 sh_num)
{
	u32 data = INSTANCE_BROADCAST_WRITES;

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff))
		data |= SH_BROADCAST_WRITES | SE_BROADCAST_WRITES;
	else if (se_num == 0xffffffff)
		data |= SE_BROADCAST_WRITES | SH_INDEX(sh_num);
	else if (sh_num == 0xffffffff)
		data |= SH_BROADCAST_WRITES | SE_INDEX(se_num);
	else
		data |= SH_INDEX(sh_num) | SE_INDEX(se_num);
	WREG32(GRBM_GFX_INDEX, data);
}

static u32 si_create_bitmask(u32 bit_width)
{
	u32 i, mask = 0;

	for (i = 0; i < bit_width; i++) {
		mask <<= 1;
		mask |= 1;
	}
	return mask;
}

static u32 si_get_cu_enabled(struct radeon_device *rdev, u32 cu_per_sh)
{
	u32 data, mask;

	data = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	if (data & 1)
		data &= INACTIVE_CUS_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_SHADER_ARRAY_CONFIG);

	data >>= INACTIVE_CUS_SHIFT;

	mask = si_create_bitmask(cu_per_sh);

	return ~data & mask;
}

static void si_setup_spi(struct radeon_device *rdev,
			 u32 se_num, u32 sh_per_se,
			 u32 cu_per_sh)
{
	int i, j, k;
	u32 data, mask, active_cu;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			si_select_se_sh(rdev, i, j);
			data = RREG32(SPI_STATIC_THREAD_MGMT_3);
			active_cu = si_get_cu_enabled(rdev, cu_per_sh);

			mask = 1;
			for (k = 0; k < 16; k++) {
				mask <<= k;
				if (active_cu & mask) {
					data &= ~mask;
					WREG32(SPI_STATIC_THREAD_MGMT_3, data);
					break;
				}
			}
		}
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

static u32 si_get_rb_disabled(struct radeon_device *rdev,
			      u32 max_rb_num_per_se,
			      u32 sh_per_se)
{
	u32 data, mask;

	data = RREG32(CC_RB_BACKEND_DISABLE);
	if (data & 1)
		data &= BACKEND_DISABLE_MASK;
	else
		data = 0;
	data |= RREG32(GC_USER_RB_BACKEND_DISABLE);

	data >>= BACKEND_DISABLE_SHIFT;

	mask = si_create_bitmask(max_rb_num_per_se / sh_per_se);

	return data & mask;
}

static void si_setup_rb(struct radeon_device *rdev,
			u32 se_num, u32 sh_per_se,
			u32 max_rb_num_per_se)
{
	int i, j;
	u32 data, mask;
	u32 disabled_rbs = 0;
	u32 enabled_rbs = 0;

	for (i = 0; i < se_num; i++) {
		for (j = 0; j < sh_per_se; j++) {
			si_select_se_sh(rdev, i, j);
			data = si_get_rb_disabled(rdev, max_rb_num_per_se, sh_per_se);
			disabled_rbs |= data << ((i * sh_per_se + j) * TAHITI_RB_BITMAP_WIDTH_PER_SH);
		}
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	mask = 1;
	for (i = 0; i < max_rb_num_per_se * se_num; i++) {
		if (!(disabled_rbs & mask))
			enabled_rbs |= mask;
		mask <<= 1;
	}

	rdev->config.si.backend_enable_mask = enabled_rbs;

	for (i = 0; i < se_num; i++) {
		si_select_se_sh(rdev, i, 0xffffffff);
		data = 0;
		for (j = 0; j < sh_per_se; j++) {
			switch (enabled_rbs & 3) {
			case 1:
				data |= (RASTER_CONFIG_RB_MAP_0 << (i * sh_per_se + j) * 2);
				break;
			case 2:
				data |= (RASTER_CONFIG_RB_MAP_3 << (i * sh_per_se + j) * 2);
				break;
			case 3:
			default:
				data |= (RASTER_CONFIG_RB_MAP_2 << (i * sh_per_se + j) * 2);
				break;
			}
			enabled_rbs >>= 2;
		}
		WREG32(PA_SC_RASTER_CONFIG, data);
	}
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);
}

static void si_gpu_init(struct radeon_device *rdev)
{
	u32 gb_addr_config = 0;
	u32 mc_arb_ramcfg;
	u32 sx_debug_1;
	u32 hdp_host_path_cntl;
	u32 tmp;
	int i, j;

	switch (rdev->family) {
	case CHIP_TAHITI:
		rdev->config.si.max_shader_engines = 2;
		rdev->config.si.max_tile_pipes = 12;
		rdev->config.si.max_cu_per_sh = 8;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 12;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x100;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_PITCAIRN:
		rdev->config.si.max_shader_engines = 2;
		rdev->config.si.max_tile_pipes = 8;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 8;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x100;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TAHITI_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_VERDE:
	default:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 2;
		rdev->config.si.max_backends_per_se = 4;
		rdev->config.si.max_texture_channel_caches = 4;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 32;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_OLAND:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 6;
		rdev->config.si.max_sh_per_se = 1;
		rdev->config.si.max_backends_per_se = 2;
		rdev->config.si.max_texture_channel_caches = 4;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 16;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = VERDE_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_HAINAN:
		rdev->config.si.max_shader_engines = 1;
		rdev->config.si.max_tile_pipes = 4;
		rdev->config.si.max_cu_per_sh = 5;
		rdev->config.si.max_sh_per_se = 1;
		rdev->config.si.max_backends_per_se = 1;
		rdev->config.si.max_texture_channel_caches = 2;
		rdev->config.si.max_gprs = 256;
		rdev->config.si.max_gs_threads = 16;
		rdev->config.si.max_hw_contexts = 8;

		rdev->config.si.sc_prim_fifo_size_frontend = 0x20;
		rdev->config.si.sc_prim_fifo_size_backend = 0x40;
		rdev->config.si.sc_hiz_tile_fifo_size = 0x30;
		rdev->config.si.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = HAINAN_GB_ADDR_CONFIG_GOLDEN;
		break;
	}

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}

	WREG32(GRBM_CNTL, GRBM_READ_TIMEOUT(0xff));
	WREG32(SRBM_INT_CNTL, 1);
	WREG32(SRBM_INT_ACK, 1);

	evergreen_fix_pci_max_read_req_size(rdev);

	WREG32(BIF_FB_EN, FB_READ_EN | FB_WRITE_EN);

	RREG32(MC_SHARED_CHMAP);
	mc_arb_ramcfg = RREG32(MC_ARB_RAMCFG);

	rdev->config.si.num_tile_pipes = rdev->config.si.max_tile_pipes;
	rdev->config.si.mem_max_burst_length_bytes = 256;
	tmp = (mc_arb_ramcfg & NOOFCOLS_MASK) >> NOOFCOLS_SHIFT;
	rdev->config.si.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
	if (rdev->config.si.mem_row_size_in_kb > 4)
		rdev->config.si.mem_row_size_in_kb = 4;
	/* XXX use MC settings? */
	rdev->config.si.shader_engine_tile_size = 32;
	rdev->config.si.num_gpus = 1;
	rdev->config.si.multi_gpu_tile_size = 64;

	/* fix up row size */
	gb_addr_config &= ~ROW_SIZE_MASK;
	switch (rdev->config.si.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config |= ROW_SIZE(0);
		break;
	case 2:
		gb_addr_config |= ROW_SIZE(1);
		break;
	case 4:
		gb_addr_config |= ROW_SIZE(2);
		break;
	}

	/* setup tiling info dword.  gb_addr_config is not adequate since it does
	 * not have bank info, so create a custom tiling dword.
	 * bits 3:0   num_pipes
	 * bits 7:4   num_banks
	 * bits 11:8  group_size
	 * bits 15:12 row_size
	 */
	rdev->config.si.tile_config = 0;
	switch (rdev->config.si.num_tile_pipes) {
	case 1:
		rdev->config.si.tile_config |= (0 << 0);
		break;
	case 2:
		rdev->config.si.tile_config |= (1 << 0);
		break;
	case 4:
		rdev->config.si.tile_config |= (2 << 0);
		break;
	case 8:
	default:
		/* XXX what about 12? */
		rdev->config.si.tile_config |= (3 << 0);
		break;
	}
	switch ((mc_arb_ramcfg & NOOFBANK_MASK) >> NOOFBANK_SHIFT) {
	case 0: /* four banks */
		rdev->config.si.tile_config |= 0 << 4;
		break;
	case 1: /* eight banks */
		rdev->config.si.tile_config |= 1 << 4;
		break;
	case 2: /* sixteen banks */
	default:
		rdev->config.si.tile_config |= 2 << 4;
		break;
	}
	rdev->config.si.tile_config |=
		((gb_addr_config & PIPE_INTERLEAVE_SIZE_MASK) >> PIPE_INTERLEAVE_SIZE_SHIFT) << 8;
	rdev->config.si.tile_config |=
		((gb_addr_config & ROW_SIZE_MASK) >> ROW_SIZE_SHIFT) << 12;

	WREG32(GB_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CONFIG, gb_addr_config);
	WREG32(DMIF_ADDR_CALC, gb_addr_config);
	WREG32(HDP_ADDR_CONFIG, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA0_REGISTER_OFFSET, gb_addr_config);
	WREG32(DMA_TILING_CONFIG + DMA1_REGISTER_OFFSET, gb_addr_config);
	if (rdev->has_uvd) {
		WREG32(UVD_UDEC_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DB_ADDR_CONFIG, gb_addr_config);
		WREG32(UVD_UDEC_DBW_ADDR_CONFIG, gb_addr_config);
	}

	si_tiling_mode_table_init(rdev);

	si_setup_rb(rdev, rdev->config.si.max_shader_engines,
		    rdev->config.si.max_sh_per_se,
		    rdev->config.si.max_backends_per_se);

	si_setup_spi(rdev, rdev->config.si.max_shader_engines,
		     rdev->config.si.max_sh_per_se,
		     rdev->config.si.max_cu_per_sh);

	rdev->config.si.active_cus = 0;
	for (i = 0; i < rdev->config.si.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.si.max_sh_per_se; j++) {
			rdev->config.si.active_cus +=
				hweight32(si_get_cu_active_bitmap(rdev, i, j));
		}
	}

	/* set HW defaults for 3D engine */
	WREG32(CP_QUEUE_THRESHOLDS, (ROQ_IB1_START(0x16) |
				     ROQ_IB2_START(0x2b)));
	WREG32(CP_MEQ_THRESHOLDS, MEQ1_START(0x30) | MEQ2_START(0x60));

	sx_debug_1 = RREG32(SX_DEBUG_1);
	WREG32(SX_DEBUG_1, sx_debug_1);

	WREG32(SPI_CONFIG_CNTL_1, VTX_DONE_DELAY(4));

	WREG32(PA_SC_FIFO_SIZE, (SC_FRONTEND_PRIM_FIFO_SIZE(rdev->config.si.sc_prim_fifo_size_frontend) |
				 SC_BACKEND_PRIM_FIFO_SIZE(rdev->config.si.sc_prim_fifo_size_backend) |
				 SC_HIZ_TILE_FIFO_SIZE(rdev->config.si.sc_hiz_tile_fifo_size) |
				 SC_EARLYZ_TILE_FIFO_SIZE(rdev->config.si.sc_earlyz_tile_fifo_size)));

	WREG32(VGT_NUM_INSTANCES, 1);

	WREG32(CP_PERFMON_CNTL, 0);

	WREG32(SQ_CONFIG, 0);

	WREG32(PA_SC_FORCE_EOV_MAX_CNTS, (FORCE_EOV_MAX_CLK_CNT(4095) |
					  FORCE_EOV_MAX_REZ_CNT(255)));

	WREG32(VGT_CACHE_INVALIDATION, CACHE_INVALIDATION(VC_AND_TC) |
	       AUTO_INVLD_EN(ES_AND_GS_AUTO));

	WREG32(VGT_GS_VERTEX_REUSE, 16);
	WREG32(PA_SC_LINE_STIPPLE_STATE, 0);

	WREG32(CB_PERFCOUNTER0_SELECT0, 0);
	WREG32(CB_PERFCOUNTER0_SELECT1, 0);
	WREG32(CB_PERFCOUNTER1_SELECT0, 0);
	WREG32(CB_PERFCOUNTER1_SELECT1, 0);
	WREG32(CB_PERFCOUNTER2_SELECT0, 0);
	WREG32(CB_PERFCOUNTER2_SELECT1, 0);
	WREG32(CB_PERFCOUNTER3_SELECT0, 0);
	WREG32(CB_PERFCOUNTER3_SELECT1, 0);

	tmp = RREG32(HDP_MISC_CNTL);
	tmp |= HDP_FLUSH_INVALIDATE_CACHE;
	WREG32(HDP_MISC_CNTL, tmp);

	hdp_host_path_cntl = RREG32(HDP_HOST_PATH_CNTL);
	WREG32(HDP_HOST_PATH_CNTL, hdp_host_path_cntl);

	WREG32(PA_CL_ENHANCE, CLIP_VTX_REORDER_ENA | NUM_CLIP_SEQ(3));

	udelay(50);
}

/*
 * GPU scratch registers helpers function.
 */
static void si_scratch_init(struct radeon_device *rdev)
{
	int i;

	rdev->scratch.num_reg = 7;
	rdev->scratch.reg_base = SCRATCH_REG0;
	for (i = 0; i < rdev->scratch.num_reg; i++) {
		rdev->scratch.free[i] = true;
		rdev->scratch.reg[i] = rdev->scratch.reg_base + (i * 4);
	}
}

void si_fence_ring_emit(struct radeon_device *rdev,
			struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* flush read cache over gart */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
	radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
	radeon_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
			  PACKET3_TC_ACTION_ENA |
			  PACKET3_SH_KCACHE_ACTION_ENA |
			  PACKET3_SH_ICACHE_ACTION_ENA);
	radeon_ring_write(ring, 0xFFFFFFFF);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 10); /* poll interval */
	/* EVENT_WRITE_EOP - flush caches, send int */
	radeon_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	radeon_ring_write(ring, EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5));
	radeon_ring_write(ring, lower_32_bits(addr));
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xff) | DATA_SEL(1) | INT_SEL(2));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, 0);
}

/*
 * IB stuff
 */
void si_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	unsigned vm_id = ib->vm ? ib->vm->ids[ib->ring].id : 0;
	u32 header;

	if (ib->is_const_ib) {
		/* set switch buffer packet before const IB */
		radeon_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		radeon_ring_write(ring, 0);

		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	} else {
		u32 next_rptr;
		if (ring->rptr_save_reg) {
			next_rptr = ring->wptr + 3 + 4 + 8;
			radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
			radeon_ring_write(ring, ((ring->rptr_save_reg -
						  PACKET3_SET_CONFIG_REG_START) >> 2));
			radeon_ring_write(ring, next_rptr);
		} else if (rdev->wb.enabled) {
			next_rptr = ring->wptr + 5 + 4 + 8;
			radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
			radeon_ring_write(ring, (1 << 8));
			radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
			radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr));
			radeon_ring_write(ring, next_rptr);
		}

		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);
	}

	radeon_ring_write(ring, header);
	radeon_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	radeon_ring_write(ring, ib->length_dw | (vm_id << 24));

	if (!ib->is_const_ib) {
		/* flush read cache over gart for this vmid */
		radeon_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		radeon_ring_write(ring, (CP_COHER_CNTL2 - PACKET3_SET_CONFIG_REG_START) >> 2);
		radeon_ring_write(ring, vm_id);
		radeon_ring_write(ring, PACKET3(PACKET3_SURFACE_SYNC, 3));
		radeon_ring_write(ring, PACKET3_TCL1_ACTION_ENA |
				  PACKET3_TC_ACTION_ENA |
				  PACKET3_SH_KCACHE_ACTION_ENA |
				  PACKET3_SH_ICACHE_ACTION_ENA);
		radeon_ring_write(ring, 0xFFFFFFFF);
		radeon_ring_write(ring, 0);
		radeon_ring_write(ring, 10); /* poll interval */
	}
}

/*
 * CP.
 */
static void si_cp_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		WREG32(CP_ME_CNTL, 0);
	else {
		if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
			radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);
		WREG32(CP_ME_CNTL, (CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT));
		WREG32(SCRATCH_UMSK, 0);
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	}
	udelay(50);
}

static int si_cp_load_microcode(struct radeon_device *rdev)
{
	int i;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw)
		return -EINVAL;

	si_cp_enable(rdev, false);

	if (rdev->new_fw) {
		const struct gfx_firmware_header_v1_0 *pfp_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->pfp_fw->data;
		const struct gfx_firmware_header_v1_0 *ce_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->ce_fw->data;
		const struct gfx_firmware_header_v1_0 *me_hdr =
			(const struct gfx_firmware_header_v1_0 *)rdev->me_fw->data;
		const __le32 *fw_data;
		u32 fw_size;

		radeon_ucode_print_gfx_hdr(&pfp_hdr->header);
		radeon_ucode_print_gfx_hdr(&ce_hdr->header);
		radeon_ucode_print_gfx_hdr(&me_hdr->header);

		/* PFP */
		fw_data = (const __le32 *)
			(rdev->pfp_fw->data + le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_PFP_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_PFP_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_PFP_UCODE_ADDR, 0);

		/* CE */
		fw_data = (const __le32 *)
			(rdev->ce_fw->data + le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_CE_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_CE_UCODE_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_CE_UCODE_ADDR, 0);

		/* ME */
		fw_data = (const __be32 *)
			(rdev->me_fw->data + le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes) / 4;
		WREG32(CP_ME_RAM_WADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(CP_ME_RAM_DATA, le32_to_cpup(fw_data++));
		WREG32(CP_ME_RAM_WADDR, 0);
	} else {
		const __be32 *fw_data;

		/* PFP */
		fw_data = (const __be32 *)rdev->pfp_fw->data;
		WREG32(CP_PFP_UCODE_ADDR, 0);
		for (i = 0; i < SI_PFP_UCODE_SIZE; i++)
			WREG32(CP_PFP_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_PFP_UCODE_ADDR, 0);

		/* CE */
		fw_data = (const __be32 *)rdev->ce_fw->data;
		WREG32(CP_CE_UCODE_ADDR, 0);
		for (i = 0; i < SI_CE_UCODE_SIZE; i++)
			WREG32(CP_CE_UCODE_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_CE_UCODE_ADDR, 0);

		/* ME */
		fw_data = (const __be32 *)rdev->me_fw->data;
		WREG32(CP_ME_RAM_WADDR, 0);
		for (i = 0; i < SI_PM4_UCODE_SIZE; i++)
			WREG32(CP_ME_RAM_DATA, be32_to_cpup(fw_data++));
		WREG32(CP_ME_RAM_WADDR, 0);
	}

	WREG32(CP_PFP_UCODE_ADDR, 0);
	WREG32(CP_CE_UCODE_ADDR, 0);
	WREG32(CP_ME_RAM_WADDR, 0);
	WREG32(CP_ME_RAM_RADDR, 0);
	return 0;
}

static int si_cp_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r, i;

	r = radeon_ring_lock(rdev, ring, 7 + 4);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}
	/* init the CP */
	radeon_ring_write(ring, PACKET3(PACKET3_ME_INITIALIZE, 5));
	radeon_ring_write(ring, 0x1);
	radeon_ring_write(ring, 0x0);
	radeon_ring_write(ring, rdev->config.si.max_hw_contexts - 1);
	radeon_ring_write(ring, PACKET3_ME_INITIALIZE_DEVICE_ID(1));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0);

	/* init the CE partitions */
	radeon_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	radeon_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	radeon_ring_write(ring, 0xc000);
	radeon_ring_write(ring, 0xe000);
	radeon_ring_unlock_commit(rdev, ring, false);

	si_cp_enable(rdev, true);

	r = radeon_ring_lock(rdev, ring, si_default_size + 10);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* setup clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	for (i = 0; i < si_default_size; i++)
		radeon_ring_write(ring, si_default_state[i]);

	radeon_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	radeon_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	/* set clear context state */
	radeon_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	radeon_ring_write(ring, 0x00000316);
	radeon_ring_write(ring, 0x0000000e); /* VGT_VERTEX_REUSE_BLOCK_CNTL */
	radeon_ring_write(ring, 0x00000010); /* VGT_OUT_DEALLOC_CNTL */

	radeon_ring_unlock_commit(rdev, ring, false);

	for (i = RADEON_RING_TYPE_GFX_INDEX; i <= CAYMAN_RING_TYPE_CP2_INDEX; ++i) {
		ring = &rdev->ring[i];
		r = radeon_ring_lock(rdev, ring, 2);
		if (r) {
			DRM_ERROR("radeon: cp failed to lock ring (%d).\n", r);
			return r;
		}

		/* clear the compute context state */
		radeon_ring_write(ring, PACKET3_COMPUTE(PACKET3_CLEAR_STATE, 0));
		radeon_ring_write(ring, 0);

		radeon_ring_unlock_commit(rdev, ring, false);
	}

	return 0;
}

static void si_cp_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	si_cp_enable(rdev, false);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	radeon_ring_fini(rdev, ring);
	radeon_scratch_free(rdev, ring->rptr_save_reg);
}

static int si_cp_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	int r;

	si_enable_gui_idle_interrupt(rdev, false);

	WREG32(CP_SEM_WAIT_TIMER, 0x0);
	WREG32(CP_SEM_INCOMPLETE_TIMER_CNTL, 0x0);

	/* Set the write pointer delay */
	WREG32(CP_RB_WPTR_DELAY, 0);

	WREG32(CP_DEBUG, 0);
	WREG32(SCRATCH_ADDR, ((rdev->wb.gpu_addr + RADEON_WB_SCRATCH_OFFSET) >> 8) & 0xFFFFFFFF);

	/* ring 0 - compute and gfx */
	/* Set ring buffer size */
	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB0_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB0_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB0_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB0_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP_RPTR_OFFSET) & 0xFF);

	if (rdev->wb.enabled)
		WREG32(SCRATCH_UMSK, 0xff);
	else {
		tmp |= RB_NO_UPDATE;
		WREG32(SCRATCH_UMSK, 0);
	}

	mdelay(1);
	WREG32(CP_RB0_CNTL, tmp);

	WREG32(CP_RB0_BASE, ring->gpu_addr >> 8);

	/* ring1  - compute only */
	/* Set ring buffer size */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB1_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB1_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB1_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB1_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP1_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB1_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP1_RPTR_OFFSET) & 0xFF);

	mdelay(1);
	WREG32(CP_RB1_CNTL, tmp);

	WREG32(CP_RB1_BASE, ring->gpu_addr >> 8);

	/* ring2 - compute only */
	/* Set ring buffer size */
	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = (order_base_2(RADEON_GPU_PAGE_SIZE/8) << 8) | rb_bufsz;
#ifdef __BIG_ENDIAN
	tmp |= BUF_SWAP_32BIT;
#endif
	WREG32(CP_RB2_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(CP_RB2_CNTL, tmp | RB_RPTR_WR_ENA);
	ring->wptr = 0;
	WREG32(CP_RB2_WPTR, ring->wptr);

	/* set the wb address whether it's enabled or not */
	WREG32(CP_RB2_RPTR_ADDR, (rdev->wb.gpu_addr + RADEON_WB_CP2_RPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(CP_RB2_RPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + RADEON_WB_CP2_RPTR_OFFSET) & 0xFF);

	mdelay(1);
	WREG32(CP_RB2_CNTL, tmp);

	WREG32(CP_RB2_BASE, ring->gpu_addr >> 8);

	/* start the rings */
	si_cp_start(rdev);
	rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = true;
	rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = true;
	r = radeon_ring_test(rdev, RADEON_RING_TYPE_GFX_INDEX, &rdev->ring[RADEON_RING_TYPE_GFX_INDEX]);
	if (r) {
		rdev->ring[RADEON_RING_TYPE_GFX_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
		return r;
	}
	r = radeon_ring_test(rdev, CAYMAN_RING_TYPE_CP1_INDEX, &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX]);
	if (r) {
		rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX].ready = false;
	}
	r = radeon_ring_test(rdev, CAYMAN_RING_TYPE_CP2_INDEX, &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX]);
	if (r) {
		rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX].ready = false;
	}

	si_enable_gui_idle_interrupt(rdev, true);

	if (rdev->asic->copy.copy_ring_index == RADEON_RING_TYPE_GFX_INDEX)
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

u32 si_gpu_check_soft_reset(struct radeon_device *rdev)
{
	u32 reset_mask = 0;
	u32 tmp;

	/* GRBM_STATUS */
	tmp = RREG32(GRBM_STATUS);
	if (tmp & (PA_BUSY | SC_BUSY |
		   BCI_BUSY | SX_BUSY |
		   TA_BUSY | VGT_BUSY |
		   DB_BUSY | CB_BUSY |
		   GDS_BUSY | SPI_BUSY |
		   IA_BUSY | IA_BUSY_NO_DMA))
		reset_mask |= RADEON_RESET_GFX;

	if (tmp & (CF_RQ_PENDING | PF_RQ_PENDING |
		   CP_BUSY | CP_COHERENCY_BUSY))
		reset_mask |= RADEON_RESET_CP;

	if (tmp & GRBM_EE_BUSY)
		reset_mask |= RADEON_RESET_GRBM | RADEON_RESET_GFX | RADEON_RESET_CP;

	/* GRBM_STATUS2 */
	tmp = RREG32(GRBM_STATUS2);
	if (tmp & (RLC_RQ_PENDING | RLC_BUSY))
		reset_mask |= RADEON_RESET_RLC;

	/* DMA_STATUS_REG 0 */
	tmp = RREG32(DMA_STATUS_REG + DMA0_REGISTER_OFFSET);
	if (!(tmp & DMA_IDLE))
		reset_mask |= RADEON_RESET_DMA;

	/* DMA_STATUS_REG 1 */
	tmp = RREG32(DMA_STATUS_REG + DMA1_REGISTER_OFFSET);
	if (!(tmp & DMA_IDLE))
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS2 */
	tmp = RREG32(SRBM_STATUS2);
	if (tmp & DMA_BUSY)
		reset_mask |= RADEON_RESET_DMA;

	if (tmp & DMA1_BUSY)
		reset_mask |= RADEON_RESET_DMA1;

	/* SRBM_STATUS */
	tmp = RREG32(SRBM_STATUS);

	if (tmp & IH_BUSY)
		reset_mask |= RADEON_RESET_IH;

	if (tmp & SEM_BUSY)
		reset_mask |= RADEON_RESET_SEM;

	if (tmp & GRBM_RQ_PENDING)
		reset_mask |= RADEON_RESET_GRBM;

	if (tmp & VMC_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	if (tmp & (MCB_BUSY | MCB_NON_DISPLAY_BUSY |
		   MCC_BUSY | MCD_BUSY))
		reset_mask |= RADEON_RESET_MC;

	if (evergreen_is_display_hung(rdev))
		reset_mask |= RADEON_RESET_DISPLAY;

	/* VM_L2_STATUS */
	tmp = RREG32(VM_L2_STATUS);
	if (tmp & L2_BUSY)
		reset_mask |= RADEON_RESET_VMC;

	/* Skip MC reset as it's mostly likely not hung, just busy */
	if (reset_mask & RADEON_RESET_MC) {
		DRM_DEBUG("MC busy: 0x%08X, clearing.\n", reset_mask);
		reset_mask &= ~RADEON_RESET_MC;
	}

	return reset_mask;
}

static void si_gpu_soft_reset(struct radeon_device *rdev, u32 reset_mask)
{
	struct evergreen_mc_save save;
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;

	if (reset_mask == 0)
		return;

	dev_info(rdev->dev, "GPU softreset: 0x%08X\n", reset_mask);

	evergreen_print_gpu_status_regs(rdev);
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR));
	dev_info(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
		 RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS));

	/* disable PG/CG */
	si_fini_pg(rdev);
	si_fini_cg(rdev);

	/* stop the rlc */
	si_rlc_stop(rdev);

	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);

	if (reset_mask & RADEON_RESET_DMA) {
		/* dma0 */
		tmp = RREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET);
		tmp &= ~DMA_RB_ENABLE;
		WREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET, tmp);
	}
	if (reset_mask & RADEON_RESET_DMA1) {
		/* dma1 */
		tmp = RREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET);
		tmp &= ~DMA_RB_ENABLE;
		WREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET, tmp);
	}

	udelay(50);

	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}

	if (reset_mask & (RADEON_RESET_GFX | RADEON_RESET_COMPUTE | RADEON_RESET_CP)) {
		grbm_soft_reset = SOFT_RESET_CB |
			SOFT_RESET_DB |
			SOFT_RESET_GDS |
			SOFT_RESET_PA |
			SOFT_RESET_SC |
			SOFT_RESET_BCI |
			SOFT_RESET_SPI |
			SOFT_RESET_SX |
			SOFT_RESET_TC |
			SOFT_RESET_TA |
			SOFT_RESET_VGT |
			SOFT_RESET_IA;
	}

	if (reset_mask & RADEON_RESET_CP) {
		grbm_soft_reset |= SOFT_RESET_CP | SOFT_RESET_VGT;

		srbm_soft_reset |= SOFT_RESET_GRBM;
	}

	if (reset_mask & RADEON_RESET_DMA)
		srbm_soft_reset |= SOFT_RESET_DMA;

	if (reset_mask & RADEON_RESET_DMA1)
		srbm_soft_reset |= SOFT_RESET_DMA1;

	if (reset_mask & RADEON_RESET_DISPLAY)
		srbm_soft_reset |= SOFT_RESET_DC;

	if (reset_mask & RADEON_RESET_RLC)
		grbm_soft_reset |= SOFT_RESET_RLC;

	if (reset_mask & RADEON_RESET_SEM)
		srbm_soft_reset |= SOFT_RESET_SEM;

	if (reset_mask & RADEON_RESET_IH)
		srbm_soft_reset |= SOFT_RESET_IH;

	if (reset_mask & RADEON_RESET_GRBM)
		srbm_soft_reset |= SOFT_RESET_GRBM;

	if (reset_mask & RADEON_RESET_VMC)
		srbm_soft_reset |= SOFT_RESET_VMC;

	if (reset_mask & RADEON_RESET_MC)
		srbm_soft_reset |= SOFT_RESET_MC;

	if (grbm_soft_reset) {
		tmp = RREG32(GRBM_SOFT_RESET);
		tmp |= grbm_soft_reset;
		dev_info(rdev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~grbm_soft_reset;
		WREG32(GRBM_SOFT_RESET, tmp);
		tmp = RREG32(GRBM_SOFT_RESET);
	}

	if (srbm_soft_reset) {
		tmp = RREG32(SRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(rdev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(SRBM_SOFT_RESET, tmp);
		tmp = RREG32(SRBM_SOFT_RESET);
	}

	/* Wait a little for things to settle down */
	udelay(50);

	evergreen_mc_resume(rdev, &save);
	udelay(50);

	evergreen_print_gpu_status_regs(rdev);
}

static void si_set_clk_bypass_mode(struct radeon_device *rdev)
{
	u32 tmp, i;

	tmp = RREG32(CG_SPLL_FUNC_CNTL);
	tmp |= SPLL_BYPASS_EN;
	WREG32(CG_SPLL_FUNC_CNTL, tmp);

	tmp = RREG32(CG_SPLL_FUNC_CNTL_2);
	tmp |= SPLL_CTLREQ_CHG;
	WREG32(CG_SPLL_FUNC_CNTL_2, tmp);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(SPLL_STATUS) & SPLL_CHG_STATUS)
			break;
		udelay(1);
	}

	tmp = RREG32(CG_SPLL_FUNC_CNTL_2);
	tmp &= ~(SPLL_CTLREQ_CHG | SCLK_MUX_UPDATE);
	WREG32(CG_SPLL_FUNC_CNTL_2, tmp);

	tmp = RREG32(MPLL_CNTL_MODE);
	tmp &= ~MPLL_MCLK_SEL;
	WREG32(MPLL_CNTL_MODE, tmp);
}

static void si_spll_powerdown(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(SPLL_CNTL_MODE);
	tmp |= SPLL_SW_DIR_CONTROL;
	WREG32(SPLL_CNTL_MODE, tmp);

	tmp = RREG32(CG_SPLL_FUNC_CNTL);
	tmp |= SPLL_RESET;
	WREG32(CG_SPLL_FUNC_CNTL, tmp);

	tmp = RREG32(CG_SPLL_FUNC_CNTL);
	tmp |= SPLL_SLEEP;
	WREG32(CG_SPLL_FUNC_CNTL, tmp);

	tmp = RREG32(SPLL_CNTL_MODE);
	tmp &= ~SPLL_SW_DIR_CONTROL;
	WREG32(SPLL_CNTL_MODE, tmp);
}

static void si_gpu_pci_config_reset(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 tmp, i;

	dev_info(rdev->dev, "GPU pci config reset\n");

	/* disable dpm? */

	/* disable cg/pg */
	si_fini_pg(rdev);
	si_fini_cg(rdev);

	/* Disable CP parsing/prefetching */
	WREG32(CP_ME_CNTL, CP_ME_HALT | CP_PFP_HALT | CP_CE_HALT);
	/* dma0 */
	tmp = RREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET);
	tmp &= ~DMA_RB_ENABLE;
	WREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET, tmp);
	/* dma1 */
	tmp = RREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET);
	tmp &= ~DMA_RB_ENABLE;
	WREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET, tmp);
	/* XXX other engines? */

	/* halt the rlc, disable cp internal ints */
	si_rlc_stop(rdev);

	udelay(50);

	/* disable mem access */
	evergreen_mc_stop(rdev, &save);
	if (evergreen_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timed out !\n");
	}

	/* set mclk/sclk to bypass */
	si_set_clk_bypass_mode(rdev);
	/* powerdown spll */
	si_spll_powerdown(rdev);
	/* disable BM */
	pci_clear_master(rdev->pdev);
	/* reset */
	radeon_pci_config_reset(rdev);
	/* wait for asic to come out of reset */
	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(CONFIG_MEMSIZE) != 0xffffffff)
			break;
		udelay(1);
	}
}

int si_asic_reset(struct radeon_device *rdev, bool hard)
{
	u32 reset_mask;

	if (hard) {
		si_gpu_pci_config_reset(rdev);
		return 0;
	}

	reset_mask = si_gpu_check_soft_reset(rdev);

	if (reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, true);

	/* try soft reset */
	si_gpu_soft_reset(rdev, reset_mask);

	reset_mask = si_gpu_check_soft_reset(rdev);

	/* try pci config reset */
	if (reset_mask && radeon_hard_reset)
		si_gpu_pci_config_reset(rdev);

	reset_mask = si_gpu_check_soft_reset(rdev);

	if (!reset_mask)
		r600_set_bios_scratch_engine_hung(rdev, false);

	return 0;
}

/**
 * si_gfx_is_lockup - Check if the GFX engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the GFX engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool si_gfx_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = si_gpu_check_soft_reset(rdev);

	if (!(reset_mask & (RADEON_RESET_GFX |
			    RADEON_RESET_COMPUTE |
			    RADEON_RESET_CP))) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

/* MC */
static void si_mc_program(struct radeon_device *rdev)
{
	struct evergreen_mc_save save;
	u32 tmp;
	int i, j;

	/* Initialize HDP */
	for (i = 0, j = 0; i < 32; i++, j += 0x18) {
		WREG32((0x2c14 + j), 0x00000000);
		WREG32((0x2c18 + j), 0x00000000);
		WREG32((0x2c1c + j), 0x00000000);
		WREG32((0x2c20 + j), 0x00000000);
		WREG32((0x2c24 + j), 0x00000000);
	}
	WREG32(HDP_REG_COHERENCY_FLUSH_CNTL, 0);

	evergreen_mc_stop(rdev, &save);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	if (!ASIC_IS_NODCE(rdev))
		/* Lockout access through VGA aperture*/
		WREG32(VGA_HDP_CONTROL, VGA_MEMORY_DISABLE);
	/* Update configuration */
	WREG32(MC_VM_SYSTEM_APERTURE_LOW_ADDR,
	       rdev->mc.vram_start >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_HIGH_ADDR,
	       rdev->mc.vram_end >> 12);
	WREG32(MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR,
	       rdev->vram_scratch.gpu_addr >> 12);
	tmp = ((rdev->mc.vram_end >> 24) & 0xFFFF) << 16;
	tmp |= ((rdev->mc.vram_start >> 24) & 0xFFFF);
	WREG32(MC_VM_FB_LOCATION, tmp);
	/* XXX double check these! */
	WREG32(HDP_NONSURFACE_BASE, (rdev->mc.vram_start >> 8));
	WREG32(HDP_NONSURFACE_INFO, (2 << 7) | (1 << 30));
	WREG32(HDP_NONSURFACE_SIZE, 0x3FFFFFFF);
	WREG32(MC_VM_AGP_BASE, 0);
	WREG32(MC_VM_AGP_TOP, 0x0FFFFFFF);
	WREG32(MC_VM_AGP_BOT, 0x0FFFFFFF);
	if (radeon_mc_wait_for_idle(rdev)) {
		dev_warn(rdev->dev, "Wait for MC idle timedout !\n");
	}
	evergreen_mc_resume(rdev, &save);
	if (!ASIC_IS_NODCE(rdev)) {
		/* we need to own VRAM, so turn off the VGA renderer here
		 * to stop it overwriting our objects */
		rv515_vga_render_disable(rdev);
	}
}

void si_vram_gtt_location(struct radeon_device *rdev,
			  struct radeon_mc *mc)
{
	if (mc->mc_vram_size > 0xFFC0000000ULL) {
		/* leave room for at least 1024M GTT */
		dev_warn(rdev->dev, "limiting VRAM\n");
		mc->real_vram_size = 0xFFC0000000ULL;
		mc->mc_vram_size = 0xFFC0000000ULL;
	}
	radeon_vram_location(rdev, &rdev->mc, 0);
	rdev->mc.gtt_base_align = 0;
	radeon_gtt_location(rdev, mc);
}

static int si_mc_init(struct radeon_device *rdev)
{
	u32 tmp;
	int chansize, numchan;

	/* Get VRAM informations */
	rdev->mc.vram_is_ddr = true;
	tmp = RREG32(MC_ARB_RAMCFG);
	if (tmp & CHANSIZE_OVERRIDE) {
		chansize = 16;
	} else if (tmp & CHANSIZE_MASK) {
		chansize = 64;
	} else {
		chansize = 32;
	}
	tmp = RREG32(MC_SHARED_CHMAP);
	switch ((tmp & NOOFCHAN_MASK) >> NOOFCHAN_SHIFT) {
	case 0:
	default:
		numchan = 1;
		break;
	case 1:
		numchan = 2;
		break;
	case 2:
		numchan = 4;
		break;
	case 3:
		numchan = 8;
		break;
	case 4:
		numchan = 3;
		break;
	case 5:
		numchan = 6;
		break;
	case 6:
		numchan = 10;
		break;
	case 7:
		numchan = 12;
		break;
	case 8:
		numchan = 16;
		break;
	}
	rdev->mc.vram_width = numchan * chansize;
	/* Could aper size report 0 ? */
	rdev->mc.aper_base = rdev->fb_aper_offset;
	rdev->mc.aper_size = rdev->fb_aper_size;
	/* size in MB on si */
	tmp = RREG32(CONFIG_MEMSIZE);
	/* some boards may have garbage in the upper 16 bits */
	if (tmp & 0xffff0000) {
		DRM_INFO("Probable bad vram size: 0x%08x\n", tmp);
		if (tmp & 0xffff)
			tmp &= 0xffff;
	}
	rdev->mc.mc_vram_size = tmp * 1024ULL * 1024ULL;
	rdev->mc.real_vram_size = rdev->mc.mc_vram_size;
	rdev->mc.visible_vram_size = rdev->mc.aper_size;
	si_vram_gtt_location(rdev, &rdev->mc);
	radeon_update_bandwidth_info(rdev);

	return 0;
}

/*
 * GART
 */
void si_pcie_gart_tlb_flush(struct radeon_device *rdev)
{
	/* flush hdp cache */
	WREG32(HDP_MEM_COHERENCY_FLUSH_CNTL, 0x1);

	/* bits 0-15 are the VM contexts0-15 */
	WREG32(VM_INVALIDATE_REQUEST, 1);
}

static int si_pcie_gart_enable(struct radeon_device *rdev)
{
	int r, i;

	if (rdev->gart.robj == NULL) {
		dev_err(rdev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}
	r = radeon_gart_table_vram_pin(rdev);
	if (r)
		return r;
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL,
	       (0xA << 7) |
	       ENABLE_L1_TLB |
	       ENABLE_L1_FRAGMENT_PROCESSING |
	       SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       ENABLE_ADVANCED_DRIVER_MODEL |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_CACHE |
	       ENABLE_L2_FRAGMENT_PROCESSING |
	       ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS | INVALIDATE_L2_CACHE);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       BANK_SELECT(4) |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(4));
	/* setup context0 */
	WREG32(VM_CONTEXT0_PAGE_TABLE_START_ADDR, rdev->mc.gtt_start >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_END_ADDR, rdev->mc.gtt_end >> 12);
	WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR, rdev->gart.table_addr >> 12);
	WREG32(VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR,
			(u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT0_CNTL2, 0);
	WREG32(VM_CONTEXT0_CNTL, (ENABLE_CONTEXT | PAGE_TABLE_DEPTH(0) |
				  RANGE_PROTECTION_FAULT_ENABLE_DEFAULT));

	WREG32(0x15D4, 0);
	WREG32(0x15D8, 0);
	WREG32(0x15DC, 0);

	/* empty context1-15 */
	/* set vm size, must be a multiple of 4 */
	WREG32(VM_CONTEXT1_PAGE_TABLE_START_ADDR, 0);
	WREG32(VM_CONTEXT1_PAGE_TABLE_END_ADDR, rdev->vm_manager.max_pfn - 1);
	/* Assign the pt base to something valid for now; the pts used for
	 * the VMs are determined by the application and setup and assigned
	 * on the fly in the vm part of radeon_gart.c
	 */
	for (i = 1; i < 16; i++) {
		if (i < 8)
			WREG32(VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2),
			       rdev->vm_manager.saved_table_addr[i]);
		else
			WREG32(VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2),
			       rdev->vm_manager.saved_table_addr[i]);
	}

	/* enable context1-15 */
	WREG32(VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR,
	       (u32)(rdev->dummy_page.addr >> 12));
	WREG32(VM_CONTEXT1_CNTL2, 4);
	WREG32(VM_CONTEXT1_CNTL, ENABLE_CONTEXT | PAGE_TABLE_DEPTH(1) |
				PAGE_TABLE_BLOCK_SIZE(radeon_vm_block_size - 9) |
				RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				RANGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT |
				PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT |
				PDE0_PROTECTION_FAULT_ENABLE_DEFAULT |
				VALID_PROTECTION_FAULT_ENABLE_INTERRUPT |
				VALID_PROTECTION_FAULT_ENABLE_DEFAULT |
				READ_PROTECTION_FAULT_ENABLE_INTERRUPT |
				READ_PROTECTION_FAULT_ENABLE_DEFAULT |
				WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT |
				WRITE_PROTECTION_FAULT_ENABLE_DEFAULT);

	si_pcie_gart_tlb_flush(rdev);
	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(rdev->mc.gtt_size >> 20),
		 (unsigned long long)rdev->gart.table_addr);
	rdev->gart.ready = true;
	return 0;
}

static void si_pcie_gart_disable(struct radeon_device *rdev)
{
	unsigned i;

	for (i = 1; i < 16; ++i) {
		uint32_t reg;
		if (i < 8)
			reg = VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (i << 2);
		else
			reg = VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((i - 8) << 2);
		rdev->vm_manager.saved_table_addr[i] = RREG32(reg);
	}

	/* Disable all tables */
	WREG32(VM_CONTEXT0_CNTL, 0);
	WREG32(VM_CONTEXT1_CNTL, 0);
	/* Setup TLB control */
	WREG32(MC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE_NOT_IN_SYS |
	       SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU);
	/* Setup L2 cache */
	WREG32(VM_L2_CNTL, ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE |
	       ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE |
	       EFFECTIVE_L2_QUEUE_SIZE(7) |
	       CONTEXT1_IDENTITY_ACCESS_MODE(1));
	WREG32(VM_L2_CNTL2, 0);
	WREG32(VM_L2_CNTL3, L2_CACHE_BIGK_ASSOCIATIVITY |
	       L2_CACHE_BIGK_FRAGMENT_SIZE(0));
	radeon_gart_table_vram_unpin(rdev);
}

static void si_pcie_gart_fini(struct radeon_device *rdev)
{
	si_pcie_gart_disable(rdev);
	radeon_gart_table_vram_free(rdev);
	radeon_gart_fini(rdev);
}

/* vm parser */
static bool si_vm_reg_valid(u32 reg)
{
	/* context regs are fine */
	if (reg >= 0x28000)
		return true;

	/* shader regs are also fine */
	if (reg >= 0xB000 && reg < 0xC000)
		return true;

	/* check config regs */
	switch (reg) {
	case GRBM_GFX_INDEX:
	case CP_STRMOUT_CNTL:
	case VGT_VTX_VECT_EJECT_REG:
	case VGT_CACHE_INVALIDATION:
	case VGT_ESGS_RING_SIZE:
	case VGT_GSVS_RING_SIZE:
	case VGT_GS_VERTEX_REUSE:
	case VGT_PRIMITIVE_TYPE:
	case VGT_INDEX_TYPE:
	case VGT_NUM_INDICES:
	case VGT_NUM_INSTANCES:
	case VGT_TF_RING_SIZE:
	case VGT_HS_OFFCHIP_PARAM:
	case VGT_TF_MEMORY_BASE:
	case PA_CL_ENHANCE:
	case PA_SU_LINE_STIPPLE_VALUE:
	case PA_SC_LINE_STIPPLE_STATE:
	case PA_SC_ENHANCE:
	case SQC_CACHES:
	case SPI_STATIC_THREAD_MGMT_1:
	case SPI_STATIC_THREAD_MGMT_2:
	case SPI_STATIC_THREAD_MGMT_3:
	case SPI_PS_MAX_WAVE_ID:
	case SPI_CONFIG_CNTL:
	case SPI_CONFIG_CNTL_1:
	case TA_CNTL_AUX:
	case TA_CS_BC_BASE_ADDR:
		return true;
	default:
		DRM_ERROR("Invalid register 0x%x in CS\n", reg);
		return false;
	}
}

static int si_vm_packet3_ce_check(struct radeon_device *rdev,
				  u32 *ib, struct radeon_cs_packet *pkt)
{
	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_SET_CE_DE_COUNTERS:
	case PACKET3_LOAD_CONST_RAM:
	case PACKET3_WRITE_CONST_RAM:
	case PACKET3_WRITE_CONST_RAM_OFFSET:
	case PACKET3_DUMP_CONST_RAM:
	case PACKET3_INCREMENT_CE_COUNTER:
	case PACKET3_WAIT_ON_DE_COUNTER:
	case PACKET3_CE_WRITE:
		break;
	default:
		DRM_ERROR("Invalid CE packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

static int si_vm_packet3_cp_dma_check(u32 *ib, u32 idx)
{
	u32 start_reg, reg, i;
	u32 command = ib[idx + 4];
	u32 info = ib[idx + 1];
	u32 idx_value = ib[idx];
	if (command & PACKET3_CP_DMA_CMD_SAS) {
		/* src address space is register */
		if (((info & 0x60000000) >> 29) == 0) {
			start_reg = idx_value << 2;
			if (command & PACKET3_CP_DMA_CMD_SAIC) {
				reg = start_reg;
				if (!si_vm_reg_valid(reg)) {
					DRM_ERROR("CP DMA Bad SRC register\n");
					return -EINVAL;
				}
			} else {
				for (i = 0; i < (command & 0x1fffff); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad SRC register\n");
						return -EINVAL;
					}
				}
			}
		}
	}
	if (command & PACKET3_CP_DMA_CMD_DAS) {
		/* dst address space is register */
		if (((info & 0x00300000) >> 20) == 0) {
			start_reg = ib[idx + 2];
			if (command & PACKET3_CP_DMA_CMD_DAIC) {
				reg = start_reg;
				if (!si_vm_reg_valid(reg)) {
					DRM_ERROR("CP DMA Bad DST register\n");
					return -EINVAL;
				}
			} else {
				for (i = 0; i < (command & 0x1fffff); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg)) {
						DRM_ERROR("CP DMA Bad DST register\n");
						return -EINVAL;
					}
				}
			}
		}
	}
	return 0;
}

static int si_vm_packet3_gfx_check(struct radeon_device *rdev,
				   u32 *ib, struct radeon_cs_packet *pkt)
{
	int r;
	u32 idx = pkt->idx + 1;
	u32 idx_value = ib[idx];
	u32 start_reg, end_reg, reg, i;

	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_CLEAR_STATE:
	case PACKET3_INDEX_BUFFER_SIZE:
	case PACKET3_DISPATCH_DIRECT:
	case PACKET3_DISPATCH_INDIRECT:
	case PACKET3_ALLOC_GDS:
	case PACKET3_WRITE_GDS_RAM:
	case PACKET3_ATOMIC_GDS:
	case PACKET3_ATOMIC:
	case PACKET3_OCCLUSION_QUERY:
	case PACKET3_SET_PREDICATION:
	case PACKET3_COND_EXEC:
	case PACKET3_PRED_EXEC:
	case PACKET3_DRAW_INDIRECT:
	case PACKET3_DRAW_INDEX_INDIRECT:
	case PACKET3_INDEX_BASE:
	case PACKET3_DRAW_INDEX_2:
	case PACKET3_CONTEXT_CONTROL:
	case PACKET3_INDEX_TYPE:
	case PACKET3_DRAW_INDIRECT_MULTI:
	case PACKET3_DRAW_INDEX_AUTO:
	case PACKET3_DRAW_INDEX_IMMD:
	case PACKET3_NUM_INSTANCES:
	case PACKET3_DRAW_INDEX_MULTI_AUTO:
	case PACKET3_STRMOUT_BUFFER_UPDATE:
	case PACKET3_DRAW_INDEX_OFFSET_2:
	case PACKET3_DRAW_INDEX_MULTI_ELEMENT:
	case PACKET3_DRAW_INDEX_INDIRECT_MULTI:
	case PACKET3_MPEG_INDEX:
	case PACKET3_WAIT_REG_MEM:
	case PACKET3_MEM_WRITE:
	case PACKET3_PFP_SYNC_ME:
	case PACKET3_SURFACE_SYNC:
	case PACKET3_EVENT_WRITE:
	case PACKET3_EVENT_WRITE_EOP:
	case PACKET3_EVENT_WRITE_EOS:
	case PACKET3_SET_CONTEXT_REG:
	case PACKET3_SET_CONTEXT_REG_INDIRECT:
	case PACKET3_SET_SH_REG:
	case PACKET3_SET_SH_REG_OFFSET:
	case PACKET3_INCREMENT_DE_COUNTER:
	case PACKET3_WAIT_ON_CE_COUNTER:
	case PACKET3_WAIT_ON_AVAIL_BUFFER:
	case PACKET3_ME_WRITE:
		break;
	case PACKET3_COPY_DATA:
		if ((idx_value & 0xf00) == 0) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_WRITE_DATA:
		if ((idx_value & 0xf00) == 0) {
			start_reg = ib[idx + 1] * 4;
			if (idx_value & 0x10000) {
				if (!si_vm_reg_valid(start_reg))
					return -EINVAL;
			} else {
				for (i = 0; i < (pkt->count - 2); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg))
						return -EINVAL;
				}
			}
		}
		break;
	case PACKET3_COND_WRITE:
		if (idx_value & 0x100) {
			reg = ib[idx + 5] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_COPY_DW:
		if (idx_value & 0x2) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_SET_CONFIG_REG:
		start_reg = (idx_value << 2) + PACKET3_SET_CONFIG_REG_START;
		end_reg = 4 * pkt->count + start_reg - 4;
		if ((start_reg < PACKET3_SET_CONFIG_REG_START) ||
		    (start_reg >= PACKET3_SET_CONFIG_REG_END) ||
		    (end_reg >= PACKET3_SET_CONFIG_REG_END)) {
			DRM_ERROR("bad PACKET3_SET_CONFIG_REG\n");
			return -EINVAL;
		}
		for (i = 0; i < pkt->count; i++) {
			reg = start_reg + (4 * i);
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
		r = si_vm_packet3_cp_dma_check(ib, idx);
		if (r)
			return r;
		break;
	default:
		DRM_ERROR("Invalid GFX packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

static int si_vm_packet3_compute_check(struct radeon_device *rdev,
				       u32 *ib, struct radeon_cs_packet *pkt)
{
	int r;
	u32 idx = pkt->idx + 1;
	u32 idx_value = ib[idx];
	u32 start_reg, reg, i;

	switch (pkt->opcode) {
	case PACKET3_NOP:
	case PACKET3_SET_BASE:
	case PACKET3_CLEAR_STATE:
	case PACKET3_DISPATCH_DIRECT:
	case PACKET3_DISPATCH_INDIRECT:
	case PACKET3_ALLOC_GDS:
	case PACKET3_WRITE_GDS_RAM:
	case PACKET3_ATOMIC_GDS:
	case PACKET3_ATOMIC:
	case PACKET3_OCCLUSION_QUERY:
	case PACKET3_SET_PREDICATION:
	case PACKET3_COND_EXEC:
	case PACKET3_PRED_EXEC:
	case PACKET3_CONTEXT_CONTROL:
	case PACKET3_STRMOUT_BUFFER_UPDATE:
	case PACKET3_WAIT_REG_MEM:
	case PACKET3_MEM_WRITE:
	case PACKET3_PFP_SYNC_ME:
	case PACKET3_SURFACE_SYNC:
	case PACKET3_EVENT_WRITE:
	case PACKET3_EVENT_WRITE_EOP:
	case PACKET3_EVENT_WRITE_EOS:
	case PACKET3_SET_CONTEXT_REG:
	case PACKET3_SET_CONTEXT_REG_INDIRECT:
	case PACKET3_SET_SH_REG:
	case PACKET3_SET_SH_REG_OFFSET:
	case PACKET3_INCREMENT_DE_COUNTER:
	case PACKET3_WAIT_ON_CE_COUNTER:
	case PACKET3_WAIT_ON_AVAIL_BUFFER:
	case PACKET3_ME_WRITE:
		break;
	case PACKET3_COPY_DATA:
		if ((idx_value & 0xf00) == 0) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_WRITE_DATA:
		if ((idx_value & 0xf00) == 0) {
			start_reg = ib[idx + 1] * 4;
			if (idx_value & 0x10000) {
				if (!si_vm_reg_valid(start_reg))
					return -EINVAL;
			} else {
				for (i = 0; i < (pkt->count - 2); i++) {
					reg = start_reg + (4 * i);
					if (!si_vm_reg_valid(reg))
						return -EINVAL;
				}
			}
		}
		break;
	case PACKET3_COND_WRITE:
		if (idx_value & 0x100) {
			reg = ib[idx + 5] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_COPY_DW:
		if (idx_value & 0x2) {
			reg = ib[idx + 3] * 4;
			if (!si_vm_reg_valid(reg))
				return -EINVAL;
		}
		break;
	case PACKET3_CP_DMA:
		r = si_vm_packet3_cp_dma_check(ib, idx);
		if (r)
			return r;
		break;
	default:
		DRM_ERROR("Invalid Compute packet3: 0x%x\n", pkt->opcode);
		return -EINVAL;
	}
	return 0;
}

int si_ib_parse(struct radeon_device *rdev, struct radeon_ib *ib)
{
	int ret = 0;
	u32 idx = 0, i;
	struct radeon_cs_packet pkt;

	do {
		pkt.idx = idx;
		pkt.type = RADEON_CP_PACKET_GET_TYPE(ib->ptr[idx]);
		pkt.count = RADEON_CP_PACKET_GET_COUNT(ib->ptr[idx]);
		pkt.one_reg_wr = 0;
		switch (pkt.type) {
		case RADEON_PACKET_TYPE0:
			dev_err(rdev->dev, "Packet0 not allowed!\n");
			ret = -EINVAL;
			break;
		case RADEON_PACKET_TYPE2:
			idx += 1;
			break;
		case RADEON_PACKET_TYPE3:
			pkt.opcode = RADEON_CP_PACKET3_GET_OPCODE(ib->ptr[idx]);
			if (ib->is_const_ib)
				ret = si_vm_packet3_ce_check(rdev, ib->ptr, &pkt);
			else {
				switch (ib->ring) {
				case RADEON_RING_TYPE_GFX_INDEX:
					ret = si_vm_packet3_gfx_check(rdev, ib->ptr, &pkt);
					break;
				case CAYMAN_RING_TYPE_CP1_INDEX:
				case CAYMAN_RING_TYPE_CP2_INDEX:
					ret = si_vm_packet3_compute_check(rdev, ib->ptr, &pkt);
					break;
				default:
					dev_err(rdev->dev, "Non-PM4 ring %d !\n", ib->ring);
					ret = -EINVAL;
					break;
				}
			}
			idx += pkt.count + 2;
			break;
		default:
			dev_err(rdev->dev, "Unknown packet type %d !\n", pkt.type);
			ret = -EINVAL;
			break;
		}
		if (ret) {
			for (i = 0; i < ib->length_dw; i++) {
				if (i == idx)
					printk("\t0x%08x <---\n", ib->ptr[i]);
				else
					printk("\t0x%08x\n", ib->ptr[i]);
			}
			break;
		}
	} while (idx < ib->length_dw);

	return ret;
}

/*
 * vm
 */
int si_vm_init(struct radeon_device *rdev)
{
	/* number of VMs */
	rdev->vm_manager.nvm = 16;
	/* base offset of vram pages */
	rdev->vm_manager.vram_base_offset = 0;

	return 0;
}

void si_vm_fini(struct radeon_device *rdev)
{
}

/**
 * si_vm_decode_fault - print human readable fault info
 *
 * @rdev: radeon_device pointer
 * @status: VM_CONTEXT1_PROTECTION_FAULT_STATUS register value
 * @addr: VM_CONTEXT1_PROTECTION_FAULT_ADDR register value
 *
 * Print human readable fault information (SI).
 */
static void si_vm_decode_fault(struct radeon_device *rdev,
			       u32 status, u32 addr)
{
	u32 mc_id = (status & MEMORY_CLIENT_ID_MASK) >> MEMORY_CLIENT_ID_SHIFT;
	u32 vmid = (status & FAULT_VMID_MASK) >> FAULT_VMID_SHIFT;
	u32 protections = (status & PROTECTIONS_MASK) >> PROTECTIONS_SHIFT;
	char *block;

	if (rdev->family == CHIP_TAHITI) {
		switch (mc_id) {
		case 160:
		case 144:
		case 96:
		case 80:
		case 224:
		case 208:
		case 32:
		case 16:
			block = "CB";
			break;
		case 161:
		case 145:
		case 97:
		case 81:
		case 225:
		case 209:
		case 33:
		case 17:
			block = "CB_FMASK";
			break;
		case 162:
		case 146:
		case 98:
		case 82:
		case 226:
		case 210:
		case 34:
		case 18:
			block = "CB_CMASK";
			break;
		case 163:
		case 147:
		case 99:
		case 83:
		case 227:
		case 211:
		case 35:
		case 19:
			block = "CB_IMMED";
			break;
		case 164:
		case 148:
		case 100:
		case 84:
		case 228:
		case 212:
		case 36:
		case 20:
			block = "DB";
			break;
		case 165:
		case 149:
		case 101:
		case 85:
		case 229:
		case 213:
		case 37:
		case 21:
			block = "DB_HTILE";
			break;
		case 167:
		case 151:
		case 103:
		case 87:
		case 231:
		case 215:
		case 39:
		case 23:
			block = "DB_STEN";
			break;
		case 72:
		case 68:
		case 64:
		case 8:
		case 4:
		case 0:
		case 136:
		case 132:
		case 128:
		case 200:
		case 196:
		case 192:
			block = "TC";
			break;
		case 112:
		case 48:
			block = "CP";
			break;
		case 49:
		case 177:
		case 50:
		case 178:
			block = "SH";
			break;
		case 53:
		case 190:
			block = "VGT";
			break;
		case 117:
			block = "IH";
			break;
		case 51:
		case 115:
			block = "RLC";
			break;
		case 119:
		case 183:
			block = "DMA0";
			break;
		case 61:
			block = "DMA1";
			break;
		case 248:
		case 120:
			block = "HDP";
			break;
		default:
			block = "unknown";
			break;
		}
	} else {
		switch (mc_id) {
		case 32:
		case 16:
		case 96:
		case 80:
		case 160:
		case 144:
		case 224:
		case 208:
			block = "CB";
			break;
		case 33:
		case 17:
		case 97:
		case 81:
		case 161:
		case 145:
		case 225:
		case 209:
			block = "CB_FMASK";
			break;
		case 34:
		case 18:
		case 98:
		case 82:
		case 162:
		case 146:
		case 226:
		case 210:
			block = "CB_CMASK";
			break;
		case 35:
		case 19:
		case 99:
		case 83:
		case 163:
		case 147:
		case 227:
		case 211:
			block = "CB_IMMED";
			break;
		case 36:
		case 20:
		case 100:
		case 84:
		case 164:
		case 148:
		case 228:
		case 212:
			block = "DB";
			break;
		case 37:
		case 21:
		case 101:
		case 85:
		case 165:
		case 149:
		case 229:
		case 213:
			block = "DB_HTILE";
			break;
		case 39:
		case 23:
		case 103:
		case 87:
		case 167:
		case 151:
		case 231:
		case 215:
			block = "DB_STEN";
			break;
		case 72:
		case 68:
		case 8:
		case 4:
		case 136:
		case 132:
		case 200:
		case 196:
			block = "TC";
			break;
		case 112:
		case 48:
			block = "CP";
			break;
		case 49:
		case 177:
		case 50:
		case 178:
			block = "SH";
			break;
		case 53:
			block = "VGT";
			break;
		case 117:
			block = "IH";
			break;
		case 51:
		case 115:
			block = "RLC";
			break;
		case 119:
		case 183:
			block = "DMA0";
			break;
		case 61:
			block = "DMA1";
			break;
		case 248:
		case 120:
			block = "HDP";
			break;
		default:
			block = "unknown";
			break;
		}
	}

	printk("VM fault (0x%02x, vmid %d) at page %u, %s from %s (%d)\n",
	       protections, vmid, addr,
	       (status & MEMORY_CLIENT_RW_MASK) ? "write" : "read",
	       block, mc_id);
}

void si_vm_flush(struct radeon_device *rdev, struct radeon_ring *ring,
		 unsigned vm_id, uint64_t pd_addr)
{
	/* write new base address */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));

	if (vm_id < 8) {
		radeon_ring_write(ring,
				  (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm_id << 2)) >> 2);
	} else {
		radeon_ring_write(ring,
				  (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm_id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, pd_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, HDP_MEM_COHERENCY_FLUSH_CNTL >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0x1);

	/* bits 0-15 are the VM contexts0-15 */
	radeon_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	radeon_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(0)));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 1 << vm_id);

	/* wait for the invalidate to complete */
	radeon_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	radeon_ring_write(ring, (WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0); /* ref */
	radeon_ring_write(ring, 0); /* mask */
	radeon_ring_write(ring, 0x20); /* poll interval */

	/* sync PFP to ME, otherwise we might get invalid PFP reads */
	radeon_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
	radeon_ring_write(ring, 0x0);
}

/*
 *  Power and clock gating
 */
static void si_wait_for_rlc_serdes(struct radeon_device *rdev)
{
	int i;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(RLC_SERDES_MASTER_BUSY_0) == 0)
			break;
		udelay(1);
	}

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(RLC_SERDES_MASTER_BUSY_1) == 0)
			break;
		udelay(1);
	}
}

static void si_enable_gui_idle_interrupt(struct radeon_device *rdev,
					 bool enable)
{
	u32 tmp = RREG32(CP_INT_CNTL_RING0);
	u32 mask;
	int i;

	if (enable)
		tmp |= (CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	else
		tmp &= ~(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);

	if (!enable) {
		/* read a gfx register */
		tmp = RREG32(DB_DEPTH_INFO);

		mask = RLC_BUSY_STATUS | GFX_POWER_STATUS | GFX_CLOCK_STATUS | GFX_LS_STATUS;
		for (i = 0; i < rdev->usec_timeout; i++) {
			if ((RREG32(RLC_STAT) & mask) == (GFX_CLOCK_STATUS | GFX_POWER_STATUS))
				break;
			udelay(1);
		}
	}
}

static void si_set_uvd_dcm(struct radeon_device *rdev,
			   bool sw_mode)
{
	u32 tmp, tmp2;

	tmp = RREG32(UVD_CGC_CTRL);
	tmp &= ~(CLK_OD_MASK | CG_DT_MASK);
	tmp |= DCM | CG_DT(1) | CLK_OD(4);

	if (sw_mode) {
		tmp &= ~0x7ffff800;
		tmp2 = DYN_OR_EN | DYN_RR_EN | G_DIV_ID(7);
	} else {
		tmp |= 0x7ffff800;
		tmp2 = 0;
	}

	WREG32(UVD_CGC_CTRL, tmp);
	WREG32_UVD_CTX(UVD_CGC_CTRL2, tmp2);
}

void si_init_uvd_internal_cg(struct radeon_device *rdev)
{
	bool hw_mode = true;

	if (hw_mode) {
		si_set_uvd_dcm(rdev, false);
	} else {
		u32 tmp = RREG32(UVD_CGC_CTRL);
		tmp &= ~DCM;
		WREG32(UVD_CGC_CTRL, tmp);
	}
}

static u32 si_halt_rlc(struct radeon_device *rdev)
{
	u32 data, orig;

	orig = data = RREG32(RLC_CNTL);

	if (data & RLC_ENABLE) {
		data &= ~RLC_ENABLE;
		WREG32(RLC_CNTL, data);

		si_wait_for_rlc_serdes(rdev);
	}

	return orig;
}

static void si_update_rlc(struct radeon_device *rdev, u32 rlc)
{
	u32 tmp;

	tmp = RREG32(RLC_CNTL);
	if (tmp != rlc)
		WREG32(RLC_CNTL, rlc);
}

static void si_enable_dma_pg(struct radeon_device *rdev, bool enable)
{
	u32 data, orig;

	orig = data = RREG32(DMA_PG);
	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_SDMA))
		data |= PG_CNTL_ENABLE;
	else
		data &= ~PG_CNTL_ENABLE;
	if (orig != data)
		WREG32(DMA_PG, data);
}

static void si_init_dma_pg(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(DMA_PGFSM_WRITE,  0x00002000);
	WREG32(DMA_PGFSM_CONFIG, 0x100010ff);

	for (tmp = 0; tmp < 5; tmp++)
		WREG32(DMA_PGFSM_WRITE, 0);
}

static void si_enable_gfx_cgpg(struct radeon_device *rdev,
			       bool enable)
{
	u32 tmp;

	if (enable && (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_PG)) {
		tmp = RLC_PUD(0x10) | RLC_PDD(0x10) | RLC_TTPD(0x10) | RLC_MSD(0x10);
		WREG32(RLC_TTOP_D, tmp);

		tmp = RREG32(RLC_PG_CNTL);
		tmp |= GFX_PG_ENABLE;
		WREG32(RLC_PG_CNTL, tmp);

		tmp = RREG32(RLC_AUTO_PG_CTRL);
		tmp |= AUTO_PG_EN;
		WREG32(RLC_AUTO_PG_CTRL, tmp);
	} else {
		tmp = RREG32(RLC_AUTO_PG_CTRL);
		tmp &= ~AUTO_PG_EN;
		WREG32(RLC_AUTO_PG_CTRL, tmp);

		tmp = RREG32(DB_RENDER_CONTROL);
	}
}

static void si_init_gfx_cgpg(struct radeon_device *rdev)
{
	u32 tmp;

	WREG32(RLC_SAVE_AND_RESTORE_BASE, rdev->rlc.save_restore_gpu_addr >> 8);

	tmp = RREG32(RLC_PG_CNTL);
	tmp |= GFX_PG_SRC;
	WREG32(RLC_PG_CNTL, tmp);

	WREG32(RLC_CLEAR_STATE_RESTORE_BASE, rdev->rlc.clear_state_gpu_addr >> 8);

	tmp = RREG32(RLC_AUTO_PG_CTRL);

	tmp &= ~GRBM_REG_SGIT_MASK;
	tmp |= GRBM_REG_SGIT(0x700);
	tmp &= ~PG_AFTER_GRBM_REG_ST_MASK;
	WREG32(RLC_AUTO_PG_CTRL, tmp);
}

static u32 si_get_cu_active_bitmap(struct radeon_device *rdev, u32 se, u32 sh)
{
	u32 mask = 0, tmp, tmp1;
	int i;

	si_select_se_sh(rdev, se, sh);
	tmp = RREG32(CC_GC_SHADER_ARRAY_CONFIG);
	tmp1 = RREG32(GC_USER_SHADER_ARRAY_CONFIG);
	si_select_se_sh(rdev, 0xffffffff, 0xffffffff);

	tmp &= 0xffff0000;

	tmp |= tmp1;
	tmp >>= 16;

	for (i = 0; i < rdev->config.si.max_cu_per_sh; i ++) {
		mask <<= 1;
		mask |= 1;
	}

	return (~tmp) & mask;
}

static void si_init_ao_cu_mask(struct radeon_device *rdev)
{
	u32 i, j, k, active_cu_number = 0;
	u32 mask, counter, cu_bitmap;
	u32 tmp = 0;

	for (i = 0; i < rdev->config.si.max_shader_engines; i++) {
		for (j = 0; j < rdev->config.si.max_sh_per_se; j++) {
			mask = 1;
			cu_bitmap = 0;
			counter  = 0;
			for (k = 0; k < rdev->config.si.max_cu_per_sh; k++) {
				if (si_get_cu_active_bitmap(rdev, i, j) & mask) {
					if (counter < 2)
						cu_bitmap |= mask;
					counter++;
				}
				mask <<= 1;
			}

			active_cu_number += counter;
			tmp |= (cu_bitmap << (i * 16 + j * 8));
		}
	}

	WREG32(RLC_PG_AO_CU_MASK, tmp);

	tmp = RREG32(RLC_MAX_PG_CU);
	tmp &= ~MAX_PU_CU_MASK;
	tmp |= MAX_PU_CU(active_cu_number);
	WREG32(RLC_MAX_PG_CU, tmp);
}

static void si_enable_cgcg(struct radeon_device *rdev,
			   bool enable)
{
	u32 data, orig, tmp;

	orig = data = RREG32(RLC_CGCG_CGLS_CTRL);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CGCG)) {
		si_enable_gui_idle_interrupt(rdev, true);

		WREG32(RLC_GCPM_GENERAL_3, 0x00000080);

		tmp = si_halt_rlc(rdev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00b000ff);

		si_wait_for_rlc_serdes(rdev);

		si_update_rlc(rdev, tmp);

		WREG32(RLC_SERDES_WR_CTRL, 0x007000ff);

		data |= CGCG_EN | CGLS_EN;
	} else {
		si_enable_gui_idle_interrupt(rdev, false);

		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);
		RREG32(CB_CGTT_SCLK_CTRL);

		data &= ~(CGCG_EN | CGLS_EN);
	}

	if (orig != data)
		WREG32(RLC_CGCG_CGLS_CTRL, data);
}

static void si_enable_mgcg(struct radeon_device *rdev,
			   bool enable)
{
	u32 data, orig, tmp = 0;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_MGCG)) {
		orig = data = RREG32(CGTS_SM_CTRL_REG);
		data = 0x96940200;
		if (orig != data)
			WREG32(CGTS_SM_CTRL_REG, data);

		if (rdev->cg_flags & RADEON_CG_SUPPORT_GFX_CP_LS) {
			orig = data = RREG32(CP_MEM_SLP_CNTL);
			data |= CP_MEM_LS_EN;
			if (orig != data)
				WREG32(CP_MEM_SLP_CNTL, data);
		}

		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data &= 0xffffffc0;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		tmp = si_halt_rlc(rdev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00d000ff);

		si_update_rlc(rdev, tmp);
	} else {
		orig = data = RREG32(RLC_CGTT_MGCG_OVERRIDE);
		data |= 0x00000003;
		if (orig != data)
			WREG32(RLC_CGTT_MGCG_OVERRIDE, data);

		data = RREG32(CP_MEM_SLP_CNTL);
		if (data & CP_MEM_LS_EN) {
			data &= ~CP_MEM_LS_EN;
			WREG32(CP_MEM_SLP_CNTL, data);
		}
		orig = data = RREG32(CGTS_SM_CTRL_REG);
		data |= LS_OVERRIDE | OVERRIDE;
		if (orig != data)
			WREG32(CGTS_SM_CTRL_REG, data);

		tmp = si_halt_rlc(rdev);

		WREG32(RLC_SERDES_WR_MASTER_MASK_0, 0xffffffff);
		WREG32(RLC_SERDES_WR_MASTER_MASK_1, 0xffffffff);
		WREG32(RLC_SERDES_WR_CTRL, 0x00e000ff);

		si_update_rlc(rdev, tmp);
	}
}

static void si_enable_uvd_mgcg(struct radeon_device *rdev,
			       bool enable)
{
	u32 orig, data, tmp;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_UVD_MGCG)) {
		tmp = RREG32_UVD_CTX(UVD_CGC_MEM_CTRL);
		tmp |= 0x3fff;
		WREG32_UVD_CTX(UVD_CGC_MEM_CTRL, tmp);

		orig = data = RREG32(UVD_CGC_CTRL);
		data |= DCM;
		if (orig != data)
			WREG32(UVD_CGC_CTRL, data);

		WREG32_SMC(SMC_CG_IND_START + CG_CGTT_LOCAL_0, 0);
		WREG32_SMC(SMC_CG_IND_START + CG_CGTT_LOCAL_1, 0);
	} else {
		tmp = RREG32_UVD_CTX(UVD_CGC_MEM_CTRL);
		tmp &= ~0x3fff;
		WREG32_UVD_CTX(UVD_CGC_MEM_CTRL, tmp);

		orig = data = RREG32(UVD_CGC_CTRL);
		data &= ~DCM;
		if (orig != data)
			WREG32(UVD_CGC_CTRL, data);

		WREG32_SMC(SMC_CG_IND_START + CG_CGTT_LOCAL_0, 0xffffffff);
		WREG32_SMC(SMC_CG_IND_START + CG_CGTT_LOCAL_1, 0xffffffff);
	}
}

static const u32 mc_cg_registers[] =
{
	MC_HUB_MISC_HUB_CG,
	MC_HUB_MISC_SIP_CG,
	MC_HUB_MISC_VM_CG,
	MC_XPB_CLK_GAT,
	ATC_MISC_CG,
	MC_CITF_MISC_WR_CG,
	MC_CITF_MISC_RD_CG,
	MC_CITF_MISC_VM_CG,
	VM_L2_CG,
};

static void si_enable_mc_ls(struct radeon_device *rdev,
			    bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_MC_LS))
			data |= MC_LS_ENABLE;
		else
			data &= ~MC_LS_ENABLE;
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void si_enable_mc_mgcg(struct radeon_device *rdev,
			       bool enable)
{
	int i;
	u32 orig, data;

	for (i = 0; i < ARRAY_SIZE(mc_cg_registers); i++) {
		orig = data = RREG32(mc_cg_registers[i]);
		if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_MC_MGCG))
			data |= MC_CG_ENABLE;
		else
			data &= ~MC_CG_ENABLE;
		if (data != orig)
			WREG32(mc_cg_registers[i], data);
	}
}

static void si_enable_dma_mgcg(struct radeon_device *rdev,
			       bool enable)
{
	u32 orig, data, offset;
	int i;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_SDMA_MGCG)) {
		for (i = 0; i < 2; i++) {
			if (i == 0)
				offset = DMA0_REGISTER_OFFSET;
			else
				offset = DMA1_REGISTER_OFFSET;
			orig = data = RREG32(DMA_POWER_CNTL + offset);
			data &= ~MEM_POWER_OVERRIDE;
			if (data != orig)
				WREG32(DMA_POWER_CNTL + offset, data);
			WREG32(DMA_CLK_CTRL + offset, 0x00000100);
		}
	} else {
		for (i = 0; i < 2; i++) {
			if (i == 0)
				offset = DMA0_REGISTER_OFFSET;
			else
				offset = DMA1_REGISTER_OFFSET;
			orig = data = RREG32(DMA_POWER_CNTL + offset);
			data |= MEM_POWER_OVERRIDE;
			if (data != orig)
				WREG32(DMA_POWER_CNTL + offset, data);

			orig = data = RREG32(DMA_CLK_CTRL + offset);
			data = 0xff000000;
			if (data != orig)
				WREG32(DMA_CLK_CTRL + offset, data);
		}
	}
}

static void si_enable_bif_mgls(struct radeon_device *rdev,
			       bool enable)
{
	u32 orig, data;

	orig = data = RREG32_PCIE(PCIE_CNTL2);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_BIF_LS))
		data |= SLV_MEM_LS_EN | MST_MEM_LS_EN |
			REPLAY_MEM_LS_EN | SLV_MEM_AGGRESSIVE_LS_EN;
	else
		data &= ~(SLV_MEM_LS_EN | MST_MEM_LS_EN |
			  REPLAY_MEM_LS_EN | SLV_MEM_AGGRESSIVE_LS_EN);

	if (orig != data)
		WREG32_PCIE(PCIE_CNTL2, data);
}

static void si_enable_hdp_mgcg(struct radeon_device *rdev,
			       bool enable)
{
	u32 orig, data;

	orig = data = RREG32(HDP_HOST_PATH_CNTL);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_HDP_MGCG))
		data &= ~CLOCK_GATING_DIS;
	else
		data |= CLOCK_GATING_DIS;

	if (orig != data)
		WREG32(HDP_HOST_PATH_CNTL, data);
}

static void si_enable_hdp_ls(struct radeon_device *rdev,
			     bool enable)
{
	u32 orig, data;

	orig = data = RREG32(HDP_MEM_POWER_LS);

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_HDP_LS))
		data |= HDP_LS_ENABLE;
	else
		data &= ~HDP_LS_ENABLE;

	if (orig != data)
		WREG32(HDP_MEM_POWER_LS, data);
}

static void si_update_cg(struct radeon_device *rdev,
			 u32 block, bool enable)
{
	if (block & RADEON_CG_BLOCK_GFX) {
		si_enable_gui_idle_interrupt(rdev, false);
		/* order matters! */
		if (enable) {
			si_enable_mgcg(rdev, true);
			si_enable_cgcg(rdev, true);
		} else {
			si_enable_cgcg(rdev, false);
			si_enable_mgcg(rdev, false);
		}
		si_enable_gui_idle_interrupt(rdev, true);
	}

	if (block & RADEON_CG_BLOCK_MC) {
		si_enable_mc_mgcg(rdev, enable);
		si_enable_mc_ls(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_SDMA) {
		si_enable_dma_mgcg(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_BIF) {
		si_enable_bif_mgls(rdev, enable);
	}

	if (block & RADEON_CG_BLOCK_UVD) {
		if (rdev->has_uvd) {
			si_enable_uvd_mgcg(rdev, enable);
		}
	}

	if (block & RADEON_CG_BLOCK_HDP) {
		si_enable_hdp_mgcg(rdev, enable);
		si_enable_hdp_ls(rdev, enable);
	}
}

static void si_init_cg(struct radeon_device *rdev)
{
	si_update_cg(rdev, (RADEON_CG_BLOCK_GFX |
			    RADEON_CG_BLOCK_MC |
			    RADEON_CG_BLOCK_SDMA |
			    RADEON_CG_BLOCK_BIF |
			    RADEON_CG_BLOCK_HDP), true);
	if (rdev->has_uvd) {
		si_update_cg(rdev, RADEON_CG_BLOCK_UVD, true);
		si_init_uvd_internal_cg(rdev);
	}
}

static void si_fini_cg(struct radeon_device *rdev)
{
	if (rdev->has_uvd) {
		si_update_cg(rdev, RADEON_CG_BLOCK_UVD, false);
	}
	si_update_cg(rdev, (RADEON_CG_BLOCK_GFX |
			    RADEON_CG_BLOCK_MC |
			    RADEON_CG_BLOCK_SDMA |
			    RADEON_CG_BLOCK_BIF |
			    RADEON_CG_BLOCK_HDP), false);
}

u32 si_get_csb_size(struct radeon_device *rdev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (rdev->rlc.cs_data == NULL)
		return 0;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = rdev->rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}
	/* pa_sc_raster_config */
	count += 3;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

void si_get_csb_buffer(struct radeon_device *rdev, volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (rdev->rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = rdev->rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index - 0xa000);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 1));
	buffer[count++] = cpu_to_le32(PA_SC_RASTER_CONFIG - PACKET3_SET_CONTEXT_REG_START);
	switch (rdev->family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		buffer[count++] = cpu_to_le32(0x2a00126a);
		break;
	case CHIP_VERDE:
		buffer[count++] = cpu_to_le32(0x0000124a);
		break;
	case CHIP_OLAND:
		buffer[count++] = cpu_to_le32(0x00000082);
		break;
	case CHIP_HAINAN:
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	default:
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void si_init_pg(struct radeon_device *rdev)
{
	if (rdev->pg_flags) {
		if (rdev->pg_flags & RADEON_PG_SUPPORT_SDMA) {
			si_init_dma_pg(rdev);
		}
		si_init_ao_cu_mask(rdev);
		if (rdev->pg_flags & RADEON_PG_SUPPORT_GFX_PG) {
			si_init_gfx_cgpg(rdev);
		} else {
			WREG32(RLC_SAVE_AND_RESTORE_BASE, rdev->rlc.save_restore_gpu_addr >> 8);
			WREG32(RLC_CLEAR_STATE_RESTORE_BASE, rdev->rlc.clear_state_gpu_addr >> 8);
		}
		si_enable_dma_pg(rdev, true);
		si_enable_gfx_cgpg(rdev, true);
	} else {
		WREG32(RLC_SAVE_AND_RESTORE_BASE, rdev->rlc.save_restore_gpu_addr >> 8);
		WREG32(RLC_CLEAR_STATE_RESTORE_BASE, rdev->rlc.clear_state_gpu_addr >> 8);
	}
}

static void si_fini_pg(struct radeon_device *rdev)
{
	if (rdev->pg_flags) {
		si_enable_dma_pg(rdev, false);
		si_enable_gfx_cgpg(rdev, false);
	}
}

/*
 * RLC
 */
void si_rlc_reset(struct radeon_device *rdev)
{
	u32 tmp = RREG32(GRBM_SOFT_RESET);

	tmp |= SOFT_RESET_RLC;
	WREG32(GRBM_SOFT_RESET, tmp);
	udelay(50);
	tmp &= ~SOFT_RESET_RLC;
	WREG32(GRBM_SOFT_RESET, tmp);
	udelay(50);
}

static void si_rlc_stop(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, 0);

	si_enable_gui_idle_interrupt(rdev, false);

	si_wait_for_rlc_serdes(rdev);
}

static void si_rlc_start(struct radeon_device *rdev)
{
	WREG32(RLC_CNTL, RLC_ENABLE);

	si_enable_gui_idle_interrupt(rdev, true);

	udelay(50);
}

static bool si_lbpw_supported(struct radeon_device *rdev)
{
	u32 tmp;

	/* Enable LBPW only for DDR3 */
	tmp = RREG32(MC_SEQ_MISC0);
	if ((tmp & 0xF0000000) == 0xB0000000)
		return true;
	return false;
}

static void si_enable_lbpw(struct radeon_device *rdev, bool enable)
{
	u32 tmp;

	tmp = RREG32(RLC_LB_CNTL);
	if (enable)
		tmp |= LOAD_BALANCE_ENABLE;
	else
		tmp &= ~LOAD_BALANCE_ENABLE;
	WREG32(RLC_LB_CNTL, tmp);

	if (!enable) {
		si_select_se_sh(rdev, 0xffffffff, 0xffffffff);
		WREG32(SPI_LB_CU_MASK, 0x00ff);
	}
}

static int si_rlc_resume(struct radeon_device *rdev)
{
	u32 i;

	if (!rdev->rlc_fw)
		return -EINVAL;

	si_rlc_stop(rdev);

	si_rlc_reset(rdev);

	si_init_pg(rdev);

	si_init_cg(rdev);

	WREG32(RLC_RL_BASE, 0);
	WREG32(RLC_RL_SIZE, 0);
	WREG32(RLC_LB_CNTL, 0);
	WREG32(RLC_LB_CNTR_MAX, 0xffffffff);
	WREG32(RLC_LB_CNTR_INIT, 0);
	WREG32(RLC_LB_INIT_CU_MASK, 0xffffffff);

	WREG32(RLC_MC_CNTL, 0);
	WREG32(RLC_UCODE_CNTL, 0);

	if (rdev->new_fw) {
		const struct rlc_firmware_header_v1_0 *hdr =
			(const struct rlc_firmware_header_v1_0 *)rdev->rlc_fw->data;
		u32 fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		const __le32 *fw_data = (const __le32 *)
			(rdev->rlc_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		radeon_ucode_print_rlc_hdr(&hdr->header);

		for (i = 0; i < fw_size; i++) {
			WREG32(RLC_UCODE_ADDR, i);
			WREG32(RLC_UCODE_DATA, le32_to_cpup(fw_data++));
		}
	} else {
		const __be32 *fw_data =
			(const __be32 *)rdev->rlc_fw->data;
		for (i = 0; i < SI_RLC_UCODE_SIZE; i++) {
			WREG32(RLC_UCODE_ADDR, i);
			WREG32(RLC_UCODE_DATA, be32_to_cpup(fw_data++));
		}
	}
	WREG32(RLC_UCODE_ADDR, 0);

	si_enable_lbpw(rdev, si_lbpw_supported(rdev));

	si_rlc_start(rdev);

	return 0;
}

static void si_enable_interrupts(struct radeon_device *rdev)
{
	u32 ih_cntl = RREG32(IH_CNTL);
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);

	ih_cntl |= ENABLE_INTR;
	ih_rb_cntl |= IH_RB_ENABLE;
	WREG32(IH_CNTL, ih_cntl);
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	rdev->ih.enabled = true;
}

static void si_disable_interrupts(struct radeon_device *rdev)
{
	u32 ih_rb_cntl = RREG32(IH_RB_CNTL);
	u32 ih_cntl = RREG32(IH_CNTL);

	ih_rb_cntl &= ~IH_RB_ENABLE;
	ih_cntl &= ~ENABLE_INTR;
	WREG32(IH_RB_CNTL, ih_rb_cntl);
	WREG32(IH_CNTL, ih_cntl);
	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);
	rdev->ih.enabled = false;
	rdev->ih.rptr = 0;
}

static void si_disable_interrupt_state(struct radeon_device *rdev)
{
	int i;
	u32 tmp;

	tmp = RREG32(CP_INT_CNTL_RING0) &
		(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);
	WREG32(CP_INT_CNTL_RING0, tmp);
	WREG32(CP_INT_CNTL_RING1, 0);
	WREG32(CP_INT_CNTL_RING2, 0);
	tmp = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, tmp);
	tmp = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;
	WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, tmp);
	WREG32(GRBM_INT_CNTL, 0);
	WREG32(SRBM_INT_CNTL, 0);
	for (i = 0; i < rdev->num_crtc; i++)
		WREG32(INT_MASK + crtc_offsets[i], 0);
	for (i = 0; i < rdev->num_crtc; i++)
		WREG32(GRPH_INT_CONTROL + crtc_offsets[i], 0);

	if (!ASIC_IS_NODCE(rdev)) {
		WREG32(DAC_AUTODETECT_INT_CONTROL, 0);

		for (i = 0; i < 6; i++)
			WREG32_AND(DC_HPDx_INT_CONTROL(i),
				   DC_HPDx_INT_POLARITY);
	}
}

static int si_irq_init(struct radeon_device *rdev)
{
	int ret = 0;
	int rb_bufsz;
	u32 interrupt_cntl, ih_cntl, ih_rb_cntl;

	/* allocate ring */
	ret = r600_ih_ring_alloc(rdev);
	if (ret)
		return ret;

	/* disable irqs */
	si_disable_interrupts(rdev);

	/* init rlc */
	ret = si_rlc_resume(rdev);
	if (ret) {
		r600_ih_ring_fini(rdev);
		return ret;
	}

	/* setup interrupt control */
	/* set dummy read address to dummy page address */
	WREG32(INTERRUPT_CNTL2, rdev->dummy_page.addr >> 8);
	interrupt_cntl = RREG32(INTERRUPT_CNTL);
	/* IH_DUMMY_RD_OVERRIDE=0 - dummy read disabled with msi, enabled without msi
	 * IH_DUMMY_RD_OVERRIDE=1 - dummy read controlled by IH_DUMMY_RD_EN
	 */
	interrupt_cntl &= ~IH_DUMMY_RD_OVERRIDE;
	/* IH_REQ_NONSNOOP_EN=1 if ring is in non-cacheable memory, e.g., vram */
	interrupt_cntl &= ~IH_REQ_NONSNOOP_EN;
	WREG32(INTERRUPT_CNTL, interrupt_cntl);

	WREG32(IH_RB_BASE, rdev->ih.gpu_addr >> 8);
	rb_bufsz = order_base_2(rdev->ih.ring_size / 4);

	ih_rb_cntl = (IH_WPTR_OVERFLOW_ENABLE |
		      IH_WPTR_OVERFLOW_CLEAR |
		      (rb_bufsz << 1));

	if (rdev->wb.enabled)
		ih_rb_cntl |= IH_WPTR_WRITEBACK_ENABLE;

	/* set the writeback address whether it's enabled or not */
	WREG32(IH_RB_WPTR_ADDR_LO, (rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFFFFFFFC);
	WREG32(IH_RB_WPTR_ADDR_HI, upper_32_bits(rdev->wb.gpu_addr + R600_WB_IH_WPTR_OFFSET) & 0xFF);

	WREG32(IH_RB_CNTL, ih_rb_cntl);

	/* set rptr, wptr to 0 */
	WREG32(IH_RB_RPTR, 0);
	WREG32(IH_RB_WPTR, 0);

	/* Default settings for IH_CNTL (disabled at first) */
	ih_cntl = MC_WRREQ_CREDIT(0x10) | MC_WR_CLEAN_CNT(0x10) | MC_VMID(0);
	/* RPTR_REARM only works if msi's are enabled */
	if (rdev->msi_enabled)
		ih_cntl |= RPTR_REARM;
	WREG32(IH_CNTL, ih_cntl);

	/* force the active interrupt state to all disabled */
	si_disable_interrupt_state(rdev);

	pci_set_master(rdev->pdev);

	/* enable irqs */
	si_enable_interrupts(rdev);

	return ret;
}

/* The order we write back each register here is important */
int si_irq_set(struct radeon_device *rdev)
{
	int i;
	u32 cp_int_cntl;
	u32 cp_int_cntl1 = 0, cp_int_cntl2 = 0;
	u32 grbm_int_cntl = 0;
	u32 dma_cntl, dma_cntl1;
	u32 thermal_int = 0;

	if (!rdev->irq.installed) {
		WARN(1, "Can't enable IRQ/MSI because no handler is installed\n");
		return -EINVAL;
	}
	/* don't enable anything if the ih is disabled */
	if (!rdev->ih.enabled) {
		si_disable_interrupts(rdev);
		/* force the active interrupt state to all disabled */
		si_disable_interrupt_state(rdev);
		return 0;
	}

	cp_int_cntl = RREG32(CP_INT_CNTL_RING0) &
		(CNTX_BUSY_INT_ENABLE | CNTX_EMPTY_INT_ENABLE);

	dma_cntl = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET) & ~TRAP_ENABLE;
	dma_cntl1 = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET) & ~TRAP_ENABLE;

	thermal_int = RREG32(CG_THERMAL_INT) &
		~(THERM_INT_MASK_HIGH | THERM_INT_MASK_LOW);

	/* enable CP interrupts on all rings */
	if (atomic_read(&rdev->irq.ring_int[RADEON_RING_TYPE_GFX_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int gfx\n");
		cp_int_cntl |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP1_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int cp1\n");
		cp_int_cntl1 |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_CP2_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int cp2\n");
		cp_int_cntl2 |= TIME_STAMP_INT_ENABLE;
	}
	if (atomic_read(&rdev->irq.ring_int[R600_RING_TYPE_DMA_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int dma\n");
		dma_cntl |= TRAP_ENABLE;
	}

	if (atomic_read(&rdev->irq.ring_int[CAYMAN_RING_TYPE_DMA1_INDEX])) {
		DRM_DEBUG("si_irq_set: sw int dma1\n");
		dma_cntl1 |= TRAP_ENABLE;
	}

	WREG32(CP_INT_CNTL_RING0, cp_int_cntl);
	WREG32(CP_INT_CNTL_RING1, cp_int_cntl1);
	WREG32(CP_INT_CNTL_RING2, cp_int_cntl2);

	WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, dma_cntl);
	WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, dma_cntl1);

	WREG32(GRBM_INT_CNTL, grbm_int_cntl);

	if (rdev->irq.dpm_thermal) {
		DRM_DEBUG("dpm thermal\n");
		thermal_int |= THERM_INT_MASK_HIGH | THERM_INT_MASK_LOW;
	}

	for (i = 0; i < rdev->num_crtc; i++) {
		radeon_irq_kms_set_irq_n_enabled(
		    rdev, INT_MASK + crtc_offsets[i], VBLANK_INT_MASK,
		    rdev->irq.crtc_vblank_int[i] ||
		    atomic_read(&rdev->irq.pflip[i]), "vblank", i);
	}

	for (i = 0; i < rdev->num_crtc; i++)
		WREG32(GRPH_INT_CONTROL + crtc_offsets[i], GRPH_PFLIP_INT_MASK);

	if (!ASIC_IS_NODCE(rdev)) {
		for (i = 0; i < 6; i++) {
			radeon_irq_kms_set_irq_n_enabled(
			    rdev, DC_HPDx_INT_CONTROL(i),
			    DC_HPDx_INT_EN | DC_HPDx_RX_INT_EN,
			    rdev->irq.hpd[i], "HPD", i);
		}
	}

	WREG32(CG_THERMAL_INT, thermal_int);

	/* posting read */
	RREG32(SRBM_STATUS);

	return 0;
}

/* The order we write back each register here is important */
static inline void si_irq_ack(struct radeon_device *rdev)
{
	int i, j;
	u32 *disp_int = rdev->irq.stat_regs.evergreen.disp_int;
	u32 *grph_int = rdev->irq.stat_regs.evergreen.grph_int;

	if (ASIC_IS_NODCE(rdev))
		return;

	for (i = 0; i < 6; i++) {
		disp_int[i] = RREG32(si_disp_int_status[i]);
		if (i < rdev->num_crtc)
			grph_int[i] = RREG32(GRPH_INT_STATUS + crtc_offsets[i]);
	}

	/* We write back each interrupt register in pairs of two */
	for (i = 0; i < rdev->num_crtc; i += 2) {
		for (j = i; j < (i + 2); j++) {
			if (grph_int[j] & GRPH_PFLIP_INT_OCCURRED)
				WREG32(GRPH_INT_STATUS + crtc_offsets[j],
				       GRPH_PFLIP_INT_CLEAR);
		}

		for (j = i; j < (i + 2); j++) {
			if (disp_int[j] & LB_D1_VBLANK_INTERRUPT)
				WREG32(VBLANK_STATUS + crtc_offsets[j],
				       VBLANK_ACK);
			if (disp_int[j] & LB_D1_VLINE_INTERRUPT)
				WREG32(VLINE_STATUS + crtc_offsets[j],
				       VLINE_ACK);
		}
	}

	for (i = 0; i < 6; i++) {
		if (disp_int[i] & DC_HPD1_INTERRUPT)
			WREG32_OR(DC_HPDx_INT_CONTROL(i), DC_HPDx_INT_ACK);
	}

	for (i = 0; i < 6; i++) {
		if (disp_int[i] & DC_HPD1_RX_INTERRUPT)
			WREG32_OR(DC_HPDx_INT_CONTROL(i), DC_HPDx_RX_INT_ACK);
	}
}

static void si_irq_disable(struct radeon_device *rdev)
{
	si_disable_interrupts(rdev);
	/* Wait and acknowledge irq */
	mdelay(1);
	si_irq_ack(rdev);
	si_disable_interrupt_state(rdev);
}

static void si_irq_suspend(struct radeon_device *rdev)
{
	si_irq_disable(rdev);
	si_rlc_stop(rdev);
}

static void si_irq_fini(struct radeon_device *rdev)
{
	si_irq_suspend(rdev);
	r600_ih_ring_fini(rdev);
}

static inline u32 si_get_ih_wptr(struct radeon_device *rdev)
{
	u32 wptr, tmp;

	if (rdev->wb.enabled)
		wptr = le32_to_cpu(rdev->wb.wb[R600_WB_IH_WPTR_OFFSET/4]);
	else
		wptr = RREG32(IH_RB_WPTR);

	if (wptr & RB_OVERFLOW) {
		wptr &= ~RB_OVERFLOW;
		/* When a ring buffer overflow happen start parsing interrupt
		 * from the last not overwritten vector (wptr + 16). Hopefully
		 * this should allow us to catchup.
		 */
		dev_warn(rdev->dev, "IH ring buffer overflow (0x%08X, 0x%08X, 0x%08X)\n",
			 wptr, rdev->ih.rptr, (wptr + 16) & rdev->ih.ptr_mask);
		rdev->ih.rptr = (wptr + 16) & rdev->ih.ptr_mask;
		tmp = RREG32(IH_RB_CNTL);
		tmp |= IH_WPTR_OVERFLOW_CLEAR;
		WREG32(IH_RB_CNTL, tmp);
	}
	return (wptr & rdev->ih.ptr_mask);
}

/*        SI IV Ring
 * Each IV ring entry is 128 bits:
 * [7:0]    - interrupt source id
 * [31:8]   - reserved
 * [59:32]  - interrupt source data
 * [63:60]  - reserved
 * [71:64]  - RINGID
 * [79:72]  - VMID
 * [127:80] - reserved
 */
int si_irq_process(struct radeon_device *rdev)
{
	u32 *disp_int = rdev->irq.stat_regs.evergreen.disp_int;
	u32 crtc_idx, hpd_idx;
	u32 mask;
	u32 wptr;
	u32 rptr;
	u32 src_id, src_data, ring_id;
	u32 ring_index;
	bool queue_hotplug = false;
	bool queue_dp = false;
	bool queue_thermal = false;
	u32 status, addr;
	const char *event_name;

	if (!rdev->ih.enabled || rdev->shutdown)
		return IRQ_NONE;

	wptr = si_get_ih_wptr(rdev);

	if (wptr == rdev->ih.rptr)
		return IRQ_NONE;
restart_ih:
	/* is somebody else already processing irqs? */
	if (atomic_xchg(&rdev->ih.lock, 1))
		return IRQ_NONE;

	rptr = rdev->ih.rptr;
	DRM_DEBUG("si_irq_process start: rptr %d, wptr %d\n", rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	/* display interrupts */
	si_irq_ack(rdev);

	while (rptr != wptr) {
		/* wptr/rptr are in bytes! */
		ring_index = rptr / 4;
		src_id =  le32_to_cpu(rdev->ih.ring[ring_index]) & 0xff;
		src_data = le32_to_cpu(rdev->ih.ring[ring_index + 1]) & 0xfffffff;
		ring_id = le32_to_cpu(rdev->ih.ring[ring_index + 2]) & 0xff;

		switch (src_id) {
		case 1: /* D1 vblank/vline */
		case 2: /* D2 vblank/vline */
		case 3: /* D3 vblank/vline */
		case 4: /* D4 vblank/vline */
		case 5: /* D5 vblank/vline */
		case 6: /* D6 vblank/vline */
			crtc_idx = src_id - 1;

			if (src_data == 0) { /* vblank */
				mask = LB_D1_VBLANK_INTERRUPT;
				event_name = "vblank";

				if (rdev->irq.crtc_vblank_int[crtc_idx]) {
					drm_handle_vblank(rdev_to_drm(rdev), crtc_idx);
					rdev->pm.vblank_sync = true;
					wake_up(&rdev->irq.vblank_queue);
				}
				if (atomic_read(&rdev->irq.pflip[crtc_idx])) {
					radeon_crtc_handle_vblank(rdev,
								  crtc_idx);
				}

			} else if (src_data == 1) { /* vline */
				mask = LB_D1_VLINE_INTERRUPT;
				event_name = "vline";
			} else {
				DRM_DEBUG("Unhandled interrupt: %d %d\n",
					  src_id, src_data);
				break;
			}

			if (!(disp_int[crtc_idx] & mask)) {
				DRM_DEBUG("IH: D%d %s - IH event w/o asserted irq bit?\n",
					  crtc_idx + 1, event_name);
			}

			disp_int[crtc_idx] &= ~mask;
			DRM_DEBUG("IH: D%d %s\n", crtc_idx + 1, event_name);

			break;
		case 8: /* D1 page flip */
		case 10: /* D2 page flip */
		case 12: /* D3 page flip */
		case 14: /* D4 page flip */
		case 16: /* D5 page flip */
		case 18: /* D6 page flip */
			DRM_DEBUG("IH: D%d flip\n", ((src_id - 8) >> 1) + 1);
			if (radeon_use_pflipirq > 0)
				radeon_crtc_handle_flip(rdev, (src_id - 8) >> 1);
			break;
		case 42: /* HPD hotplug */
			if (src_data <= 5) {
				hpd_idx = src_data;
				mask = DC_HPD1_INTERRUPT;
				queue_hotplug = true;
				event_name = "HPD";

			} else if (src_data <= 11) {
				hpd_idx = src_data - 6;
				mask = DC_HPD1_RX_INTERRUPT;
				queue_dp = true;
				event_name = "HPD_RX";

			} else {
				DRM_DEBUG("Unhandled interrupt: %d %d\n",
					  src_id, src_data);
				break;
			}

			if (!(disp_int[hpd_idx] & mask))
				DRM_DEBUG("IH: IH event w/o asserted irq bit?\n");

			disp_int[hpd_idx] &= ~mask;
			DRM_DEBUG("IH: %s%d\n", event_name, hpd_idx + 1);
			break;
		case 96:
			DRM_ERROR("SRBM_READ_ERROR: 0x%x\n", RREG32(SRBM_READ_ERROR));
			WREG32(SRBM_INT_ACK, 0x1);
			break;
		case 124: /* UVD */
			DRM_DEBUG("IH: UVD int: 0x%08x\n", src_data);
			radeon_fence_process(rdev, R600_RING_TYPE_UVD_INDEX);
			break;
		case 146:
		case 147:
			addr = RREG32(VM_CONTEXT1_PROTECTION_FAULT_ADDR);
			status = RREG32(VM_CONTEXT1_PROTECTION_FAULT_STATUS);
			/* reset addr and status */
			WREG32_P(VM_CONTEXT1_CNTL2, 1, ~1);
			if (addr == 0x0 && status == 0x0)
				break;
			dev_err(rdev->dev, "GPU fault detected: %d 0x%08x\n", src_id, src_data);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_ADDR   0x%08X\n",
				addr);
			dev_err(rdev->dev, "  VM_CONTEXT1_PROTECTION_FAULT_STATUS 0x%08X\n",
				status);
			si_vm_decode_fault(rdev, status, addr);
			break;
		case 176: /* RINGID0 CP_INT */
			radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
			break;
		case 177: /* RINGID1 CP_INT */
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
			break;
		case 178: /* RINGID2 CP_INT */
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
			break;
		case 181: /* CP EOP event */
			DRM_DEBUG("IH: CP EOP\n");
			switch (ring_id) {
			case 0:
				radeon_fence_process(rdev, RADEON_RING_TYPE_GFX_INDEX);
				break;
			case 1:
				radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
				break;
			case 2:
				radeon_fence_process(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
				break;
			}
			break;
		case 224: /* DMA trap event */
			DRM_DEBUG("IH: DMA trap\n");
			radeon_fence_process(rdev, R600_RING_TYPE_DMA_INDEX);
			break;
		case 230: /* thermal low to high */
			DRM_DEBUG("IH: thermal low to high\n");
			rdev->pm.dpm.thermal.high_to_low = false;
			queue_thermal = true;
			break;
		case 231: /* thermal high to low */
			DRM_DEBUG("IH: thermal high to low\n");
			rdev->pm.dpm.thermal.high_to_low = true;
			queue_thermal = true;
			break;
		case 233: /* GUI IDLE */
			DRM_DEBUG("IH: GUI idle\n");
			break;
		case 244: /* DMA trap event */
			DRM_DEBUG("IH: DMA1 trap\n");
			radeon_fence_process(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
			break;
		default:
			DRM_DEBUG("Unhandled interrupt: %d %d\n", src_id, src_data);
			break;
		}

		/* wptr/rptr are in bytes! */
		rptr += 16;
		rptr &= rdev->ih.ptr_mask;
		WREG32(IH_RB_RPTR, rptr);
	}
	if (queue_dp)
		schedule_work(&rdev->dp_work);
	if (queue_hotplug)
		schedule_delayed_work(&rdev->hotplug_work, 0);
	if (queue_thermal && rdev->pm.dpm_enabled)
		schedule_work(&rdev->pm.dpm.thermal.work);
	rdev->ih.rptr = rptr;
	atomic_set(&rdev->ih.lock, 0);

	/* make sure wptr hasn't changed while processing */
	wptr = si_get_ih_wptr(rdev);
	if (wptr != rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/*
 * startup/shutdown callbacks
 */
static void si_uvd_init(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = radeon_uvd_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD (%d) init.\n", r);
		/*
		 * At this point rdev->uvd.vcpu_bo is NULL which trickles down
		 * to early fails uvd_v2_2_resume() and thus nothing happens
		 * there. So it is pointless to try to go through that code
		 * hence why we disable uvd here.
		 */
		rdev->has_uvd = false;
		return;
	}
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[R600_RING_TYPE_UVD_INDEX], 4096);
}

static void si_uvd_start(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_uvd)
		return;

	r = uvd_v2_2_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed UVD resume (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_UVD_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD fences (%d).\n", r);
		goto error;
	}
	return;

error:
	rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size = 0;
}

static void si_uvd_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->has_uvd || !rdev->ring[R600_RING_TYPE_UVD_INDEX].ring_size)
		return;

	ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, PACKET0(UVD_NO_OP, 0));
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD ring (%d).\n", r);
		return;
	}
	r = uvd_v1_0_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing UVD (%d).\n", r);
		return;
	}
}

static void si_vce_init(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_vce)
		return;

	r = radeon_vce_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE (%d) init.\n", r);
		/*
		 * At this point rdev->vce.vcpu_bo is NULL which trickles down
		 * to early fails si_vce_start() and thus nothing happens
		 * there. So it is pointless to try to go through that code
		 * hence why we disable vce here.
		 */
		rdev->has_vce = false;
		return;
	}
	rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[TN_RING_TYPE_VCE1_INDEX], 4096);
	rdev->ring[TN_RING_TYPE_VCE2_INDEX].ring_obj = NULL;
	r600_ring_init(rdev, &rdev->ring[TN_RING_TYPE_VCE2_INDEX], 4096);
}

static void si_vce_start(struct radeon_device *rdev)
{
	int r;

	if (!rdev->has_vce)
		return;

	r = radeon_vce_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE resume (%d).\n", r);
		goto error;
	}
	r = vce_v1_0_resume(rdev);
	if (r) {
		dev_err(rdev->dev, "failed VCE resume (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, TN_RING_TYPE_VCE1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 fences (%d).\n", r);
		goto error;
	}
	r = radeon_fence_driver_start_ring(rdev, TN_RING_TYPE_VCE2_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE2 fences (%d).\n", r);
		goto error;
	}
	return;

error:
	rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_size = 0;
	rdev->ring[TN_RING_TYPE_VCE2_INDEX].ring_size = 0;
}

static void si_vce_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	if (!rdev->has_vce || !rdev->ring[TN_RING_TYPE_VCE1_INDEX].ring_size)
		return;

	ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, VCE_CMD_NO_OP);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 ring (%d).\n", r);
		return;
	}
	ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, 0, VCE_CMD_NO_OP);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE1 ring (%d).\n", r);
		return;
	}
	r = vce_v1_0_init(rdev);
	if (r) {
		dev_err(rdev->dev, "failed initializing VCE (%d).\n", r);
		return;
	}
}

static int si_startup(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	/* enable pcie gen2/3 link */
	si_pcie_gen3_enable(rdev);
	/* enable aspm */
	si_program_aspm(rdev);

	/* scratch needs to be initialized before MC */
	r = r600_vram_scratch_init(rdev);
	if (r)
		return r;

	si_mc_program(rdev);

	if (!rdev->pm.dpm_enabled) {
		r = si_mc_load_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load MC firmware!\n");
			return r;
		}
	}

	r = si_pcie_gart_enable(rdev);
	if (r)
		return r;
	si_gpu_init(rdev);

	/* allocate rlc buffers */
	if (rdev->family == CHIP_VERDE) {
		rdev->rlc.reg_list = verde_rlc_save_restore_register_list;
		rdev->rlc.reg_list_size =
			(u32)ARRAY_SIZE(verde_rlc_save_restore_register_list);
	}
	rdev->rlc.cs_data = si_cs_data;
	r = sumo_rlc_init(rdev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	/* allocate wb buffer */
	r = radeon_wb_init(rdev);
	if (r)
		return r;

	r = radeon_fence_driver_start_ring(rdev, RADEON_RING_TYPE_GFX_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_CP2_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing CP fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, R600_RING_TYPE_DMA_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	r = radeon_fence_driver_start_ring(rdev, CAYMAN_RING_TYPE_DMA1_INDEX);
	if (r) {
		dev_err(rdev->dev, "failed initializing DMA fences (%d).\n", r);
		return r;
	}

	si_uvd_start(rdev);
	si_vce_start(rdev);

	/* Enable IRQ */
	if (!rdev->irq.installed) {
		r = radeon_irq_kms_init(rdev);
		if (r)
			return r;
	}

	r = si_irq_init(rdev);
	if (r) {
		DRM_ERROR("radeon: IH init failed (%d).\n", r);
		radeon_irq_kms_fini(rdev);
		return r;
	}
	si_irq_set(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP1_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, RADEON_WB_CP2_RPTR_OFFSET,
			     RADEON_CP_PACKET2);
	if (r)
		return r;

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, R600_WB_DMA_RPTR_OFFSET,
			     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0));
	if (r)
		return r;

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	r = radeon_ring_init(rdev, ring, ring->ring_size, CAYMAN_WB_DMA1_RPTR_OFFSET,
			     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0));
	if (r)
		return r;

	r = si_cp_load_microcode(rdev);
	if (r)
		return r;
	r = si_cp_resume(rdev);
	if (r)
		return r;

	r = cayman_dma_resume(rdev);
	if (r)
		return r;

	si_uvd_resume(rdev);
	si_vce_resume(rdev);

	r = radeon_ib_pool_init(rdev);
	if (r) {
		dev_err(rdev->dev, "IB initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_vm_manager_init(rdev);
	if (r) {
		dev_err(rdev->dev, "vm manager initialization failed (%d).\n", r);
		return r;
	}

	r = radeon_audio_init(rdev);
	if (r)
		return r;

	return 0;
}

int si_resume(struct radeon_device *rdev)
{
	int r;

	/* Do not reset GPU before posting, on rv770 hw unlike on r500 hw,
	 * posting will perform necessary task to bring back GPU into good
	 * shape.
	 */
	/* post card */
	atom_asic_init(rdev->mode_info.atom_context);

	/* init golden registers */
	si_init_golden_registers(rdev);

	if (rdev->pm.pm_method == PM_METHOD_DPM)
		radeon_pm_resume(rdev);

	rdev->accel_working = true;
	r = si_startup(rdev);
	if (r) {
		DRM_ERROR("si startup failed on resume\n");
		rdev->accel_working = false;
		return r;
	}

	return r;

}

int si_suspend(struct radeon_device *rdev)
{
	radeon_pm_suspend(rdev);
	radeon_audio_fini(rdev);
	radeon_vm_manager_fini(rdev);
	si_cp_enable(rdev, false);
	cayman_dma_stop(rdev);
	if (rdev->has_uvd) {
		radeon_uvd_suspend(rdev);
		uvd_v1_0_fini(rdev);
	}
	if (rdev->has_vce)
		radeon_vce_suspend(rdev);
	si_fini_pg(rdev);
	si_fini_cg(rdev);
	si_irq_suspend(rdev);
	radeon_wb_disable(rdev);
	si_pcie_gart_disable(rdev);
	return 0;
}

/* Plan is to move initialization in that function and use
 * helper function so that radeon_device_init pretty much
 * do nothing more than calling asic specific function. This
 * should also allow to remove a bunch of callback function
 * like vram_info.
 */
int si_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	int r;

	/* Read BIOS */
	if (!radeon_get_bios(rdev)) {
		if (ASIC_IS_AVIVO(rdev))
			return -EINVAL;
	}
	/* Must be an ATOMBIOS */
	if (!rdev->is_atom_bios) {
		dev_err(rdev->dev, "Expecting atombios for cayman GPU\n");
		return -EINVAL;
	}
	r = radeon_atombios_init(rdev);
	if (r)
		return r;

	/* Post card if necessary */
	if (!radeon_card_posted(rdev)) {
		if (!rdev->bios) {
			dev_err(rdev->dev, "Card not posted and no BIOS - ignoring\n");
			return -EINVAL;
		}
		DRM_INFO("GPU not posted. posting now...\n");
		atom_asic_init(rdev->mode_info.atom_context);
	}
	/* init golden registers */
	si_init_golden_registers(rdev);
	/* Initialize scratch registers */
	si_scratch_init(rdev);
	/* Initialize surface registers */
	radeon_surface_init(rdev);
	/* Initialize clocks */
	radeon_get_clock_info(rdev_to_drm(rdev));

	/* Fence driver */
	radeon_fence_driver_init(rdev);

	/* initialize memory controller */
	r = si_mc_init(rdev);
	if (r)
		return r;
	/* Memory manager */
	r = radeon_bo_init(rdev);
	if (r)
		return r;

	if (!rdev->me_fw || !rdev->pfp_fw || !rdev->ce_fw ||
	    !rdev->rlc_fw || !rdev->mc_fw) {
		r = si_init_microcode(rdev);
		if (r) {
			DRM_ERROR("Failed to load firmware!\n");
			return r;
		}
	}

	/* Initialize power management */
	radeon_pm_init(rdev);

	ring = &rdev->ring[RADEON_RING_TYPE_GFX_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_CP2_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 1024 * 1024);

	ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
	ring->ring_obj = NULL;
	r600_ring_init(rdev, ring, 64 * 1024);

	si_uvd_init(rdev);
	si_vce_init(rdev);

	rdev->ih.ring_obj = NULL;
	r600_ih_ring_init(rdev, 64 * 1024);

	r = r600_pcie_gart_init(rdev);
	if (r)
		return r;

	rdev->accel_working = true;
	r = si_startup(rdev);
	if (r) {
		dev_err(rdev->dev, "disabling GPU acceleration\n");
		si_cp_fini(rdev);
		cayman_dma_fini(rdev);
		si_irq_fini(rdev);
		sumo_rlc_fini(rdev);
		radeon_wb_fini(rdev);
		radeon_ib_pool_fini(rdev);
		radeon_vm_manager_fini(rdev);
		radeon_irq_kms_fini(rdev);
		si_pcie_gart_fini(rdev);
		rdev->accel_working = false;
	}

	/* Don't start up if the MC ucode is missing.
	 * The default clocks and voltages before the MC ucode
	 * is loaded are not suffient for advanced operations.
	 */
	if (!rdev->mc_fw) {
		DRM_ERROR("radeon: MC ucode required for NI+.\n");
		return -EINVAL;
	}

	return 0;
}

void si_fini(struct radeon_device *rdev)
{
	radeon_pm_fini(rdev);
	si_cp_fini(rdev);
	cayman_dma_fini(rdev);
	si_fini_pg(rdev);
	si_fini_cg(rdev);
	si_irq_fini(rdev);
	sumo_rlc_fini(rdev);
	radeon_wb_fini(rdev);
	radeon_vm_manager_fini(rdev);
	radeon_ib_pool_fini(rdev);
	radeon_irq_kms_fini(rdev);
	if (rdev->has_uvd) {
		uvd_v1_0_fini(rdev);
		radeon_uvd_fini(rdev);
	}
	if (rdev->has_vce)
		radeon_vce_fini(rdev);
	si_pcie_gart_fini(rdev);
	r600_vram_scratch_fini(rdev);
	radeon_gem_fini(rdev);
	radeon_fence_driver_fini(rdev);
	radeon_bo_fini(rdev);
	radeon_atombios_fini(rdev);
	kfree(rdev->bios);
	rdev->bios = NULL;
}

/**
 * si_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @rdev: radeon_device pointer
 *
 * Fetches a GPU clock counter snapshot (SI).
 * Returns the 64 bit clock counter snapshot.
 */
uint64_t si_get_gpu_clock_counter(struct radeon_device *rdev)
{
	uint64_t clock;

	mutex_lock(&rdev->gpu_clock_mutex);
	WREG32(RLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32(RLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&rdev->gpu_clock_mutex);
	return clock;
}

int si_set_uvd_clocks(struct radeon_device *rdev, u32 vclk, u32 dclk)
{
	unsigned fb_div = 0, vclk_div = 0, dclk_div = 0;
	int r;

	/* bypass vclk and dclk with bclk */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		VCLK_SRC_SEL(1) | DCLK_SRC_SEL(1),
		~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	/* put PLL in bypass mode */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_BYPASS_EN_MASK, ~UPLL_BYPASS_EN_MASK);

	if (!vclk || !dclk) {
		/* keep the Bypass mode */
		return 0;
	}

	r = radeon_uvd_calc_upll_dividers(rdev, vclk, dclk, 125000, 250000,
					  16384, 0x03FFFFFF, 0, 128, 5,
					  &fb_div, &vclk_div, &dclk_div);
	if (r)
		return r;

	/* set RESET_ANTI_MUX to 0 */
	WREG32_P(CG_UPLL_FUNC_CNTL_5, 0, ~RESET_ANTI_MUX_MASK);

	/* set VCO_MODE to 1 */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_VCO_MODE_MASK, ~UPLL_VCO_MODE_MASK);

	/* disable sleep mode */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_SLEEP_MASK);

	/* deassert UPLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);

	mdelay(1);

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* assert UPLL_RESET again */
	WREG32_P(CG_UPLL_FUNC_CNTL, UPLL_RESET_MASK, ~UPLL_RESET_MASK);

	/* disable spread spectrum. */
	WREG32_P(CG_UPLL_SPREAD_SPECTRUM, 0, ~SSEN_MASK);

	/* set feedback divider */
	WREG32_P(CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(fb_div), ~UPLL_FB_DIV_MASK);

	/* set ref divider to 0 */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_REF_DIV_MASK);

	if (fb_div < 307200)
		WREG32_P(CG_UPLL_FUNC_CNTL_4, 0, ~UPLL_SPARE_ISPARE9);
	else
		WREG32_P(CG_UPLL_FUNC_CNTL_4, UPLL_SPARE_ISPARE9, ~UPLL_SPARE_ISPARE9);

	/* set PDIV_A and PDIV_B */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		UPLL_PDIV_A(vclk_div) | UPLL_PDIV_B(dclk_div),
		~(UPLL_PDIV_A_MASK | UPLL_PDIV_B_MASK));

	/* give the PLL some time to settle */
	mdelay(15);

	/* deassert PLL_RESET */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);

	mdelay(15);

	/* switch from bypass mode to normal mode */
	WREG32_P(CG_UPLL_FUNC_CNTL, 0, ~UPLL_BYPASS_EN_MASK);

	r = radeon_uvd_send_upll_ctlreq(rdev, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* switch VCLK and DCLK selection */
	WREG32_P(CG_UPLL_FUNC_CNTL_2,
		VCLK_SRC_SEL(2) | DCLK_SRC_SEL(2),
		~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));

	mdelay(100);

	return 0;
}

static void si_pcie_gen3_enable(struct radeon_device *rdev)
{
	struct pci_dev *root = rdev->pdev->bus->self;
	enum pci_bus_speed speed_cap;
	u32 speed_cntl, current_data_rate;
	int i;
	u16 tmp16;

	if (pci_is_root_bus(rdev->pdev->bus))
		return;

	if (radeon_pcie_gen2 == 0)
		return;

	if (rdev->flags & RADEON_IS_IGP)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	speed_cap = pcie_get_speed_cap(root);
	if (speed_cap == PCI_SPEED_UNKNOWN)
		return;

	if ((speed_cap != PCIE_SPEED_8_0GT) &&
	    (speed_cap != PCIE_SPEED_5_0GT))
		return;

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	current_data_rate = (speed_cntl & LC_CURRENT_DATA_RATE_MASK) >>
		LC_CURRENT_DATA_RATE_SHIFT;
	if (speed_cap == PCIE_SPEED_8_0GT) {
		if (current_data_rate == 2) {
			DRM_INFO("PCIE gen 3 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 3 link speeds, disable with radeon.pcie_gen2=0\n");
	} else if (speed_cap == PCIE_SPEED_5_0GT) {
		if (current_data_rate == 1) {
			DRM_INFO("PCIE gen 2 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 2 link speeds, disable with radeon.pcie_gen2=0\n");
	}

	if (!pci_is_pcie(root) || !pci_is_pcie(rdev->pdev))
		return;

	if (speed_cap == PCIE_SPEED_8_0GT) {
		/* re-try equalization if gen3 is not already enabled */
		if (current_data_rate != 2) {
			u16 bridge_cfg, gpu_cfg;
			u16 bridge_cfg2, gpu_cfg2;
			u32 max_lw, current_lw, tmp;

			pcie_capability_set_word(root, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_HAWD);
			pcie_capability_set_word(rdev->pdev, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_HAWD);

			tmp = RREG32_PCIE(PCIE_LC_STATUS1);
			max_lw = (tmp & LC_DETECTED_LINK_WIDTH_MASK) >> LC_DETECTED_LINK_WIDTH_SHIFT;
			current_lw = (tmp & LC_OPERATING_LINK_WIDTH_MASK) >> LC_OPERATING_LINK_WIDTH_SHIFT;

			if (current_lw < max_lw) {
				tmp = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
				if (tmp & LC_RENEGOTIATION_SUPPORT) {
					tmp &= ~(LC_LINK_WIDTH_MASK | LC_UPCONFIGURE_DIS);
					tmp |= (max_lw << LC_LINK_WIDTH_SHIFT);
					tmp |= LC_UPCONFIGURE_SUPPORT | LC_RENEGOTIATE_EN | LC_RECONFIG_NOW;
					WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, tmp);
				}
			}

			for (i = 0; i < 10; i++) {
				/* check status */
				pcie_capability_read_word(rdev->pdev,
							  PCI_EXP_DEVSTA,
							  &tmp16);
				if (tmp16 & PCI_EXP_DEVSTA_TRPND)
					break;

				pcie_capability_read_word(root, PCI_EXP_LNKCTL,
							  &bridge_cfg);
				pcie_capability_read_word(rdev->pdev,
							  PCI_EXP_LNKCTL,
							  &gpu_cfg);

				pcie_capability_read_word(root, PCI_EXP_LNKCTL2,
							  &bridge_cfg2);
				pcie_capability_read_word(rdev->pdev,
							  PCI_EXP_LNKCTL2,
							  &gpu_cfg2);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_REDO_EQ;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				drm_msleep(100);

				/* linkctl */
				pcie_capability_clear_and_set_word(root, PCI_EXP_LNKCTL,
								   PCI_EXP_LNKCTL_HAWD,
								   bridge_cfg &
								   PCI_EXP_LNKCTL_HAWD);
				pcie_capability_clear_and_set_word(rdev->pdev, PCI_EXP_LNKCTL,
								   PCI_EXP_LNKCTL_HAWD,
								   gpu_cfg &
								   PCI_EXP_LNKCTL_HAWD);

				/* linkctl2 */
				pcie_capability_clear_and_set_word(root, PCI_EXP_LNKCTL2,
								   PCI_EXP_LNKCTL2_ENTER_COMP |
								   PCI_EXP_LNKCTL2_TX_MARGIN,
								   bridge_cfg2 &
								   (PCI_EXP_LNKCTL2_ENTER_COMP |
								    PCI_EXP_LNKCTL2_TX_MARGIN));
				pcie_capability_clear_and_set_word(rdev->pdev, PCI_EXP_LNKCTL2,
								   PCI_EXP_LNKCTL2_ENTER_COMP |
								   PCI_EXP_LNKCTL2_TX_MARGIN,
								   gpu_cfg2 &
								   (PCI_EXP_LNKCTL2_ENTER_COMP |
								    PCI_EXP_LNKCTL2_TX_MARGIN));

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp &= ~LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);
			}
		}
	}

	/* set the link speed */
	speed_cntl |= LC_FORCE_EN_SW_SPEED_CHANGE | LC_FORCE_DIS_HW_SPEED_CHANGE;
	speed_cntl &= ~LC_FORCE_DIS_SW_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	tmp16 = 0;
	if (speed_cap == PCIE_SPEED_8_0GT)
		tmp16 |= PCI_EXP_LNKCTL2_TLS_8_0GT; /* gen3 */
	else if (speed_cap == PCIE_SPEED_5_0GT)
		tmp16 |= PCI_EXP_LNKCTL2_TLS_5_0GT; /* gen2 */
	else
		tmp16 |= PCI_EXP_LNKCTL2_TLS_2_5GT; /* gen1 */
	pcie_capability_clear_and_set_word(rdev->pdev, PCI_EXP_LNKCTL2,
					   PCI_EXP_LNKCTL2_TLS, tmp16);

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	speed_cntl |= LC_INITIATE_LINK_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	for (i = 0; i < rdev->usec_timeout; i++) {
		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		if ((speed_cntl & LC_INITIATE_LINK_SPEED_CHANGE) == 0)
			break;
		udelay(1);
	}
}

static void si_program_aspm(struct radeon_device *rdev)
{
	u32 data, orig;
	bool disable_l0s = false, disable_l1 = false, disable_plloff_in_l1 = false;
	bool disable_clkreq = false;

	if (radeon_aspm == 0)
		return;

	if (!(rdev->flags & RADEON_IS_PCIE))
		return;

	orig = data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
	data &= ~LC_XMIT_N_FTS_MASK;
	data |= LC_XMIT_N_FTS(0x24) | LC_XMIT_N_FTS_OVERRIDE_EN;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL3);
	data |= LC_GO_TO_RECOVERY;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_CNTL3, data);

	orig = data = RREG32_PCIE(PCIE_P_CNTL);
	data |= P_IGNORE_EDB_ERR;
	if (orig != data)
		WREG32_PCIE(PCIE_P_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
	data &= ~(LC_L0S_INACTIVITY_MASK | LC_L1_INACTIVITY_MASK);
	data |= LC_PMI_TO_L1_DIS;
	if (!disable_l0s)
		data |= LC_L0S_INACTIVITY(7);

	if (!disable_l1) {
		data |= LC_L1_INACTIVITY(7);
		data &= ~LC_PMI_TO_L1_DIS;
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);

		if (!disable_plloff_in_l1) {
			bool clk_req_support;

			orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_0, data);

			orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_1, data);

			orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_0, data);

			orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_1, data);

			if ((rdev->family != CHIP_OLAND) && (rdev->family != CHIP_HAINAN)) {
				orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_0);
				data &= ~PLL_RAMP_UP_TIME_0_MASK;
				if (orig != data)
					WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_0, data);

				orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_1);
				data &= ~PLL_RAMP_UP_TIME_1_MASK;
				if (orig != data)
					WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_1, data);

				orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_2);
				data &= ~PLL_RAMP_UP_TIME_2_MASK;
				if (orig != data)
					WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_2, data);

				orig = data = RREG32_PIF_PHY0(PB0_PIF_PWRDOWN_3);
				data &= ~PLL_RAMP_UP_TIME_3_MASK;
				if (orig != data)
					WREG32_PIF_PHY0(PB0_PIF_PWRDOWN_3, data);

				orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_0);
				data &= ~PLL_RAMP_UP_TIME_0_MASK;
				if (orig != data)
					WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_0, data);

				orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_1);
				data &= ~PLL_RAMP_UP_TIME_1_MASK;
				if (orig != data)
					WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_1, data);

				orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_2);
				data &= ~PLL_RAMP_UP_TIME_2_MASK;
				if (orig != data)
					WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_2, data);

				orig = data = RREG32_PIF_PHY1(PB1_PIF_PWRDOWN_3);
				data &= ~PLL_RAMP_UP_TIME_3_MASK;
				if (orig != data)
					WREG32_PIF_PHY1(PB1_PIF_PWRDOWN_3, data);
			}
			orig = data = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
			data &= ~LC_DYN_LANES_PWR_STATE_MASK;
			data |= LC_DYN_LANES_PWR_STATE(3);
			if (orig != data)
				WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, data);

			orig = data = RREG32_PIF_PHY0(PB0_PIF_CNTL);
			data &= ~LS2_EXIT_TIME_MASK;
			if ((rdev->family == CHIP_OLAND) || (rdev->family == CHIP_HAINAN))
				data |= LS2_EXIT_TIME(5);
			if (orig != data)
				WREG32_PIF_PHY0(PB0_PIF_CNTL, data);

			orig = data = RREG32_PIF_PHY1(PB1_PIF_CNTL);
			data &= ~LS2_EXIT_TIME_MASK;
			if ((rdev->family == CHIP_OLAND) || (rdev->family == CHIP_HAINAN))
				data |= LS2_EXIT_TIME(5);
			if (orig != data)
				WREG32_PIF_PHY1(PB1_PIF_CNTL, data);

			if (!disable_clkreq &&
			    !pci_is_root_bus(rdev->pdev->bus)) {
				struct pci_dev *root = rdev->pdev->bus->self;
				u32 lnkcap;

				clk_req_support = false;
				pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);
				if (lnkcap & PCI_EXP_LNKCAP_CLKPM)
					clk_req_support = true;
			} else {
				clk_req_support = false;
			}

			if (clk_req_support) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL2);
				data |= LC_ALLOW_PDWN_IN_L1 | LC_ALLOW_PDWN_IN_L23;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL2, data);

				orig = data = RREG32(THM_CLK_CNTL);
				data &= ~(CMON_CLK_SEL_MASK | TMON_CLK_SEL_MASK);
				data |= CMON_CLK_SEL(1) | TMON_CLK_SEL(1);
				if (orig != data)
					WREG32(THM_CLK_CNTL, data);

				orig = data = RREG32(MISC_CLK_CNTL);
				data &= ~(DEEP_SLEEP_CLK_SEL_MASK | ZCLK_SEL_MASK);
				data |= DEEP_SLEEP_CLK_SEL(1) | ZCLK_SEL(1);
				if (orig != data)
					WREG32(MISC_CLK_CNTL, data);

				orig = data = RREG32(CG_CLKPIN_CNTL);
				data &= ~BCLK_AS_XCLK;
				if (orig != data)
					WREG32(CG_CLKPIN_CNTL, data);

				orig = data = RREG32(CG_CLKPIN_CNTL_2);
				data &= ~FORCE_BIF_REFCLK_EN;
				if (orig != data)
					WREG32(CG_CLKPIN_CNTL_2, data);

				orig = data = RREG32(MPLL_BYPASSCLK_SEL);
				data &= ~MPLL_CLKOUT_SEL_MASK;
				data |= MPLL_CLKOUT_SEL(4);
				if (orig != data)
					WREG32(MPLL_BYPASSCLK_SEL, data);

				orig = data = RREG32(SPLL_CNTL_MODE);
				data &= ~SPLL_REFCLK_SEL_MASK;
				if (orig != data)
					WREG32(SPLL_CNTL_MODE, data);
			}
		}
	} else {
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
	}

	orig = data = RREG32_PCIE(PCIE_CNTL2);
	data |= SLV_MEM_LS_EN | MST_MEM_LS_EN | REPLAY_MEM_LS_EN;
	if (orig != data)
		WREG32_PCIE(PCIE_CNTL2, data);

	if (!disable_l0s) {
		data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
		if((data & LC_N_FTS_MASK) == LC_N_FTS_MASK) {
			data = RREG32_PCIE(PCIE_LC_STATUS1);
			if ((data & LC_REVERSE_XMIT) && (data & LC_REVERSE_RCVR)) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
				data &= ~LC_L0S_INACTIVITY_MASK;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
			}
		}
	}
}

static int si_vce_send_vcepll_ctlreq(struct radeon_device *rdev)
{
	unsigned i;

	/* make sure VCEPLL_CTLREQ is deasserted */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~UPLL_CTLREQ_MASK);

	mdelay(10);

	/* assert UPLL_CTLREQ */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, UPLL_CTLREQ_MASK, ~UPLL_CTLREQ_MASK);

	/* wait for CTLACK and CTLACK2 to get asserted */
	for (i = 0; i < 100; ++i) {
		uint32_t mask = UPLL_CTLACK_MASK | UPLL_CTLACK2_MASK;
		if ((RREG32_SMC(CG_VCEPLL_FUNC_CNTL) & mask) == mask)
			break;
		mdelay(10);
	}

	/* deassert UPLL_CTLREQ */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~UPLL_CTLREQ_MASK);

	if (i == 100) {
		DRM_ERROR("Timeout setting UVD clocks!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int si_set_vce_clocks(struct radeon_device *rdev, u32 evclk, u32 ecclk)
{
	unsigned fb_div = 0, evclk_div = 0, ecclk_div = 0;
	int r;

	/* bypass evclk and ecclk with bclk */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL_2,
		     EVCLK_SRC_SEL(1) | ECCLK_SRC_SEL(1),
		     ~(EVCLK_SRC_SEL_MASK | ECCLK_SRC_SEL_MASK));

	/* put PLL in bypass mode */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, VCEPLL_BYPASS_EN_MASK,
		     ~VCEPLL_BYPASS_EN_MASK);

	if (!evclk || !ecclk) {
		/* keep the Bypass mode, put PLL to sleep */
		WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, VCEPLL_SLEEP_MASK,
			     ~VCEPLL_SLEEP_MASK);
		return 0;
	}

	r = radeon_uvd_calc_upll_dividers(rdev, evclk, ecclk, 125000, 250000,
					  16384, 0x03FFFFFF, 0, 128, 5,
					  &fb_div, &evclk_div, &ecclk_div);
	if (r)
		return r;

	/* set RESET_ANTI_MUX to 0 */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL_5, 0, ~RESET_ANTI_MUX_MASK);

	/* set VCO_MODE to 1 */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, VCEPLL_VCO_MODE_MASK,
		     ~VCEPLL_VCO_MODE_MASK);

	/* toggle VCEPLL_SLEEP to 1 then back to 0 */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, VCEPLL_SLEEP_MASK,
		     ~VCEPLL_SLEEP_MASK);
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~VCEPLL_SLEEP_MASK);

	/* deassert VCEPLL_RESET */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~VCEPLL_RESET_MASK);

	mdelay(1);

	r = si_vce_send_vcepll_ctlreq(rdev);
	if (r)
		return r;

	/* assert VCEPLL_RESET again */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, VCEPLL_RESET_MASK, ~VCEPLL_RESET_MASK);

	/* disable spread spectrum. */
	WREG32_SMC_P(CG_VCEPLL_SPREAD_SPECTRUM, 0, ~SSEN_MASK);

	/* set feedback divider */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL_3, VCEPLL_FB_DIV(fb_div), ~VCEPLL_FB_DIV_MASK);

	/* set ref divider to 0 */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~VCEPLL_REF_DIV_MASK);

	/* set PDIV_A and PDIV_B */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL_2,
		     VCEPLL_PDIV_A(evclk_div) | VCEPLL_PDIV_B(ecclk_div),
		     ~(VCEPLL_PDIV_A_MASK | VCEPLL_PDIV_B_MASK));

	/* give the PLL some time to settle */
	mdelay(15);

	/* deassert PLL_RESET */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~VCEPLL_RESET_MASK);

	mdelay(15);

	/* switch from bypass mode to normal mode */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL, 0, ~VCEPLL_BYPASS_EN_MASK);

	r = si_vce_send_vcepll_ctlreq(rdev);
	if (r)
		return r;

	/* switch VCLK and DCLK selection */
	WREG32_SMC_P(CG_VCEPLL_FUNC_CNTL_2,
		     EVCLK_SRC_SEL(16) | ECCLK_SRC_SEL(16),
		     ~(EVCLK_SRC_SEL_MASK | ECCLK_SRC_SEL_MASK));

	mdelay(100);

	return 0;
}
