/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_plcr.h

 @Description   FM Policer private header
*//***************************************************************************/
#ifndef __FM_PLCR_H
#define __FM_PLCR_H

#include "std_ext.h"


/***********************************************************************/
/*          Policer defines                                            */
/***********************************************************************/

#define FM_PCD_PLCR_PAR_GO                    0x80000000
#define FM_PCD_PLCR_PAR_PWSEL_MASK            0x0000FFFF
#define FM_PCD_PLCR_PAR_R                     0x40000000

/* shifts */
#define FM_PCD_PLCR_PAR_PNUM_SHIFT            16

/* masks */
#define FM_PCD_PLCR_PEMODE_PI                 0x80000000
#define FM_PCD_PLCR_PEMODE_CBLND              0x40000000
#define FM_PCD_PLCR_PEMODE_ALG_MASK           0x30000000
#define FM_PCD_PLCR_PEMODE_ALG_RFC2698        0x10000000
#define FM_PCD_PLCR_PEMODE_ALG_RFC4115        0x20000000
#define FM_PCD_PLCR_PEMODE_DEFC_MASK          0x0C000000
#define FM_PCD_PLCR_PEMODE_DEFC_Y             0x04000000
#define FM_PCD_PLCR_PEMODE_DEFC_R             0x08000000
#define FM_PCD_PLCR_PEMODE_DEFC_OVERRIDE      0x0C000000
#define FM_PCD_PLCR_PEMODE_OVCLR_MASK         0x03000000
#define FM_PCD_PLCR_PEMODE_OVCLR_Y            0x01000000
#define FM_PCD_PLCR_PEMODE_OVCLR_R            0x02000000
#define FM_PCD_PLCR_PEMODE_OVCLR_G_NC         0x03000000
#define FM_PCD_PLCR_PEMODE_PKT                0x00800000
#define FM_PCD_PLCR_PEMODE_FPP_MASK           0x001F0000
#define FM_PCD_PLCR_PEMODE_FPP_SHIFT          16
#define FM_PCD_PLCR_PEMODE_FLS_MASK           0x0000F000
#define FM_PCD_PLCR_PEMODE_FLS_L2             0x00003000
#define FM_PCD_PLCR_PEMODE_FLS_L3             0x0000B000
#define FM_PCD_PLCR_PEMODE_FLS_L4             0x0000E000
#define FM_PCD_PLCR_PEMODE_FLS_FULL           0x0000F000
#define FM_PCD_PLCR_PEMODE_RBFLS              0x00000800
#define FM_PCD_PLCR_PEMODE_TRA                0x00000004
#define FM_PCD_PLCR_PEMODE_TRB                0x00000002
#define FM_PCD_PLCR_PEMODE_TRC                0x00000001
#define FM_PCD_PLCR_DOUBLE_ECC                0x80000000
#define FM_PCD_PLCR_INIT_ENTRY_ERROR          0x40000000
#define FM_PCD_PLCR_PRAM_SELF_INIT_COMPLETE   0x80000000
#define FM_PCD_PLCR_ATOMIC_ACTION_COMPLETE    0x40000000

#define FM_PCD_PLCR_NIA_VALID                 0x80000000

#define FM_PCD_PLCR_GCR_EN                    0x80000000
#define FM_PCD_PLCR_GCR_STEN                  0x40000000
#define FM_PCD_PLCR_GCR_DAR                   0x20000000
#define FM_PCD_PLCR_GCR_DEFNIA                0x00FFFFFF
#define FM_PCD_PLCR_NIA_ABS                   0x00000100

#define FM_PCD_PLCR_GSR_BSY                   0x80000000
#define FM_PCD_PLCR_GSR_DQS                   0x60000000
#define FM_PCD_PLCR_GSR_RPB                   0x20000000
#define FM_PCD_PLCR_GSR_FQS                   0x0C000000
#define FM_PCD_PLCR_GSR_LPALG                 0x0000C000
#define FM_PCD_PLCR_GSR_LPCA                  0x00003000
#define FM_PCD_PLCR_GSR_LPNUM                 0x000000FF

#define FM_PCD_PLCR_EVR_PSIC                  0x80000000
#define FM_PCD_PLCR_EVR_AAC                   0x40000000

