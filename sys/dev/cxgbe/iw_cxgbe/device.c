/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/ktr.h>

#include <linux/module.h>
#include <linux/moduleparam.h>

#include <rdma/ib_verbs.h>
#include <linux/idr.h>

#ifdef TCP_OFFLOAD
#include "iw_cxgbe.h"

void
c4iw_release_dev_ucontext(struct c4iw_rdev *rdev,
    struct c4iw_dev_ucontext *uctx)
{
	struct list_head *pos, *nxt;
	struct c4iw_qid_list *entry;

	mutex_lock(&uctx->lock);
	list_for_each_safe(pos, nxt, &uctx->qpids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		if (!(entry->qid & rdev->qpmask)) {
			c4iw_put_resource(&rdev->resource.qid_table,
					  entry->qid);
			mutex_lock(&rdev->stats.lock);
			rdev->stats.qid.cur -= rdev->qpmask + 1;
			mutex_unlock(&rdev->stats.lock);
		}
		kfree(entry);
	}

	list_for_each_safe(pos, nxt, &uctx->cqids) {
		entry = list_entry(pos, struct c4iw_qid_list, entry);
		list_del_init(&entry->entry);
		kfree(entry);
	}
	mutex_unlock(&uctx->lock);
}

void
c4iw_init_dev_ucontext(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx)
{

	INIT_LIST_HEAD(&uctx->qpids);
	INIT_LIST_HEAD(&uctx->cqids);
	mutex_init(&uctx->lock);
}

static int
c4iw_rdev_open(struct c4iw_rdev *rdev)
{
	struct adapter *sc = rdev->adap;
	struct sge_params *sp = &sc->params.sge;
	int rc;
	unsigned short ucq_density = 1 << sp->iq_s_qpp; /* # of user CQs/page */
	unsigned short udb_density = 1 << sp->eq_s_qpp; /* # of user DB/page */


	c4iw_init_dev_ucontext(rdev, &rdev->uctx);

	/*
	 * This implementation assumes udb_density == ucq_density!  Eventually
	 * we might need to support this but for now fail the open. Also the
	 * cqid and qpid range must match for now.
	 */
	if (udb_density != ucq_density) {
		device_printf(sc->dev, "unsupported udb/ucq densities %u/%u\n",
		    udb_density, ucq_density);
		rc = -EINVAL;
		goto err1;
	}
	if (sc->vres.qp.start != sc->vres.cq.start ||
	    sc->vres.qp.size != sc->vres.cq.size) {
		device_printf(sc->dev, "%s: unsupported qp and cq id ranges "
			"qp start %u size %u cq start %u size %u\n", __func__,
			sc->vres.qp.start, sc->vres.qp.size, sc->vres.cq.start,
			sc->vres.cq.size);
		rc = -EINVAL;
		goto err1;
	}

	rdev->qpshift = PAGE_SHIFT - sp->eq_s_qpp;
	rdev->qpmask = udb_density - 1;
	rdev->cqshift = PAGE_SHIFT - sp->iq_s_qpp;
	rdev->cqmask = ucq_density - 1;

	if (c4iw_num_stags(rdev) == 0) {
		rc = -EINVAL;
		goto err1;
	}

	rdev->stats.pd.total = T4_MAX_NUM_PD;
	rdev->stats.stag.total = sc->vres.stag.size;
	rdev->stats.pbl.total = sc->vres.pbl.size;
	rdev->stats.rqt.total = sc->vres.rq.size;
	rdev->stats.qid.total = sc->vres.qp.size;

	rc = c4iw_init_resource(rdev, c4iw_num_stags(rdev), T4_MAX_NUM_PD);
	if (rc) {
		device_printf(sc->dev, "error %d initializing resources\n", rc);
		goto err1;
	}
	rc = c4iw_pblpool_create(rdev);
	if (rc) {
		device_printf(sc->dev, "error %d initializing pbl pool\n", rc);
		goto err2;
	}
	rc = c4iw_rqtpool_create(rdev);
	if (rc) {
		device_printf(sc->dev, "error %d initializing rqt pool\n", rc);
		goto err3;
	}
	rdev->status_page = (struct t4_dev_status_page *)
				__get_free_page(GFP_KERNEL);
	if (!rdev->status_page) {
		rc = -ENOMEM;
		goto err4;
	}
	rdev->status_page->qp_start = sc->vres.qp.start;
	rdev->status_page->qp_size = sc->vres.qp.size;
	rdev->status_page->cq_start = sc->vres.cq.start;
	rdev->status_page->cq_size = sc->vres.cq.size;

	/* T5 and above devices don't need Doorbell recovery logic,
	 * so db_off is always set to '0'.
	 */
	rdev->status_page->db_off = 0;

	rdev->status_page->wc_supported = rdev->adap->iwt.wc_en;

	rdev->free_workq = create_singlethread_workqueue("iw_cxgb4_free");
	if (!rdev->free_workq) {
		rc = -ENOMEM;
		goto err5;
	}
	return (0);
err5:
	free_page((unsigned long)rdev->status_page);
err4:
	c4iw_rqtpool_destroy(rdev);
err3:
	c4iw_pblpool_destroy(rdev);
err2:
	c4iw_destroy_resource(&rdev->resource);
err1:
	return (rc);
}

