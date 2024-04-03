/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Apple RTKit IPC Library
 * Copyright (C) The Asahi Linux Contributors
 *
 * Apple's SoCs come with various co-processors running their RTKit operating
 * system. This protocol library is used by client drivers to use the
 * features provided by them.
 */
#ifndef _LINUX_APPLE_RTKIT_H_
#define _LINUX_APPLE_RTKIT_H_

#include <linux/device.h>
#include <linux/types.h>
#include <linux/mailbox_client.h>

/*
 * Struct to represent implementation-specific RTKit operations.
 *
 * @buffer:    Shared memory buffer allocated inside normal RAM.
 * @iomem:     Shared memory buffer controlled by the co-processors.
 * @size:      Size of the shared memory buffer.
 * @iova:      Device VA of shared memory buffer.
 * @is_mapped: Shared memory buffer is managed by the co-processor.
 * @private:   Private data pointer for the parent driver.
 */

struct apple_rtkit_shmem {
	void *buffer;
	void __iomem *iomem;
	size_t size;
	dma_addr_t iova;
	bool is_mapped;
	void *private;
};

/*
 * Struct to represent implementation-specific RTKit operations.
 *
 * @crashed:       Called when the co-processor has crashed. Runs in process
 *                 context.
 * @recv_message:  Function called when a message from RTKit is received
 *                 on a non-system endpoint. Called from a worker thread.
 * @recv_message_early:
 *                 Like recv_message, but called from atomic context. It
 *                 should return true if it handled the message. If it
 *                 returns false, the message will be passed on to the
 *                 worker thread.
 * @shmem_setup:   Setup shared memory buffer. If bfr.is_iomem is true the
 *                 buffer is managed by the co-processor and needs to be mapped.
 *                 Otherwise the buffer is managed by Linux and needs to be
 *                 allocated. If not specified dma_alloc_coherent is used.
 *                 Called in process context.
 * @shmem_destroy: Undo the shared memory buffer setup in shmem_setup. If not
 *                 specified dma_free_coherent is used. Called in process
 *                 context.
 */
struct apple_rtkit_ops {
	void (*crashed)(void *cookie);
	void (*recv_message)(void *cookie, u8 endpoint, u64 message);
	bool (*recv_message_early)(void *cookie, u8 endpoint, u64 message);
	int (*shmem_setup)(void *cookie, struct apple_rtkit_shmem *bfr);
	void (*shmem_destroy)(void *cookie, struct apple_rtkit_shmem *bfr);
};

struct apple_rtkit;

/*
 * Initializes the internal state required to handle RTKit. This
 * should usually be called within _probe.
 *
 * @dev:         Pointer to the device node this coprocessor is assocated with
 * @cookie:      opaque cookie passed to all functions defined in rtkit_ops
 * @mbox_name:   mailbox name used to communicate with the co-processor
 * @mbox_idx:    mailbox index to be used if mbox_name is NULL
 * @ops:         pointer to rtkit_ops to be used for this co-processor
 */
struct apple_rtkit *devm_apple_rtkit_init(struct device *dev, void *cookie,
					  const char *mbox_name, int mbox_idx,
					  const struct apple_rtkit_ops *ops);

/*
 * Non-devm version of devm_apple_rtkit_init. Must be freed with
 * apple_rtkit_free.
 *
 * @dev:         Pointer to the device node this coprocessor is assocated with
 * @cookie:      opaque cookie passed to all functions defined in rtkit_ops
 * @mbox_name:   mailbox name used to communicate with the co-processor
 * @mbox_idx:    mailbox index to be used if mbox_name is NULL
 * @ops:         pointer to rtkit_ops to be used for this co-processor
 */
struct apple_rtkit *apple_rtkit_init(struct device *dev, void *cookie,
					  const char *mbox_name, int mbox_idx,
					  const struct apple_rtkit_ops *ops);

/*
 * Free an instance of apple_rtkit.
 */
void apple_rtkit_free(struct apple_rtkit *rtk);

/*
 * Reinitialize internal structures. Must only be called with the co-processor
 * is held in reset.
 */
int apple_rtkit_reinit(struct apple_rtkit *rtk);

/*
 * Handle RTKit's boot process. Should be called after the CPU of the
 * co-processor has been started.
 */
int apple_rtkit_boot(struct apple_rtkit *rtk);

/*
 * Quiesce the co-processor.
 */
int apple_rtkit_quiesce(struct apple_rtkit *rtk);

/*
 * Wake the co-processor up from hibernation mode.
 */
int apple_rtkit_wake(struct apple_rtkit *rtk);

/*
 * Shutdown the co-processor
 */
int apple_rtkit_shutdown(struct apple_rtkit *rtk);

/*
 * Put the co-processor into idle mode
 */
int apple_rtkit_idle(struct apple_rtkit *rtk);

/*
 * Checks if RTKit is running and ready to handle messages.
 */
bool apple_rtkit_is_running(struct apple_rtkit *rtk);

/*
 * Checks if RTKit has crashed.
 */
bool apple_rtkit_is_crashed(struct apple_rtkit *rtk);

/*
 * Starts an endpoint. Must be called after boot but before any messages can be
 * sent or received from that endpoint.
 */
int apple_rtkit_start_ep(struct apple_rtkit *rtk, u8 endpoint);

/*
 * Send a message to the given endpoint.
 *
 * @rtk:            RTKit reference
 * @ep:             target endpoint
 * @message:        message to be sent
 * @completeion:    will be completed once the message has been submitted
 *                  to the hardware FIFO. Can be NULL.
 * @atomic:         if set to true this function can be called from atomic
 *                  context.
 */
int apple_rtkit_send_message(struct apple_rtkit *rtk, u8 ep, u64 message,
			     struct completion *completion, bool atomic);

/*
 * Process incoming messages in atomic context.
 * This only guarantees that messages arrive as far as the recv_message_early
 * callback; drivers expecting to handle incoming messages synchronously
 * by calling this function must do it that way.
 * Will return 1 if some data was processed, 0 if none was, or a
 * negative error code on failure.
 *
 * @rtk:            RTKit reference
 */
int apple_rtkit_poll(struct apple_rtkit *rtk);

#endif /* _LINUX_APPLE_RTKIT_H_ */
