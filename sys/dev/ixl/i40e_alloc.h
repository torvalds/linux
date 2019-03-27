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

#ifndef _I40E_ALLOC_H_
#define _I40E_ALLOC_H_

struct i40e_hw;

/* Memory allocation types */
enum i40e_memory_type {
	i40e_mem_arq_buf = 0,		/* ARQ indirect command buffer */
	i40e_mem_asq_buf = 1,
	i40e_mem_atq_buf = 2,		/* ATQ indirect command buffer */
	i40e_mem_arq_ring = 3,		/* ARQ descriptor ring */
	i40e_mem_atq_ring = 4,		/* ATQ descriptor ring */
	i40e_mem_pd = 5,		/* Page Descriptor */
	i40e_mem_bp = 6,		/* Backing Page - 4KB */
	i40e_mem_bp_jumbo = 7,		/* Backing Page - > 4KB */
	i40e_mem_reserved
};

/* prototype for functions used for dynamic memory allocation */
enum i40e_status_code i40e_allocate_dma_mem(struct i40e_hw *hw,
					    struct i40e_dma_mem *mem,
					    enum i40e_memory_type type,
					    u64 size, u32 alignment);
enum i40e_status_code i40e_free_dma_mem(struct i40e_hw *hw,
					struct i40e_dma_mem *mem);
enum i40e_status_code i40e_allocate_virt_mem(struct i40e_hw *hw,
					     struct i40e_virt_mem *mem,
					     u32 size);
enum i40e_status_code i40e_free_virt_mem(struct i40e_hw *hw,
					 struct i40e_virt_mem *mem);

#endif /* _I40E_ALLOC_H_ */
