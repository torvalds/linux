/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/stdarg.h>

#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/include/vmbus_xact.h>
#include <dev/hyperv/vmbus/hyperv_var.h>
#include <dev/hyperv/vmbus/vmbus_reg.h>
#include <dev/hyperv/vmbus/vmbus_var.h>
#include <dev/hyperv/vmbus/vmbus_brvar.h>
#include <dev/hyperv/vmbus/vmbus_chanvar.h>

struct vmbus_chan_pollarg {
	struct vmbus_channel	*poll_chan;
	u_int			poll_hz;
};

static void			vmbus_chan_update_evtflagcnt(
				    struct vmbus_softc *,
				    const struct vmbus_channel *);
static int			vmbus_chan_close_internal(
				    struct vmbus_channel *);
static int			vmbus_chan_sysctl_mnf(SYSCTL_HANDLER_ARGS);
static void			vmbus_chan_sysctl_create(
				    struct vmbus_channel *);
static struct vmbus_channel	*vmbus_chan_alloc(struct vmbus_softc *);
static void			vmbus_chan_free(struct vmbus_channel *);
static int			vmbus_chan_add(struct vmbus_channel *);
static void			vmbus_chan_cpu_default(struct vmbus_channel *);
static int			vmbus_chan_release(struct vmbus_channel *);
static void			vmbus_chan_set_chmap(struct vmbus_channel *);
static void			vmbus_chan_clear_chmap(struct vmbus_channel *);
static void			vmbus_chan_detach(struct vmbus_channel *);
static bool			vmbus_chan_wait_revoke(
				    const struct vmbus_channel *, bool);
static void			vmbus_chan_poll_timeout(void *);
static bool			vmbus_chan_poll_cancel_intq(
				    struct vmbus_channel *);
static void			vmbus_chan_poll_cancel(struct vmbus_channel *);

static void			vmbus_chan_ins_prilist(struct vmbus_softc *,
				    struct vmbus_channel *);
static void			vmbus_chan_rem_prilist(struct vmbus_softc *,
				    struct vmbus_channel *);
static void			vmbus_chan_ins_list(struct vmbus_softc *,
				    struct vmbus_channel *);
static void			vmbus_chan_rem_list(struct vmbus_softc *,
				    struct vmbus_channel *);
static void			vmbus_chan_ins_sublist(struct vmbus_channel *,
				    struct vmbus_channel *);
static void			vmbus_chan_rem_sublist(struct vmbus_channel *,
				    struct vmbus_channel *);

static void			vmbus_chan_task(void *, int);
static void			vmbus_chan_task_nobatch(void *, int);
static void			vmbus_chan_poll_task(void *, int);
static void			vmbus_chan_clrchmap_task(void *, int);
static void			vmbus_chan_pollcfg_task(void *, int);
static void			vmbus_chan_polldis_task(void *, int);
static void			vmbus_chan_poll_cancel_task(void *, int);
static void			vmbus_prichan_attach_task(void *, int);
static void			vmbus_subchan_attach_task(void *, int);
static void			vmbus_prichan_detach_task(void *, int);
static void			vmbus_subchan_detach_task(void *, int);

static void			vmbus_chan_msgproc_choffer(struct vmbus_softc *,
				    const struct vmbus_message *);
static void			vmbus_chan_msgproc_chrescind(
				    struct vmbus_softc *,
				    const struct vmbus_message *);

static int			vmbus_chan_printf(const struct vmbus_channel *,
				    const char *, ...) __printflike(2, 3);

/*
 * Vmbus channel message processing.
 */
static const vmbus_chanmsg_proc_t
vmbus_chan_msgprocs[VMBUS_CHANMSG_TYPE_MAX] = {
	VMBUS_CHANMSG_PROC(CHOFFER,	vmbus_chan_msgproc_choffer),
	VMBUS_CHANMSG_PROC(CHRESCIND,	vmbus_chan_msgproc_chrescind),

	VMBUS_CHANMSG_PROC_WAKEUP(CHOPEN_RESP),
	VMBUS_CHANMSG_PROC_WAKEUP(GPADL_CONNRESP),
	VMBUS_CHANMSG_PROC_WAKEUP(GPADL_DISCONNRESP)
};

/*
 * Notify host that there are data pending on our TX bufring.
 */
static __inline void
vmbus_chan_signal_tx(const struct vmbus_channel *chan)
{
	atomic_set_long(chan->ch_evtflag, chan->ch_evtflag_mask);
	if (chan->ch_txflags & VMBUS_CHAN_TXF_HASMNF)
		atomic_set_int(chan->ch_montrig, chan->ch_montrig_mask);
	else
		hypercall_signal_event(chan->ch_monprm_dma.hv_paddr);
}

static void
vmbus_chan_ins_prilist(struct vmbus_softc *sc, struct vmbus_channel *chan)
{

	mtx_assert(&sc->vmbus_prichan_lock, MA_OWNED);
	if (atomic_testandset_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONPRIL_SHIFT))
		panic("channel is already on the prilist");
	TAILQ_INSERT_TAIL(&sc->vmbus_prichans, chan, ch_prilink);
}

static void
vmbus_chan_rem_prilist(struct vmbus_softc *sc, struct vmbus_channel *chan)
{

	mtx_assert(&sc->vmbus_prichan_lock, MA_OWNED);
	if (atomic_testandclear_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONPRIL_SHIFT) == 0)
		panic("channel is not on the prilist");
	TAILQ_REMOVE(&sc->vmbus_prichans, chan, ch_prilink);
}

static void
vmbus_chan_ins_sublist(struct vmbus_channel *prichan,
    struct vmbus_channel *chan)
{

	mtx_assert(&prichan->ch_subchan_lock, MA_OWNED);

	if (atomic_testandset_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONSUBL_SHIFT))
		panic("channel is already on the sublist");
	TAILQ_INSERT_TAIL(&prichan->ch_subchans, chan, ch_sublink);

	/* Bump sub-channel count. */
	prichan->ch_subchan_cnt++;
}

static void
vmbus_chan_rem_sublist(struct vmbus_channel *prichan,
    struct vmbus_channel *chan)
{

	mtx_assert(&prichan->ch_subchan_lock, MA_OWNED);

	KASSERT(prichan->ch_subchan_cnt > 0,
	    ("invalid subchan_cnt %d", prichan->ch_subchan_cnt));
	prichan->ch_subchan_cnt--;

	if (atomic_testandclear_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONSUBL_SHIFT) == 0)
		panic("channel is not on the sublist");
	TAILQ_REMOVE(&prichan->ch_subchans, chan, ch_sublink);
}

static void
vmbus_chan_ins_list(struct vmbus_softc *sc, struct vmbus_channel *chan)
{

	mtx_assert(&sc->vmbus_chan_lock, MA_OWNED);
	if (atomic_testandset_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONLIST_SHIFT))
		panic("channel is already on the list");
	TAILQ_INSERT_TAIL(&sc->vmbus_chans, chan, ch_link);
}

static void
vmbus_chan_rem_list(struct vmbus_softc *sc, struct vmbus_channel *chan)
{

	mtx_assert(&sc->vmbus_chan_lock, MA_OWNED);
	if (atomic_testandclear_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_ONLIST_SHIFT) == 0)
		panic("channel is not on the list");
	TAILQ_REMOVE(&sc->vmbus_chans, chan, ch_link);
}

static int
vmbus_chan_sysctl_mnf(SYSCTL_HANDLER_ARGS)
{
	struct vmbus_channel *chan = arg1;
	int mnf = 0;

	if (chan->ch_txflags & VMBUS_CHAN_TXF_HASMNF)
		mnf = 1;
	return sysctl_handle_int(oidp, &mnf, 0, req);
}

static void
vmbus_chan_sysctl_create(struct vmbus_channel *chan)
{
	struct sysctl_oid *ch_tree, *chid_tree, *br_tree;
	struct sysctl_ctx_list *ctx;
	uint32_t ch_id;
	char name[16];

	/*
	 * Add sysctl nodes related to this channel to this
	 * channel's sysctl ctx, so that they can be destroyed
	 * independently upon close of this channel, which can
	 * happen even if the device is not detached.
	 */
	ctx = &chan->ch_sysctl_ctx;
	sysctl_ctx_init(ctx);

	/*
	 * Create dev.NAME.UNIT.channel tree.
	 */
	ch_tree = SYSCTL_ADD_NODE(ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(chan->ch_dev)),
	    OID_AUTO, "channel", CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
	if (ch_tree == NULL)
		return;

	/*
	 * Create dev.NAME.UNIT.channel.CHANID tree.
	 */
	if (VMBUS_CHAN_ISPRIMARY(chan))
		ch_id = chan->ch_id;
	else
		ch_id = chan->ch_prichan->ch_id;
	snprintf(name, sizeof(name), "%d", ch_id);
	chid_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(ch_tree),
	    OID_AUTO, name, CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
	if (chid_tree == NULL)
		return;

	if (!VMBUS_CHAN_ISPRIMARY(chan)) {
		/*
		 * Create dev.NAME.UNIT.channel.CHANID.sub tree.
		 */
		ch_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(chid_tree),
		    OID_AUTO, "sub", CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
		if (ch_tree == NULL)
			return;

		/*
		 * Create dev.NAME.UNIT.channel.CHANID.sub.SUBIDX tree.
		 *
		 * NOTE:
		 * chid_tree is changed to this new sysctl tree.
		 */
		snprintf(name, sizeof(name), "%d", chan->ch_subidx);
		chid_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(ch_tree),
		    OID_AUTO, name, CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
		if (chid_tree == NULL)
			return;

		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(chid_tree), OID_AUTO,
		    "chanid", CTLFLAG_RD, &chan->ch_id, 0, "channel id");
	}

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(chid_tree), OID_AUTO,
	    "cpu", CTLFLAG_RD, &chan->ch_cpuid, 0, "owner CPU id");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(chid_tree), OID_AUTO,
	    "mnf", CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    chan, 0, vmbus_chan_sysctl_mnf, "I",
	    "has monitor notification facilities");

	br_tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(chid_tree), OID_AUTO,
	    "br", CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
	if (br_tree != NULL) {
		/*
		 * Create sysctl tree for RX bufring.
		 */
		vmbus_br_sysctl_create(ctx, br_tree, &chan->ch_rxbr.rxbr, "rx");
		/*
		 * Create sysctl tree for TX bufring.
		 */
		vmbus_br_sysctl_create(ctx, br_tree, &chan->ch_txbr.txbr, "tx");
	}
}

