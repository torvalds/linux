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

#ifndef _E1000_82571_H_
#define _E1000_82571_H_

#define ID_LED_RESERVED_F746	0xF746
#define ID_LED_DEFAULT_82573	((ID_LED_DEF1_DEF2 << 12) | \
				 (ID_LED_OFF1_ON2  <<  8) | \
				 (ID_LED_DEF1_DEF2 <<  4) | \
				 (ID_LED_DEF1_DEF2))

#define E1000_GCR_L1_ACT_WITHOUT_L0S_RX	0x08000000
#define AN_RETRY_COUNT		5 /* Autoneg Retry Count value */

/* Intr Throttling - RW */
#define E1000_EITR_82574(_n)	(0x000E8 + (0x4 * (_n)))

#define E1000_EIAC_82574	0x000DC /* Ext. Interrupt Auto Clear - RW */
#define E1000_EIAC_MASK_82574	0x01F00000

#define E1000_IVAR_INT_ALLOC_VALID	0x8

/* Manageability Operation Mode mask */
#define E1000_NVM_INIT_CTRL2_MNGM	0x6000

#define E1000_BASE1000T_STATUS		10
#define E1000_IDLE_ERROR_COUNT_MASK	0xFF
#define E1000_RECEIVE_ERROR_COUNTER	21
#define E1000_RECEIVE_ERROR_MAX		0xFFFF
bool e1000_check_phy_82574(struct e1000_hw *hw);
bool e1000_get_laa_state_82571(struct e1000_hw *hw);
void e1000_set_laa_state_82571(struct e1000_hw *hw, bool state);

#endif
