#ifndef _LKL_HOST_H
#define _LKL_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lkl/asm/host_ops.h>
#include <lkl.h>

extern struct lkl_host_operations lkl_host_ops;

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt - printf like format string
 */
int lkl_printf(const char *fmt, ...);

extern char lkl_virtio_devs[256];

struct lkl_dev_buf {
	void *addr;
	unsigned int len;
};

extern struct lkl_dev_blk_ops lkl_dev_blk_ops;

#define LKL_DEV_BLK_TYPE_READ		0
#define LKL_DEV_BLK_TYPE_WRITE		1
#define LKL_DEV_BLK_TYPE_FLUSH		4
#define LKL_DEV_BLK_TYPE_FLUSH_OUT	5

struct lkl_blk_req {
	unsigned int type;
	unsigned int prio;
	unsigned long long sector;
	struct lkl_dev_buf *buf;
	int count;
};

struct lkl_dev_blk_ops {
	int (*get_capacity)(union lkl_disk disk, unsigned long long *res);
#define LKL_DEV_BLK_STATUS_OK		0
#define LKL_DEV_BLK_STATUS_IOERR	1
#define LKL_DEV_BLK_STATUS_UNSUP	2
	int (*request)(union lkl_disk disk, struct lkl_blk_req *req);
};

struct lkl_netdev {
	struct lkl_dev_net_ops *ops;
	lkl_thread_t rx_tid, tx_tid;
};

struct lkl_dev_net_ops {
	/* Writes a L2 packet into the net device.
	 *
	 * The data buffer can only hold 0 or 1 complete packets.
	 *
	 * @nd - pointer to the network device
	 * @data - pointer to the buffer
	 * @len - size of the buffer in bytes
	 * @returns 0 for success and -1 for failure.
	 */ 
	int (*tx)(struct lkl_netdev *nd, void *data, int len);
	/* Reads a packet from the net device.
	 *
	 * It must only read one complete packet if present.
	 *
	 * If the buffer is too small for the packet, the implementation may
	 * decide to drop it or trim it.
	 *
	 * @nd - pointer to the network device
	 * @data - pointer to the buffer to store the packet
	 * @len - pointer to the maximum size of the buffer. Also stores the
	 * real number of bytes read after return.
	 * @returns 0 for success and -1 if nothing is read.
	 */ 
	int (*rx)(struct lkl_netdev *nd, void *data, int *len);
#define LKL_DEV_NET_POLL_RX		1
#define LKL_DEV_NET_POLL_TX		2
	/* Polls a net device (level-triggered).
	 *
	 * Supports two events of LKL_DEV_NET_POLL_RX (readable) and
	 * LKL_DEV_NET_POLL_TX (writable). Blocks until at least one event is
	 * available.
	 * Must be level-triggered which means the events are always triggered
	 * as long as it's readable or writable.
	 *
	 * @nd - pointer to the network device
	 * @events - a bit mask specifying the events to poll on. Current
	 * implementation can assume only one of LKL_DEV_NET_POLL_RX or
	 * LKL_DEV_NET_POLL_TX is set.
	 * @returns the events triggered for success. -1 for failure.
	 */
	int (*poll)(struct lkl_netdev *nd, int events);
	/* Closes a net device.
	 *
	 * Implementation can choose to release any resources releated to it. In
	 * particular, the polling threads are to be killed in this function.
	 *
	 * Implemenation must guarantee it's safe to call free_mem() after this
	 * function call.
	 *
	 * Not implemented by all netdev types.
	 *
	 * @returns 0 for success. -1 for failure.
	 */
	int (*close)(struct lkl_netdev *nd);
};

#ifdef __cplusplus
}
#endif

#endif
