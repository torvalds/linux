/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013  Chris Torek <torek @ torek net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/uio.h>

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <pthread_np.h>

#include "bhyverun.h"
#include "pci_emul.h"
#include "virtio.h"

/*
 * Functions for dealing with generalized "virtual devices" as
 * defined by <https://www.google.com/#output=search&q=virtio+spec>
 */

/*
 * In case we decide to relax the "virtio softc comes at the
 * front of virtio-based device softc" constraint, let's use
 * this to convert.
 */
#define	DEV_SOFTC(vs) ((void *)(vs))

/*
 * Link a virtio_softc to its constants, the device softc, and
 * the PCI emulation.
 */
void
vi_softc_linkup(struct virtio_softc *vs, struct virtio_consts *vc,
		void *dev_softc, struct pci_devinst *pi,
		struct vqueue_info *queues)
{
	int i;

	/* vs and dev_softc addresses must match */
	assert((void *)vs == dev_softc);
	vs->vs_vc = vc;
	vs->vs_pi = pi;
	pi->pi_arg = vs;

	vs->vs_queues = queues;
	for (i = 0; i < vc->vc_nvq; i++) {
		queues[i].vq_vs = vs;
		queues[i].vq_num = i;
	}
}

/*
 * Reset device (device-wide).  This erases all queues, i.e.,
 * all the queues become invalid (though we don't wipe out the
 * internal pointers, we just clear the VQ_ALLOC flag).
 *
 * It resets negotiated features to "none".
 *
 * If MSI-X is enabled, this also resets all the vectors to NO_VECTOR.
 */
void
vi_reset_dev(struct virtio_softc *vs)
{
	struct vqueue_info *vq;
	int i, nvq;

	if (vs->vs_mtx)
		assert(pthread_mutex_isowned_np(vs->vs_mtx));

	nvq = vs->vs_vc->vc_nvq;
	for (vq = vs->vs_queues, i = 0; i < nvq; vq++, i++) {
		vq->vq_flags = 0;
		vq->vq_last_avail = 0;
		vq->vq_save_used = 0;
		vq->vq_pfn = 0;
		vq->vq_msix_idx = VIRTIO_MSI_NO_VECTOR;
	}
	vs->vs_negotiated_caps = 0;
	vs->vs_curq = 0;
	/* vs->vs_status = 0; -- redundant */
	if (vs->vs_isr)
		pci_lintr_deassert(vs->vs_pi);
	vs->vs_isr = 0;
	vs->vs_msix_cfg_idx = VIRTIO_MSI_NO_VECTOR;
}

/*
 * Set I/O BAR (usually 0) to map PCI config registers.
 */
void
vi_set_io_bar(struct virtio_softc *vs, int barnum)
{
	size_t size;

	/*
	 * ??? should we use CFG0 if MSI-X is disabled?
	 * Existing code did not...
	 */
	size = VTCFG_R_CFG1 + vs->vs_vc->vc_cfgsize;
	pci_emul_alloc_bar(vs->vs_pi, barnum, PCIBAR_IO, size);
}

/*
 * Initialize MSI-X vector capabilities if we're to use MSI-X,
 * or MSI capabilities if not.
 *
 * We assume we want one MSI-X vector per queue, here, plus one
 * for the config vec.
 */
int
vi_intr_init(struct virtio_softc *vs, int barnum, int use_msix)
{
	int nvec;

	if (use_msix) {
		vs->vs_flags |= VIRTIO_USE_MSIX;
		VS_LOCK(vs);
		vi_reset_dev(vs); /* set all vectors to NO_VECTOR */
		VS_UNLOCK(vs);
		nvec = vs->vs_vc->vc_nvq + 1;
		if (pci_emul_add_msixcap(vs->vs_pi, nvec, barnum))
			return (1);
	} else
		vs->vs_flags &= ~VIRTIO_USE_MSIX;

	/* Only 1 MSI vector for bhyve */
	pci_emul_add_msicap(vs->vs_pi, 1);

	/* Legacy interrupts are mandatory for virtio devices */
	pci_lintr_request(vs->vs_pi);

	return (0);
}

/*
 * Initialize the currently-selected virtio queue (vs->vs_curq).
 * The guest just gave us a page frame number, from which we can
 * calculate the addresses of the queue.
 */