int
vmbus_chan_open(struct vmbus_channel *chan, int txbr_size, int rxbr_size,
    const void *udata, int udlen, vmbus_chan_callback_t cb, void *cbarg)
{
	struct vmbus_chan_br cbr;
	int error;

	/*
	 * Allocate the TX+RX bufrings.
	 */
	KASSERT(chan->ch_bufring == NULL, ("bufrings are allocated"));
	chan->ch_bufring = hyperv_dmamem_alloc(bus_get_dma_tag(chan->ch_dev),
	    PAGE_SIZE, 0, txbr_size + rxbr_size, &chan->ch_bufring_dma,
	    BUS_DMA_WAITOK);
	if (chan->ch_bufring == NULL) {
		vmbus_chan_printf(chan, "bufring allocation failed\n");
		return (ENOMEM);
	}

	cbr.cbr = chan->ch_bufring;
	cbr.cbr_paddr = chan->ch_bufring_dma.hv_paddr;
	cbr.cbr_txsz = txbr_size;
	cbr.cbr_rxsz = rxbr_size;

	error = vmbus_chan_open_br(chan, &cbr, udata, udlen, cb, cbarg);
	if (error) {
		if (error == EISCONN) {
			/*
			 * XXX
			 * The bufring GPADL is still connected; abandon
			 * this bufring, instead of having mysterious
			 * crash or trashed data later on.
			 */
			vmbus_chan_printf(chan, "chan%u bufring GPADL "
			    "is still connected upon channel open error; "
			    "leak %d bytes memory\n", chan->ch_id,
			    txbr_size + rxbr_size);
		} else {
			hyperv_dmamem_free(&chan->ch_bufring_dma,
			    chan->ch_bufring);
		}
		chan->ch_bufring = NULL;
	}
	return (error);
}

int
vmbus_chan_open_br(struct vmbus_channel *chan, const struct vmbus_chan_br *cbr,
    const void *udata, int udlen, vmbus_chan_callback_t cb, void *cbarg)
{
	struct vmbus_softc *sc = chan->ch_vmbus;
	const struct vmbus_message *msg;
	struct vmbus_chanmsg_chopen *req;
	struct vmbus_msghc *mh;
	uint32_t status;
	int error, txbr_size, rxbr_size;
	task_fn_t *task_fn;
	uint8_t *br;

	if (udlen > VMBUS_CHANMSG_CHOPEN_UDATA_SIZE) {
		vmbus_chan_printf(chan,
		    "invalid udata len %d for chan%u\n", udlen, chan->ch_id);
		return (EINVAL);
	}

	br = cbr->cbr;
	txbr_size = cbr->cbr_txsz;
	rxbr_size = cbr->cbr_rxsz;
	KASSERT((txbr_size & PAGE_MASK) == 0,
	    ("send bufring size is not multiple page"));
	KASSERT((rxbr_size & PAGE_MASK) == 0,
	    ("recv bufring size is not multiple page"));
	KASSERT((cbr->cbr_paddr & PAGE_MASK) == 0,
	    ("bufring is not page aligned"));

	/*
	 * Zero out the TX/RX bufrings, in case that they were used before.
	 */
	memset(br, 0, txbr_size + rxbr_size);

	if (atomic_testandset_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_OPENED_SHIFT))
		panic("double-open chan%u", chan->ch_id);

	chan->ch_cb = cb;
	chan->ch_cbarg = cbarg;

	vmbus_chan_update_evtflagcnt(sc, chan);

	chan->ch_tq = VMBUS_PCPU_GET(chan->ch_vmbus, event_tq, chan->ch_cpuid);
	if (chan->ch_flags & VMBUS_CHAN_FLAG_BATCHREAD)
		task_fn = vmbus_chan_task;
	else
		task_fn = vmbus_chan_task_nobatch;
	TASK_INIT(&chan->ch_task, 0, task_fn, chan);

	/* TX bufring comes first */
	vmbus_txbr_setup(&chan->ch_txbr, br, txbr_size);
	/* RX bufring immediately follows TX bufring */
	vmbus_rxbr_setup(&chan->ch_rxbr, br + txbr_size, rxbr_size);

	/* Create sysctl tree for this channel */
	vmbus_chan_sysctl_create(chan);

	/*
	 * Connect the bufrings, both RX and TX, to this channel.
	 */
	error = vmbus_chan_gpadl_connect(chan, cbr->cbr_paddr,
	    txbr_size + rxbr_size, &chan->ch_bufring_gpadl);
	if (error) {
		vmbus_chan_printf(chan,
		    "failed to connect bufring GPADL to chan%u\n", chan->ch_id);
		goto failed;
	}

	/*
	 * Install this channel, before it is opened, but after everything
	 * else has been setup.
	 */
	vmbus_chan_set_chmap(chan);

	/*
	 * Open channel w/ the bufring GPADL on the target CPU.
	 */
	mh = vmbus_msghc_get(sc, sizeof(*req));
	if (mh == NULL) {
		vmbus_chan_printf(chan,
		    "can not get msg hypercall for chopen(chan%u)\n",
		    chan->ch_id);
		error = ENXIO;
		goto failed;
	}

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_CHOPEN;
	req->chm_chanid = chan->ch_id;
	req->chm_openid = chan->ch_id;
	req->chm_gpadl = chan->ch_bufring_gpadl;
	req->chm_vcpuid = chan->ch_vcpuid;
	req->chm_txbr_pgcnt = txbr_size >> PAGE_SHIFT;
	if (udlen > 0)
		memcpy(req->chm_udata, udata, udlen);

	error = vmbus_msghc_exec(sc, mh);
	if (error) {
		vmbus_chan_printf(chan,
		    "chopen(chan%u) msg hypercall exec failed: %d\n",
		    chan->ch_id, error);
		vmbus_msghc_put(sc, mh);
		goto failed;
	}

	for (;;) {
		msg = vmbus_msghc_poll_result(sc, mh);
		if (msg != NULL)
			break;
		if (vmbus_chan_is_revoked(chan)) {
			int i;

			/*
			 * NOTE:
			 * Hypervisor does _not_ send response CHOPEN to
			 * a revoked channel.
			 */
			vmbus_chan_printf(chan,
			    "chan%u is revoked, when it is being opened\n",
			    chan->ch_id);

			/*
			 * XXX
			 * Add extra delay before cancel the hypercall
			 * execution; mainly to close any possible
			 * CHRESCIND and CHOPEN_RESP races on the
			 * hypervisor side.
			 */
#define REVOKE_LINGER	100
			for (i = 0; i < REVOKE_LINGER; ++i) {
				msg = vmbus_msghc_poll_result(sc, mh);
				if (msg != NULL)
					break;
				pause("rchopen", 1);
			}
#undef REVOKE_LINGER
			if (msg == NULL)
				vmbus_msghc_exec_cancel(sc, mh);
			break;
		}
		pause("chopen", 1);
	}
	if (msg != NULL) {
		status = ((const struct vmbus_chanmsg_chopen_resp *)
		    msg->msg_data)->chm_status;
	} else {
		/* XXX any non-0 value is ok here. */
		status = 0xff;
	}

	vmbus_msghc_put(sc, mh);

	if (status == 0) {
		if (bootverbose)
			vmbus_chan_printf(chan, "chan%u opened\n", chan->ch_id);
		return (0);
	}

	vmbus_chan_printf(chan, "failed to open chan%u\n", chan->ch_id);
	error = ENXIO;

failed:
	sysctl_ctx_free(&chan->ch_sysctl_ctx);
	vmbus_chan_clear_chmap(chan);
	if (chan->ch_bufring_gpadl != 0) {
		int error1;

		error1 = vmbus_chan_gpadl_disconnect(chan,
		    chan->ch_bufring_gpadl);
		if (error1) {
			/*
			 * Give caller a hint that the bufring GPADL is still
			 * connected.
			 */
			error = EISCONN;
		}
		chan->ch_bufring_gpadl = 0;
	}
	atomic_clear_int(&chan->ch_stflags, VMBUS_CHAN_ST_OPENED);
	return (error);
}

