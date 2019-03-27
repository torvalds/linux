/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
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


#ifndef __FMAN_COMMON_H
#define __FMAN_COMMON_H

/**************************************************************************//**
  @Description       NIA Description
*//***************************************************************************/
#define NIA_ORDER_RESTOR                        0x00800000
#define NIA_ENG_FM_CTL                          0x00000000
#define NIA_ENG_PRS                             0x00440000
#define NIA_ENG_KG                              0x00480000
#define NIA_ENG_PLCR                            0x004C0000
#define NIA_ENG_BMI                             0x00500000
#define NIA_ENG_QMI_ENQ                         0x00540000
#define NIA_ENG_QMI_DEQ                         0x00580000
#define NIA_ENG_MASK                            0x007C0000

#define NIA_FM_CTL_AC_CC                        0x00000006
#define NIA_FM_CTL_AC_HC                        0x0000000C
#define NIA_FM_CTL_AC_IND_MODE_TX               0x00000008
#define NIA_FM_CTL_AC_IND_MODE_RX               0x0000000A
#define NIA_FM_CTL_AC_FRAG                      0x0000000e
#define NIA_FM_CTL_AC_PRE_FETCH                 0x00000010
#define NIA_FM_CTL_AC_POST_FETCH_PCD            0x00000012
#define NIA_FM_CTL_AC_POST_FETCH_PCD_UDP_LEN    0x00000018
#define NIA_FM_CTL_AC_POST_FETCH_NO_PCD         0x00000012
#define NIA_FM_CTL_AC_FRAG_CHECK                0x00000014
#define NIA_FM_CTL_AC_PRE_CC                    0x00000020


#define NIA_BMI_AC_ENQ_FRAME                    0x00000002
#define NIA_BMI_AC_TX_RELEASE                   0x000002C0
#define NIA_BMI_AC_RELEASE                      0x000000C0
#define NIA_BMI_AC_DISCARD                      0x000000C1
#define NIA_BMI_AC_TX                           0x00000274
#define NIA_BMI_AC_FETCH                        0x00000208
#define NIA_BMI_AC_MASK                         0x000003FF

#define NIA_KG_DIRECT                           0x00000100
#define NIA_KG_CC_EN                            0x00000200
#define NIA_PLCR_ABSOLUTE                       0x00008000

#define NIA_BMI_AC_ENQ_FRAME_WITHOUT_DMA        0x00000202
#define NIA_BMI_AC_FETCH_ALL_FRAME              0x0000020c

#endif /* __FMAN_COMMON_H */
