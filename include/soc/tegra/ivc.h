/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef __TEGRA_IVC_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/iosys-map.h>
#include <linux/types.h>

struct tegra_ivc_header;

struct tegra_ivc {
	struct device *peer;

	struct {
		struct iosys_map map;
		unsigned int position;
		dma_addr_t phys;
	} rx, tx;

	void (*notify)(struct tegra_ivc *ivc, void *data);
	void *notify_data;

	unsigned int num_frames;
	size_t frame_size;
};

/**
 * tegra_ivc_read_get_next_frame - Peek at the next frame to receive
 * @ivc		pointer of the IVC channel
 *
 * Peek at the next frame to be received, without removing it from
 * the queue.
 *
 * Returns a pointer to the frame, or an error encoded pointer.
 */
int tegra_ivc_read_get_next_frame(struct tegra_ivc *ivc, struct iosys_map *map);

/**
 * tegra_ivc_read_advance - Advance the read queue
 * @ivc		pointer of the IVC channel
 *
 * Advance the read queue
 *
 * Returns 0, or a negative error value if failed.
 */
int tegra_ivc_read_advance(struct tegra_ivc *ivc);

/**
 * tegra_ivc_write_get_next_frame - Poke at the next frame to transmit
 * @ivc		pointer of the IVC channel
 *
 * Get access to the next frame.
 *
 * Returns a pointer to the frame, or an error encoded pointer.
 */
int tegra_ivc_write_get_next_frame(struct tegra_ivc *ivc, struct iosys_map *map);

/**
 * tegra_ivc_write_advance - Advance the write queue
 * @ivc		pointer of the IVC channel
 *
 * Advance the write queue
 *
 * Returns 0, or a negative error value if failed.
 */
int tegra_ivc_write_advance(struct tegra_ivc *ivc);

/**
 * tegra_ivc_notified - handle internal messages
 * @ivc		pointer of the IVC channel
 *
 * This function must be called following every notification.
 *
 * Returns 0 if the channel is ready for communication, or -EAGAIN if a channel
 * reset is in progress.
 */
int tegra_ivc_notified(struct tegra_ivc *ivc);

/**
 * tegra_ivc_reset - initiates a reset of the shared memory state
 * @ivc		pointer of the IVC channel
 *
 * This function must be called after a channel is reserved before it is used
 * for communication. The channel will be ready for use when a subsequent call
 * to notify the remote of the channel reset.
 */
void tegra_ivc_reset(struct tegra_ivc *ivc);

size_t tegra_ivc_align(size_t size);
unsigned tegra_ivc_total_queue_size(unsigned queue_size);
int tegra_ivc_init(struct tegra_ivc *ivc, struct device *peer, const struct iosys_map *rx,
		   dma_addr_t rx_phys, const struct iosys_map *tx, dma_addr_t tx_phys,
		   unsigned int num_frames, size_t frame_size,
		   void (*notify)(struct tegra_ivc *ivc, void *data),
		   void *data);
void tegra_ivc_cleanup(struct tegra_ivc *ivc);

#endif /* __TEGRA_IVC_H */
