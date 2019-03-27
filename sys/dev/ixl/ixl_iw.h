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

#ifndef _IXL_IW_H_
#define _IXL_IW_H_

#define IXL_IW_MAX_USER_PRIORITY 8
#define IXL_IW_MAX_MSIX	64

struct ixl_iw_msix_mapping {
	u8	itr_indx;
	int	aeq_vector;
	int	ceq_cnt;
	int	*ceq_vector;
};

struct ixl_iw_msix {
	int	base;
	int	count;
};

struct ixl_iw_pf {
	void		*handle;
	struct ifnet	*ifp;
	device_t	dev;
	struct resource	*pci_mem;
	u8		pf_id;
	u16		mtu;
	struct ixl_iw_msix	iw_msix;
	u16	qs_handle[IXL_IW_MAX_USER_PRIORITY];
};

struct ixl_iw_ops {
	int (*init)(struct ixl_iw_pf *pf_info);
	int (*stop)(struct ixl_iw_pf *pf_info);
};

int	ixl_iw_pf_reset(void *pf_handle);
int	ixl_iw_pf_msix_init(void *pf_handle,
	    struct ixl_iw_msix_mapping *msix_info);
int	ixl_iw_register(struct ixl_iw_ops *iw_ops);
int	ixl_iw_unregister(void);

#endif /* _IXL_IW_H_ */
