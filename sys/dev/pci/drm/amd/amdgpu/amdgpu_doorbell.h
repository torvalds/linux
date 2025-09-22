/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef AMDGPU_DOORBELL_H
#define AMDGPU_DOORBELL_H

/*
 * GPU doorbell structures, functions & helpers
 */
struct amdgpu_doorbell {
	/* doorbell mmio */
	resource_size_t		base;
	resource_size_t		size;

	/* Number of doorbells reserved for amdgpu kernel driver */
	u32 num_kernel_doorbells;

	/* Kernel doorbells */
	struct amdgpu_bo *kernel_doorbells;

	/* For CPU access of doorbells */
	uint32_t *cpu_addr;
};

/* Reserved doorbells for amdgpu (including multimedia).
 * KFD can use all the rest in the 2M doorbell bar.
 * For asic before vega10, doorbell is 32-bit, so the
 * index/offset is in dword. For vega10 and after, doorbell
 * can be 64-bit, so the index defined is in qword.
 */
struct amdgpu_doorbell_index {
	uint32_t kiq;
	uint32_t mec_ring0;
	uint32_t mec_ring1;
	uint32_t mec_ring2;
	uint32_t mec_ring3;
	uint32_t mec_ring4;
	uint32_t mec_ring5;
	uint32_t mec_ring6;
	uint32_t mec_ring7;
	uint32_t userqueue_start;
	uint32_t userqueue_end;
	uint32_t gfx_ring0;
	uint32_t gfx_ring1;
	uint32_t gfx_userqueue_start;
	uint32_t gfx_userqueue_end;
	uint32_t sdma_engine[16];
	uint32_t mes_ring0;
	uint32_t mes_ring1;
	uint32_t ih;
	union {
		struct {
			uint32_t vcn_ring0_1;
			uint32_t vcn_ring2_3;
			uint32_t vcn_ring4_5;
			uint32_t vcn_ring6_7;
		} vcn;
		struct {
			uint32_t uvd_ring0_1;
			uint32_t uvd_ring2_3;
			uint32_t uvd_ring4_5;
			uint32_t uvd_ring6_7;
			uint32_t vce_ring0_1;
			uint32_t vce_ring2_3;
			uint32_t vce_ring4_5;
			uint32_t vce_ring6_7;
		} uvd_vce;
	};
	uint32_t vpe_ring;
	uint32_t first_non_cp;
	uint32_t last_non_cp;
	uint32_t max_assignment;
	/* Per engine SDMA doorbell size in dword */
	uint32_t sdma_doorbell_range;
	/* Per xcc doorbell size for KIQ/KCQ */
	uint32_t xcc_doorbell_range;
};

enum AMDGPU_DOORBELL_ASSIGNMENT {
	AMDGPU_DOORBELL_KIQ                     = 0x000,
	AMDGPU_DOORBELL_HIQ                     = 0x001,
	AMDGPU_DOORBELL_DIQ                     = 0x002,
	AMDGPU_DOORBELL_MEC_RING0               = 0x010,
	AMDGPU_DOORBELL_MEC_RING1               = 0x011,
	AMDGPU_DOORBELL_MEC_RING2               = 0x012,
	AMDGPU_DOORBELL_MEC_RING3               = 0x013,
	AMDGPU_DOORBELL_MEC_RING4               = 0x014,
	AMDGPU_DOORBELL_MEC_RING5               = 0x015,
	AMDGPU_DOORBELL_MEC_RING6               = 0x016,
	AMDGPU_DOORBELL_MEC_RING7               = 0x017,
	AMDGPU_DOORBELL_GFX_RING0               = 0x020,
	AMDGPU_DOORBELL_sDMA_ENGINE0            = 0x1E0,
	AMDGPU_DOORBELL_sDMA_ENGINE1            = 0x1E1,
	AMDGPU_DOORBELL_IH                      = 0x1E8,
	AMDGPU_DOORBELL_MAX_ASSIGNMENT          = 0x3FF,
	AMDGPU_DOORBELL_INVALID                 = 0xFFFF
};

