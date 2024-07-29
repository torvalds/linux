/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QMC management
 *
 * Copyright 2022 CS GROUP France
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */
#ifndef __SOC_FSL_QMC_H__
#define __SOC_FSL_QMC_H__

#include <linux/bits.h>
#include <linux/types.h>

struct device_node;
struct device;
struct qmc_chan;

int qmc_chan_count_phandles(struct device_node *np, const char *phandles_name);

struct qmc_chan *qmc_chan_get_byphandles_index(struct device_node *np,
					       const char *phandles_name,
					       int index);
struct qmc_chan *devm_qmc_chan_get_byphandles_index(struct device *dev,
						    struct device_node *np,
						    const char *phandles_name,
						    int index);

static inline struct qmc_chan *qmc_chan_get_byphandle(struct device_node *np,
						      const char *phandle_name)
{
	return qmc_chan_get_byphandles_index(np, phandle_name, 0);
}

static inline struct qmc_chan *devm_qmc_chan_get_byphandle(struct device *dev,
							   struct device_node *np,
							   const char *phandle_name)
{
	return devm_qmc_chan_get_byphandles_index(dev, np, phandle_name, 0);
}

struct qmc_chan *qmc_chan_get_bychild(struct device_node *np);
void qmc_chan_put(struct qmc_chan *chan);

struct qmc_chan *devm_qmc_chan_get_bychild(struct device *dev, struct device_node *np);

enum qmc_mode {
	QMC_TRANSPARENT,
	QMC_HDLC,
};

struct qmc_chan_info {
	enum qmc_mode mode;
	unsigned long rx_fs_rate;
	unsigned long rx_bit_rate;
	u8 nb_rx_ts;
	unsigned long tx_fs_rate;
	unsigned long tx_bit_rate;
	u8 nb_tx_ts;
};

int qmc_chan_get_info(struct qmc_chan *chan, struct qmc_chan_info *info);

struct qmc_chan_ts_info {
	u64 rx_ts_mask_avail;
	u64 tx_ts_mask_avail;
	u64 rx_ts_mask;
	u64 tx_ts_mask;
};

int qmc_chan_get_ts_info(struct qmc_chan *chan, struct qmc_chan_ts_info *ts_info);
int qmc_chan_set_ts_info(struct qmc_chan *chan, const struct qmc_chan_ts_info *ts_info);

struct qmc_chan_param {
	enum qmc_mode mode;
	union {
		struct {
			u16 max_rx_buf_size;
			u16 max_rx_frame_size;
			bool is_crc32;
		} hdlc;
		struct {
			u16 max_rx_buf_size;
		} transp;
	};
};

int qmc_chan_set_param(struct qmc_chan *chan, const struct qmc_chan_param *param);

int qmc_chan_write_submit(struct qmc_chan *chan, dma_addr_t addr, size_t length,
			  void (*complete)(void *context), void *context);

/* Flags available (ORed) for read complete() flags parameter in HDLC mode.
 * No flags are available in transparent mode and the read complete() flags
 * parameter has no meaning in transparent mode.
 */
#define QMC_RX_FLAG_HDLC_LAST	BIT(11) /* Last in frame */
#define QMC_RX_FLAG_HDLC_FIRST	BIT(10) /* First in frame */
#define QMC_RX_FLAG_HDLC_OVF	BIT(5)  /* Data overflow */
#define QMC_RX_FLAG_HDLC_UNA	BIT(4)  /* Unaligned (ie. bits received not multiple of 8) */
#define QMC_RX_FLAG_HDLC_ABORT	BIT(3)  /* Received an abort sequence (seven consecutive ones) */
#define QMC_RX_FLAG_HDLC_CRC	BIT(2)  /* CRC error */

int qmc_chan_read_submit(struct qmc_chan *chan, dma_addr_t addr, size_t length,
			 void (*complete)(void *context, size_t length,
					  unsigned int flags),
			 void *context);

#define QMC_CHAN_READ  (1<<0)
#define QMC_CHAN_WRITE (1<<1)
#define QMC_CHAN_ALL   (QMC_CHAN_READ | QMC_CHAN_WRITE)

int qmc_chan_start(struct qmc_chan *chan, int direction);
int qmc_chan_stop(struct qmc_chan *chan, int direction);
int qmc_chan_reset(struct qmc_chan *chan, int direction);

#endif /* __SOC_FSL_QMC_H__ */
