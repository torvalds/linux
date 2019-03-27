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

#include "ixl.h"
#include "ixl_pf.h"
#include "ixl_iw.h"
#include "ixl_iw_int.h"

#ifdef	IXL_IW

#define IXL_IW_VEC_BASE(pf)	((pf)->msix - (pf)->iw_msix)
#define IXL_IW_VEC_COUNT(pf)	((pf)->iw_msix)
#define IXL_IW_VEC_LIMIT(pf)	((pf)->msix)

extern int ixl_enable_iwarp;

static struct ixl_iw_state ixl_iw;
static int ixl_iw_ref_cnt;

static void
ixl_iw_pf_msix_reset(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 reg;
	int vec;

	for (vec = IXL_IW_VEC_BASE(pf); vec < IXL_IW_VEC_LIMIT(pf); vec++) {
		reg = I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK;
		wr32(hw, I40E_PFINT_LNKLSTN(vec - 1), reg);
	}

	return;
}

static void
ixl_iw_invoke_op(void *context, int pending)
{
	struct ixl_iw_pf_entry *pf_entry = (struct ixl_iw_pf_entry *)context;
	struct ixl_iw_pf info;
	bool initialize;
	int err;

	INIT_DEBUGOUT("begin");

	mtx_lock(&ixl_iw.mtx);
	if ((pf_entry->state.iw_scheduled == IXL_IW_PF_STATE_ON) &&
	    (pf_entry->state.iw_current == IXL_IW_PF_STATE_OFF))
		initialize = true;
	else if ((pf_entry->state.iw_scheduled == IXL_IW_PF_STATE_OFF) &&
	         (pf_entry->state.iw_current == IXL_IW_PF_STATE_ON))
		initialize = false;
	else {
		/* nothing to be done, so finish here */
		mtx_unlock(&ixl_iw.mtx);
		return;
	}
	info = pf_entry->pf_info;
	mtx_unlock(&ixl_iw.mtx);

	if (initialize) {
		err = ixl_iw.ops->init(&info);
		if (err)
			device_printf(pf_entry->pf->dev,
				"%s: failed to initialize iwarp (err %d)\n",
				__func__, err);
		else
			pf_entry->state.iw_current = IXL_IW_PF_STATE_ON;
	} else {
		err = ixl_iw.ops->stop(&info);
		if (err)
			device_printf(pf_entry->pf->dev,
				"%s: failed to stop iwarp (err %d)\n",
				__func__, err);
		else {
			ixl_iw_pf_msix_reset(pf_entry->pf);
			pf_entry->state.iw_current = IXL_IW_PF_STATE_OFF;
		}
	}
	return;
}

static void
ixl_iw_uninit(void)
{
	INIT_DEBUGOUT("begin");

	mtx_destroy(&ixl_iw.mtx);

	return;
}

static void
ixl_iw_init(void)
{
	INIT_DEBUGOUT("begin");

	LIST_INIT(&ixl_iw.pfs);
	mtx_init(&ixl_iw.mtx, "ixl_iw_pfs", NULL, MTX_DEF);
	ixl_iw.registered = false;

	return;
}

/******************************************************************************
 * if_ixl internal API
 *****************************************************************************/

int
ixl_iw_pf_init(struct ixl_pf *pf)
{
	struct ixl_iw_pf_entry *pf_entry;
	struct ixl_iw_pf *pf_info;
	int err = 0;

	INIT_DEBUGOUT("begin");

	mtx_lock(&ixl_iw.mtx);

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->pf == pf)
			break;
	if (pf_entry == NULL) {
		/* attempt to initialize PF not yet attached - sth is wrong */
		device_printf(pf->dev, "%s: PF not found\n", __func__);
		err = ENOENT;
		goto out;
	}

	pf_info = &pf_entry->pf_info;

	pf_info->handle	= (void *)pf;

	pf_info->ifp		= pf->vsi.ifp;
	pf_info->dev		= pf->dev;
	pf_info->pci_mem	= pf->pci_mem;
	pf_info->pf_id		= pf->hw.pf_id;
	pf_info->mtu		= pf->vsi.ifp->if_mtu;

	pf_info->iw_msix.count	= IXL_IW_VEC_COUNT(pf);
	pf_info->iw_msix.base	= IXL_IW_VEC_BASE(pf);

	for (int i = 0; i < IXL_IW_MAX_USER_PRIORITY; i++)
		pf_info->qs_handle[i] = le16_to_cpu(pf->vsi.info.qs_handle[0]);

	pf_entry->state.pf = IXL_IW_PF_STATE_ON;
	if (ixl_iw.registered) {
		pf_entry->state.iw_scheduled = IXL_IW_PF_STATE_ON;
		taskqueue_enqueue(ixl_iw.tq, &pf_entry->iw_task);
	}

out:
	mtx_unlock(&ixl_iw.mtx);

	return (err);
}

