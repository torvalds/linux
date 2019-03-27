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

#ifndef _E1000_82543_H_
#define _E1000_82543_H_

#define PHY_PREAMBLE		0xFFFFFFFF
#define PHY_PREAMBLE_SIZE	32
#define PHY_SOF			0x1
#define PHY_OP_READ		0x2
#define PHY_OP_WRITE		0x1
#define PHY_TURNAROUND		0x2

#define TBI_COMPAT_ENABLED	0x1 /* Global "knob" for the workaround */
/* If TBI_COMPAT_ENABLED, then this is the current state (on/off) */
#define TBI_SBP_ENABLED		0x2

void e1000_tbi_adjust_stats_82543(struct e1000_hw *hw,
				  struct e1000_hw_stats *stats,
				  u32 frame_len, u8 *mac_addr,
				  u32 max_frame_size);
void e1000_set_tbi_compatibility_82543(struct e1000_hw *hw,
				       bool state);
bool e1000_tbi_sbp_enabled_82543(struct e1000_hw *hw);

#endif
