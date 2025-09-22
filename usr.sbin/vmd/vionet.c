/*	$OpenBSD: vionet.c,v 1.26 2025/08/03 08:50:25 dv Exp $	*/

/*
 * Copyright (c) 2023 Dave Voutila <dv@openbsd.org>
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

#include <dev/pci/virtio_pcireg.h>
#include <dev/pv/virtioreg.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atomicio.h"
#include "virtio.h"
#include "vmd.h"

#define VIONET_DEBUG	0
#ifdef DPRINTF
#undef DPRINTF
#endif
#if VIONET_DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif	/* VIONET_DEBUG */

#define VIRTIO_NET_CONFIG_MAC		 0 /*  8 bit x 6 byte */

#define VIRTIO_NET_F_MAC	(1 << 5)
#define RXQ	0
#define TXQ	1

extern char *__progname;
extern struct vmd_vm *current_vm;

struct packet {
	uint8_t	*buf;
	size_t	 len;
};

static void *rx_run_loop(void *);
static void *tx_run_loop(void *);
static int vionet_rx(struct virtio_dev *, int);
static ssize_t vionet_rx_copy(struct vionet_dev *, int, const struct iovec *,
    int, size_t);
static ssize_t vionet_rx_zerocopy(struct vionet_dev *, int,
    const struct iovec *, int);
static void vionet_rx_event(int, short, void *);
static uint32_t vionet_read(struct virtio_dev *, struct viodev_msg *, int *);
static void vionet_write(struct virtio_dev *, struct viodev_msg *);
static uint32_t vionet_cfg_read(struct virtio_dev *, struct viodev_msg *);
static void vionet_cfg_write(struct virtio_dev *, struct viodev_msg *);

static int vionet_tx(struct virtio_dev *);
static void vionet_notifyq(struct virtio_dev *, uint16_t);
static uint32_t vionet_dev_read(struct virtio_dev *, struct viodev_msg *);
static void dev_dispatch_vm(int, short, void *);
static void handle_sync_io(int, short, void *);
static void read_pipe_main(int, short, void *);
static void read_pipe_rx(int, short, void *);
static void read_pipe_tx(int, short, void *);
static void vionet_assert_pic_irq(struct virtio_dev *);
static void vionet_deassert_pic_irq(struct virtio_dev *);

/* Device Globals */
struct event ev_tap;
struct event ev_inject;
struct event_base *ev_base_main;
struct event_base *ev_base_rx;
struct event_base *ev_base_tx;
pthread_t rx_thread;
pthread_t tx_thread;
struct vm_dev_pipe pipe_main;
struct vm_dev_pipe pipe_rx;
struct vm_dev_pipe pipe_tx;
int pipe_inject[2];
#define READ	0
#define WRITE	1
struct iovec iov_rx[VIRTIO_QUEUE_SIZE_MAX];
struct iovec iov_tx[VIRTIO_QUEUE_SIZE_MAX];
pthread_rwlock_t lock = NULL;		/* Guards device config state. */
int resetting = 0;	/* Transient reset state used to coordinate reset. */
int rx_enabled = 0;	/* 1: we expect to read the tap, 0: wait for notify. */

