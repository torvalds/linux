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

#ifndef _IXL_DEBUG_H_
#define _IXL_DEBUG_H_

#define MAC_FORMAT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_FORMAT_ARGS(mac_addr) \
	(mac_addr)[0], (mac_addr)[1], (mac_addr)[2], (mac_addr)[3], \
	(mac_addr)[4], (mac_addr)[5]
#define ON_OFF_STR(is_set) ((is_set) ? "On" : "Off")

#ifdef IXL_DEBUG

#define _DBG_PRINTF(S, ...)		printf("%s: " S "\n", __func__, ##__VA_ARGS__)
#define _DEV_DBG_PRINTF(dev, S, ...)	device_printf(dev, "%s: " S "\n", __func__, ##__VA_ARGS__)
#define _IF_DBG_PRINTF(ifp, S, ...)	if_printf(ifp, "%s: " S "\n", __func__, ##__VA_ARGS__)

/* Defines for printing generic debug information */
#define DPRINTF(...)			_DBG_PRINTF(__VA_ARGS__)
#define DDPRINTF(...)			_DEV_DBG_PRINTF(__VA_ARGS__)
#define IDPRINTF(...)			_IF_DBG_PRINTF(__VA_ARGS__)

/* Defines for printing specific debug information */
#define DEBUG_INIT  1
#define DEBUG_IOCTL 1
#define DEBUG_HW    1

#define INIT_DEBUGOUT(...)		if (DEBUG_INIT) _DBG_PRINTF(__VA_ARGS__)
#define INIT_DBG_DEV(...)		if (DEBUG_INIT) _DEV_DBG_PRINTF(__VA_ARGS__)
#define INIT_DBG_IF(...)		if (DEBUG_INIT) _IF_DBG_PRINTF(__VA_ARGS__)

#define IOCTL_DEBUGOUT(...)		if (DEBUG_IOCTL) _DBG_PRINTF(__VA_ARGS__)
#define IOCTL_DBG_IF2(ifp, S, ...)	if (DEBUG_IOCTL) \
					    if_printf(ifp, S "\n", ##__VA_ARGS__)
#define IOCTL_DBG_IF(...)		if (DEBUG_IOCTL) _IF_DBG_PRINTF(__VA_ARGS__)

#define HW_DEBUGOUT(...)		if (DEBUG_HW) _DBG_PRINTF(__VA_ARGS__)

#else /* no IXL_DEBUG */
#define DEBUG_INIT  0
#define DEBUG_IOCTL 0
#define DEBUG_HW    0

#define DPRINTF(...)
#define DDPRINTF(...)
#define IDPRINTF(...)

#define INIT_DEBUGOUT(...)
#define INIT_DBG_DEV(...)
#define INIT_DBG_IF(...)
#define IOCTL_DEBUGOUT(...)
#define IOCTL_DBG_IF2(...)
#define IOCTL_DBG_IF(...)
#define HW_DEBUGOUT(...)
#endif /* IXL_DEBUG */

enum ixl_dbg_mask {
	IXL_DBG_INFO			= 0x00000001,
	IXL_DBG_EN_DIS			= 0x00000002,
	IXL_DBG_AQ			= 0x00000004,
	IXL_DBG_NVMUPD			= 0x00000008,
	IXL_DBG_FILTER			= 0x00000010,

	IXL_DEBUG_RSS			= 0x00000100,

	IXL_DBG_IOV			= 0x00001000,
	IXL_DBG_IOV_VC			= 0x00002000,

	IXL_DBG_SWITCH_INFO		= 0x00010000,
	IXL_DBG_I2C			= 0x00020000,

	IXL_DBG_ALL			= 0xFFFFFFFF
};

enum iavf_dbg_mask {
	IAVF_DBG_INFO			= 0x00000001,
	IAVF_DBG_EN_DIS			= 0x00000002,
	IAVF_DBG_AQ			= 0x00000004,
	IAVF_DBG_INIT			= 0x00000008,
	IAVF_DBG_FILTER			= 0x00000010,

	IAVF_DEBUG_RSS			= 0x00000100,

	IAVF_DBG_VC			= 0x00001000,

	IAVF_DBG_SWITCH_INFO		= 0x00010000,

	IAVF_DBG_ALL			= 0xFFFFFFFF
};

#endif /* _IXL_DEBUG_H_ */