enum AMDGPU_VEGA20_DOORBELL_ASSIGNMENT {

	/* Compute + GFX: 0~255 */
	AMDGPU_VEGA20_DOORBELL_KIQ                     = 0x000,
	AMDGPU_VEGA20_DOORBELL_HIQ                     = 0x001,
	AMDGPU_VEGA20_DOORBELL_DIQ                     = 0x002,
	AMDGPU_VEGA20_DOORBELL_MEC_RING0               = 0x003,
	AMDGPU_VEGA20_DOORBELL_MEC_RING1               = 0x004,
	AMDGPU_VEGA20_DOORBELL_MEC_RING2               = 0x005,
	AMDGPU_VEGA20_DOORBELL_MEC_RING3               = 0x006,
	AMDGPU_VEGA20_DOORBELL_MEC_RING4               = 0x007,
	AMDGPU_VEGA20_DOORBELL_MEC_RING5               = 0x008,
	AMDGPU_VEGA20_DOORBELL_MEC_RING6               = 0x009,
	AMDGPU_VEGA20_DOORBELL_MEC_RING7               = 0x00A,
	AMDGPU_VEGA20_DOORBELL_USERQUEUE_START	       = 0x00B,
	AMDGPU_VEGA20_DOORBELL_USERQUEUE_END	       = 0x08A,
	AMDGPU_VEGA20_DOORBELL_GFX_RING0               = 0x08B,
	/* SDMA:256~335*/
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE0            = 0x100,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE1            = 0x10A,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE2            = 0x114,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE3            = 0x11E,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE4            = 0x128,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE5            = 0x132,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE6            = 0x13C,
	AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE7            = 0x146,
	/* IH: 376~391 */
	AMDGPU_VEGA20_DOORBELL_IH                      = 0x178,
	/* MMSCH: 392~407
	 * overlap the doorbell assignment with VCN as they are  mutually exclusive
	 * VCN engine's doorbell is 32 bit and two VCN ring share one QWORD
	 */
	AMDGPU_VEGA20_DOORBELL64_VCN0_1                  = 0x188, /* VNC0 */
	AMDGPU_VEGA20_DOORBELL64_VCN2_3                  = 0x189,
	AMDGPU_VEGA20_DOORBELL64_VCN4_5                  = 0x18A,
	AMDGPU_VEGA20_DOORBELL64_VCN6_7                  = 0x18B,

	AMDGPU_VEGA20_DOORBELL64_VCN8_9                  = 0x18C, /* VNC1 */
	AMDGPU_VEGA20_DOORBELL64_VCNa_b                  = 0x18D,
	AMDGPU_VEGA20_DOORBELL64_VCNc_d                  = 0x18E,
	AMDGPU_VEGA20_DOORBELL64_VCNe_f                  = 0x18F,

	AMDGPU_VEGA20_DOORBELL64_UVD_RING0_1             = 0x188,
	AMDGPU_VEGA20_DOORBELL64_UVD_RING2_3             = 0x189,
	AMDGPU_VEGA20_DOORBELL64_UVD_RING4_5             = 0x18A,
	AMDGPU_VEGA20_DOORBELL64_UVD_RING6_7             = 0x18B,

	AMDGPU_VEGA20_DOORBELL64_VCE_RING0_1             = 0x18C,
	AMDGPU_VEGA20_DOORBELL64_VCE_RING2_3             = 0x18D,
	AMDGPU_VEGA20_DOORBELL64_VCE_RING4_5             = 0x18E,
	AMDGPU_VEGA20_DOORBELL64_VCE_RING6_7             = 0x18F,

	AMDGPU_VEGA20_DOORBELL64_FIRST_NON_CP            = AMDGPU_VEGA20_DOORBELL_sDMA_ENGINE0,
	AMDGPU_VEGA20_DOORBELL64_LAST_NON_CP             = AMDGPU_VEGA20_DOORBELL64_VCE_RING6_7,