void
vi_vq_init(struct virtio_softc *vs, uint32_t pfn)
{
	struct vqueue_info *vq;
	uint64_t phys;
	size_t size;
	char *base;

	vq = &vs->vs_queues[vs->vs_curq];
	vq->vq_pfn = pfn;
	phys = (uint64_t)pfn << VRING_PFN;
	size = vring_size(vq->vq_qsize);
	base = paddr_guest2host(vs->vs_pi->pi_vmctx, phys, size);

	/* First page(s) are descriptors... */
	vq->vq_desc = (struct virtio_desc *)base;
	base += vq->vq_qsize * sizeof(struct virtio_desc);

	/* ... immediately followed by "avail" ring (entirely uint16_t's) */
	vq->vq_avail = (struct vring_avail *)base;
	base += (2 + vq->vq_qsize + 1) * sizeof(uint16_t);

	/* Then it's rounded up to the next page... */
	base = (char *)roundup2((uintptr_t)base, VRING_ALIGN);

	/* ... and the last page(s) are the used ring. */
	vq->vq_used = (struct vring_used *)base;

	/* Mark queue as allocated, and start at 0 when we use it. */
	vq->vq_flags = VQ_ALLOC;
	vq->vq_last_avail = 0;
	vq->vq_save_used = 0;
}

/*
 * Helper inline for vq_getchain(): record the i'th "real"
 * descriptor.
 */
static inline void
_vq_record(int i, volatile struct virtio_desc *vd, struct vmctx *ctx,
	   struct iovec *iov, int n_iov, uint16_t *flags) {

	if (i >= n_iov)
		return;
	iov[i].iov_base = paddr_guest2host(ctx, vd->vd_addr, vd->vd_len);
	iov[i].iov_len = vd->vd_len;
	if (flags != NULL)
		flags[i] = vd->vd_flags;
}
#define	VQ_MAX_DESCRIPTORS	512	/* see below */

/*
 * Examine the chain of descriptors starting at the "next one" to
 * make sure that they describe a sensible request.  If so, return
 * the number of "real" descriptors that would be needed/used in
 * acting on this request.  This may be smaller than the number of
 * available descriptors, e.g., if there are two available but
 * they are two separate requests, this just returns 1.  Or, it
 * may be larger: if there are indirect descriptors involved,
 * there may only be one descriptor available but it may be an
 * indirect pointing to eight more.  We return 8 in this case,
 * i.e., we do not count the indirect descriptors, only the "real"
 * ones.
 *
 * Basically, this vets the vd_flags and vd_next field of each
 * descriptor and tells you how many are involved.  Since some may
 * be indirect, this also needs the vmctx (in the pci_devinst
 * at vs->vs_pi) so that it can find indirect descriptors.
 *
 * As we process each descriptor, we copy and adjust it (guest to
 * host address wise, also using the vmtctx) into the given iov[]
 * array (of the given size).  If the array overflows, we stop
 * placing values into the array but keep processing descriptors,
 * up to VQ_MAX_DESCRIPTORS, before giving up and returning -1.
 * So you, the caller, must not assume that iov[] is as big as the
 * return value (you can process the same thing twice to allocate
 * a larger iov array if needed, or supply a zero length to find
 * out how much space is needed).
 *
 * If you want to verify the WRITE flag on each descriptor, pass a
 * non-NULL "flags" pointer to an array of "uint16_t" of the same size
 * as n_iov and we'll copy each vd_flags field after unwinding any
 * indirects.
 *
 * If some descriptor(s) are invalid, this prints a diagnostic message
 * and returns -1.  If no descriptors are ready now it simply returns 0.
 *
 * You are assumed to have done a vq_ring_ready() if needed (note
 * that vq_has_descs() does one).
 */
int
vq_getchain(struct vqueue_info *vq, uint16_t *pidx,
	    struct iovec *iov, int n_iov, uint16_t *flags)
{
	int i;
	u_int ndesc, n_indir;
	u_int idx, next;
	volatile struct virtio_desc *vdir, *vindir, *vp;
	struct vmctx *ctx;
	struct virtio_softc *vs;
	const char *name;

	vs = vq->vq_vs;
	name = vs->vs_vc->vc_name;