int
vmbus_chan_gpadl_connect(struct vmbus_channel *chan, bus_addr_t paddr,
    int size, uint32_t *gpadl0)
{
	struct vmbus_softc *sc = chan->ch_vmbus;
	struct vmbus_msghc *mh;
	struct vmbus_chanmsg_gpadl_conn *req;
	const struct vmbus_message *msg;
	size_t reqsz;
	uint32_t gpadl, status;
	int page_count, range_len, i, cnt, error;
	uint64_t page_id;

	KASSERT(*gpadl0 == 0, ("GPADL is not zero"));

	/*
	 * Preliminary checks.
	 */

	KASSERT((size & PAGE_MASK) == 0,
	    ("invalid GPA size %d, not multiple page size", size));
	page_count = size >> PAGE_SHIFT;

	KASSERT((paddr & PAGE_MASK) == 0,
	    ("GPA is not page aligned %jx", (uintmax_t)paddr));
	page_id = paddr >> PAGE_SHIFT;

	range_len = __offsetof(struct vmbus_gpa_range, gpa_page[page_count]);
	/*
	 * We don't support multiple GPA ranges.
	 */
	if (range_len > UINT16_MAX) {
		vmbus_chan_printf(chan, "GPA too large, %d pages\n",
		    page_count);
		return EOPNOTSUPP;
	}

	/*
	 * Allocate GPADL id.
	 */
	gpadl = vmbus_gpadl_alloc(sc);

	/*
	 * Connect this GPADL to the target channel.
	 *
	 * NOTE:
	 * Since each message can only hold small set of page
	 * addresses, several messages may be required to
	 * complete the connection.
	 */
	if (page_count > VMBUS_CHANMSG_GPADL_CONN_PGMAX)
		cnt = VMBUS_CHANMSG_GPADL_CONN_PGMAX;
	else
		cnt = page_count;
	page_count -= cnt;

	reqsz = __offsetof(struct vmbus_chanmsg_gpadl_conn,
	    chm_range.gpa_page[cnt]);
	mh = vmbus_msghc_get(sc, reqsz);
	if (mh == NULL) {
		vmbus_chan_printf(chan,
		    "can not get msg hypercall for gpadl_conn(chan%u)\n",
		    chan->ch_id);
		return EIO;
	}

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_GPADL_CONN;
	req->chm_chanid = chan->ch_id;
	req->chm_gpadl = gpadl;
	req->chm_range_len = range_len;
	req->chm_range_cnt = 1;
	req->chm_range.gpa_len = size;
	req->chm_range.gpa_ofs = 0;
	for (i = 0; i < cnt; ++i)
		req->chm_range.gpa_page[i] = page_id++;

	error = vmbus_msghc_exec(sc, mh);
	if (error) {
		vmbus_chan_printf(chan,
		    "gpadl_conn(chan%u) msg hypercall exec failed: %d\n",
		    chan->ch_id, error);
		vmbus_msghc_put(sc, mh);
		return error;
	}

	while (page_count > 0) {
		struct vmbus_chanmsg_gpadl_subconn *subreq;

		if (page_count > VMBUS_CHANMSG_GPADL_SUBCONN_PGMAX)
			cnt = VMBUS_CHANMSG_GPADL_SUBCONN_PGMAX;
		else
			cnt = page_count;
		page_count -= cnt;

		reqsz = __offsetof(struct vmbus_chanmsg_gpadl_subconn,
		    chm_gpa_page[cnt]);
		vmbus_msghc_reset(mh, reqsz);

		subreq = vmbus_msghc_dataptr(mh);
		subreq->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_GPADL_SUBCONN;
		subreq->chm_gpadl = gpadl;
		for (i = 0; i < cnt; ++i)
			subreq->chm_gpa_page[i] = page_id++;

		vmbus_msghc_exec_noresult(mh);
	}
	KASSERT(page_count == 0, ("invalid page count %d", page_count));

	msg = vmbus_msghc_wait_result(sc, mh);
	status = ((const struct vmbus_chanmsg_gpadl_connresp *)
	    msg->msg_data)->chm_status;

	vmbus_msghc_put(sc, mh);

	if (status != 0) {
		vmbus_chan_printf(chan, "gpadl_conn(chan%u) failed: %u\n",
		    chan->ch_id, status);
		return EIO;
	}

	/* Done; commit the GPADL id. */
	*gpadl0 = gpadl;
	if (bootverbose) {
		vmbus_chan_printf(chan, "gpadl_conn(chan%u) succeeded\n",
		    chan->ch_id);
	}
	return 0;
}

static bool
vmbus_chan_wait_revoke(const struct vmbus_channel *chan, bool can_sleep)
{
#define WAIT_COUNT	200	/* 200ms */

	int i;

	for (i = 0; i < WAIT_COUNT; ++i) {
		if (vmbus_chan_is_revoked(chan))
			return (true);
		if (can_sleep)
			pause("wchrev", 1);
		else
			DELAY(1000);
	}
	return (false);

#undef WAIT_COUNT
}

/*
 * Disconnect the GPA from the target channel
 */
int
vmbus_chan_gpadl_disconnect(struct vmbus_channel *chan, uint32_t gpadl)
{
	struct vmbus_softc *sc = chan->ch_vmbus;
	struct vmbus_msghc *mh;
	struct vmbus_chanmsg_gpadl_disconn *req;
	int error;

	KASSERT(gpadl != 0, ("GPADL is zero"));

	mh = vmbus_msghc_get(sc, sizeof(*req));
	if (mh == NULL) {
		vmbus_chan_printf(chan,
		    "can not get msg hypercall for gpadl_disconn(chan%u)\n",
		    chan->ch_id);
		return (EBUSY);
	}

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_GPADL_DISCONN;
	req->chm_chanid = chan->ch_id;
	req->chm_gpadl = gpadl;

	error = vmbus_msghc_exec(sc, mh);
	if (error) {
		vmbus_msghc_put(sc, mh);

		if (vmbus_chan_wait_revoke(chan, true)) {
			/*
			 * Error is benign; this channel is revoked,
			 * so this GPADL will not be touched anymore.
			 */
			vmbus_chan_printf(chan,
			    "gpadl_disconn(revoked chan%u) msg hypercall "
			    "exec failed: %d\n", chan->ch_id, error);
			return (0);
		}
		vmbus_chan_printf(chan,
		    "gpadl_disconn(chan%u) msg hypercall exec failed: %d\n",
		    chan->ch_id, error);
		return (error);
	}

	vmbus_msghc_wait_result(sc, mh);
	/* Discard result; no useful information */
	vmbus_msghc_put(sc, mh);

	return (0);
}

static void
vmbus_chan_detach(struct vmbus_channel *chan)
{
	int refs;

	KASSERT(chan->ch_refs > 0, ("chan%u: invalid refcnt %d",
	    chan->ch_id, chan->ch_refs));
	refs = atomic_fetchadd_int(&chan->ch_refs, -1);
#ifdef INVARIANTS
	if (VMBUS_CHAN_ISPRIMARY(chan)) {
		KASSERT(refs == 1, ("chan%u: invalid refcnt %d for prichan",
		    chan->ch_id, refs + 1));
	}
#endif
	if (refs == 1) {
		/*
		 * Detach the target channel.
		 */
		if (bootverbose) {
			vmbus_chan_printf(chan, "chan%u detached\n",
			    chan->ch_id);
		}
		taskqueue_enqueue(chan->ch_mgmt_tq, &chan->ch_detach_task);
	}
}

static void
vmbus_chan_clrchmap_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;

	chan->ch_vmbus->vmbus_chmap[chan->ch_id] = NULL;
}

static void
vmbus_chan_clear_chmap(struct vmbus_channel *chan)
{
	struct task chmap_task;

	TASK_INIT(&chmap_task, 0, vmbus_chan_clrchmap_task, chan);
	vmbus_chan_run_task(chan, &chmap_task);
}

static void
vmbus_chan_set_chmap(struct vmbus_channel *chan)
{
	__compiler_membar();
	chan->ch_vmbus->vmbus_chmap[chan->ch_id] = chan;
}

static void
vmbus_chan_poll_cancel_task(void *xchan, int pending __unused)
{

	vmbus_chan_poll_cancel_intq(xchan);
}

static void
vmbus_chan_poll_cancel(struct vmbus_channel *chan)
{
	struct task poll_cancel;

	TASK_INIT(&poll_cancel, 0, vmbus_chan_poll_cancel_task, chan);
	vmbus_chan_run_task(chan, &poll_cancel);
}