__dead void
vionet_main(int fd, int fd_vmm)
{
	struct virtio_dev	 dev;
	struct vionet_dev	*vionet = NULL;
	struct viodev_msg 	 msg;
	struct vmd_vm	 	 vm;
	struct vm_create_params	*vcp;
	ssize_t			 sz;
	int			 ret;

	/*
	 * stdio - needed for read/write to disk fds and channels to the vm.
	 * vmm + proc - needed to create shared vm mappings.
	 */
	if (pledge("stdio vmm proc", NULL) == -1)
		fatal("pledge");

	/* Initialize iovec arrays. */
	memset(iov_rx, 0, sizeof(iov_rx));
	memset(iov_tx, 0, sizeof(iov_tx));

	/* Receive our vionet_dev, mostly preconfigured. */
	sz = atomicio(read, fd, &dev, sizeof(dev));
	if (sz != sizeof(dev)) {
		ret = errno;
		log_warn("failed to receive vionet");
		goto fail;
	}
	if (dev.dev_type != VMD_DEVTYPE_NET) {
		ret = EINVAL;
		log_warn("received invalid device type");
		goto fail;
	}
	dev.sync_fd = fd;
	vionet = &dev.vionet;

	log_debug("%s: got vionet dev. tap fd = %d, syncfd = %d, asyncfd = %d"
	    ", vmm fd = %d", __func__, vionet->data_fd, dev.sync_fd,
	    dev.async_fd, fd_vmm);

	/* Receive our vm information from the vm process. */
	memset(&vm, 0, sizeof(vm));
	sz = atomicio(read, dev.sync_fd, &vm, sizeof(vm));
	if (sz != sizeof(vm)) {
		ret = EIO;
		log_warnx("failed to receive vm details");
		goto fail;
	}
	vcp = &vm.vm_params.vmc_params;
	current_vm = &vm;
	setproctitle("%s/vionet%d", vcp->vcp_name, vionet->idx);
	log_procinit("vm/%s/vionet%d", vcp->vcp_name, vionet->idx);

	/* Now that we have our vm information, we can remap memory. */
	ret = remap_guest_mem(&vm, fd_vmm);
	if (ret) {
		fatal("%s: failed to remap", __func__);
		goto fail;
	}

	/*
	 * We no longer need /dev/vmm access.
	 */
	close_fd(fd_vmm);
	if (pledge("stdio", NULL) == -1)
		fatal("pledge2");

	/* Initialize our packet injection pipe. */
	if (pipe2(pipe_inject, O_NONBLOCK) == -1) {
		log_warn("%s: injection pipe", __func__);
		goto fail;
	}

	/* Initialize inter-thread communication channels. */
	vm_pipe_init2(&pipe_main, read_pipe_main, &dev);
	vm_pipe_init2(&pipe_rx, read_pipe_rx, &dev);
	vm_pipe_init2(&pipe_tx, read_pipe_tx, &dev);

	/* Initialize RX and TX threads . */
	ret = pthread_create(&rx_thread, NULL, rx_run_loop, &dev);
	if (ret) {
		errno = ret;
		log_warn("%s: failed to initialize rx thread", __func__);
		goto fail;
	}
	pthread_set_name_np(rx_thread, "rx");
	ret = pthread_create(&tx_thread, NULL, tx_run_loop, &dev);
	if (ret) {
		errno = ret;
		log_warn("%s: failed to initialize tx thread", __func__);
		goto fail;
	}
	pthread_set_name_np(tx_thread, "tx");

	/* Initialize our rwlock for guarding shared device state. */
	ret = pthread_rwlock_init(&lock, NULL);
	if (ret) {
		errno = ret;
		log_warn("%s: failed to initialize rwlock", __func__);
		goto fail;
	}

	/* Initialize libevent so we can start wiring event handlers. */
	ev_base_main = event_base_new();

	/* Add our handler for receiving messages from the RX/TX threads. */
	event_base_set(ev_base_main, &pipe_main.read_ev);
	event_add(&pipe_main.read_ev, NULL);

	/* Wire up an async imsg channel. */
	log_debug("%s: wiring in async vm event handler (fd=%d)", __func__,
		dev.async_fd);
	if (vm_device_pipe(&dev, dev_dispatch_vm, ev_base_main)) {
		ret = EIO;
		log_warnx("vm_device_pipe");
		goto fail;
	}

	/* Configure our sync channel event handler. */
	log_debug("%s: wiring in sync channel handler (fd=%d)", __func__,
		dev.sync_fd);
	if (imsgbuf_init(&dev.sync_iev.ibuf, dev.sync_fd) == -1) {
		log_warnx("imsgbuf_init");
		goto fail;
	}
	imsgbuf_allow_fdpass(&dev.sync_iev.ibuf);
	dev.sync_iev.handler = handle_sync_io;
	dev.sync_iev.data = &dev;
	dev.sync_iev.events = EV_READ;
	imsg_event_add2(&dev.sync_iev, ev_base_main);

	/* Send a ready message over the sync channel. */
	log_debug("%s: telling vm %s device is ready", __func__, vcp->vcp_name);
	memset(&msg, 0, sizeof(msg));
	msg.type = VIODEV_MSG_READY;
	imsg_compose_event2(&dev.sync_iev, IMSG_DEVOP_MSG, 0, 0, -1, &msg,
	    sizeof(msg), ev_base_main);

	/* Send a ready message over the async channel. */
	log_debug("%s: sending async ready message", __func__);
	ret = imsg_compose_event2(&dev.async_iev, IMSG_DEVOP_MSG, 0, 0, -1,
	    &msg, sizeof(msg), ev_base_main);
	if (ret == -1) {
		log_warnx("%s: failed to send async ready message!", __func__);
		goto fail;
	}

	/* Engage the event loop! */
	ret = event_base_dispatch(ev_base_main);
	event_base_free(ev_base_main);

	/* Try stopping the rx & tx threads cleanly by messaging them. */
	vm_pipe_send(&pipe_rx, VIRTIO_THREAD_STOP);
	vm_pipe_send(&pipe_tx, VIRTIO_THREAD_STOP);

	/* Wait for threads to stop. */
	pthread_join(rx_thread, NULL);
	pthread_join(tx_thread, NULL);
	pthread_rwlock_destroy(&lock);

	/* Cleanup */
	if (ret == 0) {
		close_fd(dev.sync_fd);
		close_fd(dev.async_fd);
		close_fd(vionet->data_fd);
		close_fd(pipe_main.read);
		close_fd(pipe_main.write);
		close_fd(pipe_rx.write);
		close_fd(pipe_tx.write);
		close_fd(pipe_inject[READ]);
		close_fd(pipe_inject[WRITE]);
		_exit(ret);
		/* NOTREACHED */
	}
fail:
	/* Try firing off a message to the vm saying we're dying. */
	memset(&msg, 0, sizeof(msg));
	msg.type = VIODEV_MSG_ERROR;
	msg.data = ret;
	imsg_compose(&dev.sync_iev.ibuf, IMSG_DEVOP_MSG, 0, 0, -1, &msg,
	    sizeof(msg));
	imsgbuf_flush(&dev.sync_iev.ibuf);

	close_fd(dev.sync_fd);
	close_fd(dev.async_fd);
	close_fd(pipe_inject[READ]);
	close_fd(pipe_inject[WRITE]);
	if (vionet != NULL)
		close_fd(vionet->data_fd);
	if (lock != NULL)
		pthread_rwlock_destroy(&lock);
	_exit(ret);
}

/*
 * vionet_rx
 *
 * Pull packet from the provided fd and fill the receive-side virtqueue. We
 * selectively use zero-copy approaches when possible.
 *
 * Returns 1 if guest notification is needed. Otherwise, returns -1 on failure
 * or 0 if no notification is needed.
 */