	/*
	 * Note: it's the responsibility of the guest not to
	 * update vq->vq_avail->va_idx until all of the descriptors
         * the guest has written are valid (including all their
         * vd_next fields and vd_flags).
	 *
	 * Compute (last_avail - va_idx) in integers mod 2**16.  This is
	 * the number of descriptors the device has made available
	 * since the last time we updated vq->vq_last_avail.
	 *
	 * We just need to do the subtraction as an unsigned int,
	 * then trim off excess bits.
	 */
	idx = vq->vq_last_avail;
	ndesc = (uint16_t)((u_int)vq->vq_avail->va_idx - idx);
	if (ndesc == 0)
		return (0);
	if (ndesc > vq->vq_qsize) {
		/* XXX need better way to diagnose issues */
		fprintf(stderr,
		    "%s: ndesc (%u) out of range, driver confused?\r\n",
		    name, (u_int)ndesc);
		return (-1);
	}

	/*
	 * Now count/parse "involved" descriptors starting from
	 * the head of the chain.
	 *
	 * To prevent loops, we could be more complicated and
	 * check whether we're re-visiting a previously visited
	 * index, but we just abort if the count gets excessive.
	 */
	ctx = vs->vs_pi->pi_vmctx;
	*pidx = next = vq->vq_avail->va_ring[idx & (vq->vq_qsize - 1)];
	vq->vq_last_avail++;
	for (i = 0; i < VQ_MAX_DESCRIPTORS; next = vdir->vd_next) {
		if (next >= vq->vq_qsize) {
			fprintf(stderr,
			    "%s: descriptor index %u out of range, "
			    "driver confused?\r\n",
			    name, next);
			return (-1);
		}
		vdir = &vq->vq_desc[next];
		if ((vdir->vd_flags & VRING_DESC_F_INDIRECT) == 0) {
			_vq_record(i, vdir, ctx, iov, n_iov, flags);
			i++;
		} else if ((vs->vs_vc->vc_hv_caps &
		    VIRTIO_RING_F_INDIRECT_DESC) == 0) {
			fprintf(stderr,
			    "%s: descriptor has forbidden INDIRECT flag, "
			    "driver confused?\r\n",
			    name);
			return (-1);
		} else {
			n_indir = vdir->vd_len / 16;
			if ((vdir->vd_len & 0xf) || n_indir == 0) {
				fprintf(stderr,
				    "%s: invalid indir len 0x%x, "
				    "driver confused?\r\n",
				    name, (u_int)vdir->vd_len);
				return (-1);
			}
			vindir = paddr_guest2host(ctx,
			    vdir->vd_addr, vdir->vd_len);
			/*
			 * Indirects start at the 0th, then follow
			 * their own embedded "next"s until those run
			 * out.  Each one's indirect flag must be off
			 * (we don't really have to check, could just
			 * ignore errors...).
			 */
			next = 0;
			for (;;) {
				vp = &vindir[next];
				if (vp->vd_flags & VRING_DESC_F_INDIRECT) {
					fprintf(stderr,
					    "%s: indirect desc has INDIR flag,"
					    " driver confused?\r\n",
					    name);
					return (-1);
				}
				_vq_record(i, vp, ctx, iov, n_iov, flags);
				if (++i > VQ_MAX_DESCRIPTORS)
					goto loopy;
				if ((vp->vd_flags & VRING_DESC_F_NEXT) == 0)
					break;
				next = vp->vd_next;
				if (next >= n_indir) {
					fprintf(stderr,
					    "%s: invalid next %u > %u, "
					    "driver confused?\r\n",
					    name, (u_int)next, n_indir);
					return (-1);
				}
			}
		}
		if ((vdir->vd_flags & VRING_DESC_F_NEXT) == 0)
			return (i);
	}
loopy:
	fprintf(stderr,
	    "%s: descriptor loop? count > %d - driver confused?\r\n",
	    name, i);
	return (-1);
}

/*
 * Return the currently-first request chain back to the available queue.
 *
 * (This chain is the one you handled when you called vq_getchain()
 * and used its positive return value.)
 */
void
vq_retchain(struct vqueue_info *vq)
{

	vq->vq_last_avail--;
}

/*
 * Return specified request chain to the guest, setting its I/O length
 * to the provided value.
 *
 * (This chain is the one you handled when you called vq_getchain()
 * and used its positive return value.)
 */