static int
vmbus_chan_close_internal(struct vmbus_channel *chan)
{
	struct vmbus_softc *sc = chan->ch_vmbus;
	struct vmbus_msghc *mh;
	struct vmbus_chanmsg_chclose *req;
	uint32_t old_stflags;
	int error;

	/*
	 * NOTE:
	 * Sub-channels are closed upon their primary channel closing,
	 * so they can be closed even before they are opened.
	 */
	for (;;) {
		old_stflags = chan->ch_stflags;
		if (atomic_cmpset_int(&chan->ch_stflags, old_stflags,
		    old_stflags & ~VMBUS_CHAN_ST_OPENED))
			break;
	}
	if ((old_stflags & VMBUS_CHAN_ST_OPENED) == 0) {
		/* Not opened yet; done */
		if (bootverbose) {
			vmbus_chan_printf(chan, "chan%u not opened\n",
			    chan->ch_id);
		}
		return (0);
	}

	/*
	 * Free this channel's sysctl tree attached to its device's
	 * sysctl tree.
	 */
	sysctl_ctx_free(&chan->ch_sysctl_ctx);

	/*
	 * Cancel polling, if it is enabled.
	 */
	vmbus_chan_poll_cancel(chan);

	/*
	 * NOTE:
	 * Order is critical.  This channel _must_ be uninstalled first,
	 * else the channel task may be enqueued by the IDT after it has
	 * been drained.
	 */
	vmbus_chan_clear_chmap(chan);
	taskqueue_drain(chan->ch_tq, &chan->ch_task);
	chan->ch_tq = NULL;

	/*
	 * Close this channel.
	 */
	mh = vmbus_msghc_get(sc, sizeof(*req));
	if (mh == NULL) {
		vmbus_chan_printf(chan,
		    "can not get msg hypercall for chclose(chan%u)\n",
		    chan->ch_id);
		error = ENXIO;
		goto disconnect;
	}

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_CHCLOSE;
	req->chm_chanid = chan->ch_id;

	error = vmbus_msghc_exec_noresult(mh);
	vmbus_msghc_put(sc, mh);

	if (error) {
		vmbus_chan_printf(chan,
		    "chclose(chan%u) msg hypercall exec failed: %d\n",
		    chan->ch_id, error);
		goto disconnect;
	}

	if (bootverbose)
		vmbus_chan_printf(chan, "chan%u closed\n", chan->ch_id);

disconnect:
	/*
	 * Disconnect the TX+RX bufrings from this channel.
	 */
	if (chan->ch_bufring_gpadl != 0) {
		int error1;

		error1 = vmbus_chan_gpadl_disconnect(chan,
		    chan->ch_bufring_gpadl);
		if (error1) {
			/*
			 * XXX
			 * The bufring GPADL is still connected; abandon
			 * this bufring, instead of having mysterious
			 * crash or trashed data later on.
			 */
			vmbus_chan_printf(chan, "chan%u bufring GPADL "
			    "is still connected after close\n", chan->ch_id);
			chan->ch_bufring = NULL;
			/*
			 * Give caller a hint that the bufring GPADL is
			 * still connected.
			 */
			error = EISCONN;
		}
		chan->ch_bufring_gpadl = 0;
	}

	/*
	 * Destroy the TX+RX bufrings.
	 */
	if (chan->ch_bufring != NULL) {
		hyperv_dmamem_free(&chan->ch_bufring_dma, chan->ch_bufring);
		chan->ch_bufring = NULL;
	}
	return (error);
}

int
vmbus_chan_close_direct(struct vmbus_channel *chan)
{
	int error;

#ifdef INVARIANTS
	if (VMBUS_CHAN_ISPRIMARY(chan)) {
		struct vmbus_channel *subchan;

		/*
		 * All sub-channels _must_ have been closed, or are _not_
		 * opened at all.
		 */
		mtx_lock(&chan->ch_subchan_lock);
		TAILQ_FOREACH(subchan, &chan->ch_subchans, ch_sublink) {
			KASSERT(
			   (subchan->ch_stflags & VMBUS_CHAN_ST_OPENED) == 0,
			   ("chan%u: subchan%u is still opened",
			    chan->ch_id, subchan->ch_subidx));
		}
		mtx_unlock(&chan->ch_subchan_lock);
	}
#endif

	error = vmbus_chan_close_internal(chan);
	if (!VMBUS_CHAN_ISPRIMARY(chan)) {
		/*
		 * This sub-channel is referenced, when it is linked to
		 * the primary channel; drop that reference now.
		 */
		vmbus_chan_detach(chan);
	}
	return (error);
}

/*
 * Caller should make sure that all sub-channels have
 * been added to 'chan' and all to-be-closed channels
 * are not being opened.
 */
void
vmbus_chan_close(struct vmbus_channel *chan)
{
	int subchan_cnt;

	if (!VMBUS_CHAN_ISPRIMARY(chan)) {
		/*
		 * Sub-channel is closed when its primary channel
		 * is closed; done.
		 */
		return;
	}

	/*
	 * Close all sub-channels, if any.
	 */
	subchan_cnt = chan->ch_subchan_cnt;
	if (subchan_cnt > 0) {
		struct vmbus_channel **subchan;
		int i;

		subchan = vmbus_subchan_get(chan, subchan_cnt);
		for (i = 0; i < subchan_cnt; ++i) {
			vmbus_chan_close_internal(subchan[i]);
			/*
			 * This sub-channel is referenced, when it is
			 * linked to the primary channel; drop that
			 * reference now.
			 */
			vmbus_chan_detach(subchan[i]);
		}
		vmbus_subchan_rel(subchan, subchan_cnt);
	}

	/* Then close the primary channel. */
	vmbus_chan_close_internal(chan);
}

void
vmbus_chan_intr_drain(struct vmbus_channel *chan)
{

	taskqueue_drain(chan->ch_tq, &chan->ch_task);
}

int
vmbus_chan_send(struct vmbus_channel *chan, uint16_t type, uint16_t flags,
    void *data, int dlen, uint64_t xactid)
{
	struct vmbus_chanpkt pkt;
	int pktlen, pad_pktlen, hlen, error;
	uint64_t pad = 0;
	struct iovec iov[3];
	boolean_t send_evt;

	hlen = sizeof(pkt);
	pktlen = hlen + dlen;
	pad_pktlen = VMBUS_CHANPKT_TOTLEN(pktlen);
	KASSERT(pad_pktlen <= vmbus_txbr_maxpktsz(&chan->ch_txbr),
	    ("invalid packet size %d", pad_pktlen));

	pkt.cp_hdr.cph_type = type;
	pkt.cp_hdr.cph_flags = flags;
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_hlen, hlen);
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_tlen, pad_pktlen);
	pkt.cp_hdr.cph_xactid = xactid;

	iov[0].iov_base = &pkt;
	iov[0].iov_len = hlen;
	iov[1].iov_base = data;
	iov[1].iov_len = dlen;
	iov[2].iov_base = &pad;
	iov[2].iov_len = pad_pktlen - pktlen;

	error = vmbus_txbr_write(&chan->ch_txbr, iov, 3, &send_evt);
	if (!error && send_evt)
		vmbus_chan_signal_tx(chan);
	return error;
}

int
vmbus_chan_send_sglist(struct vmbus_channel *chan,
    struct vmbus_gpa sg[], int sglen, void *data, int dlen, uint64_t xactid)
{
	struct vmbus_chanpkt_sglist pkt;
	int pktlen, pad_pktlen, hlen, error;
	struct iovec iov[4];
	boolean_t send_evt;
	uint64_t pad = 0;

	hlen = __offsetof(struct vmbus_chanpkt_sglist, cp_gpa[sglen]);
	pktlen = hlen + dlen;
	pad_pktlen = VMBUS_CHANPKT_TOTLEN(pktlen);
	KASSERT(pad_pktlen <= vmbus_txbr_maxpktsz(&chan->ch_txbr),
	    ("invalid packet size %d", pad_pktlen));

	pkt.cp_hdr.cph_type = VMBUS_CHANPKT_TYPE_GPA;
	pkt.cp_hdr.cph_flags = VMBUS_CHANPKT_FLAG_RC;
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_hlen, hlen);
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_tlen, pad_pktlen);
	pkt.cp_hdr.cph_xactid = xactid;
	pkt.cp_rsvd = 0;
	pkt.cp_gpa_cnt = sglen;

	iov[0].iov_base = &pkt;
	iov[0].iov_len = sizeof(pkt);
	iov[1].iov_base = sg;
	iov[1].iov_len = sizeof(struct vmbus_gpa) * sglen;
	iov[2].iov_base = data;
	iov[2].iov_len = dlen;
	iov[3].iov_base = &pad;
	iov[3].iov_len = pad_pktlen - pktlen;

	error = vmbus_txbr_write(&chan->ch_txbr, iov, 4, &send_evt);
	if (!error && send_evt)
		vmbus_chan_signal_tx(chan);
	return error;
}

int
vmbus_chan_send_prplist(struct vmbus_channel *chan,
    struct vmbus_gpa_range *prp, int prp_cnt, void *data, int dlen,
    uint64_t xactid)
{
	struct vmbus_chanpkt_prplist pkt;
	int pktlen, pad_pktlen, hlen, error;
	struct iovec iov[4];
	boolean_t send_evt;
	uint64_t pad = 0;

	hlen = __offsetof(struct vmbus_chanpkt_prplist,
	    cp_range[0].gpa_page[prp_cnt]);
	pktlen = hlen + dlen;
	pad_pktlen = VMBUS_CHANPKT_TOTLEN(pktlen);
	KASSERT(pad_pktlen <= vmbus_txbr_maxpktsz(&chan->ch_txbr),
	    ("invalid packet size %d", pad_pktlen));

	pkt.cp_hdr.cph_type = VMBUS_CHANPKT_TYPE_GPA;
	pkt.cp_hdr.cph_flags = VMBUS_CHANPKT_FLAG_RC;
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_hlen, hlen);
	VMBUS_CHANPKT_SETLEN(pkt.cp_hdr.cph_tlen, pad_pktlen);
	pkt.cp_hdr.cph_xactid = xactid;
	pkt.cp_rsvd = 0;
	pkt.cp_range_cnt = 1;

	iov[0].iov_base = &pkt;
	iov[0].iov_len = sizeof(pkt);
	iov[1].iov_base = prp;
	iov[1].iov_len = __offsetof(struct vmbus_gpa_range, gpa_page[prp_cnt]);
	iov[2].iov_base = data;
	iov[2].iov_len = dlen;
	iov[3].iov_base = &pad;
	iov[3].iov_len = pad_pktlen - pktlen;

	error = vmbus_txbr_write(&chan->ch_txbr, iov, 4, &send_evt);
	if (!error && send_evt)
		vmbus_chan_signal_tx(chan);
	return error;
}

