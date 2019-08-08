/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *            au88x0_a3d.h
 *
 *  Fri Jul 18 14:16:03 2003
 *  Copyright  2003  mjander
 *  mjander@users.sourceforge.net
 ****************************************************************************/

/*
 */

#ifndef _AU88X0_A3D_H
#define _AU88X0_A3D_H

//#include <openal.h>

#define HRTF_SZ 0x38
#define DLINE_SZ 0x28

#define CTRLID_HRTF		1
#define CTRLID_ITD		2
#define CTRLID_ILD		4
#define CTRLID_FILTER	8
#define CTRLID_GAINS	16

/* 3D parameter structs */
typedef unsigned short int a3d_Hrtf_t[HRTF_SZ];
typedef unsigned short int a3d_ItdDline_t[DLINE_SZ];
typedef unsigned short int a3d_atmos_t[5];
typedef unsigned short int a3d_LRGains_t[2];
typedef unsigned short int a3d_Itd_t[2];
typedef unsigned short int a3d_Ild_t[2];

typedef struct {
	void *vortex;		// Formerly CAsp4HwIO*, now vortex_t*.
	unsigned int source;	/* this_04 */
	unsigned int slice;	/* this_08 */
	a3d_Hrtf_t hrtf[2];
	a3d_Itd_t itd;
	a3d_Ild_t ild;
	a3d_ItdDline_t dline;
	a3d_atmos_t filter;
} a3dsrc_t;

/* First Register bank */

#define A3D_A_HrtfCurrent	0x18000	/* 56 ULONG */
#define A3D_A_GainCurrent	0x180E0
#define A3D_A_GainTarget	0x180E4
#define A3D_A_A12Current	0x180E8	/* Atmospheric current. */
#define A3D_A_A21Target		0x180EC	/* Atmospheric target */
#define A3D_A_B01Current	0x180F0	/* Atmospheric current */
#define A3D_A_B10Target		0x180F4	/* Atmospheric target */
#define A3D_A_B2Current		0x180F8	/* Atmospheric current */
#define A3D_A_B2Target		0x180FC	/* Atmospheric target */
#define A3D_A_HrtfTarget	0x18100	/* 56 ULONG */
#define A3D_A_ITDCurrent	0x181E0
#define A3D_A_ITDTarget		0x181E4
#define A3D_A_HrtfDelayLine	0x181E8	/* 56 ULONG */
#define A3D_A_ITDDelayLine	0x182C8	/* 40/45 ULONG */
#define A3D_A_HrtfTrackTC	0x1837C	/* Time Constants */
#define A3D_A_GainTrackTC	0x18380
#define A3D_A_CoeffTrackTC	0x18384
#define A3D_A_ITDTrackTC	0x18388
#define A3D_A_x1			0x1838C
#define A3D_A_x2			0x18390
#define A3D_A_y1			0x18394
#define A3D_A_y2			0x18398
#define A3D_A_HrtfOutL		0x1839C
#define A3D_A_HrtfOutR		0x183A0
#define 	A3D_A_TAIL		0x183A4

/* Second register bank */
#define A3D_B_HrtfCurrent	0x19000	/* 56 ULONG */
#define A3D_B_GainCurrent	0x190E0
#define A3D_B_GainTarget	0x190E4
#define A3D_B_A12Current	0x190E8
#define A3D_B_A21Target		0x190EC
#define A3D_B_B01Current	0x190F0
#define A3D_B_B10Target		0x190F4
#define A3D_B_B2Current		0x190F8
#define A3D_B_B2Target		0x190FC
#define A3D_B_HrtfTarget	0x19100	/* 56 ULONG */
#define A3D_B_ITDCurrent	0x191E0
#define A3D_B_ITDTarget		0x191E4
#define A3D_B_HrtfDelayLine	0x191E8	/* 56 ULONG */
#define 	A3D_B_TAIL		0x192C8

/* There are 4 slices, 4 a3d each = 16 a3d sources. */
#define A3D_SLICE_BANK_A		0x18000	/* 4 sources */
#define A3D_SLICE_BANK_B		0x19000	/* 4 sources */
#define A3D_SLICE_VDBDest		0x19C00	/* 8 ULONG */
#define A3D_SLICE_VDBSource		0x19C20	/* 4 ULONG */
#define A3D_SLICE_ABReg			0x19C30
#define A3D_SLICE_CReg			0x19C34
#define A3D_SLICE_Control		0x19C38
#define A3D_SLICE_DebugReserved	0x19C3c	/* Dangerous! */
#define A3D_SLICE_Pointers		0x19C40
#define 	A3D_SLICE_TAIL		0x1A000

// Slice size: 0x2000
// Source size: 0x3A4, 0x2C8

/* Address generator macro. */
#define a3d_addrA(slice,source,reg) (((slice)<<0xd)+((source)*0x3A4)+(reg))
#define a3d_addrB(slice,source,reg) (((slice)<<0xd)+((source)*0x2C8)+(reg))
#define a3d_addrS(slice,reg) (((slice)<<0xd)+(reg))
//#define a3d_addr(slice,source,reg) (((reg)>=0x19000) ? a3d_addr2((slice),(source),(reg)) : a3d_addr1((slice),(source),(reg)))

#endif				/* _AU88X0_A3D_H */
