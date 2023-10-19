/*
 *  BSD LICENSE
 *
 *  Copyright(c) 2017 Broadcom Corporation.  All rights reserved.
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

#ifndef __DT_BINDINGS_PINCTRL_BRCM_STINGRAY_H__
#define __DT_BINDINGS_PINCTRL_BRCM_STINGRAY_H__

/* Alternate functions available in MUX controller */
#define MODE_NITRO				0
#define MODE_NAND				1
#define MODE_PNOR				2
#define MODE_GPIO				3

/* Pad configuration attribute */
#define PAD_SLEW_RATE_ENA			(1 << 0)
#define PAD_SLEW_RATE_ENA_MASK			(1 << 0)

#define PAD_DRIVE_STRENGTH_2_MA			(0 << 1)
#define PAD_DRIVE_STRENGTH_4_MA			(1 << 1)
#define PAD_DRIVE_STRENGTH_6_MA			(2 << 1)
#define PAD_DRIVE_STRENGTH_8_MA			(3 << 1)
#define PAD_DRIVE_STRENGTH_10_MA		(4 << 1)
#define PAD_DRIVE_STRENGTH_12_MA		(5 << 1)
#define PAD_DRIVE_STRENGTH_14_MA		(6 << 1)
#define PAD_DRIVE_STRENGTH_16_MA		(7 << 1)
#define PAD_DRIVE_STRENGTH_MASK			(7 << 1)

#define PAD_PULL_UP_ENA				(1 << 4)
#define PAD_PULL_UP_ENA_MASK			(1 << 4)

#define PAD_PULL_DOWN_ENA			(1 << 5)
#define PAD_PULL_DOWN_ENA_MASK			(1 << 5)

#define PAD_INPUT_PATH_DIS			(1 << 6)
#define PAD_INPUT_PATH_DIS_MASK			(1 << 6)

#define PAD_HYSTERESIS_ENA			(1 << 7)
#define PAD_HYSTERESIS_ENA_MASK			(1 << 7)

#endif
