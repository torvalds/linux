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
 *
 * $FreeBSD$
 */

#ifndef	_VIRTIO_H_
#define	_VIRTIO_H_

/*
 * These are derived from several virtio specifications.
 *
 * Some useful links:
 *    https://github.com/rustyrussell/virtio-spec
 *    http://people.redhat.com/pbonzini/virtio-spec.pdf
 */

/*
 * A virtual device has zero or more "virtual queues" (virtqueue).
 * Each virtqueue uses at least two 4096-byte pages, laid out thus:
 *
 *      +-----------------------------------------------+
 *      |    "desc":  <N> descriptors, 16 bytes each    |
 *      |   -----------------------------------------   |
 *      |   "avail":   2 uint16; <N> uint16; 1 uint16   |
 *      |   -----------------------------------------   |
 *      |              pad to 4k boundary               |
 *      +-----------------------------------------------+
 *      |   "used": 2 x uint16; <N> elems; 1 uint16     |
 *      |   -----------------------------------------   |
 *      |              pad to 4k boundary               |
 *      +-----------------------------------------------+
 *
 * The number <N> that appears here is always a power of two and is
 * limited to no more than 32768 (as it must fit in a 16-bit field).
 * If <N> is sufficiently large, the above will occupy more than
 * two pages.  In any case, all pages must be physically contiguous
 * within the guest's physical address space.
 *
 * The <N> 16-byte "desc" descriptors consist of a 64-bit guest
 * physical address <addr>, a 32-bit length <len>, a 16-bit
 * <flags>, and a 16-bit <next> field (all in guest byte order).
 *
 * There are three flags that may be set :
 *	NEXT    descriptor is chained, so use its "next" field
 *	WRITE   descriptor is for host to write into guest RAM
 *		(else host is to read from guest RAM)
 *	INDIRECT   descriptor address field is (guest physical)
 *		address of a linear array of descriptors
 *
 * Unless INDIRECT is set, <len> is the number of bytes that may
 * be read/written from guest physical address <addr>.  If
 * INDIRECT is set, WRITE is ignored and <len> provides the length
 * of the indirect descriptors (and <len> must be a multiple of
 * 16).  Note that NEXT may still be set in the main descriptor
 * pointing to the indirect, and should be set in each indirect
 * descriptor that uses the next descriptor (these should generally
 * be numbered sequentially).  However, INDIRECT must not be set
 * in the indirect descriptors.  Upon reaching an indirect descriptor
 * without a NEXT bit, control returns to the direct descriptors.
 *
 * Except inside an indirect, each <next> value must be in the
 * range [0 .. N) (i.e., the half-open interval).  (Inside an
 * indirect, each <next> must be in the range [0 .. <len>/16).)
 *
 * The "avail" data structures reside in the same pages as the
 * "desc" structures since both together are used by the device to
 * pass information to the hypervisor's virtual driver.  These
 * begin with a 16-bit <flags> field and 16-bit index <idx>, then
 * have <N> 16-bit <ring> values, followed by one final 16-bit
 * field <used_event>.  The <N> <ring> entries are simply indices
 * indices into the descriptor ring (and thus must meet the same
 * constraints as each <next> value).  However, <idx> is counted
 * up from 0 (initially) and simply wraps around after 65535; it
 * is taken mod <N> to find the next available entry.
 *
 * The "used" ring occupies a separate page or pages, and contains
 * values written from the virtual driver back to the guest OS.
 * This begins with a 16-bit <flags> and 16-bit <idx>, then there
 * are <N> "vring_used" elements, followed by a 16-bit <avail_event>.
 * The <N> "vring_used" elements consist of a 32-bit <id> and a
 * 32-bit <len> (vu_tlen below).  The <id> is simply the index of
 * the head of a descriptor chain the guest made available
 * earlier, and the <len> is the number of bytes actually written,
 * e.g., in the case of a network driver that provided a large
 * receive buffer but received only a small amount of data.
 *
 * The two event fields, <used_event> and <avail_event>, in the
 * avail and used rings (respectively -- note the reversal!), are
 * always provided, but are used only if the virtual device
 * negotiates the VIRTIO_RING_F_EVENT_IDX feature during feature
 * negotiation.  Similarly, both rings provide a flag --
 * VRING_AVAIL_F_NO_INTERRUPT and VRING_USED_F_NO_NOTIFY -- in
 * their <flags> field, indicating that the guest does not need an
 * interrupt, or that the hypervisor driver does not need a
 * notify, when descriptors are added to the corresponding ring.
 * (These are provided only for interrupt optimization and need
 * not be implemented.)
 */
#define VRING_ALIGN	4096

#define VRING_DESC_F_NEXT	(1 << 0)
#define VRING_DESC_F_WRITE	(1 << 1)
#define VRING_DESC_F_INDIRECT	(1 << 2)