static void c4iw_rdev_close(struct c4iw_rdev *rdev)
{
	free_page((unsigned long)rdev->status_page);
	c4iw_pblpool_destroy(rdev);
	c4iw_rqtpool_destroy(rdev);
	c4iw_destroy_resource(&rdev->resource);
}

static void
c4iw_dealloc(struct c4iw_dev *iwsc)
{

	c4iw_rdev_close(&iwsc->rdev);
	idr_destroy(&iwsc->cqidr);
	idr_destroy(&iwsc->qpidr);
	idr_destroy(&iwsc->mmidr);
	ib_dealloc_device(&iwsc->ibdev);
}

static struct c4iw_dev *
c4iw_alloc(struct adapter *sc)
{
	struct c4iw_dev *iwsc;
	int rc;

	iwsc = (struct c4iw_dev *)ib_alloc_device(sizeof(*iwsc));
	if (iwsc == NULL) {
		device_printf(sc->dev, "Cannot allocate ib device.\n");
		return (ERR_PTR(-ENOMEM));
	}
	iwsc->rdev.adap = sc;

	/* init various hw-queue params based on lld info */
	iwsc->rdev.hw_queue.t4_eq_status_entries =
		sc->params.sge.spg_len / EQ_ESIZE;
	iwsc->rdev.hw_queue.t4_max_eq_size = 65520;
	iwsc->rdev.hw_queue.t4_max_iq_size = 65520;
	iwsc->rdev.hw_queue.t4_max_rq_size = 8192 -
		iwsc->rdev.hw_queue.t4_eq_status_entries - 1;
	iwsc->rdev.hw_queue.t4_max_sq_size =
		iwsc->rdev.hw_queue.t4_max_eq_size -
		iwsc->rdev.hw_queue.t4_eq_status_entries - 1;
	iwsc->rdev.hw_queue.t4_max_qp_depth =
		iwsc->rdev.hw_queue.t4_max_rq_size;
	iwsc->rdev.hw_queue.t4_max_cq_depth =
		iwsc->rdev.hw_queue.t4_max_iq_size - 2;
	iwsc->rdev.hw_queue.t4_stat_len = iwsc->rdev.adap->params.sge.spg_len;

	/* As T5 and above devices support BAR2 kernel doorbells & WC, we map
	 * all of BAR2, for both User and Kernel Doorbells-GTS.
	 */
	iwsc->rdev.bar2_kva = (void __iomem *)((u64)iwsc->rdev.adap->udbs_base);
	iwsc->rdev.bar2_pa = vtophys(iwsc->rdev.adap->udbs_base);
	iwsc->rdev.bar2_len = rman_get_size(iwsc->rdev.adap->udbs_res);

	rc = c4iw_rdev_open(&iwsc->rdev);
	if (rc != 0) {
		device_printf(sc->dev, "Unable to open CXIO rdev (%d)\n", rc);
		ib_dealloc_device(&iwsc->ibdev);
		return (ERR_PTR(rc));
	}

	idr_init(&iwsc->cqidr);
	idr_init(&iwsc->qpidr);
	idr_init(&iwsc->mmidr);
	spin_lock_init(&iwsc->lock);
	mutex_init(&iwsc->rdev.stats.lock);
	iwsc->avail_ird = iwsc->rdev.adap->params.max_ird_adapter;

	return (iwsc);
}

static int c4iw_mod_load(void);
static int c4iw_mod_unload(void);
static int c4iw_activate(struct adapter *);
static int c4iw_deactivate(struct adapter *);

static struct uld_info c4iw_uld_info = {
	.uld_id = ULD_IWARP,
	.activate = c4iw_activate,
	.deactivate = c4iw_deactivate,
};