void
vq_relchain(struct vqueue_info *vq, uint16_t idx, uint32_t iolen)
{
	uint16_t uidx, mask;
	volatile struct vring_used *vuh;
	volatile struct virtio_used *vue;

	/*
	 * Notes:
	 *  - mask is N-1 where N is a power of 2 so computes x % N
	 *  - vuh points to the "used" data shared with guest
	 *  - vue points to the "used" ring entry we want to update
	 *  - head is the same value we compute in vq_iovecs().
	 *
	 * (I apologize for the two fields named vu_idx; the
	 * virtio spec calls the one that vue points to, "id"...)
	 */
	mask = vq->vq_qsize - 1;
	vuh = vq->vq_used;

	uidx = vuh->vu_idx;
	vue = &vuh->vu_ring[uidx++ & mask];
	vue->vu_idx = idx;
	vue->vu_tlen = iolen;
	vuh->vu_idx = uidx;
}

/*
 * Driver has finished processing "available" chains and calling
 * vq_relchain on each one.  If driver used all the available
 * chains, used_all should be set.
 *
 * If the "used" index moved we may need to inform the guest, i.e.,
 * deliver an interrupt.  Even if the used index did NOT move we
 * may need to deliver an interrupt, if the avail ring is empty and
 * we are supposed to interrupt on empty.
 *
 * Note that used_all_avail is provided by the caller because it's
 * a snapshot of the ring state when he decided to finish interrupt
 * processing -- it's possible that descriptors became available after
 * that point.  (It's also typically a constant 1/True as well.)
 */
void
vq_endchains(struct vqueue_info *vq, int used_all_avail)
{
	struct virtio_softc *vs;
	uint16_t event_idx, new_idx, old_idx;
	int intr;

	/*
	 * Interrupt generation: if we're using EVENT_IDX,
	 * interrupt if we've crossed the event threshold.
	 * Otherwise interrupt is generated if we added "used" entries,
	 * but suppressed by VRING_AVAIL_F_NO_INTERRUPT.
	 *
	 * In any case, though, if NOTIFY_ON_EMPTY is set and the
	 * entire avail was processed, we need to interrupt always.
	 */
	vs = vq->vq_vs;
	old_idx = vq->vq_save_used;
	vq->vq_save_used = new_idx = vq->vq_used->vu_idx;
	if (used_all_avail &&
	    (vs->vs_negotiated_caps & VIRTIO_F_NOTIFY_ON_EMPTY))
		intr = 1;
	else if (vs->vs_negotiated_caps & VIRTIO_RING_F_EVENT_IDX) {
		event_idx = VQ_USED_EVENT_IDX(vq);
		/*
		 * This calculation is per docs and the kernel
		 * (see src/sys/dev/virtio/virtio_ring.h).
		 */
		intr = (uint16_t)(new_idx - event_idx - 1) <
			(uint16_t)(new_idx - old_idx);
	} else {
		intr = new_idx != old_idx &&
		    !(vq->vq_avail->va_flags & VRING_AVAIL_F_NO_INTERRUPT);
	}
	if (intr)
		vq_interrupt(vs, vq);
}

/* Note: these are in sorted order to make for a fast search */
static struct config_reg {
	uint16_t	cr_offset;	/* register offset */
	uint8_t		cr_size;	/* size (bytes) */
	uint8_t		cr_ro;		/* true => reg is read only */
	const char	*cr_name;	/* name of reg */
} config_regs[] = {
	{ VTCFG_R_HOSTCAP,	4, 1, "HOSTCAP" },
	{ VTCFG_R_GUESTCAP,	4, 0, "GUESTCAP" },
	{ VTCFG_R_PFN,		4, 0, "PFN" },
	{ VTCFG_R_QNUM,		2, 1, "QNUM" },
	{ VTCFG_R_QSEL,		2, 0, "QSEL" },
	{ VTCFG_R_QNOTIFY,	2, 0, "QNOTIFY" },
	{ VTCFG_R_STATUS,	1, 0, "STATUS" },
	{ VTCFG_R_ISR,		1, 0, "ISR" },
	{ VTCFG_R_CFGVEC,	2, 0, "CFGVEC" },
	{ VTCFG_R_QVEC,		2, 0, "QVEC" },
};