struct virtio_desc {			/* AKA vring_desc */
	uint64_t	vd_addr;	/* guest physical address */
	uint32_t	vd_len;		/* length of scatter/gather seg */
	uint16_t	vd_flags;	/* VRING_F_DESC_* */
	uint16_t	vd_next;	/* next desc if F_NEXT */
} __packed;

struct virtio_used {			/* AKA vring_used_elem */
	uint32_t	vu_idx;		/* head of used descriptor chain */
	uint32_t	vu_tlen;	/* length written-to */
} __packed;

#define VRING_AVAIL_F_NO_INTERRUPT   1

struct vring_avail {
	uint16_t	va_flags;	/* VRING_AVAIL_F_* */
	uint16_t	va_idx;		/* counts to 65535, then cycles */
	uint16_t	va_ring[];	/* size N, reported in QNUM value */
/*	uint16_t	va_used_event;	-- after N ring entries */
} __packed;

#define	VRING_USED_F_NO_NOTIFY		1
struct vring_used {
	uint16_t	vu_flags;	/* VRING_USED_F_* */
	uint16_t	vu_idx;		/* counts to 65535, then cycles */
	struct virtio_used vu_ring[];	/* size N */
/*	uint16_t	vu_avail_event;	-- after N ring entries */
} __packed;

/*
 * The address of any given virtual queue is determined by a single
 * Page Frame Number register.  The guest writes the PFN into the
 * PCI config space.  However, a device that has two or more
 * virtqueues can have a different PFN, and size, for each queue.
 * The number of queues is determinable via the PCI config space
 * VTCFG_R_QSEL register.  Writes to QSEL select the queue: 0 means
 * queue #0, 1 means queue#1, etc.  Once a queue is selected, the
 * remaining PFN and QNUM registers refer to that queue.
 *
 * QNUM is a read-only register containing a nonzero power of two
 * that indicates the (hypervisor's) queue size.  Or, if reading it
 * produces zero, the hypervisor does not have a corresponding
 * queue.  (The number of possible queues depends on the virtual
 * device.  The block device has just one; the network device
 * provides either two -- 0 = receive, 1 = transmit -- or three,
 * with 2 = control.)
 *
 * PFN is a read/write register giving the physical page address of
 * the virtqueue in guest memory (the guest must allocate enough space
 * based on the hypervisor's provided QNUM).
 *
 * QNOTIFY is effectively write-only: when the guest writes a queue
 * number to the register, the hypervisor should scan the specified
 * virtqueue. (Reading QNOTIFY currently always gets 0).
 */

/*
 * PFN register shift amount
 */
#define	VRING_PFN		12

/*
 * Virtio device types
 *
 * XXX Should really be merged with <dev/virtio/virtio.h> defines
 */
#define	VIRTIO_TYPE_NET		1
#define	VIRTIO_TYPE_BLOCK	2
#define	VIRTIO_TYPE_CONSOLE	3
#define	VIRTIO_TYPE_ENTROPY	4
#define	VIRTIO_TYPE_BALLOON	5
#define	VIRTIO_TYPE_IOMEMORY	6
#define	VIRTIO_TYPE_RPMSG	7
#define	VIRTIO_TYPE_SCSI	8
#define	VIRTIO_TYPE_9P		9

/* experimental IDs start at 65535 and work down */

/*
 * PCI vendor/device IDs
 */
#define	VIRTIO_VENDOR		0x1AF4
#define	VIRTIO_DEV_NET		0x1000
#define	VIRTIO_DEV_BLOCK	0x1001
#define	VIRTIO_DEV_CONSOLE	0x1003
#define	VIRTIO_DEV_RANDOM	0x1005
#define	VIRTIO_DEV_SCSI		0x1008

/*
 * PCI config space constants.
 *
 * If MSI-X is enabled, the ISR register is generally not used,
 * and the configuration vector and queue vector appear at offsets
 * 20 and 22 with the remaining configuration registers at 24.
 * If MSI-X is not enabled, those two registers disappear and
 * the remaining configuration registers start at offset 20.
 */
#define	VTCFG_R_HOSTCAP		0
#define	VTCFG_R_GUESTCAP	4
#define	VTCFG_R_PFN		8
#define	VTCFG_R_QNUM		12
#define	VTCFG_R_QSEL		14
#define	VTCFG_R_QNOTIFY		16
#define	VTCFG_R_STATUS		18
#define	VTCFG_R_ISR		19
#define	VTCFG_R_CFGVEC		20
#define	VTCFG_R_QVEC		22
#define	VTCFG_R_CFG0		20	/* No MSI-X */
#define	VTCFG_R_CFG1		24	/* With MSI-X */
#define	VTCFG_R_MSIX		20

/*
 * Bits in VTCFG_R_STATUS.  Guests need not actually set any of these,
 * but a guest writing 0 to this register means "please reset".
 */
