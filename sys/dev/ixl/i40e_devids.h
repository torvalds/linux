/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

#ifndef _I40E_DEVIDS_H_
#define _I40E_DEVIDS_H_

/* Vendor ID */
#define I40E_INTEL_VENDOR_ID		0x8086

/* Device IDs */
#define I40E_DEV_ID_SFP_XL710		0x1572
#define I40E_DEV_ID_QEMU		0x1574
#define I40E_DEV_ID_KX_B		0x1580
#define I40E_DEV_ID_KX_C		0x1581
#define I40E_DEV_ID_QSFP_A		0x1583
#define I40E_DEV_ID_QSFP_B		0x1584
#define I40E_DEV_ID_QSFP_C		0x1585
#define I40E_DEV_ID_10G_BASE_T		0x1586
#define I40E_DEV_ID_20G_KR2		0x1587
#define I40E_DEV_ID_20G_KR2_A		0x1588
#define I40E_DEV_ID_10G_BASE_T4		0x1589
#define I40E_DEV_ID_25G_B		0x158A
#define I40E_DEV_ID_25G_SFP28		0x158B
#define I40E_DEV_ID_VF			0x154C
#define I40E_DEV_ID_VF_HV		0x1571
#define I40E_DEV_ID_ADAPTIVE_VF		0x1889
#define I40E_DEV_ID_KX_X722		0x37CE
#define I40E_DEV_ID_QSFP_X722		0x37CF
#define I40E_DEV_ID_SFP_X722		0x37D0
#define I40E_DEV_ID_1G_BASE_T_X722	0x37D1
#define I40E_DEV_ID_10G_BASE_T_X722	0x37D2
#define I40E_DEV_ID_SFP_I_X722		0x37D3
#define I40E_DEV_ID_X722_VF		0x37CD

#define i40e_is_40G_device(d)		((d) == I40E_DEV_ID_QSFP_A  || \
					 (d) == I40E_DEV_ID_QSFP_B  || \
					 (d) == I40E_DEV_ID_QSFP_C)

#define i40e_is_25G_device(d)		((d) == I40E_DEV_ID_25G_B  || \
					 (d) == I40E_DEV_ID_25G_SFP28)

#endif /* _I40E_DEVIDS_H_ */
