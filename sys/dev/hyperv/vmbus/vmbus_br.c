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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <dev/hyperv/vmbus/vmbus_reg.h>
#include <dev/hyperv/vmbus/vmbus_brvar.h>

/* Amount of space available for write */
#define	VMBUS_BR_WAVAIL(r, w, z)	\
	(((w) >= (r)) ? ((z) - ((w) - (r))) : ((r) - (w)))

/* Increase bufing index */
#define VMBUS_BR_IDXINC(idx, inc, sz)	(((idx) + (inc)) % (sz))

static int			vmbus_br_sysctl_state(SYSCTL_HANDLER_ARGS);
static int			vmbus_br_sysctl_state_bin(SYSCTL_HANDLER_ARGS);
static void			vmbus_br_setup(struct vmbus_br *, void *, int);

static int
vmbus_br_sysctl_state(SYSCTL_HANDLER_ARGS)
{
	const struct vmbus_br *br = arg1;
	uint32_t rindex, windex, imask, ravail, wavail;
	char state[256];

	rindex = br->vbr_rindex;
	windex = br->vbr_windex;
	imask = br->vbr_imask;
	wavail = VMBUS_BR_WAVAIL(rindex, windex, br->vbr_dsize);
	ravail = br->vbr_dsize - wavail;

	snprintf(state, sizeof(state),
	    "rindex:%u windex:%u imask:%u ravail:%u wavail:%u",
	    rindex, windex, imask, ravail, wavail);
	return sysctl_handle_string(oidp, state, sizeof(state), req);
}

/*
 * Binary bufring states.
 */
static int
vmbus_br_sysctl_state_bin(SYSCTL_HANDLER_ARGS)
{
#define BR_STATE_RIDX	0
#define BR_STATE_WIDX	1
#define BR_STATE_IMSK	2
#define BR_STATE_RSPC	3
#define BR_STATE_WSPC	4
#define BR_STATE_MAX	5

	const struct vmbus_br *br = arg1;
	uint32_t rindex, windex, wavail, state[BR_STATE_MAX];

	rindex = br->vbr_rindex;
	windex = br->vbr_windex;
	wavail = VMBUS_BR_WAVAIL(rindex, windex, br->vbr_dsize);

	state[BR_STATE_RIDX] = rindex;
	state[BR_STATE_WIDX] = windex;
	state[BR_STATE_IMSK] = br->vbr_imask;
	state[BR_STATE_WSPC] = wavail;
	state[BR_STATE_RSPC] = br->vbr_dsize - wavail;

	return sysctl_handle_opaque(oidp, state, sizeof(state), req);
}

void
vmbus_br_sysctl_create(struct sysctl_ctx_list *ctx, struct sysctl_oid *br_tree,
    struct vmbus_br *br, const char *name)
{
	struct sysctl_oid *tree;
	char desc[64];

	tree = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(br_tree), OID_AUTO,
	    name, CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "");
	if (tree == NULL)
		return;

	snprintf(desc, sizeof(desc), "%s state", name);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "state",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    br, 0, vmbus_br_sysctl_state, "A", desc);

	snprintf(desc, sizeof(desc), "%s binary state", name);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "state_bin",
	    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    br, 0, vmbus_br_sysctl_state_bin, "IU", desc);
}

void
vmbus_rxbr_intr_mask(struct vmbus_rxbr *rbr)
{
	rbr->rxbr_imask = 1;
	mb();
}

static __inline uint32_t
vmbus_rxbr_avail(const struct vmbus_rxbr *rbr)
{
	uint32_t rindex, windex;

	/* Get snapshot */
	rindex = rbr->rxbr_rindex;
	windex = rbr->rxbr_windex;

	return (rbr->rxbr_dsize -
	    VMBUS_BR_WAVAIL(rindex, windex, rbr->rxbr_dsize));
}

uint32_t
vmbus_rxbr_intr_unmask(struct vmbus_rxbr *rbr)
{
	rbr->rxbr_imask = 0;
	mb();

	/*
	 * Now check to see if the ring buffer is still empty.
	 * If it is not, we raced and we need to process new
	 * incoming channel packets.
	 */
	return vmbus_rxbr_avail(rbr);
}

