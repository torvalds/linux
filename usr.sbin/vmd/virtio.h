/*	$OpenBSD: virtio.h,v 1.56 2025/08/02 15:16:18 dv Exp $	*/

/*
 * Copyright (c) 2015 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <dev/pv/virtioreg.h>
#include <dev/pci/virtio_pcireg.h>
#include <net/if_tun.h>

#include <event.h>

#include "vmd.h"
#include "pci.h"

#ifndef _VIRTIO_H_
#define _VIRTIO_H_

#define VIRTQUEUE_ALIGN(n)	(((n)+(VIRTIO_PAGE_SIZE-1))&    \
				    ~(VIRTIO_PAGE_SIZE-1))
#define ALIGNSZ(sz, align)	((sz + align - 1) & ~(align - 1))
#define MIN(a,b)		(((a)<(b))?(a):(b))

#define VIO1_PCI_DEVICE_FEATURE_SELECT					\
	(offsetof(struct virtio_pci_common_cfg, device_feature_select))
#define VIO1_PCI_DEVICE_FEATURE						\
	(offsetof(struct virtio_pci_common_cfg, device_feature))
#define VIO1_PCI_DRIVER_FEATURE_SELECT					\
	(offsetof(struct virtio_pci_common_cfg, driver_feature_select))
#define VIO1_PCI_DRIVER_FEATURE						\
	(offsetof(struct virtio_pci_common_cfg, driver_feature))
#define VIO1_PCI_CONFIG_MSIX_VECTOR					\
	(offsetof(struct virtio_pci_common_cfg, config_msix_vector))
#define VIO1_PCI_NUM_QUEUES						\
	(offsetof(struct virtio_pci_common_cfg, num_queues))
#define VIO1_PCI_DEVICE_STATUS						\
	(offsetof(struct virtio_pci_common_cfg, device_status))
#define VIO1_PCI_CONFIG_GENERATION					\
	(offsetof(struct virtio_pci_common_cfg, config_generation))
#define VIO1_PCI_QUEUE_SELECT						\
	(offsetof(struct virtio_pci_common_cfg, queue_select))
#define VIO1_PCI_QUEUE_SIZE						\
	(offsetof(struct virtio_pci_common_cfg, queue_size))
#define VIO1_PCI_QUEUE_MSIX_VECTOR					\
	(offsetof(struct virtio_pci_common_cfg, queue_msix_vector))
#define VIO1_PCI_QUEUE_ENABLE						\
	(offsetof(struct virtio_pci_common_cfg, queue_enable))
#define VIO1_PCI_QUEUE_NOTIFY_OFF					\
	(offsetof(struct virtio_pci_common_cfg, queue_notify_off))
#define VIO1_PCI_QUEUE_DESC						\
	(offsetof(struct virtio_pci_common_cfg, queue_desc))
#define VIO1_PCI_QUEUE_AVAIL						\
	(offsetof(struct virtio_pci_common_cfg, queue_avail))
#define VIO1_PCI_QUEUE_USED						\
	(offsetof(struct virtio_pci_common_cfg, queue_used))

#define VIO1_CFG_BAR_OFFSET		0x000
#define VIO1_NOTIFY_BAR_OFFSET		0x100
#define VIO1_ISR_BAR_OFFSET		0x200
#define VIO1_DEV_BAR_OFFSET		0x300

/* Queue sizes must be power of two and less than IOV_MAX (1024). */
#define VIRTIO_QUEUE_SIZE_MAX		IOV_MAX
#define VIORND_QUEUE_SIZE_DEFAULT	64
#define VIOBLK_QUEUE_SIZE_DEFAULT	128
#define VIOSCSI_QUEUE_SIZE_DEFAULT	128
#define VIONET_QUEUE_SIZE_DEFAULT	256

#define VIOBLK_SEG_MAX_DEFAULT		(VIOBLK_QUEUE_SIZE_DEFAULT - 2)