static int
vionet_rx(struct virtio_dev *dev, int fd)
{
	uint16_t idx, hdr_idx;
	char *vr = NULL;
	size_t chain_len = 0, iov_cnt;
	struct vionet_dev *vionet = &dev->vionet;
	struct vring_desc *desc, *table;
	struct vring_avail *avail;
	struct vring_used *used;
	struct virtio_net_hdr *hdr = NULL;
	struct virtio_vq_info *vq_info;
	struct iovec *iov;
	int notify = 0;
	ssize_t sz;
	uint8_t status = 0;

	status = dev->status & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK;
	if (status != VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK) {
		log_warnx("%s: driver not ready", __func__);
		return (0);
	}

	vq_info = &dev->vq[RXQ];
	idx = vq_info->last_avail;
	vr = vq_info->q_hva;
	if (vr == NULL)
		fatalx("%s: vr == NULL", __func__);

	/* Compute offsets in ring of descriptors, avail ring, and used ring */
	table = (struct vring_desc *)(vr);
	avail = (struct vring_avail *)(vr + vq_info->vq_availoffset);
	used = (struct vring_used *)(vr + vq_info->vq_usedoffset);
	used->flags |= VRING_USED_F_NO_NOTIFY;

	while (idx != avail->idx) {
		hdr_idx = avail->ring[idx & vq_info->mask];
		desc = &table[hdr_idx & vq_info->mask];
		if (!DESC_WRITABLE(desc)) {
			log_warnx("%s: invalid descriptor state", __func__);
			goto reset;
		}

		iov = &iov_rx[0];
		iov_cnt = 1;

		/*
		 * First descriptor should be at least as large as the
		 * virtio_net_hdr. It's not technically required, but in
		 * legacy devices it should be safe to assume.
		 */
		iov->iov_len = desc->len;
		if (iov->iov_len < sizeof(struct virtio_net_hdr)) {
			log_warnx("%s: invalid descriptor length", __func__);
			goto reset;
		}

		/*
		 * Insert the virtio_net_hdr and adjust len/base. We do the
		 * pointer math here before it's a void*.
		 */
		iov->iov_base = hvaddr_mem(desc->addr, iov->iov_len);
		if (iov->iov_base == NULL)
			goto reset;
		hdr = iov->iov_base;
		memset(hdr, 0, sizeof(struct virtio_net_hdr));

		/* Tweak the iovec to account for the virtio_net_hdr. */
		iov->iov_len -= sizeof(struct virtio_net_hdr);
		iov->iov_base = hvaddr_mem(desc->addr +
		    sizeof(struct virtio_net_hdr), iov->iov_len);
		if (iov->iov_base == NULL)
			goto reset;
		chain_len = iov->iov_len;

		/*
		 * Walk the remaining chain and collect remaining addresses
		 * and lengths.
		 */
		while (desc->flags & VRING_DESC_F_NEXT) {
			desc = &table[desc->next & vq_info->mask];
			if (!DESC_WRITABLE(desc)) {
				log_warnx("%s: invalid descriptor state",
				    __func__);
				goto reset;
			}

			/* Collect our IO information. Translate gpa's. */
			iov = &iov_rx[iov_cnt];
			iov->iov_len = desc->len;
			iov->iov_base = hvaddr_mem(desc->addr, iov->iov_len);
			if (iov->iov_base == NULL)
				goto reset;
			chain_len += iov->iov_len;

			/* Guard against infinitely looping chains. */
			if (++iov_cnt >= nitems(iov_rx)) {
				log_warnx("%s: infinite chain detected",
				    __func__);
				goto reset;
			}
		}

		/* Make sure the driver gave us the bare minimum buffers. */
		if (chain_len < VIONET_MIN_TXLEN) {
			log_warnx("%s: insufficient buffers provided",
			    __func__);
			goto reset;
		}

		hdr->num_buffers = iov_cnt;

		/*
		 * If we're enforcing hardware address or handling an injected
		 * packet, we need to use a copy-based approach.
		 */
		if (vionet->lockedmac || fd != vionet->data_fd)
			sz = vionet_rx_copy(vionet, fd, iov_rx, iov_cnt,
			    chain_len);
		else
			sz = vionet_rx_zerocopy(vionet, fd, iov_rx, iov_cnt);
		if (sz == -1)
			goto reset;
		if (sz == 0)	/* No packets, so bail out for now. */
			break;

		/*
		 * Account for the prefixed header since it wasn't included
		 * in the copy or zerocopy operations.
		 */
		sz += sizeof(struct virtio_net_hdr);

		/* Mark our buffers as used. */
		used->ring[used->idx & vq_info->mask].id = hdr_idx;
		used->ring[used->idx & vq_info->mask].len = sz;
		__sync_synchronize();
		used->idx++;
		idx++;
	}

	if (idx != vq_info->last_avail &&
	    !(avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
		notify = 1;
	}

	vq_info->last_avail = idx;
	return (notify);
reset:
	return (-1);
}

/*
 * vionet_rx_copy
 *
 * Read a packet off the provided file descriptor, validating packet
 * characteristics, and copy into the provided buffers in the iovec array.
 *
 * It's assumed that the provided iovec array contains validated host virtual
 * address translations and not guest physical addreses.
 *
 * Returns number of bytes copied on success, 0 if packet is dropped, and
 * -1 on an error.
 */
ssize_t
vionet_rx_copy(struct vionet_dev *dev, int fd, const struct iovec *iov,
    int iov_cnt, size_t chain_len)
{
	static uint8_t		 buf[VIONET_HARD_MTU];
	struct packet		*pkt = NULL;
	struct ether_header	*eh = NULL;
	uint8_t			*payload = buf;
	size_t			 i, chunk, nbytes, copied = 0;
	ssize_t			 sz;

	/* If reading from the tap(4), try to right-size the read. */
	if (fd == dev->data_fd)
		nbytes = MIN(chain_len, VIONET_HARD_MTU);
	else if (fd == pipe_inject[READ])
		nbytes = sizeof(struct packet);
	else {
		log_warnx("%s: invalid fd: %d", __func__, fd);
		return (-1);
	}

	/*
	 * Try to pull a packet. The fd should be non-blocking and we don't
	 * care if we under-read (i.e. sz != nbytes) as we may not have a
	 * packet large enough to fill the buffer.
	 */
	sz = read(fd, buf, nbytes);
	if (sz == -1) {
		if (errno != EAGAIN) {
			log_warn("%s: error reading packet", __func__);
			return (-1);
		}
		return (0);
	} else if (fd == dev->data_fd && sz < VIONET_MIN_TXLEN) {
		/* If reading the tap(4), we should get valid ethernet. */
		log_warnx("%s: invalid packet size", __func__);
		return (0);
	} else if (fd == pipe_inject[READ] && sz != sizeof(struct packet)) {
		log_warnx("%s: invalid injected packet object (sz=%ld)",
		    __func__, sz);
		return (0);
	}

	/* Decompose an injected packet, if that's what we're working with. */
	if (fd == pipe_inject[READ]) {
		pkt = (struct packet *)buf;
		if (pkt->buf == NULL) {
			log_warnx("%s: invalid injected packet, no buffer",
			    __func__);
			return (0);
		}
		if (sz < VIONET_MIN_TXLEN || sz > VIONET_MAX_TXLEN) {
			log_warnx("%s: invalid injected packet size", __func__);
			goto drop;
		}
		payload = pkt->buf;
		sz = (ssize_t)pkt->len;
	}

	/* Validate the ethernet header, if required. */
	if (dev->lockedmac) {
		eh = (struct ether_header *)(payload);
		if (!ETHER_IS_MULTICAST(eh->ether_dhost) &&
		    memcmp(eh->ether_dhost, dev->mac,
		    sizeof(eh->ether_dhost)) != 0)
			goto drop;
	}

	/* Truncate one last time to the chain length, if shorter. */
	sz = MIN(chain_len, (size_t)sz);

	/*
	 * Copy the packet into the provided buffers. We can use memcpy(3)
	 * here as the gpa was validated and translated to an hva previously.
	 */
	for (i = 0; (int)i < iov_cnt && (size_t)sz > copied; i++) {
		chunk = MIN(iov[i].iov_len, (size_t)(sz - copied));
		memcpy(iov[i].iov_base, payload + copied, chunk);
		copied += chunk;
	}

drop:
	/* Free any injected packet buffer. */
	if (pkt != NULL)
		free(pkt->buf);

	return (copied);
}

/*
 * vionet_rx_zerocopy
 *
 * Perform a vectorized read from the given fd into the guest physical memory
 * pointed to by iovecs.
 *
 * Returns number of bytes read on success, -1 on error, or 0 if EAGAIN was
 * returned by readv.
 *
 */
static ssize_t
vionet_rx_zerocopy(struct vionet_dev *dev, int fd, const struct iovec *iov,
    int iov_cnt)
{
	ssize_t		sz;

	if (dev->lockedmac) {
		log_warnx("%s: zerocopy not available for locked lladdr",
		    __func__);
		return (-1);
	}

	sz = readv(fd, iov, iov_cnt);
	if (sz == -1 && errno == EAGAIN)
		return (0);
	return (sz);
}


/*
 * vionet_rx_event
 *
 * Called when new data can be received on the tap fd of a vionet device.
 */
static void
vionet_rx_event(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev *)arg;
	int			 ret = 0;

	if (!(event & EV_READ))
		fatalx("%s: invalid event type", __func__);

	pthread_rwlock_rdlock(&lock);
	ret = vionet_rx(dev, fd);
	pthread_rwlock_unlock(&lock);

	if (ret == 0) {
		/* Nothing to do. */
		return;
	}

	pthread_rwlock_wrlock(&lock);
	if (ret == 1) {
		/* Notify the driver. */
		dev->isr |= 1;
	} else {
		/* Need a reset. Something went wrong. */
		log_warnx("%s: requesting device reset", __func__);
		dev->status |= DEVICE_NEEDS_RESET;
		dev->isr |= VIRTIO_CONFIG_ISR_CONFIG_CHANGE;
	}
	pthread_rwlock_unlock(&lock);

	vm_pipe_send(&pipe_main, VIRTIO_RAISE_IRQ);
}