void
ixl_iw_pf_stop(struct ixl_pf *pf)
{
	struct ixl_iw_pf_entry *pf_entry;

	INIT_DEBUGOUT("begin");

	mtx_lock(&ixl_iw.mtx);

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->pf == pf)
			break;
	if (pf_entry == NULL) {
		/* attempt to stop PF which has not been attached - sth is wrong */
		device_printf(pf->dev, "%s: PF not found\n", __func__);
		goto out;
	}

	pf_entry->state.pf = IXL_IW_PF_STATE_OFF;
	if (pf_entry->state.iw_scheduled == IXL_IW_PF_STATE_ON) {
		pf_entry->state.iw_scheduled = IXL_IW_PF_STATE_OFF;
		if (ixl_iw.registered)
			taskqueue_enqueue(ixl_iw.tq, &pf_entry->iw_task);
	}

out:
	mtx_unlock(&ixl_iw.mtx);

	return;
}

int
ixl_iw_pf_attach(struct ixl_pf *pf)
{
	struct ixl_iw_pf_entry *pf_entry;
	int err = 0;

	INIT_DEBUGOUT("begin");

	if (ixl_iw_ref_cnt == 0)
		ixl_iw_init();

	mtx_lock(&ixl_iw.mtx);

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->pf == pf) {
			device_printf(pf->dev, "%s: PF already exists\n",
			    __func__);
			err = EEXIST;
			goto out;
		}

	pf_entry = malloc(sizeof(struct ixl_iw_pf_entry),
			M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pf_entry == NULL) {
		device_printf(pf->dev,
		    "%s: failed to allocate memory to attach new PF\n",
		    __func__);
		err = ENOMEM;
		goto out;
	}
	pf_entry->pf = pf;
	pf_entry->state.pf		= IXL_IW_PF_STATE_OFF;
	pf_entry->state.iw_scheduled	= IXL_IW_PF_STATE_OFF;
	pf_entry->state.iw_current	= IXL_IW_PF_STATE_OFF;

	LIST_INSERT_HEAD(&ixl_iw.pfs, pf_entry, node);
	ixl_iw_ref_cnt++;

	TASK_INIT(&pf_entry->iw_task, 0, ixl_iw_invoke_op, pf_entry);
out:
	mtx_unlock(&ixl_iw.mtx);

	return (err);
}

int
ixl_iw_pf_detach(struct ixl_pf *pf)
{
	struct ixl_iw_pf_entry *pf_entry;
	int err = 0;

	INIT_DEBUGOUT("begin");

	mtx_lock(&ixl_iw.mtx);

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->pf == pf)
			break;
	if (pf_entry == NULL) {
		/* attempt to stop PF which has not been attached - sth is wrong */
		device_printf(pf->dev, "%s: PF not found\n", __func__);
		err = ENOENT;
		goto out;
	}

	if (pf_entry->state.pf != IXL_IW_PF_STATE_OFF) {
		/* attempt to detach PF which has not yet been stopped - sth is wrong */
		device_printf(pf->dev, "%s: failed - PF is still active\n",
		    __func__);
		err = EBUSY;
		goto out;
	}
	LIST_REMOVE(pf_entry, node);
	free(pf_entry, M_DEVBUF);
	ixl_iw_ref_cnt--;

out:
	mtx_unlock(&ixl_iw.mtx);

	if (ixl_iw_ref_cnt == 0)
		ixl_iw_uninit();

	return (err);
}


/******************************************************************************
 * API exposed to iw_ixl module
 *****************************************************************************/

int
ixl_iw_pf_reset(void *pf_handle)
{
	struct ixl_pf *pf = (struct ixl_pf *)pf_handle;

	INIT_DEBUGOUT("begin");

	IXL_PF_LOCK(pf);
	ixl_init_locked(pf);
	IXL_PF_UNLOCK(pf);

	return (0);
}

int
ixl_iw_pf_msix_init(void *pf_handle,
	struct ixl_iw_msix_mapping *msix_info)
{
	struct ixl_pf *pf = (struct ixl_pf *)pf_handle;
	struct i40e_hw *hw = &pf->hw;
	u32 reg;
	int vec, i;

	INIT_DEBUGOUT("begin");

	if ((msix_info->aeq_vector < IXL_IW_VEC_BASE(pf)) ||
	    (msix_info->aeq_vector >= IXL_IW_VEC_LIMIT(pf))) {
		printf("%s: invalid MSI-X vector (%i) for AEQ\n",
		    __func__, msix_info->aeq_vector);
		return (EINVAL);
	}
	reg = I40E_PFINT_AEQCTL_CAUSE_ENA_MASK |
		(msix_info->aeq_vector << I40E_PFINT_AEQCTL_MSIX_INDX_SHIFT) |
		(msix_info->itr_indx << I40E_PFINT_AEQCTL_ITR_INDX_SHIFT);
	wr32(hw, I40E_PFINT_AEQCTL, reg);