/* Virtio network device is backed by tap(4), so inherit limits */
#define VIONET_HARD_MTU		TUNMRU
#define VIONET_MIN_TXLEN	ETHER_HDR_LEN
#define VIONET_MAX_TXLEN	VIONET_HARD_MTU + ETHER_HDR_LEN

/* VMM Control Interface shutdown timeout (in seconds) */
#define VMMCI_TIMEOUT_SHORT	3
#define VMMCI_TIMEOUT_LONG	120

/*
 * All the devices we support have either 1, 2 or 3 virtqueues.
 * No devices currently support VIRTIO_*_F_MQ so values are fixed.
 */
#define VIRTIO_RND_QUEUES	1
#define VIRTIO_BLK_QUEUES	1
#define VIRTIO_NET_QUEUES	2
#define VIRTIO_SCSI_QUEUES	3
#define VIRTIO_VMMCI_QUEUES	0
#define VIRTIO_MAX_QUEUES	3

#define MAXPHYS	(64 * 1024)	/* max raw I/O transfer size */

/*
 * Rename the address config register to be more descriptive.
 */
#define VIRTIO_CONFIG_QUEUE_PFN	VIRTIO_CONFIG_QUEUE_ADDRESS
#define DEVICE_NEEDS_RESET	VIRTIO_CONFIG_DEVICE_STATUS_DEVICE_NEEDS_RESET
#define DESC_WRITABLE(/* struct vring_desc */ x)	\
	(((x)->flags & VRING_DESC_F_WRITE) ? 1 : 0)

struct virtio_pci_common_cap {
	union {
		struct pci_cap pci;
		struct virtio_pci_cap virtio;
		struct virtio_pci_notify_cap notify;
		struct virtio_pci_cfg_cap cfg;
	};
} __packed;

/*
 * VM <-> Device messaging.
 */
struct viodev_msg {
	uint8_t type;
#define VIODEV_MSG_INVALID	0
#define VIODEV_MSG_READY	1
#define VIODEV_MSG_ERROR	2
#define VIODEV_MSG_KICK		3
#define VIODEV_MSG_IO_READ	4
#define VIODEV_MSG_IO_WRITE	5
#define VIODEV_MSG_DUMP		6
#define VIODEV_MSG_SHUTDOWN	7

	uint16_t reg;		/* VirtIO register */
	uint8_t io_sz;		/* IO instruction size */
	uint8_t vcpu;		/* VCPU id */
	uint8_t irq;		/* IRQ number */

	int8_t state;		/* Interrupt state toggle (if any) */
#define INTR_STATE_ASSERT	 1
#define INTR_STATE_NOOP		 0
#define INTR_STATE_DEASSERT	-1

	uint32_t data;		/* Data (if any) */
	uint8_t data_valid;	/* 1 if data field is populated. */
} __packed;

/*
 * Legacy Virtio 0.9 register state.
 */
struct virtio_io_cfg {
	uint32_t device_feature;
	uint32_t guest_feature;
	uint32_t queue_pfn;
	uint16_t queue_select;
	uint16_t queue_size;
	uint16_t queue_notify;
};

struct virtio_backing {
	void  *p;
	ssize_t (*pread)(void *, char *, size_t, off_t);
	ssize_t (*preadv)(void *, struct iovec *, int, off_t);
	ssize_t (*pwrite)(void *, char *, size_t, off_t);
	ssize_t (*pwritev)(void *, struct iovec *, int, off_t);
	void (*close)(void *, int);
};

/*
 * A virtio device can have several virtqs. For example, vionet has one virtq
 * each for transmitting and receiving packets. This struct describes the state
 * of one virtq, such as their address in memory, size, offsets of rings, etc.
 * There is one virtio_vq_info per virtq.
 */
struct virtio_vq_info {
	/* Guest physical address of virtq */
	uint64_t q_gpa;

	/* Host virtual address of virtq */
	void *q_hva;