	/* kiq/kcq from second XCD. Max 8 XCDs */
	AMDGPU_VEGA20_DOORBELL_XCC1_KIQ_START             = 0x190,
	/* 8 compute rings per GC. Max to 0x1CE */
	AMDGPU_VEGA20_DOORBELL_XCC1_MEC_RING0_START       = 0x197,

	/* AID1 SDMA: 0x1D0 ~ 0x1F7 */
	AMDGPU_VEGA20_DOORBELL_AID1_sDMA_START           = 0x1D0,

	AMDGPU_VEGA20_DOORBELL_MAX_ASSIGNMENT            = 0x1F7,
	AMDGPU_VEGA20_DOORBELL_INVALID                   = 0xFFFF
};

enum AMDGPU_NAVI10_DOORBELL_ASSIGNMENT {

	/* Compute + GFX: 0~255 */
	AMDGPU_NAVI10_DOORBELL_KIQ			= 0x000,
	AMDGPU_NAVI10_DOORBELL_HIQ			= 0x001,
	AMDGPU_NAVI10_DOORBELL_DIQ			= 0x002,
	AMDGPU_NAVI10_DOORBELL_MEC_RING0		= 0x003,
	AMDGPU_NAVI10_DOORBELL_MEC_RING1		= 0x004,
	AMDGPU_NAVI10_DOORBELL_MEC_RING2		= 0x005,
	AMDGPU_NAVI10_DOORBELL_MEC_RING3		= 0x006,
	AMDGPU_NAVI10_DOORBELL_MEC_RING4		= 0x007,
	AMDGPU_NAVI10_DOORBELL_MEC_RING5		= 0x008,
	AMDGPU_NAVI10_DOORBELL_MEC_RING6		= 0x009,
	AMDGPU_NAVI10_DOORBELL_MEC_RING7		= 0x00A,
	AMDGPU_NAVI10_DOORBELL_MES_RING0	        = 0x00B,
	AMDGPU_NAVI10_DOORBELL_MES_RING1		= 0x00C,
	AMDGPU_NAVI10_DOORBELL_USERQUEUE_START		= 0x00D,
	AMDGPU_NAVI10_DOORBELL_USERQUEUE_END		= 0x08A,
	AMDGPU_NAVI10_DOORBELL_GFX_RING0		= 0x08B,
	AMDGPU_NAVI10_DOORBELL_GFX_RING1		= 0x08C,
	AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_START	= 0x08D,
	AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_END	= 0x0FF,

	/* SDMA:256~335*/
	AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0		= 0x100,
	AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE1		= 0x10A,
	AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE2		= 0x114,
	AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE3		= 0x11E,
	/* IH: 376~391 */
	AMDGPU_NAVI10_DOORBELL_IH			= 0x178,
	/* MMSCH: 392~407
	 * overlap the doorbell assignment with VCN as they are  mutually exclusive
	 * VCE engine's doorbell is 32 bit and two VCE ring share one QWORD
	 */
	AMDGPU_NAVI10_DOORBELL64_VCN0_1			= 0x188, /* lower 32 bits for VNC0 and upper 32 bits for VNC1 */
	AMDGPU_NAVI10_DOORBELL64_VCN2_3			= 0x189,
	AMDGPU_NAVI10_DOORBELL64_VCN4_5			= 0x18A,
	AMDGPU_NAVI10_DOORBELL64_VCN6_7			= 0x18B,

	AMDGPU_NAVI10_DOORBELL64_VCN8_9			= 0x18C,
	AMDGPU_NAVI10_DOORBELL64_VCNa_b			= 0x18D,
	AMDGPU_NAVI10_DOORBELL64_VCNc_d			= 0x18E,
	AMDGPU_NAVI10_DOORBELL64_VCNe_f			= 0x18F,

	AMDGPU_NAVI10_DOORBELL64_VPE			= 0x190,

	AMDGPU_NAVI10_DOORBELL64_FIRST_NON_CP		= AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0,
	AMDGPU_NAVI10_DOORBELL64_LAST_NON_CP		= AMDGPU_NAVI10_DOORBELL64_VPE,