static int
c4iw_activate(struct adapter *sc)
{
	struct c4iw_dev *iwsc;
	int rc;

	ASSERT_SYNCHRONIZED_OP(sc);

	if (is_t4(sc)) {
		device_printf(sc->dev, "No iWARP support for T4 devices, "
				"please install T5 or above devices.\n");
		return (ENOSYS);
	}

	if (uld_active(sc, ULD_IWARP)) {
		KASSERT(0, ("%s: RDMA already eanbled on sc %p", __func__, sc));
		return (0);
	}

	if (sc->rdmacaps == 0) {
		device_printf(sc->dev,
		    "RDMA not supported or RDMA cap is not enabled.\n");
		return (ENOSYS);
	}

	iwsc = c4iw_alloc(sc);
	if (IS_ERR(iwsc)) {
		rc = -PTR_ERR(iwsc);
		device_printf(sc->dev, "initialization failed: %d\n", rc);
		return (rc);
	}

	sc->iwarp_softc = iwsc;

	rc = -c4iw_register_device(iwsc);
	if (rc) {
		device_printf(sc->dev, "RDMA registration failed: %d\n", rc);
		c4iw_dealloc(iwsc);
		sc->iwarp_softc = NULL;
	}

	return (rc);
}

static int
c4iw_deactivate(struct adapter *sc)
{
	struct c4iw_dev *iwsc = sc->iwarp_softc;

	ASSERT_SYNCHRONIZED_OP(sc);

	c4iw_unregister_device(iwsc);
	c4iw_dealloc(iwsc);
	sc->iwarp_softc = NULL;

	return (0);
}

static void
c4iw_activate_all(struct adapter *sc, void *arg __unused)
{

	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4iwact") != 0)
		return;

	/* Activate iWARP if any port on this adapter has IFCAP_TOE enabled. */
	if (sc->offload_map && !uld_active(sc, ULD_IWARP))
		(void) t4_activate_uld(sc, ULD_IWARP);

	end_synchronized_op(sc, 0);
}

static void
c4iw_deactivate_all(struct adapter *sc, void *arg __unused)
{

	if (begin_synchronized_op(sc, NULL, SLEEP_OK | INTR_OK, "t4iwdea") != 0)
		return;

	if (uld_active(sc, ULD_IWARP))
	    (void) t4_deactivate_uld(sc, ULD_IWARP);

	end_synchronized_op(sc, 0);
}

static int
c4iw_mod_load(void)
{
	int rc;

	rc = -c4iw_cm_init();
	if (rc != 0)
		return (rc);

	rc = t4_register_uld(&c4iw_uld_info);
	if (rc != 0) {
		c4iw_cm_term();
		return (rc);
	}

	t4_iterate(c4iw_activate_all, NULL);

	return (rc);
}

static int
c4iw_mod_unload(void)
{

	t4_iterate(c4iw_deactivate_all, NULL);

	c4iw_cm_term();

	if (t4_unregister_uld(&c4iw_uld_info) == EBUSY)
		return (EBUSY);

	return (0);
}

#endif

/*
 * t4_tom won't load on kernels without TCP_OFFLOAD and this module's dependency
 * on t4_tom ensures that it won't either.  So we don't directly check for
 * TCP_OFFLOAD here.
 */
static int
c4iw_modevent(module_t mod, int cmd, void *arg)
{
	int rc = 0;

#ifdef TCP_OFFLOAD
	switch (cmd) {
	case MOD_LOAD:
		rc = c4iw_mod_load();
		if (rc == 0)
			printf("iw_cxgbe: Chelsio T5/T6 RDMA driver loaded.\n");
		break;

	case MOD_UNLOAD:
		rc = c4iw_mod_unload();
		break;

	default:
		rc = EINVAL;
	}
#else
	printf("t4_tom: compiled without TCP_OFFLOAD support.\n");
	rc = EOPNOTSUPP;
#endif
	return (rc);
}

static moduledata_t c4iw_mod_data = {
	"iw_cxgbe",
	c4iw_modevent,
	0
};

MODULE_VERSION(iw_cxgbe, 1);
MODULE_DEPEND(iw_cxgbe, t4nex, 1, 1, 1);
MODULE_DEPEND(iw_cxgbe, t4_tom, 1, 1, 1);
MODULE_DEPEND(iw_cxgbe, ibcore, 1, 1, 1);
MODULE_DEPEND(iw_cxgbe, linuxkpi, 1, 1, 1);
DECLARE_MODULE(iw_cxgbe, c4iw_mod_data, SI_SUB_EXEC, SI_ORDER_ANY);