static void
vionet_notifyq(struct virtio_dev *dev, uint16_t vq_idx)
{
	switch (vq_idx) {
	case RXQ:
		rx_enabled = 1;
		vm_pipe_send(&pipe_rx, VIRTIO_NOTIFY);
		break;
	case TXQ:
		vm_pipe_send(&pipe_tx, VIRTIO_NOTIFY);
		break;
	default:
		/*
		 * Catch the unimplemented queue ID 2 (control queue) as
		 * well as any bogus queue IDs.
		 */
		log_debug("%s: notify for unimplemented queue ID %d",
		    __func__, dev->cfg.queue_notify);
		break;
	}
}

static int
vionet_tx(struct virtio_dev *dev)
{
	uint16_t idx, hdr_idx;
	size_t chain_len, iov_cnt;
	ssize_t dhcpsz = 0, sz;
	int notify = 0;
	char *vr = NULL, *dhcppkt = NULL;
	struct vionet_dev *vionet = &dev->vionet;
	struct vring_desc *desc, *table;
	struct vring_avail *avail;
	struct vring_used *used;
	struct virtio_vq_info *vq_info;
	struct ether_header *eh;
	struct iovec *iov;
	struct packet pkt;
	uint8_t status = 0;

	status = dev->status & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK;
	if (status != VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK) {
		log_warnx("%s: driver not ready", __func__);
		return (0);
	}

	vq_info = &dev->vq[TXQ];
	idx = vq_info->last_avail;
	vr = vq_info->q_hva;
	if (vr == NULL)
		fatalx("%s: vr == NULL", __func__);

	/* Compute offsets in ring of descriptors, avail ring, and used ring */
	table = (struct vring_desc *)(vr);
	avail = (struct vring_avail *)(vr + vq_info->vq_availoffset);
	used = (struct vring_used *)(vr + vq_info->vq_usedoffset);

	while (idx != avail->idx) {
		hdr_idx = avail->ring[idx & vq_info->mask];
		desc = &table[hdr_idx & vq_info->mask];
		if (DESC_WRITABLE(desc)) {
			log_warnx("%s: invalid descriptor state", __func__);
			goto reset;
		}

		iov = &iov_tx[0];
		iov_cnt = 0;
		chain_len = 0;

		/*
		 * We do not negotiate VIRTIO_NET_F_HASH_REPORT so we
		 * assume the header length is fixed.
		 */
		if (desc->len < sizeof(struct virtio_net_hdr)) {
			log_warnx("%s: invalid descriptor length", __func__);
			goto reset;
		}
		iov->iov_len = desc->len;

		if (iov->iov_len > sizeof(struct virtio_net_hdr)) {
			/* Chop off the virtio header, leaving packet data. */
			iov->iov_len -= sizeof(struct virtio_net_hdr);
			iov->iov_base = hvaddr_mem(desc->addr +
			    sizeof(struct virtio_net_hdr), iov->iov_len);
			if (iov->iov_base == NULL)
				goto reset;

			chain_len += iov->iov_len;
			iov_cnt++;
		}

		/*
		 * Walk the chain and collect remaining addresses and lengths.
		 */
		while (desc->flags & VRING_DESC_F_NEXT) {
			desc = &table[desc->next & vq_info->mask];
			if (DESC_WRITABLE(desc)) {
				log_warnx("%s: invalid descriptor state",
				    __func__);
				goto reset;
			}

			/* Collect our IO information, translating gpa's. */
			iov = &iov_tx[iov_cnt];
			iov->iov_len = desc->len;
			iov->iov_base = hvaddr_mem(desc->addr, iov->iov_len);
			if (iov->iov_base == NULL)
				goto reset;
			chain_len += iov->iov_len;

			/* Guard against infinitely looping chains. */
			if (++iov_cnt >= nitems(iov_tx)) {
				log_warnx("%s: infinite chain detected",
				    __func__);
				goto reset;
			}
		}

		/* Check if we've got a minimum viable amount of data. */
		if (chain_len < VIONET_MIN_TXLEN)
			goto drop;

		/*
		 * Packet inspection for ethernet header (if using a "local"
		 * interface) for possibility of a DHCP packet or (if using
		 * locked lladdr) for validating ethernet header.
		 *
		 * To help preserve zero-copy semantics, we require the first
		 * descriptor with packet data contains a large enough buffer
		 * for this inspection.
		 */
		iov = &iov_tx[0];
		if (vionet->lockedmac) {
			if (iov->iov_len < ETHER_HDR_LEN) {
				log_warnx("%s: insufficient header data",
				    __func__);
				goto drop;
			}
			eh = (struct ether_header *)iov->iov_base;
			if (memcmp(eh->ether_shost, vionet->mac,
			    sizeof(eh->ether_shost)) != 0) {
				log_warnx("%s: bad source address %s",
				    __func__, ether_ntoa((struct ether_addr *)
					eh->ether_shost));
				goto drop;
			}
		}
		if (vionet->local) {
			dhcpsz = dhcp_request(dev, iov->iov_base, iov->iov_len,
			    &dhcppkt);
			if (dhcpsz > 0) {
				log_debug("%s: detected dhcp request of %zu bytes",
				    __func__, dhcpsz);
				goto drop;
			}
		}

		/* Write our packet to the tap(4). */
		sz = writev(vionet->data_fd, iov_tx, iov_cnt);
		if (sz == -1 && errno != ENOBUFS) {
			log_warn("%s", __func__);
			goto reset;
		}
		chain_len += sizeof(struct virtio_net_hdr);
drop:
		used->ring[used->idx & vq_info->mask].id = hdr_idx;
		used->ring[used->idx & vq_info->mask].len = chain_len;
		__sync_synchronize();
		used->idx++;
		idx++;

		/* Facilitate DHCP reply injection, if needed. */
		if (dhcpsz > 0) {
			pkt.buf = dhcppkt;
			pkt.len = dhcpsz;
			sz = write(pipe_inject[WRITE], &pkt, sizeof(pkt));
			if (sz == -1 && errno != EAGAIN) {
				log_warn("%s: packet injection", __func__);
				free(pkt.buf);
			} else if (sz == -1 && errno == EAGAIN) {
				log_debug("%s: dropping dhcp reply", __func__);
				free(pkt.buf);
			} else if (sz != sizeof(pkt)) {
				log_warnx("%s: failed packet injection",
				    __func__);
				free(pkt.buf);
			}
		}
	}

	if (idx != vq_info->last_avail &&
	    !(avail->flags & VRING_AVAIL_F_NO_INTERRUPT))
		notify = 1;

	vq_info->last_avail = idx;
	return (notify);
reset:
	return (-1);
}

