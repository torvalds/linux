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


#ifndef _IXL_PF_IOV_H_
#define _IXL_PF_IOV_H_

#include "ixl_pf.h"

#include <sys/nv.h>
#include <sys/iov_schema.h>
#include <dev/pci/pci_iov.h>

/* Public functions */

/*
 * These three are DEVMETHODs required for SR-IOV PF support in iflib.
 */
int		ixl_if_iov_init(if_ctx_t ctx, uint16_t num_vfs, const nvlist_t *params);
void		ixl_if_iov_uninit(if_ctx_t ctx);
int		ixl_if_iov_vf_add(if_ctx_t ctx, uint16_t vfnum, const nvlist_t *params);

/*
 * The base PF driver needs to call these during normal execution when
 * SR-IOV mode is active.
 */
void		ixl_initialize_sriov(struct ixl_pf *pf);
void		ixl_handle_vf_msg(struct ixl_pf *pf, struct i40e_arq_event_info *event);
void		ixl_handle_vflr(struct ixl_pf *pf);
void		ixl_broadcast_link_state(struct ixl_pf *pf);

#endif /* _IXL_PF_IOV_H_ */