int
vmbus_chan_recv(struct vmbus_channel *chan, void *data, int *dlen0,
    uint64_t *xactid)
{
	struct vmbus_chanpkt_hdr pkt;
	int error, dlen, hlen;

	error = vmbus_rxbr_peek(&chan->ch_rxbr, &pkt, sizeof(pkt));
	if (error)
		return (error);

	if (__predict_false(pkt.cph_hlen < VMBUS_CHANPKT_HLEN_MIN)) {
		vmbus_chan_printf(chan, "invalid hlen %u\n", pkt.cph_hlen);
		/* XXX this channel is dead actually. */
		return (EIO);
	}
	if (__predict_false(pkt.cph_hlen > pkt.cph_tlen)) {
		vmbus_chan_printf(chan, "invalid hlen %u and tlen %u\n",
		    pkt.cph_hlen, pkt.cph_tlen);
		/* XXX this channel is dead actually. */
		return (EIO);
	}

	hlen = VMBUS_CHANPKT_GETLEN(pkt.cph_hlen);
	dlen = VMBUS_CHANPKT_GETLEN(pkt.cph_tlen) - hlen;

	if (*dlen0 < dlen) {
		/* Return the size of this packet's data. */
		*dlen0 = dlen;
		return (ENOBUFS);
	}

	*xactid = pkt.cph_xactid;
	*dlen0 = dlen;

	/* Skip packet header */
	error = vmbus_rxbr_read(&chan->ch_rxbr, data, dlen, hlen);
	KASSERT(!error, ("vmbus_rxbr_read failed"));

	return (0);
}

int
vmbus_chan_recv_pkt(struct vmbus_channel *chan,
    struct vmbus_chanpkt_hdr *pkt, int *pktlen0)
{
	int error, pktlen, pkt_hlen;

	pkt_hlen = sizeof(*pkt);
	error = vmbus_rxbr_peek(&chan->ch_rxbr, pkt, pkt_hlen);
	if (error)
		return (error);

	if (__predict_false(pkt->cph_hlen < VMBUS_CHANPKT_HLEN_MIN)) {
		vmbus_chan_printf(chan, "invalid hlen %u\n", pkt->cph_hlen);
		/* XXX this channel is dead actually. */
		return (EIO);
	}
	if (__predict_false(pkt->cph_hlen > pkt->cph_tlen)) {
		vmbus_chan_printf(chan, "invalid hlen %u and tlen %u\n",
		    pkt->cph_hlen, pkt->cph_tlen);
		/* XXX this channel is dead actually. */
		return (EIO);
	}

	pktlen = VMBUS_CHANPKT_GETLEN(pkt->cph_tlen);
	if (*pktlen0 < pktlen) {
		/* Return the size of this packet. */
		*pktlen0 = pktlen;
		return (ENOBUFS);
	}
	*pktlen0 = pktlen;

	/*
	 * Skip the fixed-size packet header, which has been filled
	 * by the above vmbus_rxbr_peek().
	 */
	error = vmbus_rxbr_read(&chan->ch_rxbr, pkt + 1,
	    pktlen - pkt_hlen, pkt_hlen);
	KASSERT(!error, ("vmbus_rxbr_read failed"));

	return (0);
}

static void
vmbus_chan_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;
	vmbus_chan_callback_t cb = chan->ch_cb;
	void *cbarg = chan->ch_cbarg;

	KASSERT(chan->ch_poll_intvl == 0,
	    ("chan%u: interrupted in polling mode", chan->ch_id));

	/*
	 * Optimize host to guest signaling by ensuring:
	 * 1. While reading the channel, we disable interrupts from
	 *    host.
	 * 2. Ensure that we process all posted messages from the host
	 *    before returning from this callback.
	 * 3. Once we return, enable signaling from the host. Once this
	 *    state is set we check to see if additional packets are
	 *    available to read. In this case we repeat the process.
	 *
	 * NOTE: Interrupt has been disabled in the ISR.
	 */
	for (;;) {
		uint32_t left;

		cb(chan, cbarg);

		left = vmbus_rxbr_intr_unmask(&chan->ch_rxbr);
		if (left == 0) {
			/* No more data in RX bufring; done */
			break;
		}
		vmbus_rxbr_intr_mask(&chan->ch_rxbr);
	}
}

static void
vmbus_chan_task_nobatch(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;

	KASSERT(chan->ch_poll_intvl == 0,
	    ("chan%u: interrupted in polling mode", chan->ch_id));
	chan->ch_cb(chan, chan->ch_cbarg);
}

static void
vmbus_chan_poll_timeout(void *xchan)
{
	struct vmbus_channel *chan = xchan;

	KASSERT(chan->ch_poll_intvl != 0,
	    ("chan%u: polling timeout in interrupt mode", chan->ch_id));
	taskqueue_enqueue(chan->ch_tq, &chan->ch_poll_task);
}

static void
vmbus_chan_poll_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;

	KASSERT(chan->ch_poll_intvl != 0,
	    ("chan%u: polling in interrupt mode", chan->ch_id));
	callout_reset_sbt_curcpu(&chan->ch_poll_timeo, chan->ch_poll_intvl, 0,
	    vmbus_chan_poll_timeout, chan, chan->ch_poll_flags);
	chan->ch_cb(chan, chan->ch_cbarg);
}

static void
vmbus_chan_pollcfg_task(void *xarg, int pending __unused)
{
	const struct vmbus_chan_pollarg *arg = xarg;
	struct vmbus_channel *chan = arg->poll_chan;
	sbintime_t intvl;
	int poll_flags;

	/*
	 * Save polling interval.
	 */
	intvl = SBT_1S / arg->poll_hz;
	if (intvl == 0)
		intvl = 1;
	if (intvl == chan->ch_poll_intvl) {
		/* Nothing changes; done */
		return;
	}
	chan->ch_poll_intvl = intvl;

	/* Adjust callout flags. */
	poll_flags = C_DIRECT_EXEC;
	if (arg->poll_hz <= hz)
		poll_flags |= C_HARDCLOCK;
	chan->ch_poll_flags = poll_flags;

	/*
	 * Disconnect this channel from the channel map to make sure that
	 * the RX bufring interrupt enabling bit can not be touched, and
	 * ISR can not enqueue this channel task anymore.  THEN, disable
	 * interrupt from the RX bufring (TX bufring does not generate
	 * interrupt to VM).
	 *
	 * NOTE: order is critical.
	 */
	chan->ch_vmbus->vmbus_chmap[chan->ch_id] = NULL;
	__compiler_membar();
	vmbus_rxbr_intr_mask(&chan->ch_rxbr);

	/*
	 * NOTE:
	 * At this point, this channel task will not be enqueued by
	 * the ISR anymore, time to cancel the pending one.
	 */
	taskqueue_cancel(chan->ch_tq, &chan->ch_task, NULL);

	/* Kick start! */
	taskqueue_enqueue(chan->ch_tq, &chan->ch_poll_task);
}

static bool
vmbus_chan_poll_cancel_intq(struct vmbus_channel *chan)
{

	if (chan->ch_poll_intvl == 0) {
		/* Not enabled. */
		return (false);
	}

	/*
	 * Stop polling callout, so that channel polling task
	 * will not be enqueued anymore.
	 */
	callout_drain(&chan->ch_poll_timeo);

	/*
	 * Disable polling by resetting polling interval.
	 *
	 * NOTE:
	 * The polling interval resetting MUST be conducted
	 * after the callout is drained; mainly to keep the
	 * proper assertion in place.
	 */
	chan->ch_poll_intvl = 0;

	/*
	 * NOTE:
	 * At this point, this channel polling task will not be
	 * enqueued by the callout anymore, time to cancel the
	 * pending one.
	 */
	taskqueue_cancel(chan->ch_tq, &chan->ch_poll_task, NULL);

	/* Polling was enabled. */
	return (true);
}

static void
vmbus_chan_polldis_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;

	if (!vmbus_chan_poll_cancel_intq(chan)) {
		/* Already disabled; done. */
		return;
	}

	/*
	 * Plug this channel back to the channel map and unmask
	 * the RX bufring interrupt.
	 */
	chan->ch_vmbus->vmbus_chmap[chan->ch_id] = chan;
	__compiler_membar();
	vmbus_rxbr_intr_unmask(&chan->ch_rxbr);

	/*
	 * Kick start the interrupt task, just in case unmasking
	 * interrupt races ISR.
	 */
	taskqueue_enqueue(chan->ch_tq, &chan->ch_task);
}

