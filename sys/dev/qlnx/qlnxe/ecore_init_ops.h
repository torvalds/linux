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

#ifndef __ECORE_INIT_OPS__
#define __ECORE_INIT_OPS__

#include "ecore.h"

/**
 * @brief ecore_init_iro_array - init iro_arr.
 *
 *
 * @param p_dev
 */
void ecore_init_iro_array(struct ecore_dev *p_dev);

/**
 * @brief ecore_init_run - Run the init-sequence.
 *
 *
 * @param p_hwfn
 * @param p_ptt
 * @param phase 
 * @param phase_id 
 * @param modes
 * @return _ecore_status_t
 */
enum _ecore_status_t ecore_init_run(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt  *p_ptt,
				    int               phase,
				    int               phase_id,
				    int               modes);

/**
 * @brief ecore_init_hwfn_allocate - Allocate RT array, Store 'values' ptrs.
 *
 *
 * @param p_hwfn
 *
 * @return _ecore_status_t
 */
enum _ecore_status_t ecore_init_alloc(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_init_hwfn_deallocate
 *
 *
 * @param p_hwfn
 */
void ecore_init_free(struct ecore_hwfn *p_hwfn);


/**
 * @brief ecore_init_clear_rt_data - Clears the runtime init array.
 *
 *
 * @param p_hwfn
 */
void ecore_init_clear_rt_data(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_init_store_rt_reg - Store a configuration value in the RT array.
 *
 *
 * @param p_hwfn
 * @param rt_offset
 * @param val
 */
void ecore_init_store_rt_reg(struct ecore_hwfn *p_hwfn,
			     u32               	rt_offset,
			     u32               	val);

#define STORE_RT_REG(hwfn, offset, val)				\
	ecore_init_store_rt_reg(hwfn, offset, val)

#define OVERWRITE_RT_REG(hwfn, offset, val)			\
	ecore_init_store_rt_reg(hwfn, offset, val)

/**
* @brief
*
*
* @param p_hwfn
* @param rt_offset
* @param val
* @param size
*/

void ecore_init_store_rt_agg(struct ecore_hwfn *p_hwfn,
			     u32               rt_offset,
			     u32               *val,
			     osal_size_t       size);

#define STORE_RT_REG_AGG(hwfn, offset, val)			\
	ecore_init_store_rt_agg(hwfn, offset, (u32*)&val, sizeof(val))


/**
 * @brief 
 *      Initialize GTT global windows and set admin window
 *      related params of GTT/PTT to default values. 
 * 
 * @param p_hwfn 
 */
void ecore_gtt_init(struct ecore_hwfn *p_hwfn,
		    struct ecore_ptt *p_ptt);
#endif /* __ECORE_INIT_OPS__ */