static void
vmbus_br_setup(struct vmbus_br *br, void *buf, int blen)
{
	br->vbr = buf;
	br->vbr_dsize = blen - sizeof(struct vmbus_bufring);
}

void
vmbus_rxbr_init(struct vmbus_rxbr *rbr)
{
	mtx_init(&rbr->rxbr_lock, "vmbus_rxbr", NULL, MTX_SPIN);
}

void
vmbus_rxbr_deinit(struct vmbus_rxbr *rbr)
{
	mtx_destroy(&rbr->rxbr_lock);
}

void
vmbus_rxbr_setup(struct vmbus_rxbr *rbr, void *buf, int blen)
{
	vmbus_br_setup(&rbr->rxbr, buf, blen);
}

void
vmbus_txbr_init(struct vmbus_txbr *tbr)
{
	mtx_init(&tbr->txbr_lock, "vmbus_txbr", NULL, MTX_SPIN);
}

void
vmbus_txbr_deinit(struct vmbus_txbr *tbr)
{
	mtx_destroy(&tbr->txbr_lock);
}

void
vmbus_txbr_setup(struct vmbus_txbr *tbr, void *buf, int blen)
{
	vmbus_br_setup(&tbr->txbr, buf, blen);
}

/*
 * When we write to the ring buffer, check if the host needs to be
 * signaled.
 *
 * The contract:
 * - The host guarantees that while it is draining the TX bufring,
 *   it will set the br_imask to indicate it does not need to be
 *   interrupted when new data are added.
 * - The host guarantees that it will completely drain the TX bufring
 *   before exiting the read loop.  Further, once the TX bufring is
 *   empty, it will clear the br_imask and re-check to see if new
 *   data have arrived.
 */
static __inline boolean_t
vmbus_txbr_need_signal(const struct vmbus_txbr *tbr, uint32_t old_windex)
{
	mb();
	if (tbr->txbr_imask)
		return (FALSE);

	__compiler_membar();

	/*
	 * This is the only case we need to signal when the
	 * ring transitions from being empty to non-empty.
	 */
	if (old_windex == tbr->txbr_rindex)
		return (TRUE);

	return (FALSE);
}

static __inline uint32_t
vmbus_txbr_avail(const struct vmbus_txbr *tbr)
{
	uint32_t rindex, windex;

	/* Get snapshot */
	rindex = tbr->txbr_rindex;
	windex = tbr->txbr_windex;

	return VMBUS_BR_WAVAIL(rindex, windex, tbr->txbr_dsize);
}

static __inline uint32_t
vmbus_txbr_copyto(const struct vmbus_txbr *tbr, uint32_t windex,
    const void *src0, uint32_t cplen)
{
	const uint8_t *src = src0;
	uint8_t *br_data = tbr->txbr_data;
	uint32_t br_dsize = tbr->txbr_dsize;

	if (cplen > br_dsize - windex) {
		uint32_t fraglen = br_dsize - windex;

		/* Wrap-around detected */
		memcpy(br_data + windex, src, fraglen);
		memcpy(br_data, src + fraglen, cplen - fraglen);
	} else {
		memcpy(br_data + windex, src, cplen);
	}
	return VMBUS_BR_IDXINC(windex, cplen, br_dsize);
}

/*
 * Write scattered channel packet to TX bufring.
 *
 * The offset of this channel packet is written as a 64bits value
 * immediately after this channel packet.
 */