static void
dev_dispatch_vm(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = arg;
	struct vionet_dev	*vionet = &dev->vionet;
	struct imsgev		*iev = &dev->async_iev;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg	 	 imsg;
	ssize_t			 n = 0;
	int			 verbose;
	uint32_t		 type;

	if (dev == NULL)
		fatalx("%s: missing vionet pointer", __func__);

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("%s: imsgbuf_read", __func__);
		if (n == 0) {
			/* this pipe is dead, so remove the event handler */
			log_debug("%s: pipe dead (EV_READ)", __func__);
			event_del(&iev->ev);
			event_base_loopexit(ev_base_main, NULL);
			return;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE) {
				/* this pipe is dead, remove the handler */
				log_debug("%s: pipe dead (EV_WRITE)", __func__);
				event_del(&iev->ev);
				event_loopexit(NULL);
				return;
			}
			fatal("%s: imsgbuf_write", __func__);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get", __func__);
		if (n == 0)
			break;

		type = imsg_get_type(&imsg);
		switch (type) {
		case IMSG_DEVOP_HOSTMAC:
			vionet_hostmac_read(&imsg, vionet);
			log_debug("%s: set hostmac", __func__);
			break;
		case IMSG_VMDOP_PAUSE_VM:
			log_debug("%s: pausing", __func__);
			vm_pipe_send(&pipe_rx, VIRTIO_THREAD_PAUSE);
			break;
		case IMSG_VMDOP_UNPAUSE_VM:
			log_debug("%s: unpausing", __func__);
			if (rx_enabled)
				vm_pipe_send(&pipe_rx, VIRTIO_THREAD_START);
			break;
		case IMSG_CTL_VERBOSE:
			if (imsg_get_data(&imsg, &verbose, sizeof(verbose)))
				fatal("%s", __func__);
			log_setverbose(verbose);
			break;
		}
		imsg_free(&imsg);
	}
	imsg_event_add2(iev, ev_base_main);
}

/*
 * Synchronous IO handler.
 *
 */
static void
handle_sync_io(int fd, short event, void *arg)
{
	struct virtio_dev *dev = (struct virtio_dev *)arg;
	struct imsgev *iev = &dev->sync_iev;
	struct imsgbuf *ibuf = &iev->ibuf;
	struct viodev_msg msg;
	struct imsg imsg;
	ssize_t n;
	int deassert = 0;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("%s: imsgbuf_read", __func__);
		if (n == 0) {
			/* this pipe is dead, so remove the event handler */
			log_debug("%s: pipe dead (EV_READ)", __func__);
			event_del(&iev->ev);
			event_base_loopexit(ev_base_main, NULL);
			return;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE) {
				/* this pipe is dead, remove the handler */
				log_debug("%s: pipe dead (EV_WRITE)", __func__);
				event_del(&iev->ev);
				event_loopexit(NULL);
				return;
			}
			fatal("%s: imsgbuf_write", __func__);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatalx("%s: imsg_get (n=%ld)", __func__, n);
		if (n == 0)
			break;

		/* Unpack our message. They ALL should be dev messeges! */
		viodev_msg_read(&imsg, &msg);
		imsg_free(&imsg);

		switch (msg.type) {
		case VIODEV_MSG_IO_READ:
			/* Read IO: make sure to send a reply */
			msg.data = vionet_read(dev, &msg, &deassert);
			msg.data_valid = 1;
			if (deassert)
				msg.state = INTR_STATE_DEASSERT;
			imsg_compose_event2(iev, IMSG_DEVOP_MSG, 0, 0, -1, &msg,
			    sizeof(msg), ev_base_main);
			break;
		case VIODEV_MSG_IO_WRITE:
			/* Write IO: no reply needed */
			vionet_write(dev, &msg);
			break;
		case VIODEV_MSG_SHUTDOWN:
			event_del(&dev->sync_iev.ev);
			event_base_loopbreak(ev_base_main);
			return;
		default:
			fatalx("%s: invalid msg type %d", __func__, msg.type);
		}
	}
	imsg_event_add2(iev, ev_base_main);
}