	/* Queue size: number of queue entries in virtq */
	uint32_t qs;

	/* Queue mask */
	uint32_t mask;

	/*
	 * The offset of the 'available' ring within the virtq located at
	 * guest physical address qa above
	 */
	uint32_t vq_availoffset;

	/*
	 * The offset of the 'used' ring within the virtq located at guest
	 * physical address qa above
	 */
	uint32_t vq_usedoffset;

	/*
	 * The index into a slot of the 'available' ring that a virtio device
	 * can consume next
	 */
	uint16_t last_avail;

	/*
	 * The most recent index into the 'available' ring that a virtio
	 * driver notified to the host.
	 */
	uint16_t notified_avail;

	uint8_t vq_enabled;
};

/*
 * Each virtio driver has a notifyq method where one or more messages
 * are ready to be processed on a given virtq.  As such, various
 * pieces of information are needed to provide ring accounting while
 * processing a given message such as virtq indexes, vring pointers, and
 * vring descriptors.
 */
struct virtio_vq_acct {

	/* index of previous avail vring message */
	uint16_t idx;

	/* index of current message containing the request */
	uint16_t req_idx;

	/* index of current message containing the response */
	uint16_t resp_idx;

	/* vring descriptor pointer */
	struct vring_desc *desc;

	/* vring descriptor pointer for request header and data */
	struct vring_desc *req_desc;

	/* vring descriptor pointer for response header and data */
	struct vring_desc *resp_desc;

	/* pointer to the available vring */
	struct vring_avail *avail;

	/* pointer to the used vring */
	struct vring_used *used;
};

struct vioblk_dev {
	struct virtio_backing file;
	int disk_fd[VM_MAX_BASE_PER_DISK];	/* fds for disk image(s) */

	uint8_t ndisk_fd;	/* number of valid disk fds */
	uint64_t capacity;	/* size in 512 byte sectors */
	uint32_t seg_max;	/* maximum number of segments */

	unsigned int idx;
};

/* vioscsi will use at least 3 queues - 5.6.2 Virtqueues
 * Current implementation will use 3
 * 0 - control
 * 1 - event
 * 2 - requests
 */
struct vioscsi_dev {
	struct virtio_backing file;

	int locked;		/* is the device locked? */
	uint64_t sz;		/* size of iso file in bytes */
	uint64_t lba;		/* last block address read */
	uint64_t n_blocks;	/* number of blocks represented in iso */
	uint32_t max_xfer;
};

struct vionet_dev {
	int data_fd;		/* fd for our tap device */

	uint8_t mac[6];
	uint8_t hostmac[6];
	int lockedmac;
	int local;
	int pxeboot;
	struct local_prefix local_prefix;

	unsigned int idx;
};

struct virtio_net_hdr {
	uint8_t flags;
	uint8_t gso_type;
	uint16_t hdr_len;
	uint16_t gso_size;
	uint16_t csum_start;
	uint16_t csum_offset;
	uint16_t num_buffers;

	/*
	 * The following fields exist only if VIRTIO_NET_F_HASH_REPORT
	 * is negotiated.
	 */
	/*
	uint32_t hash_value;
	uint16_t hash_report;
	uint16_t padding_reserved;
	*/
};

enum vmmci_cmd {
	VMMCI_NONE = 0,
	VMMCI_SHUTDOWN,
	VMMCI_REBOOT,
	VMMCI_SYNCRTC,
};

struct vmmci_dev {
	struct event timeout;
	struct timeval time;
	enum vmmci_cmd cmd;

	pthread_mutex_t mutex;
	struct vm_dev_pipe dev_pipe;
};

/* XXX to be removed once vioscsi is adapted to vectorized io. */
struct ioinfo {
	uint8_t *buf;
	ssize_t len;
	off_t offset;
};

struct virtio_dev {
	uint16_t device_id;			/* Virtio device id [r] */
	union {
		/* Multi-process enabled. */
		struct vioblk_dev vioblk;
		struct vionet_dev vionet;