	AMDGPU_NAVI10_DOORBELL_MAX_ASSIGNMENT		= AMDGPU_NAVI10_DOORBELL64_VPE,
	AMDGPU_NAVI10_DOORBELL_INVALID			= 0xFFFF
};

/*
 * 64bit doorbell, offset are in QWORD, occupy 2KB doorbell space
 */
enum AMDGPU_DOORBELL64_ASSIGNMENT {
	/*
	 * All compute related doorbells: kiq, hiq, diq, traditional compute queue, user queue, should locate in
	 * a continues range so that programming CP_MEC_DOORBELL_RANGE_LOWER/UPPER can cover this range.
	 *  Compute related doorbells are allocated from 0x00 to 0x8a
	 */


	/* kernel scheduling */
	AMDGPU_DOORBELL64_KIQ                     = 0x00,

	/* HSA interface queue and debug queue */
	AMDGPU_DOORBELL64_HIQ                     = 0x01,
	AMDGPU_DOORBELL64_DIQ                     = 0x02,

	/* Compute engines */
	AMDGPU_DOORBELL64_MEC_RING0               = 0x03,
	AMDGPU_DOORBELL64_MEC_RING1               = 0x04,
	AMDGPU_DOORBELL64_MEC_RING2               = 0x05,
	AMDGPU_DOORBELL64_MEC_RING3               = 0x06,
	AMDGPU_DOORBELL64_MEC_RING4               = 0x07,
	AMDGPU_DOORBELL64_MEC_RING5               = 0x08,
	AMDGPU_DOORBELL64_MEC_RING6               = 0x09,
	AMDGPU_DOORBELL64_MEC_RING7               = 0x0a,

	/* User queue doorbell range (128 doorbells) */
	AMDGPU_DOORBELL64_USERQUEUE_START         = 0x0b,
	AMDGPU_DOORBELL64_USERQUEUE_END           = 0x8a,

	/* Graphics engine */
	AMDGPU_DOORBELL64_GFX_RING0               = 0x8b,

	/*
	 * Other graphics doorbells can be allocated here: from 0x8c to 0xdf
	 * Graphics voltage island aperture 1
	 * default non-graphics QWORD index is 0xe0 - 0xFF inclusive
	 */

	/* For vega10 sriov, the sdma doorbell must be fixed as follow
	 * to keep the same setting with host driver, or it will
	 * happen conflicts
	 */
	AMDGPU_DOORBELL64_sDMA_ENGINE0            = 0xF0,
	AMDGPU_DOORBELL64_sDMA_HI_PRI_ENGINE0     = 0xF1,
	AMDGPU_DOORBELL64_sDMA_ENGINE1            = 0xF2,
	AMDGPU_DOORBELL64_sDMA_HI_PRI_ENGINE1     = 0xF3,

	/* Interrupt handler */
	AMDGPU_DOORBELL64_IH                      = 0xF4,  /* For legacy interrupt ring buffer */
	AMDGPU_DOORBELL64_IH_RING1                = 0xF5,  /* For page migration request log */
	AMDGPU_DOORBELL64_IH_RING2                = 0xF6,  /* For page migration translation/invalidation log */

	/* VCN engine use 32 bits doorbell  */
	AMDGPU_DOORBELL64_VCN0_1                  = 0xF8, /* lower 32 bits for VNC0 and upper 32 bits for VNC1 */
	AMDGPU_DOORBELL64_VCN2_3                  = 0xF9,
	AMDGPU_DOORBELL64_VCN4_5                  = 0xFA,
	AMDGPU_DOORBELL64_VCN6_7                  = 0xFB,

	/* overlap the doorbell assignment with VCN as they are  mutually exclusive
	 * VCE engine's doorbell is 32 bit and two VCE ring share one QWORD
	 */
	AMDGPU_DOORBELL64_UVD_RING0_1             = 0xF8,
	AMDGPU_DOORBELL64_UVD_RING2_3             = 0xF9,
	AMDGPU_DOORBELL64_UVD_RING4_5             = 0xFA,
	AMDGPU_DOORBELL64_UVD_RING6_7             = 0xFB,