static inline struct config_reg *
vi_find_cr(int offset) {
	u_int hi, lo, mid;
	struct config_reg *cr;

	lo = 0;
	hi = sizeof(config_regs) / sizeof(*config_regs) - 1;
	while (hi >= lo) {
		mid = (hi + lo) >> 1;
		cr = &config_regs[mid];
		if (cr->cr_offset == offset)
			return (cr);
		if (cr->cr_offset < offset)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return (NULL);
}

/*
 * Handle pci config space reads.
 * If it's to the MSI-X info, do that.
 * If it's part of the virtio standard stuff, do that.
 * Otherwise dispatch to the actual driver.
 */
uint64_t
vi_pci_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	    int baridx, uint64_t offset, int size)
{
	struct virtio_softc *vs = pi->pi_arg;
	struct virtio_consts *vc;
	struct config_reg *cr;
	uint64_t virtio_config_size, max;
	const char *name;
	uint32_t newoff;
	uint32_t value;
	int error;

	if (vs->vs_flags & VIRTIO_USE_MSIX) {
		if (baridx == pci_msix_table_bar(pi) ||
		    baridx == pci_msix_pba_bar(pi)) {
			return (pci_emul_msix_tread(pi, offset, size));
		}
	}

	/* XXX probably should do something better than just assert() */
	assert(baridx == 0);

	if (vs->vs_mtx)
		pthread_mutex_lock(vs->vs_mtx);

	vc = vs->vs_vc;
	name = vc->vc_name;
	value = size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;

	if (size != 1 && size != 2 && size != 4)
		goto bad;

	if (pci_msix_enabled(pi))
		virtio_config_size = VTCFG_R_CFG1;
	else
		virtio_config_size = VTCFG_R_CFG0;

	if (offset >= virtio_config_size) {
		/*
		 * Subtract off the standard size (including MSI-X
		 * registers if enabled) and dispatch to underlying driver.
		 * If that fails, fall into general code.
		 */
		newoff = offset - virtio_config_size;
		max = vc->vc_cfgsize ? vc->vc_cfgsize : 0x100000000;
		if (newoff + size > max)
			goto bad;
		error = (*vc->vc_cfgread)(DEV_SOFTC(vs), newoff, size, &value);
		if (!error)
			goto done;
	}

bad:
	cr = vi_find_cr(offset);
	if (cr == NULL || cr->cr_size != size) {
		if (cr != NULL) {
			/* offset must be OK, so size must be bad */
			fprintf(stderr,
			    "%s: read from %s: bad size %d\r\n",
			    name, cr->cr_name, size);
		} else {
			fprintf(stderr,
			    "%s: read from bad offset/size %jd/%d\r\n",
			    name, (uintmax_t)offset, size);
		}
		goto done;
	}

	switch (offset) {
	case VTCFG_R_HOSTCAP:
		value = vc->vc_hv_caps;
		break;
	case VTCFG_R_GUESTCAP:
		value = vs->vs_negotiated_caps;
		break;
	case VTCFG_R_PFN:
		if (vs->vs_curq < vc->vc_nvq)
			value = vs->vs_queues[vs->vs_curq].vq_pfn;
		break;
	case VTCFG_R_QNUM:
		value = vs->vs_curq < vc->vc_nvq ?
		    vs->vs_queues[vs->vs_curq].vq_qsize : 0;
		break;
	case VTCFG_R_QSEL:
		value = vs->vs_curq;
		break;
	case VTCFG_R_QNOTIFY:
		value = 0;	/* XXX */
		break;
	case VTCFG_R_STATUS:
		value = vs->vs_status;
		break;
	case VTCFG_R_ISR:
		value = vs->vs_isr;
		vs->vs_isr = 0;		/* a read clears this flag */
		if (value)
			pci_lintr_deassert(pi);
		break;
	case VTCFG_R_CFGVEC:
		value = vs->vs_msix_cfg_idx;
		break;
	case VTCFG_R_QVEC:
		value = vs->vs_curq < vc->vc_nvq ?
		    vs->vs_queues[vs->vs_curq].vq_msix_idx :
		    VIRTIO_MSI_NO_VECTOR;
		break;
	}
done:
	if (vs->vs_mtx)
		pthread_mutex_unlock(vs->vs_mtx);
	return (value);
}

/*
 * Handle pci config space writes.
 * If it's to the MSI-X info, do that.
 * If it's part of the virtio standard stuff, do that.
 * Otherwise dispatch to the actual driver.
 */