		/* In-process only. */
		struct vmmci_dev vmmci;
		struct vioscsi_dev vioscsi;
	};

	struct virtio_io_cfg		cfg;		/* Virtio 0.9 */
	struct virtio_pci_common_cfg	pci_cfg;	/* Virtio 1.x */
	struct virtio_vq_info		vq[VIRTIO_MAX_QUEUES];	/* Virtqueues */

	uint16_t num_queues;			/* number of virtqueues [r] */
	uint16_t queue_size;			/* default queue size [r] */

	uint8_t		isr;			/* isr status register [rw] */
	uint8_t		status;			/* device status register [rw] */
	uint64_t	device_feature;		/* device features [r] */
	uint64_t 	driver_feature;		/* driver features [rw] */

	uint8_t		pci_id;			/* pci device id [r] */
	uint32_t	vm_id;			/* vmm(4) vm identifier [r] */
	int		irq;			/* assigned irq [r] */

	/* Multi-process emulation fields. */
	struct imsgev async_iev;		/* async imsg event [r] */
	struct imsgev sync_iev;			/* sync imsg event [r] */

	int sync_fd;				/* fd for synchronous channel */
	int async_fd;				/* fd for async channel */

	uint32_t	vm_vmid;		/* vmd(8) vm identifier [r] */
	pid_t		dev_pid;		/* pid of emulator process */
	char		dev_type;		/* device type (as char) */
	SLIST_ENTRY(virtio_dev) dev_next;
};

/* virtio.c */
extern struct virtio_dev vmmci;

void virtio_init(struct vmd_vm *, int, int[][VM_MAX_BASE_PER_DISK], int *);
void virtio_vq_init(struct virtio_dev *, size_t);
void virtio_broadcast_imsg(struct vmd_vm *, uint16_t, void *, uint16_t);
void virtio_stop(struct vmd_vm *);
void virtio_start(struct vmd_vm *);
void virtio_shutdown(struct vmd_vm *);
const char *virtio_reg_name(uint8_t);
uint32_t vring_size(uint32_t);
int vm_device_pipe(struct virtio_dev *, void (*)(int, short, void *),
    struct event_base *);
int virtio_pci_io(int, uint16_t, uint32_t *, uint8_t *, void *, uint8_t);
void virtio_assert_irq(struct virtio_dev *, int);
void virtio_deassert_irq(struct virtio_dev *, int);
uint32_t virtio_io_cfg(struct virtio_dev *, int, uint8_t, uint32_t, uint8_t);

void virtio_update_qs(struct virtio_dev *);
void virtio_update_qa(struct virtio_dev *);

ssize_t virtio_qcow2_get_base(int, char *, size_t, const char *);
int virtio_qcow2_create(const char *, const char *, uint64_t);
int virtio_qcow2_init(struct virtio_backing *, off_t *, int*, size_t);
int virtio_raw_create(const char *, uint64_t);
int virtio_raw_init(struct virtio_backing *, off_t *, int*, size_t);

void vionet_set_hostmac(struct vmd_vm *, unsigned int, uint8_t *);

int vmmci_io(int, uint16_t, uint32_t *, uint8_t *, void *, uint8_t);
int vmmci_ctl(struct virtio_dev *, unsigned int);
void vmmci_timeout(int, short, void *);

const char *vioblk_cmd_name(uint32_t);

/* dhcp.c */
ssize_t dhcp_request(struct virtio_dev *, char *, size_t, char **);

/* vioscsi.c */
int vioscsi_io(int, uint16_t, uint32_t *, uint8_t *, void *, uint8_t);
int vioscsi_notifyq(struct virtio_dev *, uint16_t);

/* imsg handling */
void	viodev_msg_read(struct imsg *, struct viodev_msg *);
void	vionet_hostmac_read(struct imsg *, struct vionet_dev *);

#endif /* _VIRTIO_H_ */