static __inline void
vmbus_event_flags_proc(struct vmbus_softc *sc, volatile u_long *event_flags,
    int flag_cnt)
{
	int f;

	for (f = 0; f < flag_cnt; ++f) {
		uint32_t chid_base;
		u_long flags;
		int chid_ofs;

		if (event_flags[f] == 0)
			continue;

		flags = atomic_swap_long(&event_flags[f], 0);
		chid_base = f << VMBUS_EVTFLAG_SHIFT;

		while ((chid_ofs = ffsl(flags)) != 0) {
			struct vmbus_channel *chan;

			--chid_ofs; /* NOTE: ffsl is 1-based */
			flags &= ~(1UL << chid_ofs);

			chan = sc->vmbus_chmap[chid_base + chid_ofs];
			if (__predict_false(chan == NULL)) {
				/* Channel is closed. */
				continue;
			}
			__compiler_membar();

			if (chan->ch_flags & VMBUS_CHAN_FLAG_BATCHREAD)
				vmbus_rxbr_intr_mask(&chan->ch_rxbr);
			taskqueue_enqueue(chan->ch_tq, &chan->ch_task);
		}
	}
}

void
vmbus_event_proc(struct vmbus_softc *sc, int cpu)
{
	struct vmbus_evtflags *eventf;

	/*
	 * On Host with Win8 or above, the event page can be checked directly
	 * to get the id of the channel that has the pending interrupt.
	 */
	eventf = VMBUS_PCPU_GET(sc, event_flags, cpu) + VMBUS_SINT_MESSAGE;
	vmbus_event_flags_proc(sc, eventf->evt_flags,
	    VMBUS_PCPU_GET(sc, event_flags_cnt, cpu));
}

void
vmbus_event_proc_compat(struct vmbus_softc *sc, int cpu)
{
	struct vmbus_evtflags *eventf;

	eventf = VMBUS_PCPU_GET(sc, event_flags, cpu) + VMBUS_SINT_MESSAGE;
	if (atomic_testandclear_long(&eventf->evt_flags[0], 0)) {
		vmbus_event_flags_proc(sc, sc->vmbus_rx_evtflags,
		    VMBUS_CHAN_MAX_COMPAT >> VMBUS_EVTFLAG_SHIFT);
	}
}

static void
vmbus_chan_update_evtflagcnt(struct vmbus_softc *sc,
    const struct vmbus_channel *chan)
{
	volatile int *flag_cnt_ptr;
	int flag_cnt;

	flag_cnt = (chan->ch_id / VMBUS_EVTFLAG_LEN) + 1;
	flag_cnt_ptr = VMBUS_PCPU_PTR(sc, event_flags_cnt, chan->ch_cpuid);

	for (;;) {
		int old_flag_cnt;

		old_flag_cnt = *flag_cnt_ptr;
		if (old_flag_cnt >= flag_cnt)
			break;
		if (atomic_cmpset_int(flag_cnt_ptr, old_flag_cnt, flag_cnt)) {
			if (bootverbose) {
				vmbus_chan_printf(chan,
				    "chan%u update cpu%d flag_cnt to %d\n",
				    chan->ch_id, chan->ch_cpuid, flag_cnt);
			}
			break;
		}
	}
}