	for (vec = IXL_IW_VEC_BASE(pf); vec < IXL_IW_VEC_LIMIT(pf); vec++) {
		for (i = 0; i < msix_info->ceq_cnt; i++)
			if (msix_info->ceq_vector[i] == vec)
				break;
		if (i == msix_info->ceq_cnt) {
			/* this vector has no CEQ mapped */
			reg = I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK;
			wr32(hw, I40E_PFINT_LNKLSTN(vec - 1), reg);
		} else {
			reg = (i & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK) |
			    (I40E_QUEUE_TYPE_PE_CEQ <<
			    I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT);
			wr32(hw, I40E_PFINT_LNKLSTN(vec - 1), reg);

			reg = I40E_PFINT_CEQCTL_CAUSE_ENA_MASK |
			    (vec << I40E_PFINT_CEQCTL_MSIX_INDX_SHIFT) |
			    (msix_info->itr_indx <<
			    I40E_PFINT_CEQCTL_ITR_INDX_SHIFT) |
			    (IXL_QUEUE_EOL <<
			    I40E_PFINT_CEQCTL_NEXTQ_INDX_SHIFT);
			wr32(hw, I40E_PFINT_CEQCTL(i), reg);
		}
	}

	return (0);
}

int
ixl_iw_register(struct ixl_iw_ops *ops)
{
	struct ixl_iw_pf_entry *pf_entry;
	int err = 0;
	int iwarp_cap_on_pfs = 0;

	INIT_DEBUGOUT("begin");
	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		iwarp_cap_on_pfs += pf_entry->pf->hw.func_caps.iwarp;
	if (!iwarp_cap_on_pfs && ixl_enable_iwarp) {
		printf("%s: the device is not iwarp-capable, registering dropped\n",
		    __func__);
		return (ENODEV);
	}
	if (ixl_enable_iwarp == 0) {
		printf("%s: enable_iwarp is off, registering dropped\n",
		    __func__);
		return (EACCES);
	}

	if ((ops->init == NULL) || (ops->stop == NULL)) {
		printf("%s: invalid iwarp driver ops\n", __func__);
		return (EINVAL);
	}

	mtx_lock(&ixl_iw.mtx);
	if (ixl_iw.registered) {
		printf("%s: iwarp driver already registered\n", __func__);
		err = (EBUSY);
		goto out;
	}
	ixl_iw.registered = true;
	mtx_unlock(&ixl_iw.mtx);

	ixl_iw.tq = taskqueue_create("ixl_iw", M_NOWAIT,
		taskqueue_thread_enqueue, &ixl_iw.tq);
	if (ixl_iw.tq == NULL) {
		printf("%s: failed to create queue\n", __func__);
		ixl_iw.registered = false;
		return (ENOMEM);
	}
	taskqueue_start_threads(&ixl_iw.tq, 1, PI_NET, "ixl iw");

	ixl_iw.ops = malloc(sizeof(struct ixl_iw_ops),
			M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ixl_iw.ops == NULL) {
		printf("%s: failed to allocate memory\n", __func__);
		taskqueue_free(ixl_iw.tq);
		ixl_iw.registered = false;
		return (ENOMEM);
	}

	ixl_iw.ops->init = ops->init;
	ixl_iw.ops->stop = ops->stop;

	mtx_lock(&ixl_iw.mtx);
	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->state.pf == IXL_IW_PF_STATE_ON) {
			pf_entry->state.iw_scheduled = IXL_IW_PF_STATE_ON;
			taskqueue_enqueue(ixl_iw.tq, &pf_entry->iw_task);
		}
out:
	mtx_unlock(&ixl_iw.mtx);

	return (err);
}

int
ixl_iw_unregister(void)
{
	struct ixl_iw_pf_entry *pf_entry;
	int iwarp_cap_on_pfs = 0;

	INIT_DEBUGOUT("begin");

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		iwarp_cap_on_pfs += pf_entry->pf->hw.func_caps.iwarp;
	if (!iwarp_cap_on_pfs && ixl_enable_iwarp) {
		printf("%s: attempt to unregister driver when no iwarp-capable device present\n",
		    __func__);
		return (ENODEV);
	}

	if (ixl_enable_iwarp == 0) {
		printf("%s: attempt to unregister driver when enable_iwarp is off\n",
		    __func__);
		return (ENODEV);
	}
	mtx_lock(&ixl_iw.mtx);

	if (!ixl_iw.registered) {
		printf("%s: failed - iwarp driver has not been registered\n",
		    __func__);
		mtx_unlock(&ixl_iw.mtx);
		return (ENOENT);
	}

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		if (pf_entry->state.iw_scheduled == IXL_IW_PF_STATE_ON) {
			pf_entry->state.iw_scheduled = IXL_IW_PF_STATE_OFF;
			taskqueue_enqueue(ixl_iw.tq, &pf_entry->iw_task);
		}

	ixl_iw.registered = false;

	mtx_unlock(&ixl_iw.mtx);

	LIST_FOREACH(pf_entry, &ixl_iw.pfs, node)
		taskqueue_drain(ixl_iw.tq, &pf_entry->iw_task);
	taskqueue_free(ixl_iw.tq);
	ixl_iw.tq = NULL;
	free(ixl_iw.ops, M_DEVBUF);
	ixl_iw.ops = NULL;

	return (0);
}

#endif /* IXL_IW */