static uint32_t
vionet_cfg_read(struct virtio_dev *dev, struct viodev_msg *msg)
{
	struct virtio_pci_common_cfg *pci_cfg = &dev->pci_cfg;
	uint32_t data = (uint32_t)(-1);
	uint16_t reg = msg->reg & 0x00FF;

	pthread_rwlock_rdlock(&lock);
	switch (reg) {
	case VIO1_PCI_DEVICE_FEATURE_SELECT:
		data = pci_cfg->device_feature_select;
		break;
	case VIO1_PCI_DEVICE_FEATURE:
		if (pci_cfg->device_feature_select == 0)
			data = dev->device_feature & (uint32_t)(-1);
		else if (pci_cfg->device_feature_select == 1)
			data = dev->device_feature >> 32;
		else {
			DPRINTF("%s: ignoring device feature read",
			    __func__);
		}
		break;
	case VIO1_PCI_DRIVER_FEATURE_SELECT:
		data = pci_cfg->driver_feature_select;
		break;
	case VIO1_PCI_DRIVER_FEATURE:
		if (pci_cfg->driver_feature_select == 0)
			data = dev->driver_feature & (uint32_t)(-1);
		else if (pci_cfg->driver_feature_select == 1)
			data = dev->driver_feature >> 32;
		else {
			DPRINTF("%s: ignoring driver feature read",
			    __func__);
		}
		break;
	case VIO1_PCI_CONFIG_MSIX_VECTOR:
		data = VIRTIO_MSI_NO_VECTOR;	/* Unsupported */
		break;
	case VIO1_PCI_NUM_QUEUES:
		data = dev->num_queues;
		break;
	case VIO1_PCI_DEVICE_STATUS:
		data = dev->status;
		break;
	case VIO1_PCI_CONFIG_GENERATION:
		data = pci_cfg->config_generation;
		break;
	case VIO1_PCI_QUEUE_SELECT:
		data = pci_cfg->queue_select;
		break;
	case VIO1_PCI_QUEUE_SIZE:
		data = pci_cfg->queue_size;
		break;
	case VIO1_PCI_QUEUE_MSIX_VECTOR:
		data = VIRTIO_MSI_NO_VECTOR;	/* Unsupported */
		break;
	case VIO1_PCI_QUEUE_ENABLE:
		data = pci_cfg->queue_enable;
		break;
	case VIO1_PCI_QUEUE_NOTIFY_OFF:
		data = pci_cfg->queue_notify_off;
		break;
	case VIO1_PCI_QUEUE_DESC:
		data = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_desc);
		break;
	case VIO1_PCI_QUEUE_DESC + 4:
		data = (uint32_t)(pci_cfg->queue_desc >> 32);
		break;
	case VIO1_PCI_QUEUE_AVAIL:
		data = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_avail);
		break;
	case VIO1_PCI_QUEUE_AVAIL + 4:
		data = (uint32_t)(pci_cfg->queue_avail >> 32);
		break;
	case VIO1_PCI_QUEUE_USED:
		data = (uint32_t)(0xFFFFFFFF & pci_cfg->queue_used);
		break;
	case VIO1_PCI_QUEUE_USED + 4:
		data = (uint32_t)(pci_cfg->queue_used >> 32);
		break;
	default:
		log_warnx("%s: invalid register 0x%04x", __func__, reg);
	}
	pthread_rwlock_unlock(&lock);

	return (data);
}

static void
vionet_cfg_write(struct virtio_dev *dev, struct viodev_msg *msg)
{
	struct virtio_pci_common_cfg *pci_cfg = &dev->pci_cfg;
	uint32_t data = msg->data;
	uint16_t reg = msg->reg & 0xFF;
	uint8_t sz = msg->io_sz;
	int i, pause_devices = 0;

	DPRINTF("%s: write reg=%d data=0x%x", __func__, msg->reg, data);

	pthread_rwlock_wrlock(&lock);
	switch (reg) {
	case VIO1_PCI_DEVICE_FEATURE_SELECT:
		if (sz != 4)
			log_warnx("%s: unaligned write to device "
			    "feature select (sz=%u)", __func__, sz);
		else
			pci_cfg->device_feature_select = data;
		break;
	case VIO1_PCI_DEVICE_FEATURE:
		log_warnx("%s: illegal write to device feature "
		    "register", __progname);
		break;
	case VIO1_PCI_DRIVER_FEATURE_SELECT:
		if (sz != 4)
			log_warnx("%s: unaligned write to driver "
			    "feature select register (sz=%u)", __func__,
			    sz);
		else
			pci_cfg->driver_feature_select = data;
		break;
	case VIO1_PCI_DRIVER_FEATURE:
		if (sz != 4) {
			log_warnx("%s: unaligned write to driver "
			    "feature register (sz=%u)", __func__, sz);
			break;
		}
		if (pci_cfg->driver_feature_select > 1) {
			/* We only support a 64-bit feature space. */
			DPRINTF("%s: ignoring driver feature write",
			    __func__);
			break;
		}
		pci_cfg->driver_feature = data;
		if (pci_cfg->driver_feature_select == 0)
			dev->driver_feature |= pci_cfg->driver_feature;
		else
			dev->driver_feature |=
			    ((uint64_t)pci_cfg->driver_feature << 32);
		dev->driver_feature &= dev->device_feature;
		DPRINTF("%s: driver features 0x%llx", __func__,
		    dev->driver_feature);
		break;
	case VIO1_PCI_CONFIG_MSIX_VECTOR:
		/* Ignore until we support MSIX. */
		break;
	case VIO1_PCI_NUM_QUEUES:
		log_warnx("%s: illegal write to num queues register",
		    __progname);
		break;
	case VIO1_PCI_DEVICE_STATUS:
		if (sz != 1) {
			log_warnx("%s: unaligned write to device "
			    "status register (sz=%u)", __func__, sz);
			break;
		}
		dev->status = data;
		if (dev->status == 0) {
			/* Reset device and virtqueues (if any). */
			dev->driver_feature = 0;
			dev->isr = 0;

			pci_cfg->queue_select = 0;
			virtio_update_qs(dev);

			if (dev->num_queues > 0) {
				/*
				 * Reset virtqueues to initial state and
				 * set to disabled status. Clear PCI
				 * configuration registers.
				 */
				for (i = 0; i < dev->num_queues; i++)
					virtio_vq_init(dev, i);
			}

			resetting = 2;		/* Wait on two acks: rx & tx */
			pause_devices = 1;
		}
		DPRINTF("%s: dev %u status [%s%s%s%s%s%s]", __func__,
		    dev->pci_id,
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_ACK) ?
		    "[ack]" : "",
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER) ?
		    "[driver]" : "",
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK) ?
		    "[driver ok]" : "",
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_FEATURES_OK) ?
		    "[features ok]" : "",
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_DEVICE_NEEDS_RESET)
		    ? "[needs reset]" : "",
		    (data & VIRTIO_CONFIG_DEVICE_STATUS_FAILED) ?
		    "[failed]" : "");
		break;
	case VIO1_PCI_CONFIG_GENERATION:
		log_warnx("%s: illegal write to config generation "
		    "register", __progname);
		break;
	case VIO1_PCI_QUEUE_SELECT:
		pci_cfg->queue_select = data;
		virtio_update_qs(dev);
		break;
	case VIO1_PCI_QUEUE_SIZE:
		if (data <= VIRTIO_QUEUE_SIZE_MAX)
			pci_cfg->queue_size = data;
		else {
			log_warnx("%s: clamping queue size", __func__);
			pci_cfg->queue_size = VIRTIO_QUEUE_SIZE_MAX;
		}
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_MSIX_VECTOR:
		/* Ignore until we support MSI-X. */
		break;
	case VIO1_PCI_QUEUE_ENABLE:
		pci_cfg->queue_enable = data;
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_NOTIFY_OFF:
		log_warnx("%s: illegal write to queue notify offset "
		    "register", __progname);
		break;
	case VIO1_PCI_QUEUE_DESC:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue "
			    "desc. register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_desc &= 0xffffffff00000000;
		pci_cfg->queue_desc |= (uint64_t)data;
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_DESC + 4:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue "
			    "desc. register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_desc &= 0x00000000ffffffff;
		pci_cfg->queue_desc |= ((uint64_t)data << 32);
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_AVAIL:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue "
			    "available register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_avail &= 0xffffffff00000000;
		pci_cfg->queue_avail |= (uint64_t)data;
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_AVAIL + 4:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue "
			    "available register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_avail &= 0x00000000ffffffff;
		pci_cfg->queue_avail |= ((uint64_t)data << 32);
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_USED:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue used "
			    "register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_used &= 0xffffffff00000000;
		pci_cfg->queue_used |= (uint64_t)data;
		virtio_update_qa(dev);
		break;
	case VIO1_PCI_QUEUE_USED + 4:
		if (sz != 4) {
			log_warnx("%s: unaligned write to queue used "
			    "register (sz=%u)", __func__, sz);
			break;
		}
		pci_cfg->queue_used &= 0x00000000ffffffff;
		pci_cfg->queue_used |= ((uint64_t)data << 32);
		virtio_update_qa(dev);
		break;
	default:
		log_warnx("%s: invalid register 0x%04x", __func__, reg);
	}
	pthread_rwlock_unlock(&lock);

	if (pause_devices) {
		rx_enabled = 0;
		vionet_deassert_pic_irq(dev);
		vm_pipe_send(&pipe_rx, VIRTIO_THREAD_PAUSE);
		vm_pipe_send(&pipe_tx, VIRTIO_THREAD_PAUSE);
	}
}