	AMDGPU_DOORBELL64_VCE_RING0_1             = 0xFC,
	AMDGPU_DOORBELL64_VCE_RING2_3             = 0xFD,
	AMDGPU_DOORBELL64_VCE_RING4_5             = 0xFE,
	AMDGPU_DOORBELL64_VCE_RING6_7             = 0xFF,

	AMDGPU_DOORBELL64_FIRST_NON_CP            = AMDGPU_DOORBELL64_sDMA_ENGINE0,
	AMDGPU_DOORBELL64_LAST_NON_CP             = AMDGPU_DOORBELL64_VCE_RING6_7,

	AMDGPU_DOORBELL64_MAX_ASSIGNMENT          = 0xFF,
	AMDGPU_DOORBELL64_INVALID                 = 0xFFFF
};

enum AMDGPU_DOORBELL_ASSIGNMENT_LAYOUT1 {

	/* XCC0: 0x00 ~20, XCC1: 20 ~ 2F ... */

	/* KIQ/HIQ/DIQ */
	AMDGPU_DOORBELL_LAYOUT1_KIQ_START		= 0x000,
	AMDGPU_DOORBELL_LAYOUT1_HIQ			= 0x001,
	AMDGPU_DOORBELL_LAYOUT1_DIQ			= 0x002,
	/* Compute: 0x08 ~ 0x20  */
	AMDGPU_DOORBELL_LAYOUT1_MEC_RING_START		= 0x008,
	AMDGPU_DOORBELL_LAYOUT1_MEC_RING_END		= 0x00F,
	AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_START		= 0x010,
	AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_END		= 0x01F,
	AMDGPU_DOORBELL_LAYOUT1_XCC_RANGE		= 0x020,

	/* SDMA: 0x100 ~ 0x19F */
	AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_START	= 0x100,
	AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_END		= 0x19F,
	/* IH: 0x1A0 ~ 0x1AF */
	AMDGPU_DOORBELL_LAYOUT1_IH                      = 0x1A0,
	/* VCN: 0x1B0 ~ 0x1E8 */
	AMDGPU_DOORBELL_LAYOUT1_VCN_START               = 0x1B0,
	AMDGPU_DOORBELL_LAYOUT1_VCN_END                 = 0x1E8,

	AMDGPU_DOORBELL_LAYOUT1_FIRST_NON_CP		= AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_START,
	AMDGPU_DOORBELL_LAYOUT1_LAST_NON_CP		= AMDGPU_DOORBELL_LAYOUT1_VCN_END,

	AMDGPU_DOORBELL_LAYOUT1_MAX_ASSIGNMENT          = 0x1E8,
	AMDGPU_DOORBELL_LAYOUT1_INVALID                 = 0xFFFF
};

u32 amdgpu_mm_rdoorbell(struct amdgpu_device *adev, u32 index);
void amdgpu_mm_wdoorbell(struct amdgpu_device *adev, u32 index, u32 v);
u64 amdgpu_mm_rdoorbell64(struct amdgpu_device *adev, u32 index);
void amdgpu_mm_wdoorbell64(struct amdgpu_device *adev, u32 index, u64 v);

/*
 * GPU doorbell aperture helpers function.
 */
int amdgpu_doorbell_init(struct amdgpu_device *adev);
void amdgpu_doorbell_fini(struct amdgpu_device *adev);
int amdgpu_doorbell_create_kernel_doorbells(struct amdgpu_device *adev);
uint32_t amdgpu_doorbell_index_on_bar(struct amdgpu_device *adev,
				      struct amdgpu_bo *db_bo,
				      uint32_t doorbell_index,
				      uint32_t db_size);

#define RDOORBELL32(index) amdgpu_mm_rdoorbell(adev, (index))
#define WDOORBELL32(index, v) amdgpu_mm_wdoorbell(adev, (index), (v))
#define RDOORBELL64(index) amdgpu_mm_rdoorbell64(adev, (index))
#define WDOORBELL64(index, v) amdgpu_mm_wdoorbell64(adev, (index), (v))

#endif
