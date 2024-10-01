/*
 *  BSD LICENSE
 *
 *  Copyright(c) 2014 Broadcom Corporation.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Broadcom Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CLOCK_BCM_CYGNUS_H
#define _CLOCK_BCM_CYGNUS_H

/* GENPLL clock ID */
#define BCM_CYGNUS_GENPLL                     0
#define BCM_CYGNUS_GENPLL_AXI21_CLK           1
#define BCM_CYGNUS_GENPLL_250MHZ_CLK          2
#define BCM_CYGNUS_GENPLL_IHOST_SYS_CLK       3
#define BCM_CYGNUS_GENPLL_ENET_SW_CLK         4
#define BCM_CYGNUS_GENPLL_AUDIO_125_CLK       5
#define BCM_CYGNUS_GENPLL_CAN_CLK             6

/* LCPLL0 clock ID */
#define BCM_CYGNUS_LCPLL0                     0
#define BCM_CYGNUS_LCPLL0_PCIE_PHY_REF_CLK    1
#define BCM_CYGNUS_LCPLL0_DDR_PHY_CLK         2
#define BCM_CYGNUS_LCPLL0_SDIO_CLK            3
#define BCM_CYGNUS_LCPLL0_USB_PHY_REF_CLK     4
#define BCM_CYGNUS_LCPLL0_SMART_CARD_CLK      5
#define BCM_CYGNUS_LCPLL0_CH5_UNUSED          6

/* MIPI PLL clock ID */
#define BCM_CYGNUS_MIPIPLL                    0
#define BCM_CYGNUS_MIPIPLL_CH0_UNUSED         1
#define BCM_CYGNUS_MIPIPLL_CH1_LCD            2
#define BCM_CYGNUS_MIPIPLL_CH2_V3D            3
#define BCM_CYGNUS_MIPIPLL_CH3_UNUSED         4
#define BCM_CYGNUS_MIPIPLL_CH4_UNUSED         5
#define BCM_CYGNUS_MIPIPLL_CH5_UNUSED         6

/* ASIU clock ID */
#define BCM_CYGNUS_ASIU_KEYPAD_CLK    0
#define BCM_CYGNUS_ASIU_ADC_CLK       1
#define BCM_CYGNUS_ASIU_PWM_CLK       2

/* AUDIO clock ID */
#define BCM_CYGNUS_AUDIOPLL           0
#define BCM_CYGNUS_AUDIOPLL_CH0       1
#define BCM_CYGNUS_AUDIOPLL_CH1       2
#define BCM_CYGNUS_AUDIOPLL_CH2       3

#endif /* _CLOCK_BCM_CYGNUS_H */
