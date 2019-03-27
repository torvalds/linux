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

#ifndef _IXL_IW_INT_H_
#define _IXL_IW_INT_H_

enum ixl_iw_pf_state {
	IXL_IW_PF_STATE_OFF,
	IXL_IW_PF_STATE_ON
};

struct ixl_iw_pf_entry_state {
	enum ixl_iw_pf_state pf;
	enum ixl_iw_pf_state iw_scheduled;
	enum ixl_iw_pf_state iw_current;
};

struct ixl_iw_pf_entry {
	LIST_ENTRY(ixl_iw_pf_entry)	node;
	struct ixl_pf			*pf;
	struct ixl_iw_pf_entry_state	state;
	struct ixl_iw_pf		pf_info;
	struct task			iw_task;
};

LIST_HEAD(ixl_iw_pfs_head, ixl_iw_pf_entry);
struct ixl_iw_state {
	struct ixl_iw_ops	*ops;
	bool			registered;
	struct ixl_iw_pfs_head	pfs;
	struct mtx		mtx;
	struct taskqueue 	*tq;
};

int	ixl_iw_pf_init(struct ixl_pf *pf);
void	ixl_iw_pf_stop(struct ixl_pf *pf);
int	ixl_iw_pf_attach(struct ixl_pf *pf);
int	ixl_iw_pf_detach(struct ixl_pf *pf);

#endif /* _IXL_IW_INT_H_ */