#define FM_PCD_PLCR_PAR_PSI                   0x20000000
#define FM_PCD_PLCR_PAR_PNUM                  0x00FF0000
/* PWSEL Selctive select options */
#define FM_PCD_PLCR_PAR_PWSEL_PEMODE          0x00008000    /* 0 */
#define FM_PCD_PLCR_PAR_PWSEL_PEGNIA          0x00004000    /* 1 */
#define FM_PCD_PLCR_PAR_PWSEL_PEYNIA          0x00002000    /* 2 */
#define FM_PCD_PLCR_PAR_PWSEL_PERNIA          0x00001000    /* 3 */
#define FM_PCD_PLCR_PAR_PWSEL_PECIR           0x00000800    /* 4 */
#define FM_PCD_PLCR_PAR_PWSEL_PECBS           0x00000400    /* 5 */
#define FM_PCD_PLCR_PAR_PWSEL_PEPIR_EIR       0x00000200    /* 6 */
#define FM_PCD_PLCR_PAR_PWSEL_PEPBS_EBS       0x00000100    /* 7 */
#define FM_PCD_PLCR_PAR_PWSEL_PELTS           0x00000080    /* 8 */
#define FM_PCD_PLCR_PAR_PWSEL_PECTS           0x00000040    /* 9 */
#define FM_PCD_PLCR_PAR_PWSEL_PEPTS_ETS       0x00000020    /* 10 */
#define FM_PCD_PLCR_PAR_PWSEL_PEGPC           0x00000010    /* 11 */
#define FM_PCD_PLCR_PAR_PWSEL_PEYPC           0x00000008    /* 12 */
#define FM_PCD_PLCR_PAR_PWSEL_PERPC           0x00000004    /* 13 */
#define FM_PCD_PLCR_PAR_PWSEL_PERYPC          0x00000002    /* 14 */
#define FM_PCD_PLCR_PAR_PWSEL_PERRPC          0x00000001    /* 15 */

#define FM_PCD_PLCR_PAR_PMR_BRN_1TO1          0x0000   /* - Full bit replacement. {PBNUM[0:N-1]
                                                           1-> 2^N specific locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_2TO2          0x1      /* - {PBNUM[0:N-2],PNUM[N-1]}.
                                                           2-> 2^(N-1) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_4TO4          0x2      /* - {PBNUM[0:N-3],PNUM[N-2:N-1]}.
                                                           4-> 2^(N-2) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_8TO8          0x3      /* - {PBNUM[0:N-4],PNUM[N-3:N-1]}.
                                                           8->2^(N-3) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_16TO16        0x4      /* - {PBNUM[0:N-5],PNUM[N-4:N-1]}.
                                                           16-> 2^(N-4) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_32TO32        0x5      /* {PBNUM[0:N-6],PNUM[N-5:N-1]}.
                                                           32-> 2^(N-5) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_64TO64        0x6      /* {PBNUM[0:N-7],PNUM[N-6:N-1]}.
                                                           64-> 2^(N-6) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_128TO128      0x7      /* {PBNUM[0:N-8],PNUM[N-7:N-1]}.
                                                            128-> 2^(N-7) base locations. */
#define FM_PCD_PLCR_PAR_PMR_BRN_256TO256      0x8      /* - No bit replacement for N=8. {PNUM[N-8:N-1]}.
                                                            When N=8 this option maps all 256 profiles by the DISPATCH bus into one group. */

#define FM_PCD_PLCR_PMR_V                     0x80000000
#define PLCR_ERR_ECC_CAP                      0x80000000
#define PLCR_ERR_ECC_TYPE_DOUBLE              0x40000000
#define PLCR_ERR_ECC_PNUM_MASK                0x00000FF0
#define PLCR_ERR_ECC_OFFSET_MASK              0x0000000F

#define PLCR_ERR_UNINIT_CAP                   0x80000000
#define PLCR_ERR_UNINIT_NUM_MASK              0x000000FF
#define PLCR_ERR_UNINIT_PID_MASK              0x003f0000
#define PLCR_ERR_UNINIT_ABSOLUTE_MASK         0x00008000

/* shifts */
#define PLCR_ERR_ECC_PNUM_SHIFT               4
#define PLCR_ERR_UNINIT_PID_SHIFT             16

#define FM_PCD_PLCR_PMR_BRN_SHIFT             16

#define PLCR_PORT_WINDOW_SIZE(hardwarePortId)


#endif /* __FM_PLCR_H */