static struct vmbus_channel *
vmbus_chan_alloc(struct vmbus_softc *sc)
{
	struct vmbus_channel *chan;

	chan = malloc(sizeof(*chan), M_DEVBUF, M_WAITOK | M_ZERO);

	chan->ch_monprm = hyperv_dmamem_alloc(bus_get_dma_tag(sc->vmbus_dev),
	    HYPERCALL_PARAM_ALIGN, 0, sizeof(struct hyperv_mon_param),
	    &chan->ch_monprm_dma, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (chan->ch_monprm == NULL) {
		device_printf(sc->vmbus_dev, "monprm alloc failed\n");
		free(chan, M_DEVBUF);
		return NULL;
	}

	chan->ch_refs = 1;
	chan->ch_vmbus = sc;
	mtx_init(&chan->ch_subchan_lock, "vmbus subchan", NULL, MTX_DEF);
	sx_init(&chan->ch_orphan_lock, "vmbus chorphan");
	TAILQ_INIT(&chan->ch_subchans);
	vmbus_rxbr_init(&chan->ch_rxbr);
	vmbus_txbr_init(&chan->ch_txbr);

	TASK_INIT(&chan->ch_poll_task, 0, vmbus_chan_poll_task, chan);
	callout_init(&chan->ch_poll_timeo, 1);

	return chan;
}

static void
vmbus_chan_free(struct vmbus_channel *chan)
{

	KASSERT(TAILQ_EMPTY(&chan->ch_subchans) && chan->ch_subchan_cnt == 0,
	    ("still owns sub-channels"));
	KASSERT((chan->ch_stflags &
	    (VMBUS_CHAN_ST_OPENED |
	     VMBUS_CHAN_ST_ONPRIL |
	     VMBUS_CHAN_ST_ONSUBL |
	     VMBUS_CHAN_ST_ONLIST)) == 0, ("free busy channel"));
	KASSERT(chan->ch_orphan_xact == NULL,
	    ("still has orphan xact installed"));
	KASSERT(chan->ch_refs == 0, ("chan%u: invalid refcnt %d",
	    chan->ch_id, chan->ch_refs));
	KASSERT(chan->ch_poll_intvl == 0, ("chan%u: polling is activated",
	    chan->ch_id));

	hyperv_dmamem_free(&chan->ch_monprm_dma, chan->ch_monprm);
	mtx_destroy(&chan->ch_subchan_lock);
	sx_destroy(&chan->ch_orphan_lock);
	vmbus_rxbr_deinit(&chan->ch_rxbr);
	vmbus_txbr_deinit(&chan->ch_txbr);
	free(chan, M_DEVBUF);
}

static int
vmbus_chan_add(struct vmbus_channel *newchan)
{
	struct vmbus_softc *sc = newchan->ch_vmbus;
	struct vmbus_channel *prichan;

	if (newchan->ch_id == 0) {
		/*
		 * XXX
		 * Chan0 will neither be processed nor should be offered;
		 * skip it.
		 */
		device_printf(sc->vmbus_dev, "got chan0 offer, discard\n");
		return EINVAL;
	} else if (newchan->ch_id >= VMBUS_CHAN_MAX) {
		device_printf(sc->vmbus_dev, "invalid chan%u offer\n",
		    newchan->ch_id);
		return EINVAL;
	}

	mtx_lock(&sc->vmbus_prichan_lock);
	TAILQ_FOREACH(prichan, &sc->vmbus_prichans, ch_prilink) {
		/*
		 * Sub-channel will have the same type GUID and instance
		 * GUID as its primary channel.
		 */
		if (memcmp(&prichan->ch_guid_type, &newchan->ch_guid_type,
		    sizeof(struct hyperv_guid)) == 0 &&
		    memcmp(&prichan->ch_guid_inst, &newchan->ch_guid_inst,
		    sizeof(struct hyperv_guid)) == 0)
			break;
	}
	if (VMBUS_CHAN_ISPRIMARY(newchan)) {
		if (prichan == NULL) {
			/* Install the new primary channel */
			vmbus_chan_ins_prilist(sc, newchan);
			mtx_unlock(&sc->vmbus_prichan_lock);
			goto done;
		} else {
			mtx_unlock(&sc->vmbus_prichan_lock);
			device_printf(sc->vmbus_dev,
			    "duplicated primary chan%u\n", newchan->ch_id);
			return EINVAL;
		}
	} else { /* Sub-channel */
		if (prichan == NULL) {
			mtx_unlock(&sc->vmbus_prichan_lock);
			device_printf(sc->vmbus_dev,
			    "no primary chan for chan%u\n", newchan->ch_id);
			return EINVAL;
		}
		/*
		 * Found the primary channel for this sub-channel and
		 * move on.
		 *
		 * XXX refcnt prichan
		 */
	}
	mtx_unlock(&sc->vmbus_prichan_lock);

	/*
	 * This is a sub-channel; link it with the primary channel.
	 */
	KASSERT(!VMBUS_CHAN_ISPRIMARY(newchan),
	    ("new channel is not sub-channel"));
	KASSERT(prichan != NULL, ("no primary channel"));

	/*
	 * Reference count this sub-channel; it will be dereferenced
	 * when this sub-channel is closed.
	 */
	KASSERT(newchan->ch_refs == 1, ("chan%u: invalid refcnt %d",
	    newchan->ch_id, newchan->ch_refs));
	atomic_add_int(&newchan->ch_refs, 1);

	newchan->ch_prichan = prichan;
	newchan->ch_dev = prichan->ch_dev;

	mtx_lock(&prichan->ch_subchan_lock);
	vmbus_chan_ins_sublist(prichan, newchan);
	mtx_unlock(&prichan->ch_subchan_lock);
	/*
	 * Notify anyone that is interested in this sub-channel,
	 * after this sub-channel is setup.
	 */
	wakeup(prichan);
done:
	/*
	 * Hook this channel up for later revocation.
	 */
	mtx_lock(&sc->vmbus_chan_lock);
	vmbus_chan_ins_list(sc, newchan);
	mtx_unlock(&sc->vmbus_chan_lock);

	if (bootverbose) {
		vmbus_chan_printf(newchan, "chan%u subidx%u offer\n",
		    newchan->ch_id, newchan->ch_subidx);
	}

	/* Select default cpu for this channel. */
	vmbus_chan_cpu_default(newchan);

	return 0;
}

void
vmbus_chan_cpu_set(struct vmbus_channel *chan, int cpu)
{
	KASSERT(cpu >= 0 && cpu < mp_ncpus, ("invalid cpu %d", cpu));

	if (chan->ch_vmbus->vmbus_version == VMBUS_VERSION_WS2008 ||
	    chan->ch_vmbus->vmbus_version == VMBUS_VERSION_WIN7) {
		/* Only cpu0 is supported */
		cpu = 0;
	}

	chan->ch_cpuid = cpu;
	chan->ch_vcpuid = VMBUS_PCPU_GET(chan->ch_vmbus, vcpuid, cpu);

	if (bootverbose) {
		vmbus_chan_printf(chan,
		    "chan%u assigned to cpu%u [vcpu%u]\n",
		    chan->ch_id, chan->ch_cpuid, chan->ch_vcpuid);
	}
}

void
vmbus_chan_cpu_rr(struct vmbus_channel *chan)
{
	static uint32_t vmbus_chan_nextcpu;
	int cpu;

	cpu = atomic_fetchadd_int(&vmbus_chan_nextcpu, 1) % mp_ncpus;
	vmbus_chan_cpu_set(chan, cpu);
}

static void
vmbus_chan_cpu_default(struct vmbus_channel *chan)
{
	/*
	 * By default, pin the channel to cpu0.  Devices having
	 * special channel-cpu mapping requirement should call
	 * vmbus_chan_cpu_{set,rr}().
	 */
	vmbus_chan_cpu_set(chan, 0);
}

static void
vmbus_chan_msgproc_choffer(struct vmbus_softc *sc,
    const struct vmbus_message *msg)
{
	const struct vmbus_chanmsg_choffer *offer;
	struct vmbus_channel *chan;
	task_fn_t *detach_fn, *attach_fn;
	int error;

	offer = (const struct vmbus_chanmsg_choffer *)msg->msg_data;

	chan = vmbus_chan_alloc(sc);
	if (chan == NULL) {
		device_printf(sc->vmbus_dev, "allocate chan%u failed\n",
		    offer->chm_chanid);
		return;
	}

	chan->ch_id = offer->chm_chanid;
	chan->ch_subidx = offer->chm_subidx;
	chan->ch_guid_type = offer->chm_chtype;
	chan->ch_guid_inst = offer->chm_chinst;

	/* Batch reading is on by default */
	chan->ch_flags |= VMBUS_CHAN_FLAG_BATCHREAD;

	chan->ch_monprm->mp_connid = VMBUS_CONNID_EVENT;
	if (sc->vmbus_version != VMBUS_VERSION_WS2008)
		chan->ch_monprm->mp_connid = offer->chm_connid;

	if (offer->chm_flags1 & VMBUS_CHOFFER_FLAG1_HASMNF) {
		int trig_idx;

		/*
		 * Setup MNF stuffs.
		 */
		chan->ch_txflags |= VMBUS_CHAN_TXF_HASMNF;

		trig_idx = offer->chm_montrig / VMBUS_MONTRIG_LEN;
		if (trig_idx >= VMBUS_MONTRIGS_MAX)
			panic("invalid monitor trigger %u", offer->chm_montrig);
		chan->ch_montrig =
		    &sc->vmbus_mnf2->mnf_trigs[trig_idx].mt_pending;

		chan->ch_montrig_mask =
		    1 << (offer->chm_montrig % VMBUS_MONTRIG_LEN);
	}

	/*
	 * Setup event flag.
	 */
	chan->ch_evtflag =
	    &sc->vmbus_tx_evtflags[chan->ch_id >> VMBUS_EVTFLAG_SHIFT];
	chan->ch_evtflag_mask = 1UL << (chan->ch_id & VMBUS_EVTFLAG_MASK);

	/*
	 * Setup attach and detach tasks.
	 */
	if (VMBUS_CHAN_ISPRIMARY(chan)) {
		chan->ch_mgmt_tq = sc->vmbus_devtq;
		attach_fn = vmbus_prichan_attach_task;
		detach_fn = vmbus_prichan_detach_task;
	} else {
		chan->ch_mgmt_tq = sc->vmbus_subchtq;
		attach_fn = vmbus_subchan_attach_task;
		detach_fn = vmbus_subchan_detach_task;
	}
	TASK_INIT(&chan->ch_attach_task, 0, attach_fn, chan);
	TASK_INIT(&chan->ch_detach_task, 0, detach_fn, chan);

	error = vmbus_chan_add(chan);
	if (error) {
		device_printf(sc->vmbus_dev, "add chan%u failed: %d\n",
		    chan->ch_id, error);
		atomic_subtract_int(&chan->ch_refs, 1);
		vmbus_chan_free(chan);
		return;
	}
	taskqueue_enqueue(chan->ch_mgmt_tq, &chan->ch_attach_task);
}

static void
vmbus_chan_msgproc_chrescind(struct vmbus_softc *sc,
    const struct vmbus_message *msg)
{
	const struct vmbus_chanmsg_chrescind *note;
	struct vmbus_channel *chan;

	note = (const struct vmbus_chanmsg_chrescind *)msg->msg_data;
	if (note->chm_chanid > VMBUS_CHAN_MAX) {
		device_printf(sc->vmbus_dev, "invalid revoked chan%u\n",
		    note->chm_chanid);
		return;
	}

	/*
	 * Find and remove the target channel from the channel list.
	 */
	mtx_lock(&sc->vmbus_chan_lock);
	TAILQ_FOREACH(chan, &sc->vmbus_chans, ch_link) {
		if (chan->ch_id == note->chm_chanid)
			break;
	}
	if (chan == NULL) {
		mtx_unlock(&sc->vmbus_chan_lock);
		device_printf(sc->vmbus_dev, "chan%u is not offered\n",
		    note->chm_chanid);
		return;
	}
	vmbus_chan_rem_list(sc, chan);
	mtx_unlock(&sc->vmbus_chan_lock);

	if (VMBUS_CHAN_ISPRIMARY(chan)) {
		/*
		 * The target channel is a primary channel; remove the
		 * target channel from the primary channel list now,
		 * instead of later, so that it will not be found by
		 * other sub-channel offers, which are processed in
		 * this thread.
		 */
		mtx_lock(&sc->vmbus_prichan_lock);
		vmbus_chan_rem_prilist(sc, chan);
		mtx_unlock(&sc->vmbus_prichan_lock);
	}

	/*
	 * NOTE:
	 * The following processing order is critical:
	 * Set the REVOKED state flag before orphaning the installed xact.
	 */

	if (atomic_testandset_int(&chan->ch_stflags,
	    VMBUS_CHAN_ST_REVOKED_SHIFT))
		panic("channel has already been revoked");

	sx_xlock(&chan->ch_orphan_lock);
	if (chan->ch_orphan_xact != NULL)
		vmbus_xact_ctx_orphan(chan->ch_orphan_xact);
	sx_xunlock(&chan->ch_orphan_lock);

	if (bootverbose)
		vmbus_chan_printf(chan, "chan%u revoked\n", note->chm_chanid);
	vmbus_chan_detach(chan);
}

static int
vmbus_chan_release(struct vmbus_channel *chan)
{
	struct vmbus_softc *sc = chan->ch_vmbus;
	struct vmbus_chanmsg_chfree *req;
	struct vmbus_msghc *mh;
	int error;

	mh = vmbus_msghc_get(sc, sizeof(*req));
	if (mh == NULL) {
		vmbus_chan_printf(chan,
		    "can not get msg hypercall for chfree(chan%u)\n",
		    chan->ch_id);
		return (ENXIO);
	}

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_CHFREE;
	req->chm_chanid = chan->ch_id;

	error = vmbus_msghc_exec_noresult(mh);
	vmbus_msghc_put(sc, mh);

	if (error) {
		vmbus_chan_printf(chan,
		    "chfree(chan%u) msg hypercall exec failed: %d\n",
		    chan->ch_id, error);
	} else {
		if (bootverbose)
			vmbus_chan_printf(chan, "chan%u freed\n", chan->ch_id);
	}
	return (error);
}

static void
vmbus_prichan_detach_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;

	KASSERT(VMBUS_CHAN_ISPRIMARY(chan),
	    ("chan%u is not primary channel", chan->ch_id));

	/* Delete and detach the device associated with this channel. */
	vmbus_delete_child(chan);

	/* Release this channel (back to vmbus). */
	vmbus_chan_release(chan);

	/* Free this channel's resource. */
	vmbus_chan_free(chan);
}

static void
vmbus_subchan_detach_task(void *xchan, int pending __unused)
{
	struct vmbus_channel *chan = xchan;
	struct vmbus_channel *pri_chan = chan->ch_prichan;

	KASSERT(!VMBUS_CHAN_ISPRIMARY(chan),
	    ("chan%u is primary channel", chan->ch_id));

	/* Release this channel (back to vmbus). */
	vmbus_chan_release(chan);

	/* Unlink from its primary channel's sub-channel list. */
	mtx_lock(&pri_chan->ch_subchan_lock);
	vmbus_chan_rem_sublist(pri_chan, chan);
	mtx_unlock(&pri_chan->ch_subchan_lock);
	/* Notify anyone that is waiting for this sub-channel to vanish. */
	wakeup(pri_chan);

	/* Free this channel's resource. */
	vmbus_chan_free(chan);
}