void
vi_pci_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
	     int baridx, uint64_t offset, int size, uint64_t value)
{
	struct virtio_softc *vs = pi->pi_arg;
	struct vqueue_info *vq;
	struct virtio_consts *vc;
	struct config_reg *cr;
	uint64_t virtio_config_size, max;
	const char *name;
	uint32_t newoff;
	int error;

	if (vs->vs_flags & VIRTIO_USE_MSIX) {
		if (baridx == pci_msix_table_bar(pi) ||
		    baridx == pci_msix_pba_bar(pi)) {
			pci_emul_msix_twrite(pi, offset, size, value);
			return;
		}
	}

	/* XXX probably should do something better than just assert() */
	assert(baridx == 0);

	if (vs->vs_mtx)
		pthread_mutex_lock(vs->vs_mtx);

	vc = vs->vs_vc;
	name = vc->vc_name;

	if (size != 1 && size != 2 && size != 4)
		goto bad;

	if (pci_msix_enabled(pi))
		virtio_config_size = VTCFG_R_CFG1;
	else
		virtio_config_size = VTCFG_R_CFG0;

	if (offset >= virtio_config_size) {
		/*
		 * Subtract off the standard size (including MSI-X
		 * registers if enabled) and dispatch to underlying driver.
		 */
		newoff = offset - virtio_config_size;
		max = vc->vc_cfgsize ? vc->vc_cfgsize : 0x100000000;
		if (newoff + size > max)
			goto bad;
		error = (*vc->vc_cfgwrite)(DEV_SOFTC(vs), newoff, size, value);
		if (!error)
			goto done;
	}

bad:
	cr = vi_find_cr(offset);
	if (cr == NULL || cr->cr_size != size || cr->cr_ro) {
		if (cr != NULL) {
			/* offset must be OK, wrong size and/or reg is R/O */
			if (cr->cr_size != size)
				fprintf(stderr,
				    "%s: write to %s: bad size %d\r\n",
				    name, cr->cr_name, size);
			if (cr->cr_ro)
				fprintf(stderr,
				    "%s: write to read-only reg %s\r\n",
				    name, cr->cr_name);
		} else {
			fprintf(stderr,
			    "%s: write to bad offset/size %jd/%d\r\n",
			    name, (uintmax_t)offset, size);
		}
		goto done;
	}

	switch (offset) {
	case VTCFG_R_GUESTCAP:
		vs->vs_negotiated_caps = value & vc->vc_hv_caps;
		if (vc->vc_apply_features)
			(*vc->vc_apply_features)(DEV_SOFTC(vs),
			    vs->vs_negotiated_caps);
		break;
	case VTCFG_R_PFN:
		if (vs->vs_curq >= vc->vc_nvq)
			goto bad_qindex;
		vi_vq_init(vs, value);
		break;
	case VTCFG_R_QSEL:
		/*
		 * Note that the guest is allowed to select an
		 * invalid queue; we just need to return a QNUM
		 * of 0 while the bad queue is selected.
		 */
		vs->vs_curq = value;
		break;
	case VTCFG_R_QNOTIFY:
		if (value >= vc->vc_nvq) {
			fprintf(stderr, "%s: queue %d notify out of range\r\n",
				name, (int)value);
			goto done;
		}
		vq = &vs->vs_queues[value];
		if (vq->vq_notify)
			(*vq->vq_notify)(DEV_SOFTC(vs), vq);
		else if (vc->vc_qnotify)
			(*vc->vc_qnotify)(DEV_SOFTC(vs), vq);
		else
			fprintf(stderr,
			    "%s: qnotify queue %d: missing vq/vc notify\r\n",
				name, (int)value);
		break;
	case VTCFG_R_STATUS:
		vs->vs_status = value;
		if (value == 0)
			(*vc->vc_reset)(DEV_SOFTC(vs));
		break;
	case VTCFG_R_CFGVEC:
		vs->vs_msix_cfg_idx = value;
		break;
	case VTCFG_R_QVEC:
		if (vs->vs_curq >= vc->vc_nvq)
			goto bad_qindex;
		vq = &vs->vs_queues[vs->vs_curq];
		vq->vq_msix_idx = value;
		break;
	}
	goto done;

bad_qindex:
	fprintf(stderr,
	    "%s: write config reg %s: curq %d >= max %d\r\n",
	    name, cr->cr_name, vs->vs_curq, vc->vc_nvq);
done:
	if (vs->vs_mtx)
		pthread_mutex_unlock(vs->vs_mtx);
}