int
vmbus_txbr_write(struct vmbus_txbr *tbr, const struct iovec iov[], int iovlen,
    boolean_t *need_sig)
{
	uint32_t old_windex, windex, total;
	uint64_t save_windex;
	int i;

	total = 0;
	for (i = 0; i < iovlen; i++)
		total += iov[i].iov_len;
	total += sizeof(save_windex);

	mtx_lock_spin(&tbr->txbr_lock);

	/*
	 * NOTE:
	 * If this write is going to make br_windex same as br_rindex,
	 * i.e. the available space for write is same as the write size,
	 * we can't do it then, since br_windex == br_rindex means that
	 * the bufring is empty.
	 */
	if (vmbus_txbr_avail(tbr) <= total) {
		mtx_unlock_spin(&tbr->txbr_lock);
		return (EAGAIN);
	}

	/* Save br_windex for later use */
	old_windex = tbr->txbr_windex;

	/*
	 * Copy the scattered channel packet to the TX bufring.
	 */
	windex = old_windex;
	for (i = 0; i < iovlen; i++) {
		windex = vmbus_txbr_copyto(tbr, windex,
		    iov[i].iov_base, iov[i].iov_len);
	}

	/*
	 * Set the offset of the current channel packet.
	 */
	save_windex = ((uint64_t)old_windex) << 32;
	windex = vmbus_txbr_copyto(tbr, windex, &save_windex,
	    sizeof(save_windex));

	/*
	 * Update the write index _after_ the channel packet
	 * is copied.
	 */
	__compiler_membar();
	tbr->txbr_windex = windex;

	mtx_unlock_spin(&tbr->txbr_lock);

	*need_sig = vmbus_txbr_need_signal(tbr, old_windex);

	return (0);
}

static __inline uint32_t
vmbus_rxbr_copyfrom(const struct vmbus_rxbr *rbr, uint32_t rindex,
    void *dst0, int cplen)
{
	uint8_t *dst = dst0;
	const uint8_t *br_data = rbr->rxbr_data;
	uint32_t br_dsize = rbr->rxbr_dsize;

	if (cplen > br_dsize - rindex) {
		uint32_t fraglen = br_dsize - rindex;

		/* Wrap-around detected. */
		memcpy(dst, br_data + rindex, fraglen);
		memcpy(dst + fraglen, br_data, cplen - fraglen);
	} else {
		memcpy(dst, br_data + rindex, cplen);
	}
	return VMBUS_BR_IDXINC(rindex, cplen, br_dsize);
}

int
vmbus_rxbr_peek(struct vmbus_rxbr *rbr, void *data, int dlen)
{
	mtx_lock_spin(&rbr->rxbr_lock);

	/*
	 * The requested data and the 64bits channel packet
	 * offset should be there at least.
	 */
	if (vmbus_rxbr_avail(rbr) < dlen + sizeof(uint64_t)) {
		mtx_unlock_spin(&rbr->rxbr_lock);
		return (EAGAIN);
	}
	vmbus_rxbr_copyfrom(rbr, rbr->rxbr_rindex, data, dlen);

	mtx_unlock_spin(&rbr->rxbr_lock);

	return (0);
}

/*
 * NOTE:
 * We assume (dlen + skip) == sizeof(channel packet).
 */
int
vmbus_rxbr_read(struct vmbus_rxbr *rbr, void *data, int dlen, uint32_t skip)
{
	uint32_t rindex, br_dsize = rbr->rxbr_dsize;

	KASSERT(dlen + skip > 0, ("invalid dlen %d, offset %u", dlen, skip));

	mtx_lock_spin(&rbr->rxbr_lock);

	if (vmbus_rxbr_avail(rbr) < dlen + skip + sizeof(uint64_t)) {
		mtx_unlock_spin(&rbr->rxbr_lock);
		return (EAGAIN);
	}

	/*
	 * Copy channel packet from RX bufring.
	 */
	rindex = VMBUS_BR_IDXINC(rbr->rxbr_rindex, skip, br_dsize);
	rindex = vmbus_rxbr_copyfrom(rbr, rindex, data, dlen);

	/*
	 * Discard this channel packet's 64bits offset, which is useless to us.
	 */
	rindex = VMBUS_BR_IDXINC(rindex, sizeof(uint64_t), br_dsize);

	/*
	 * Update the read index _after_ the channel packet is fetched.
	 */
	__compiler_membar();
	rbr->rxbr_rindex = rindex;

	mtx_unlock_spin(&rbr->rxbr_lock);

	return (0);
}
