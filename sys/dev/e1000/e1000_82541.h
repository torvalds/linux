/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _E1000_82541_H_
#define _E1000_82541_H_

#define NVM_WORD_SIZE_BASE_SHIFT_82541 (NVM_WORD_SIZE_BASE_SHIFT + 1)

#define IGP01E1000_PHY_CHANNEL_NUM		4

#define IGP01E1000_PHY_AGC_A			0x1172
#define IGP01E1000_PHY_AGC_B			0x1272
#define IGP01E1000_PHY_AGC_C			0x1472
#define IGP01E1000_PHY_AGC_D			0x1872

#define IGP01E1000_PHY_AGC_PARAM_A		0x1171
#define IGP01E1000_PHY_AGC_PARAM_B		0x1271
#define IGP01E1000_PHY_AGC_PARAM_C		0x1471
#define IGP01E1000_PHY_AGC_PARAM_D		0x1871

#define IGP01E1000_PHY_EDAC_MU_INDEX		0xC000
#define IGP01E1000_PHY_EDAC_SIGN_EXT_9_BITS	0x8000

#define IGP01E1000_PHY_DSP_RESET		0x1F33

#define IGP01E1000_PHY_DSP_FFE			0x1F35
#define IGP01E1000_PHY_DSP_FFE_CM_CP		0x0069
#define IGP01E1000_PHY_DSP_FFE_DEFAULT		0x002A

#define IGP01E1000_IEEE_FORCE_GIG		0x0140
#define IGP01E1000_IEEE_RESTART_AUTONEG		0x3300

#define IGP01E1000_AGC_LENGTH_SHIFT		7
#define IGP01E1000_AGC_RANGE			10

#define FFE_IDLE_ERR_COUNT_TIMEOUT_20		20
#define FFE_IDLE_ERR_COUNT_TIMEOUT_100		100

#define IGP01E1000_ANALOG_FUSE_STATUS		0x20D0
#define IGP01E1000_ANALOG_SPARE_FUSE_STATUS	0x20D1
#define IGP01E1000_ANALOG_FUSE_CONTROL		0x20DC
#define IGP01E1000_ANALOG_FUSE_BYPASS		0x20DE

#define IGP01E1000_ANALOG_SPARE_FUSE_ENABLED	0x0100
#define IGP01E1000_ANALOG_FUSE_FINE_MASK	0x0F80
#define IGP01E1000_ANALOG_FUSE_COARSE_MASK	0x0070
#define IGP01E1000_ANALOG_FUSE_COARSE_THRESH	0x0040
#define IGP01E1000_ANALOG_FUSE_COARSE_10	0x0010
#define IGP01E1000_ANALOG_FUSE_FINE_1		0x0080
#define IGP01E1000_ANALOG_FUSE_FINE_10		0x0500
#define IGP01E1000_ANALOG_FUSE_POLY_MASK	0xF000
#define IGP01E1000_ANALOG_FUSE_ENABLE_SW_CONTROL 0x0002

#define IGP01E1000_MSE_CHANNEL_D		0x000F
#define IGP01E1000_MSE_CHANNEL_C		0x00F0
#define IGP01E1000_MSE_CHANNEL_B		0x0F00
#define IGP01E1000_MSE_CHANNEL_A		0xF000


void e1000_init_script_state_82541(struct e1000_hw *hw, bool state);
#endif