#define	VTCFG_STATUS_ACK	0x01	/* guest OS has acknowledged dev */
#define	VTCFG_STATUS_DRIVER	0x02	/* guest OS driver is loaded */
#define	VTCFG_STATUS_DRIVER_OK	0x04	/* guest OS driver ready */
#define	VTCFG_STATUS_FAILED	0x80	/* guest has given up on this dev */

/*
 * Bits in VTCFG_R_ISR.  These apply only if not using MSI-X.
 *
 * (We don't [yet?] ever use CONF_CHANGED.)
 */
#define	VTCFG_ISR_QUEUES	0x01	/* re-scan queues */
#define	VTCFG_ISR_CONF_CHANGED	0x80	/* configuration changed */

#define	VIRTIO_MSI_NO_VECTOR	0xFFFF

/*
 * Feature flags.
 * Note: bits 0 through 23 are reserved to each device type.
 */
#define	VIRTIO_F_NOTIFY_ON_EMPTY	(1 << 24)
#define	VIRTIO_RING_F_INDIRECT_DESC	(1 << 28)
#define	VIRTIO_RING_F_EVENT_IDX		(1 << 29)

/* From section 2.3, "Virtqueue Configuration", of the virtio specification */
static inline size_t
vring_size(u_int qsz)
{
	size_t size;

	/* constant 3 below = va_flags, va_idx, va_used_event */
	size = sizeof(struct virtio_desc) * qsz + sizeof(uint16_t) * (3 + qsz);
	size = roundup2(size, VRING_ALIGN);

	/* constant 3 below = vu_flags, vu_idx, vu_avail_event */
	size += sizeof(uint16_t) * 3 + sizeof(struct virtio_used) * qsz;
	size = roundup2(size, VRING_ALIGN);

	return (size);
}

struct vmctx;
struct pci_devinst;
struct vqueue_info;

/*
 * A virtual device, with some number (possibly 0) of virtual
 * queues and some size (possibly 0) of configuration-space
 * registers private to the device.  The virtio_softc should come
 * at the front of each "derived class", so that a pointer to the
 * virtio_softc is also a pointer to the more specific, derived-
 * from-virtio driver's softc.
 *
 * Note: inside each hypervisor virtio driver, changes to these
 * data structures must be locked against other threads, if any.
 * Except for PCI config space register read/write, we assume each
 * driver does the required locking, but we need a pointer to the
 * lock (if there is one) for PCI config space read/write ops.
 *
 * When the guest reads or writes the device's config space, the
 * generic layer checks for operations on the special registers
 * described above.  If the offset of the register(s) being read
 * or written is past the CFG area (CFG0 or CFG1), the request is
 * passed on to the virtual device, after subtracting off the
 * generic-layer size.  (So, drivers can just use the offset as
 * an offset into "struct config", for instance.)
 *
 * (The virtio layer also makes sure that the read or write is to/
 * from a "good" config offset, hence vc_cfgsize, and on BAR #0.
 * However, the driver must verify the read or write size and offset
 * and that no one is writing a readonly register.)
 *
 * The BROKED flag ("this thing done gone and broked") is for future
 * use.
 */
#define	VIRTIO_USE_MSIX		0x01
#define	VIRTIO_EVENT_IDX	0x02	/* use the event-index values */
#define	VIRTIO_BROKED		0x08	/* ??? */

struct virtio_softc {
	struct virtio_consts *vs_vc;	/* constants (see below) */
	int	vs_flags;		/* VIRTIO_* flags from above */
	pthread_mutex_t *vs_mtx;	/* POSIX mutex, if any */
	struct pci_devinst *vs_pi;	/* PCI device instance */
	uint32_t vs_negotiated_caps;	/* negotiated capabilities */
	struct vqueue_info *vs_queues;	/* one per vc_nvq */
	int	vs_curq;		/* current queue */
	uint8_t	vs_status;		/* value from last status write */
	uint8_t	vs_isr;			/* ISR flags, if not MSI-X */
	uint16_t vs_msix_cfg_idx;	/* MSI-X vector for config event */
};

#define	VS_LOCK(vs)							\
do {									\
	if (vs->vs_mtx)							\
		pthread_mutex_lock(vs->vs_mtx);				\
} while (0)

#define	VS_UNLOCK(vs)							\
do {									\
	if (vs->vs_mtx)							\
		pthread_mutex_unlock(vs->vs_mtx);			\
} while (0)

