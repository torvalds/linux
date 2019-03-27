/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_FCOE_H__
#define __ECORE_FCOE_H__

#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_hsi_common.h"
#include "ecore_hsi_fcoe.h"
#include "ecore_fcoe_api.h"

struct ecore_fcoe_info {
	osal_spinlock_t	lock;
	osal_list_t	free_list;
};

#ifdef CONFIG_ECORE_FCOE
enum _ecore_status_t ecore_fcoe_alloc(struct ecore_hwfn *p_hwfn);

void ecore_fcoe_setup(struct ecore_hwfn *p_hwfn);

void ecore_fcoe_free(struct ecore_hwfn *p_hwfn);
#else
static inline enum _ecore_status_t ecore_fcoe_alloc(struct ecore_hwfn OSAL_UNUSED *p_hwfn)
{
	return ECORE_INVAL;
}

static inline void ecore_fcoe_setup(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}

static inline void ecore_fcoe_free(struct ecore_hwfn OSAL_UNUSED *p_hwfn) {}
#endif

#ifndef __EXTRACT__LINUX__THROW__
enum _ecore_status_t
ecore_sp_fcoe_conn_offload(struct ecore_hwfn *p_hwfn,
			   struct ecore_fcoe_conn *p_conn,
			   enum spq_mode comp_mode,
			   struct ecore_spq_comp_cb *p_comp_addr);

enum _ecore_status_t
ecore_sp_fcoe_conn_destroy(struct ecore_hwfn *p_hwfn,
			   struct ecore_fcoe_conn *p_conn,
			   enum spq_mode comp_mode,
			   struct ecore_spq_comp_cb *p_comp_addr);
#endif

#endif  /*__ECORE_FCOE_H__*/