static uint32_t
vionet_read(struct virtio_dev *dev, struct viodev_msg *msg, int *deassert)
{
	uint32_t data = (uint32_t)(-1);
	uint16_t reg = msg->reg;

	switch (reg & 0xFF00) {
	case VIO1_CFG_BAR_OFFSET:
		data = vionet_cfg_read(dev, msg);
		break;
	case VIO1_DEV_BAR_OFFSET:
		data = vionet_dev_read(dev, msg);
		break;
	case VIO1_NOTIFY_BAR_OFFSET:
		/* Reads of notify register return all 1's. */
		break;
	case VIO1_ISR_BAR_OFFSET:
		pthread_rwlock_wrlock(&lock);
		data = dev->isr;
		dev->isr = 0;
		*deassert = 1;
		pthread_rwlock_unlock(&lock);
		break;
	default:
		log_debug("%s: no handler for reg 0x%04x", __func__, reg);
	}

	return (data);
}

static void
vionet_write(struct virtio_dev *dev, struct viodev_msg *msg)
{
	uint16_t reg = msg->reg;

	switch (reg & 0xFF00) {
	case VIO1_CFG_BAR_OFFSET:
		(void)vionet_cfg_write(dev, msg);
		break;
	case VIO1_DEV_BAR_OFFSET:
		/* Ignore all writes to device configuration registers. */
		break;
	case VIO1_NOTIFY_BAR_OFFSET:
		vionet_notifyq(dev, (uint16_t)(msg->data));
		break;
	case VIO1_ISR_BAR_OFFSET:
		/* ignore writes to ISR. */
		break;
	default:
		log_debug("%s: no handler for reg 0x%04x", __func__, reg);
	}
}

static uint32_t
vionet_dev_read(struct virtio_dev *dev, struct viodev_msg *msg)
{
	struct vionet_dev *vionet = (struct vionet_dev *)&dev->vionet;
	uint32_t data = (uint32_t)(-1);
	uint16_t reg = msg->reg & 0xFF;

	switch (reg) {
	case VIRTIO_NET_CONFIG_MAC:
	case VIRTIO_NET_CONFIG_MAC + 1:
	case VIRTIO_NET_CONFIG_MAC + 2:
	case VIRTIO_NET_CONFIG_MAC + 3:
	case VIRTIO_NET_CONFIG_MAC + 4:
	case VIRTIO_NET_CONFIG_MAC + 5:
		data = (uint8_t)vionet->mac[reg - VIRTIO_NET_CONFIG_MAC];
		break;
	default:
		log_warnx("%s: invalid register 0x%04x", __func__, reg);
		return (uint32_t)(-1);
	}

	return (data);
}

/*
 * Handle the rx side processing, communicating to the main thread via pipe.
 */
static void *
rx_run_loop(void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev *)arg;
	struct vionet_dev	*vionet = &dev->vionet;
	int			 ret;

	ev_base_rx = event_base_new();

	/* Wire up event handling for the tap fd. */
	event_set(&ev_tap, vionet->data_fd, EV_READ | EV_PERSIST,
	    vionet_rx_event, dev);
	event_base_set(ev_base_rx, &ev_tap);

	/* Wire up event handling for the packet injection pipe. */
	event_set(&ev_inject, pipe_inject[READ], EV_READ | EV_PERSIST,
	    vionet_rx_event, dev);
	event_base_set(ev_base_rx, &ev_inject);

	/* Wire up event handling for our inter-thread communication channel. */
	event_base_set(ev_base_rx, &pipe_rx.read_ev);
	event_add(&pipe_rx.read_ev, NULL);

	/* Begin our event loop with our channel event active. */
	ret = event_base_dispatch(ev_base_rx);
	event_base_free(ev_base_rx);

	log_debug("%s: exiting (%d)", __func__, ret);

	close_fd(pipe_rx.read);
	close_fd(pipe_inject[READ]);

	return (NULL);
}