struct virtio_consts {
	const char *vc_name;		/* name of driver (for diagnostics) */
	int	vc_nvq;			/* number of virtual queues */
	size_t	vc_cfgsize;		/* size of dev-specific config regs */
	void	(*vc_reset)(void *);	/* called on virtual device reset */
	void	(*vc_qnotify)(void *, struct vqueue_info *);
					/* called on QNOTIFY if no VQ notify */
	int	(*vc_cfgread)(void *, int, int, uint32_t *);
					/* called to read config regs */
	int	(*vc_cfgwrite)(void *, int, int, uint32_t);
					/* called to write config regs */
	void    (*vc_apply_features)(void *, uint64_t);
				/* called to apply negotiated features */
	uint64_t vc_hv_caps;		/* hypervisor-provided capabilities */
};

/*
 * Data structure allocated (statically) per virtual queue.
 *
 * Drivers may change vq_qsize after a reset.  When the guest OS
 * requests a device reset, the hypervisor first calls
 * vs->vs_vc->vc_reset(); then the data structure below is
 * reinitialized (for each virtqueue: vs->vs_vc->vc_nvq).
 *
 * The remaining fields should only be fussed-with by the generic
 * code.
 *
 * Note: the addresses of vq_desc, vq_avail, and vq_used are all
 * computable from each other, but it's a lot simpler if we just
 * keep a pointer to each one.  The event indices are similarly
 * (but more easily) computable, and this time we'll compute them:
 * they're just XX_ring[N].
 */
#define	VQ_ALLOC	0x01	/* set once we have a pfn */
#define	VQ_BROKED	0x02	/* ??? */
struct vqueue_info {
	uint16_t vq_qsize;	/* size of this queue (a power of 2) */
	void	(*vq_notify)(void *, struct vqueue_info *);
				/* called instead of vc_notify, if not NULL */

	struct virtio_softc *vq_vs;	/* backpointer to softc */
	uint16_t vq_num;	/* we're the num'th queue in the softc */

	uint16_t vq_flags;	/* flags (see above) */
	uint16_t vq_last_avail;	/* a recent value of vq_avail->va_idx */
	uint16_t vq_save_used;	/* saved vq_used->vu_idx; see vq_endchains */
	uint16_t vq_msix_idx;	/* MSI-X index, or VIRTIO_MSI_NO_VECTOR */

	uint32_t vq_pfn;	/* PFN of virt queue (not shifted!) */

	volatile struct virtio_desc *vq_desc;	/* descriptor array */
	volatile struct vring_avail *vq_avail;	/* the "avail" ring */
	volatile struct vring_used *vq_used;	/* the "used" ring */

};
/* as noted above, these are sort of backwards, name-wise */
#define VQ_AVAIL_EVENT_IDX(vq) \
	(*(volatile uint16_t *)&(vq)->vq_used->vu_ring[(vq)->vq_qsize])
#define VQ_USED_EVENT_IDX(vq) \
	((vq)->vq_avail->va_ring[(vq)->vq_qsize])

/*
 * Is this ring ready for I/O?
 */
static inline int
vq_ring_ready(struct vqueue_info *vq)
{

	return (vq->vq_flags & VQ_ALLOC);
}

/*
 * Are there "available" descriptors?  (This does not count
 * how many, just returns True if there are some.)
 */
static inline int
vq_has_descs(struct vqueue_info *vq)
{

	return (vq_ring_ready(vq) && vq->vq_last_avail !=
	    vq->vq_avail->va_idx);
}

/*
 * Deliver an interrupt to guest on the given virtual queue
 * (if possible, or a generic MSI interrupt if not using MSI-X).
 */
static inline void
vq_interrupt(struct virtio_softc *vs, struct vqueue_info *vq)
{

	if (pci_msix_enabled(vs->vs_pi))
		pci_generate_msix(vs->vs_pi, vq->vq_msix_idx);
	else {
		VS_LOCK(vs);
		vs->vs_isr |= VTCFG_ISR_QUEUES;
		pci_generate_msi(vs->vs_pi, 0);
		pci_lintr_assert(vs->vs_pi);
		VS_UNLOCK(vs);
	}
}

struct iovec;
void	vi_softc_linkup(struct virtio_softc *vs, struct virtio_consts *vc,
			void *dev_softc, struct pci_devinst *pi,
			struct vqueue_info *queues);
int	vi_intr_init(struct virtio_softc *vs, int barnum, int use_msix);
void	vi_reset_dev(struct virtio_softc *);
void	vi_set_io_bar(struct virtio_softc *, int);

int	vq_getchain(struct vqueue_info *vq, uint16_t *pidx,
		    struct iovec *iov, int n_iov, uint16_t *flags);
void	vq_retchain(struct vqueue_info *vq);
void	vq_relchain(struct vqueue_info *vq, uint16_t idx, uint32_t iolen);
void	vq_endchains(struct vqueue_info *vq, int used_all_avail);

uint64_t vi_pci_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		     int baridx, uint64_t offset, int size);
void	vi_pci_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		     int baridx, uint64_t offset, int size, uint64_t value);
#endif	/* _VIRTIO_H_ */