static void
vmbus_prichan_attach_task(void *xchan, int pending __unused)
{

	/*
	 * Add device for this primary channel.
	 */
	vmbus_add_child(xchan);
}

static void
vmbus_subchan_attach_task(void *xchan __unused, int pending __unused)
{

	/* Nothing */
}

void
vmbus_chan_destroy_all(struct vmbus_softc *sc)
{

	/*
	 * Detach all devices and destroy the corresponding primary
	 * channels.
	 */
	for (;;) {
		struct vmbus_channel *chan;

		mtx_lock(&sc->vmbus_chan_lock);
		TAILQ_FOREACH(chan, &sc->vmbus_chans, ch_link) {
			if (VMBUS_CHAN_ISPRIMARY(chan))
				break;
		}
		if (chan == NULL) {
			/* No more primary channels; done. */
			mtx_unlock(&sc->vmbus_chan_lock);
			break;
		}
		vmbus_chan_rem_list(sc, chan);
		mtx_unlock(&sc->vmbus_chan_lock);

		mtx_lock(&sc->vmbus_prichan_lock);
		vmbus_chan_rem_prilist(sc, chan);
		mtx_unlock(&sc->vmbus_prichan_lock);

		taskqueue_enqueue(chan->ch_mgmt_tq, &chan->ch_detach_task);
	}
}

struct vmbus_channel **
vmbus_subchan_get(struct vmbus_channel *pri_chan, int subchan_cnt)
{
	struct vmbus_channel **ret, *chan;
	int i;

	KASSERT(subchan_cnt > 0, ("invalid sub-channel count %d", subchan_cnt));

	ret = malloc(subchan_cnt * sizeof(struct vmbus_channel *), M_TEMP,
	    M_WAITOK);

	mtx_lock(&pri_chan->ch_subchan_lock);

	while (pri_chan->ch_subchan_cnt < subchan_cnt)
		mtx_sleep(pri_chan, &pri_chan->ch_subchan_lock, 0, "subch", 0);

	i = 0;
	TAILQ_FOREACH(chan, &pri_chan->ch_subchans, ch_sublink) {
		/* TODO: refcnt chan */
		ret[i] = chan;

		++i;
		if (i == subchan_cnt)
			break;
	}
	KASSERT(i == subchan_cnt, ("invalid subchan count %d, should be %d",
	    pri_chan->ch_subchan_cnt, subchan_cnt));

	mtx_unlock(&pri_chan->ch_subchan_lock);

	return ret;
}

void
vmbus_subchan_rel(struct vmbus_channel **subchan, int subchan_cnt __unused)
{

	free(subchan, M_TEMP);
}

void
vmbus_subchan_drain(struct vmbus_channel *pri_chan)
{
	mtx_lock(&pri_chan->ch_subchan_lock);
	while (pri_chan->ch_subchan_cnt > 0)
		mtx_sleep(pri_chan, &pri_chan->ch_subchan_lock, 0, "dsubch", 0);
	mtx_unlock(&pri_chan->ch_subchan_lock);
}

void
vmbus_chan_msgproc(struct vmbus_softc *sc, const struct vmbus_message *msg)
{
	vmbus_chanmsg_proc_t msg_proc;
	uint32_t msg_type;

	msg_type = ((const struct vmbus_chanmsg_hdr *)msg->msg_data)->chm_type;
	KASSERT(msg_type < VMBUS_CHANMSG_TYPE_MAX,
	    ("invalid message type %u", msg_type));

	msg_proc = vmbus_chan_msgprocs[msg_type];
	if (msg_proc != NULL)
		msg_proc(sc, msg);
}

void
vmbus_chan_set_readbatch(struct vmbus_channel *chan, bool on)
{
	if (!on)
		chan->ch_flags &= ~VMBUS_CHAN_FLAG_BATCHREAD;
	else
		chan->ch_flags |= VMBUS_CHAN_FLAG_BATCHREAD;
}

uint32_t
vmbus_chan_id(const struct vmbus_channel *chan)
{
	return chan->ch_id;
}

uint32_t
vmbus_chan_subidx(const struct vmbus_channel *chan)
{
	return chan->ch_subidx;
}

bool
vmbus_chan_is_primary(const struct vmbus_channel *chan)
{
	if (VMBUS_CHAN_ISPRIMARY(chan))
		return true;
	else
		return false;
}

const struct hyperv_guid *
vmbus_chan_guid_inst(const struct vmbus_channel *chan)
{
	return &chan->ch_guid_inst;
}

int
vmbus_chan_prplist_nelem(int br_size, int prpcnt_max, int dlen_max)
{
	int elem_size;

	elem_size = __offsetof(struct vmbus_chanpkt_prplist,
	    cp_range[0].gpa_page[prpcnt_max]);
	elem_size += dlen_max;
	elem_size = VMBUS_CHANPKT_TOTLEN(elem_size);

	return (vmbus_br_nelem(br_size, elem_size));
}

bool
vmbus_chan_tx_empty(const struct vmbus_channel *chan)
{

	return (vmbus_txbr_empty(&chan->ch_txbr));
}

bool
vmbus_chan_rx_empty(const struct vmbus_channel *chan)
{

	return (vmbus_rxbr_empty(&chan->ch_rxbr));
}

static int
vmbus_chan_printf(const struct vmbus_channel *chan, const char *fmt, ...)
{
	va_list ap;
	device_t dev;
	int retval;

	if (chan->ch_dev == NULL || !device_is_alive(chan->ch_dev))
		dev = chan->ch_vmbus->vmbus_dev;
	else
		dev = chan->ch_dev;

	retval = device_print_prettyname(dev);
	va_start(ap, fmt);
	retval += vprintf(fmt, ap);
	va_end(ap);

	return (retval);
}

void
vmbus_chan_run_task(struct vmbus_channel *chan, struct task *task)
{

	taskqueue_enqueue(chan->ch_tq, task);
	taskqueue_drain(chan->ch_tq, task);
}

struct taskqueue *
vmbus_chan_mgmt_tq(const struct vmbus_channel *chan)
{

	return (chan->ch_mgmt_tq);
}

bool
vmbus_chan_is_revoked(const struct vmbus_channel *chan)
{

	if (chan->ch_stflags & VMBUS_CHAN_ST_REVOKED)
		return (true);
	return (false);
}

void
vmbus_chan_set_orphan(struct vmbus_channel *chan, struct vmbus_xact_ctx *xact)
{

	sx_xlock(&chan->ch_orphan_lock);
	chan->ch_orphan_xact = xact;
	sx_xunlock(&chan->ch_orphan_lock);
}

void
vmbus_chan_unset_orphan(struct vmbus_channel *chan)
{

	sx_xlock(&chan->ch_orphan_lock);
	chan->ch_orphan_xact = NULL;
	sx_xunlock(&chan->ch_orphan_lock);
}

const void *
vmbus_chan_xact_wait(const struct vmbus_channel *chan,
    struct vmbus_xact *xact, size_t *resp_len, bool can_sleep)
{
	const void *ret;

	if (can_sleep)
		ret = vmbus_xact_wait(xact, resp_len);
	else
		ret = vmbus_xact_busywait(xact, resp_len);
	if (vmbus_chan_is_revoked(chan)) {
		/*
		 * This xact probably is interrupted, and the
		 * interruption can race the reply reception,
		 * so we have to make sure that there are nothing
		 * left on the RX bufring, i.e. this xact will
		 * not be touched, once this function returns.
		 *
		 * Since the hypervisor will not put more data
		 * onto the RX bufring once the channel is revoked,
		 * the following loop will be terminated, once all
		 * data are drained by the driver's channel
		 * callback.
		 */
		while (!vmbus_chan_rx_empty(chan)) {
			if (can_sleep)
				pause("chxact", 1);
			else
				DELAY(1000);
		}
	}
	return (ret);
}

void
vmbus_chan_poll_enable(struct vmbus_channel *chan, u_int pollhz)
{
	struct vmbus_chan_pollarg arg;
	struct task poll_cfg;

	KASSERT(chan->ch_flags & VMBUS_CHAN_FLAG_BATCHREAD,
	    ("enable polling on non-batch chan%u", chan->ch_id));
	KASSERT(pollhz >= VMBUS_CHAN_POLLHZ_MIN &&
	    pollhz <= VMBUS_CHAN_POLLHZ_MAX, ("invalid pollhz %u", pollhz));

	arg.poll_chan = chan;
	arg.poll_hz = pollhz;
	TASK_INIT(&poll_cfg, 0, vmbus_chan_pollcfg_task, &arg);
	vmbus_chan_run_task(chan, &poll_cfg);
}

void
vmbus_chan_poll_disable(struct vmbus_channel *chan)
{
	struct task poll_dis;

	KASSERT(chan->ch_flags & VMBUS_CHAN_FLAG_BATCHREAD,
	    ("disable polling on non-batch chan%u", chan->ch_id));

	TASK_INIT(&poll_dis, 0, vmbus_chan_polldis_task, chan);
	vmbus_chan_run_task(chan, &poll_dis);
}