/*
 * Handle the tx side processing, communicating to the main thread via pipe.
 */
static void *
tx_run_loop(void *arg)
{
	int			 ret;

	ev_base_tx = event_base_new();

	/* Wire up event handling for our inter-thread communication channel. */
	event_base_set(ev_base_tx, &pipe_tx.read_ev);
	event_add(&pipe_tx.read_ev, NULL);

	/* Begin our event loop with our channel event active. */
	ret = event_base_dispatch(ev_base_tx);
	event_base_free(ev_base_tx);

	log_debug("%s: exiting (%d)", __func__, ret);

	close_fd(pipe_tx.read);

	return (NULL);
}

/*
 * Read events sent by the main thread to the rx thread.
 */
static void
read_pipe_rx(int fd, short event, void *arg)
{
	enum pipe_msg_type	msg;

	if (!(event & EV_READ))
		fatalx("%s: invalid event type", __func__);

	msg = vm_pipe_recv(&pipe_rx);

	switch (msg) {
	case VIRTIO_NOTIFY:
	case VIRTIO_THREAD_START:
		event_add(&ev_tap, NULL);
		event_add(&ev_inject, NULL);
		break;
	case VIRTIO_THREAD_PAUSE:
		event_del(&ev_tap);
		event_del(&ev_inject);
		vm_pipe_send(&pipe_main, VIRTIO_THREAD_ACK);
		break;
	case VIRTIO_THREAD_STOP:
		event_del(&ev_tap);
		event_del(&ev_inject);
		event_base_loopexit(ev_base_rx, NULL);
		break;
	default:
		fatalx("%s: invalid channel message: %d", __func__, msg);
	}
}

/*
 * Read events sent by the main thread to the tx thread.
 */
static void
read_pipe_tx(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev*)arg;
	enum pipe_msg_type	 msg;
	int			 ret = 0;

	if (!(event & EV_READ))
		fatalx("%s: invalid event type", __func__);

	msg = vm_pipe_recv(&pipe_tx);

	switch (msg) {
	case VIRTIO_NOTIFY:
		pthread_rwlock_rdlock(&lock);
		ret = vionet_tx(dev);
		pthread_rwlock_unlock(&lock);
		break;
	case VIRTIO_THREAD_START:
		/* Ignore Start messages. */
		break;
	case VIRTIO_THREAD_PAUSE:
		/*
		 * Nothing to do when pausing on the tx side, but ACK so main
		 * thread knows we're not transmitting.
		 */
		vm_pipe_send(&pipe_main, VIRTIO_THREAD_ACK);
		break;
	case VIRTIO_THREAD_STOP:
		event_base_loopexit(ev_base_tx, NULL);
		break;
	default:
		fatalx("%s: invalid channel message: %d", __func__, msg);
	}

	if (ret == 0) {
		/* No notification needed. Return early. */
		return;
	}

	pthread_rwlock_wrlock(&lock);
	if (ret == 1) {
		/* Notify the driver. */
		dev->isr |= 1;
	} else {
		/* Need a reset. Something went wrong. */
		log_warnx("%s: requesting device reset", __func__);
		dev->status |= DEVICE_NEEDS_RESET;
		dev->isr |= VIRTIO_CONFIG_ISR_CONFIG_CHANGE;
	}
	pthread_rwlock_unlock(&lock);

	vm_pipe_send(&pipe_main, VIRTIO_RAISE_IRQ);
}

/*
 * Read events sent by the rx/tx threads to the main thread.
 */
static void
read_pipe_main(int fd, short event, void *arg)
{
	struct virtio_dev	*dev = (struct virtio_dev*)arg;
	struct vionet_dev	*vionet = &dev->vionet;
	enum pipe_msg_type	 msg;

	if (!(event & EV_READ))
		fatalx("%s: invalid event type", __func__);

	msg = vm_pipe_recv(&pipe_main);
	switch (msg) {
	case VIRTIO_RAISE_IRQ:
		vionet_assert_pic_irq(dev);
		break;
	case VIRTIO_THREAD_ACK:
		resetting--;
		if (resetting == 0) {
			log_debug("%s: resetting virtio network device %d",
			    __func__, vionet->idx);
			pthread_rwlock_wrlock(&lock);
			dev->status = 0;
			dev->cfg.guest_feature = 0;
			dev->cfg.queue_pfn = 0;
			dev->cfg.queue_select = 0;
			dev->cfg.queue_notify = 0;
			dev->isr = 0;
			virtio_vq_init(dev, TXQ);
			virtio_vq_init(dev, RXQ);
			pthread_rwlock_unlock(&lock);
		}
		break;
	default:
		fatalx("%s: invalid channel msg: %d", __func__, msg);
	}
}

/*
 * Message the vm process asking to raise the irq. Must be called from the main
 * thread.
 */
static void
vionet_assert_pic_irq(struct virtio_dev *dev)
{
	struct viodev_msg	msg;
	int			ret;

	memset(&msg, 0, sizeof(msg));
	msg.irq = dev->irq;
	msg.vcpu = 0; /* XXX: smp */
	msg.type = VIODEV_MSG_KICK;
	msg.state = INTR_STATE_ASSERT;

	ret = imsg_compose_event2(&dev->async_iev, IMSG_DEVOP_MSG, 0, 0, -1,
	    &msg, sizeof(msg), ev_base_main);
	if (ret == -1)
		log_warnx("%s: failed to assert irq %d", __func__, dev->irq);
}

/*
 * Message the vm process asking to lower the irq. Must be called from the main
 * thread.
 */
static void
vionet_deassert_pic_irq(struct virtio_dev *dev)
{
	struct viodev_msg	msg;
	int			ret;

	memset(&msg, 0, sizeof(msg));
	msg.irq = dev->irq;
	msg.vcpu = 0; /* XXX: smp */
	msg.type = VIODEV_MSG_KICK;
	msg.state = INTR_STATE_DEASSERT;

	ret = imsg_compose_event2(&dev->async_iev, IMSG_DEVOP_MSG, 0, 0, -1,
	    &msg, sizeof(msg), ev_base_main);
	if (ret == -1)
		log_warnx("%s: failed to assert irq %d", __func__, dev->irq);
}
