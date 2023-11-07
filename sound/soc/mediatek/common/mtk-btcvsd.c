// SPDX-License-Identifier: GPL-2.0
//
// Mediatek ALSA BT SCO CVSD/MSBC Driver
//
// Copyright (c) 2019 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/sched/clock.h>

#include <sound/soc.h>

#define BTCVSD_SND_NAME "mtk-btcvsd-snd"

#define BT_CVSD_TX_NREADY	BIT(21)
#define BT_CVSD_RX_READY	BIT(22)
#define BT_CVSD_TX_UNDERFLOW	BIT(23)
#define BT_CVSD_RX_OVERFLOW	BIT(24)
#define BT_CVSD_INTERRUPT	BIT(31)

#define BT_CVSD_CLEAR \
	(BT_CVSD_TX_NREADY | BT_CVSD_RX_READY | BT_CVSD_TX_UNDERFLOW |\
	 BT_CVSD_RX_OVERFLOW | BT_CVSD_INTERRUPT)

/* TX */
#define SCO_TX_ENCODE_SIZE (60)
/* 18 = 6 * 180 / SCO_TX_ENCODE_SIZE */
#define SCO_TX_PACKER_BUF_NUM (18)

/* RX */
#define SCO_RX_PLC_SIZE (30)
#define SCO_RX_PACKER_BUF_NUM (64)
#define SCO_RX_PACKET_MASK (0x3F)

#define SCO_CVSD_PACKET_VALID_SIZE 2

#define SCO_PACKET_120 120
#define SCO_PACKET_180 180

#define BTCVSD_RX_PACKET_SIZE (SCO_RX_PLC_SIZE + SCO_CVSD_PACKET_VALID_SIZE)
#define BTCVSD_TX_PACKET_SIZE (SCO_TX_ENCODE_SIZE)

#define BTCVSD_RX_BUF_SIZE (BTCVSD_RX_PACKET_SIZE * SCO_RX_PACKER_BUF_NUM)
#define BTCVSD_TX_BUF_SIZE (BTCVSD_TX_PACKET_SIZE * SCO_TX_PACKER_BUF_NUM)

enum bt_sco_state {
	BT_SCO_STATE_IDLE,
	BT_SCO_STATE_RUNNING,
	BT_SCO_STATE_ENDING,
	BT_SCO_STATE_LOOPBACK,
};

enum bt_sco_direct {
	BT_SCO_DIRECT_BT2ARM,
	BT_SCO_DIRECT_ARM2BT,
};

enum bt_sco_packet_len {
	BT_SCO_CVSD_30 = 0,
	BT_SCO_CVSD_60,
	BT_SCO_CVSD_90,
	BT_SCO_CVSD_120,
	BT_SCO_CVSD_10,
	BT_SCO_CVSD_20,
	BT_SCO_CVSD_MAX,
};

enum BT_SCO_BAND {
	BT_SCO_NB,
	BT_SCO_WB,
};

struct mtk_btcvsd_snd_hw_info {
	unsigned int num_valid_addr;
	unsigned long bt_sram_addr[20];
	unsigned int packet_length;
	unsigned int packet_num;
};

struct mtk_btcvsd_snd_stream {
	struct snd_pcm_substream *substream;
	int stream;

	enum bt_sco_state state;

	unsigned int packet_size;
	unsigned int buf_size;
	u8 temp_packet_buf[SCO_PACKET_180];

	int packet_w;
	int packet_r;
	snd_pcm_uframes_t prev_frame;
	int prev_packet_idx;

	unsigned int xrun:1;
	unsigned int timeout:1;
	unsigned int mute:1;
	unsigned int trigger_start:1;
	unsigned int wait_flag:1;
	unsigned int rw_cnt;

	unsigned long long time_stamp;
	unsigned long long buf_data_equivalent_time;

	struct mtk_btcvsd_snd_hw_info buffer_info;
};

struct mtk_btcvsd_snd {
	struct device *dev;
	int irq_id;

	struct regmap *infra;
	void __iomem *bt_pkv_base;
	void __iomem *bt_sram_bank2_base;

	unsigned int infra_misc_offset;
	unsigned int conn_bt_cvsd_mask;
	unsigned int cvsd_mcu_read_offset;
	unsigned int cvsd_mcu_write_offset;
	unsigned int cvsd_packet_indicator;

	u32 *bt_reg_pkt_r;
	u32 *bt_reg_pkt_w;
	u32 *bt_reg_ctl;

	unsigned int irq_disabled:1;

	spinlock_t tx_lock;	/* spinlock for bt tx stream control */
	spinlock_t rx_lock;	/* spinlock for bt rx stream control */
	wait_queue_head_t tx_wait;
	wait_queue_head_t rx_wait;

	struct mtk_btcvsd_snd_stream *tx;
	struct mtk_btcvsd_snd_stream *rx;
	u8 tx_packet_buf[BTCVSD_TX_BUF_SIZE];
	u8 rx_packet_buf[BTCVSD_RX_BUF_SIZE];

	enum BT_SCO_BAND band;
};

struct mtk_btcvsd_snd_time_buffer_info {
	unsigned long long data_count_equi_time;
	unsigned long long time_stamp_us;
};

static const unsigned int btsco_packet_valid_mask[BT_SCO_CVSD_MAX][6] = {
	{0x1, 0x1 << 1, 0x1 << 2, 0x1 << 3, 0x1 << 4, 0x1 << 5},
	{0x1, 0x1, 0x2, 0x2, 0x4, 0x4},
	{0x1, 0x1, 0x1, 0x2, 0x2, 0x2},
	{0x1, 0x1, 0x1, 0x1, 0x0, 0x0},
	{0x7, 0x7 << 3, 0x7 << 6, 0x7 << 9, 0x7 << 12, 0x7 << 15},
	{0x3, 0x3 << 1, 0x3 << 3, 0x3 << 4, 0x3 << 6, 0x3 << 7},
};

static const unsigned int btsco_packet_info[BT_SCO_CVSD_MAX][4] = {
	{30, 6, SCO_PACKET_180 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_180 / SCO_RX_PLC_SIZE},
	{60, 3, SCO_PACKET_180 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_180 / SCO_RX_PLC_SIZE},
	{90, 2, SCO_PACKET_180 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_180 / SCO_RX_PLC_SIZE},
	{120, 1, SCO_PACKET_120 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_120 / SCO_RX_PLC_SIZE},
	{10, 18, SCO_PACKET_180 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_180 / SCO_RX_PLC_SIZE},
	{20, 9, SCO_PACKET_180 / SCO_TX_ENCODE_SIZE,
	 SCO_PACKET_180 / SCO_RX_PLC_SIZE},
};

static const u8 table_msbc_silence[SCO_PACKET_180] = {
	0x01, 0x38, 0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00,
	0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d,
	0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7,
	0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd,
	0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77,
	0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c, 0x00,
	0x01, 0xc8, 0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00,
	0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d,
	0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7,
	0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd,
	0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77,
	0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c, 0x00,
	0x01, 0xf8, 0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00,
	0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d,
	0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7,
	0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd,
	0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77,
	0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c, 0x00
};

static void mtk_btcvsd_snd_irq_enable(struct mtk_btcvsd_snd *bt)
{
	regmap_update_bits(bt->infra, bt->infra_misc_offset,
			   bt->conn_bt_cvsd_mask, 0);
}

static void mtk_btcvsd_snd_irq_disable(struct mtk_btcvsd_snd *bt)
{
	regmap_update_bits(bt->infra, bt->infra_misc_offset,
			   bt->conn_bt_cvsd_mask, bt->conn_bt_cvsd_mask);
}

static void mtk_btcvsd_snd_set_state(struct mtk_btcvsd_snd *bt,
				     struct mtk_btcvsd_snd_stream *bt_stream,
				     int state)
{
	dev_dbg(bt->dev, "%s(), stream %d, state %d, tx->state %d, rx->state %d, irq_disabled %d\n",
		__func__,
		bt_stream->stream, state,
		bt->tx->state, bt->rx->state, bt->irq_disabled);

	bt_stream->state = state;

	if (bt->tx->state == BT_SCO_STATE_IDLE &&
	    bt->rx->state == BT_SCO_STATE_IDLE) {
		if (!bt->irq_disabled) {
			disable_irq(bt->irq_id);
			mtk_btcvsd_snd_irq_disable(bt);
			bt->irq_disabled = 1;
		}
	} else {
		if (bt->irq_disabled) {
			enable_irq(bt->irq_id);
			mtk_btcvsd_snd_irq_enable(bt);
			bt->irq_disabled = 0;
		}
	}
}

static int mtk_btcvsd_snd_tx_init(struct mtk_btcvsd_snd *bt)
{
	memset(bt->tx, 0, sizeof(*bt->tx));
	memset(bt->tx_packet_buf, 0, sizeof(bt->tx_packet_buf));

	bt->tx->packet_size = BTCVSD_TX_PACKET_SIZE;
	bt->tx->buf_size = BTCVSD_TX_BUF_SIZE;
	bt->tx->timeout = 0;
	bt->tx->rw_cnt = 0;
	bt->tx->stream = SNDRV_PCM_STREAM_PLAYBACK;
	return 0;
}

static int mtk_btcvsd_snd_rx_init(struct mtk_btcvsd_snd *bt)
{
	memset(bt->rx, 0, sizeof(*bt->rx));
	memset(bt->rx_packet_buf, 0, sizeof(bt->rx_packet_buf));

	bt->rx->packet_size = BTCVSD_RX_PACKET_SIZE;
	bt->rx->buf_size = BTCVSD_RX_BUF_SIZE;
	bt->rx->timeout = 0;
	bt->rx->rw_cnt = 0;
	bt->rx->stream = SNDRV_PCM_STREAM_CAPTURE;
	return 0;
}

static void get_tx_time_stamp(struct mtk_btcvsd_snd *bt,
			      struct mtk_btcvsd_snd_time_buffer_info *ts)
{
	ts->time_stamp_us = bt->tx->time_stamp;
	ts->data_count_equi_time = bt->tx->buf_data_equivalent_time;
}

static void get_rx_time_stamp(struct mtk_btcvsd_snd *bt,
			      struct mtk_btcvsd_snd_time_buffer_info *ts)
{
	ts->time_stamp_us = bt->rx->time_stamp;
	ts->data_count_equi_time = bt->rx->buf_data_equivalent_time;
}

static int btcvsd_bytes_to_frame(struct snd_pcm_substream *substream,
				 int bytes)
{
	int count = bytes;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (runtime->format == SNDRV_PCM_FORMAT_S32_LE ||
	    runtime->format == SNDRV_PCM_FORMAT_U32_LE)
		count = count >> 2;
	else
		count = count >> 1;

	count = count / runtime->channels;
	return count;
}

static void mtk_btcvsd_snd_data_transfer(enum bt_sco_direct dir,
					 u8 *src, u8 *dst,
					 unsigned int blk_size,
					 unsigned int blk_num)
{
	unsigned int i, j;

	if (blk_size == 60 || blk_size == 120 || blk_size == 20) {
		u32 *src_32 = (u32 *)src;
		u32 *dst_32 = (u32 *)dst;

		for (i = 0; i < (blk_size * blk_num / 4); i++)
			*dst_32++ = *src_32++;
	} else {
		u16 *src_16 = (u16 *)src;
		u16 *dst_16 = (u16 *)dst;

		for (j = 0; j < blk_num; j++) {
			for (i = 0; i < (blk_size / 2); i++)
				*dst_16++ = *src_16++;

			if (dir == BT_SCO_DIRECT_BT2ARM)
				src_16++;
			else
				dst_16++;
		}
	}
}

/* write encoded mute data to bt sram */
static int btcvsd_tx_clean_buffer(struct mtk_btcvsd_snd *bt)
{
	unsigned int i;
	unsigned int num_valid_addr;
	unsigned long flags;
	enum BT_SCO_BAND band = bt->band;

	/* prepare encoded mute data */
	if (band == BT_SCO_NB)
		memset(bt->tx->temp_packet_buf, 170, SCO_PACKET_180);
	else
		memcpy(bt->tx->temp_packet_buf,
		       table_msbc_silence, SCO_PACKET_180);

	/* write mute data to bt tx sram buffer */
	spin_lock_irqsave(&bt->tx_lock, flags);
	num_valid_addr = bt->tx->buffer_info.num_valid_addr;

	dev_info(bt->dev, "%s(), band %d, num_valid_addr %u\n",
		 __func__, band, num_valid_addr);

	for (i = 0; i < num_valid_addr; i++) {
		void *dst;

		dev_info(bt->dev, "%s(), clean addr 0x%lx\n", __func__,
			 bt->tx->buffer_info.bt_sram_addr[i]);

		dst = (void *)bt->tx->buffer_info.bt_sram_addr[i];

		mtk_btcvsd_snd_data_transfer(BT_SCO_DIRECT_ARM2BT,
					     bt->tx->temp_packet_buf, dst,
					     bt->tx->buffer_info.packet_length,
					     bt->tx->buffer_info.packet_num);
	}
	spin_unlock_irqrestore(&bt->tx_lock, flags);

	return 0;
}

static int mtk_btcvsd_read_from_bt(struct mtk_btcvsd_snd *bt,
				   enum bt_sco_packet_len packet_type,
				   unsigned int packet_length,
				   unsigned int packet_num,
				   unsigned int blk_size,
				   unsigned int control)
{
	unsigned int i;
	int pv;
	u8 *src;
	unsigned int packet_buf_ofs;
	unsigned long flags;
	unsigned long connsys_addr_rx, ap_addr_rx;

	connsys_addr_rx = *bt->bt_reg_pkt_r;
	ap_addr_rx = (unsigned long)bt->bt_sram_bank2_base +
		     (connsys_addr_rx & 0xFFFF);

	if (connsys_addr_rx == 0xdeadfeed) {
		/* bt return 0xdeadfeed if read register during bt sleep */
		dev_warn(bt->dev, "%s(), connsys_addr_rx == 0xdeadfeed",
			 __func__);
		return -EIO;
	}

	src = (u8 *)ap_addr_rx;

	mtk_btcvsd_snd_data_transfer(BT_SCO_DIRECT_BT2ARM, src,
				     bt->rx->temp_packet_buf, packet_length,
				     packet_num);

	spin_lock_irqsave(&bt->rx_lock, flags);
	for (i = 0; i < blk_size; i++) {
		packet_buf_ofs = (bt->rx->packet_w & SCO_RX_PACKET_MASK) *
				 bt->rx->packet_size;
		memcpy(bt->rx_packet_buf + packet_buf_ofs,
		       bt->rx->temp_packet_buf + (SCO_RX_PLC_SIZE * i),
		       SCO_RX_PLC_SIZE);
		if ((control & btsco_packet_valid_mask[packet_type][i]) ==
		    btsco_packet_valid_mask[packet_type][i])
			pv = 1;
		else
			pv = 0;

		packet_buf_ofs += SCO_RX_PLC_SIZE;
		memcpy(bt->rx_packet_buf + packet_buf_ofs, (void *)&pv,
		       SCO_CVSD_PACKET_VALID_SIZE);
		bt->rx->packet_w++;
	}
	spin_unlock_irqrestore(&bt->rx_lock, flags);
	return 0;
}

static int mtk_btcvsd_write_to_bt(struct mtk_btcvsd_snd *bt,
				  enum bt_sco_packet_len packet_type,
				  unsigned int packet_length,
				  unsigned int packet_num,
				  unsigned int blk_size)
{
	unsigned int i;
	unsigned long flags;
	u8 *dst;
	unsigned long connsys_addr_tx, ap_addr_tx;
	bool new_ap_addr_tx = true;

	connsys_addr_tx = *bt->bt_reg_pkt_w;
	ap_addr_tx = (unsigned long)bt->bt_sram_bank2_base +
		     (connsys_addr_tx & 0xFFFF);

	if (connsys_addr_tx == 0xdeadfeed) {
		/* bt return 0xdeadfeed if read register during bt sleep */
		dev_warn(bt->dev, "%s(), connsys_addr_tx == 0xdeadfeed\n",
			 __func__);
		return -EIO;
	}

	spin_lock_irqsave(&bt->tx_lock, flags);
	for (i = 0; i < blk_size; i++) {
		memcpy(bt->tx->temp_packet_buf + (bt->tx->packet_size * i),
		       (bt->tx_packet_buf +
			(bt->tx->packet_r % SCO_TX_PACKER_BUF_NUM) *
			bt->tx->packet_size),
		       bt->tx->packet_size);

		bt->tx->packet_r++;
	}
	spin_unlock_irqrestore(&bt->tx_lock, flags);

	dst = (u8 *)ap_addr_tx;

	if (!bt->tx->mute) {
		mtk_btcvsd_snd_data_transfer(BT_SCO_DIRECT_ARM2BT,
					     bt->tx->temp_packet_buf, dst,
					     packet_length, packet_num);
	}

	/* store bt tx buffer sram info */
	bt->tx->buffer_info.packet_length = packet_length;
	bt->tx->buffer_info.packet_num = packet_num;
	for (i = 0; i < bt->tx->buffer_info.num_valid_addr; i++) {
		if (bt->tx->buffer_info.bt_sram_addr[i] == ap_addr_tx) {
			new_ap_addr_tx = false;
			break;
		}
	}
	if (new_ap_addr_tx) {
		unsigned int next_idx;

		spin_lock_irqsave(&bt->tx_lock, flags);
		bt->tx->buffer_info.num_valid_addr++;
		next_idx = bt->tx->buffer_info.num_valid_addr - 1;
		bt->tx->buffer_info.bt_sram_addr[next_idx] = ap_addr_tx;
		spin_unlock_irqrestore(&bt->tx_lock, flags);
		dev_info(bt->dev, "%s(), new ap_addr_tx = 0x%lx, num_valid_addr %d\n",
			 __func__, ap_addr_tx,
			 bt->tx->buffer_info.num_valid_addr);
	}

	if (bt->tx->mute)
		btcvsd_tx_clean_buffer(bt);

	return 0;
}

static irqreturn_t mtk_btcvsd_snd_irq_handler(int irq_id, void *dev)
{
	struct mtk_btcvsd_snd *bt = dev;
	unsigned int packet_type, packet_num, packet_length;
	unsigned int buf_cnt_tx, buf_cnt_rx, control;

	if (bt->rx->state != BT_SCO_STATE_RUNNING &&
	    bt->rx->state != BT_SCO_STATE_ENDING &&
	    bt->tx->state != BT_SCO_STATE_RUNNING &&
	    bt->tx->state != BT_SCO_STATE_ENDING &&
	    bt->tx->state != BT_SCO_STATE_LOOPBACK) {
		dev_warn(bt->dev, "%s(), in idle state: rx->state: %d, tx->state: %d\n",
			 __func__, bt->rx->state, bt->tx->state);
		goto irq_handler_exit;
	}

	control = *bt->bt_reg_ctl;
	packet_type = (control >> 18) & 0x7;

	if (((control >> 31) & 1) == 0) {
		dev_warn(bt->dev, "%s(), ((control >> 31) & 1) == 0, control 0x%x\n",
			 __func__, control);
		goto irq_handler_exit;
	}

	if (packet_type >= BT_SCO_CVSD_MAX) {
		dev_warn(bt->dev, "%s(), invalid packet_type %u, exit\n",
			 __func__, packet_type);
		goto irq_handler_exit;
	}

	packet_length = btsco_packet_info[packet_type][0];
	packet_num = btsco_packet_info[packet_type][1];
	buf_cnt_tx = btsco_packet_info[packet_type][2];
	buf_cnt_rx = btsco_packet_info[packet_type][3];

	if (bt->tx->state == BT_SCO_STATE_LOOPBACK) {
		u8 *src, *dst;
		unsigned long connsys_addr_rx, ap_addr_rx;
		unsigned long connsys_addr_tx, ap_addr_tx;

		connsys_addr_rx = *bt->bt_reg_pkt_r;
		ap_addr_rx = (unsigned long)bt->bt_sram_bank2_base +
			     (connsys_addr_rx & 0xFFFF);

		connsys_addr_tx = *bt->bt_reg_pkt_w;
		ap_addr_tx = (unsigned long)bt->bt_sram_bank2_base +
			     (connsys_addr_tx & 0xFFFF);

		if (connsys_addr_tx == 0xdeadfeed ||
		    connsys_addr_rx == 0xdeadfeed) {
			/* bt return 0xdeadfeed if read reg during bt sleep */
			dev_warn(bt->dev, "%s(), connsys_addr_tx == 0xdeadfeed\n",
				 __func__);
			goto irq_handler_exit;
		}

		src = (u8 *)ap_addr_rx;
		dst = (u8 *)ap_addr_tx;

		mtk_btcvsd_snd_data_transfer(BT_SCO_DIRECT_BT2ARM, src,
					     bt->tx->temp_packet_buf,
					     packet_length,
					     packet_num);
		mtk_btcvsd_snd_data_transfer(BT_SCO_DIRECT_ARM2BT,
					     bt->tx->temp_packet_buf, dst,
					     packet_length,
					     packet_num);
		bt->rx->rw_cnt++;
		bt->tx->rw_cnt++;
	}

	if (bt->rx->state == BT_SCO_STATE_RUNNING ||
	    bt->rx->state == BT_SCO_STATE_ENDING) {
		if (bt->rx->xrun) {
			if (bt->rx->packet_w - bt->rx->packet_r <=
			    SCO_RX_PACKER_BUF_NUM - 2 * buf_cnt_rx) {
				/*
				 * free space is larger then
				 * twice interrupt rx data size
				 */
				bt->rx->xrun = 0;
				dev_warn(bt->dev, "%s(), rx->xrun 0!\n",
					 __func__);
			}
		}

		if (!bt->rx->xrun &&
		    (bt->rx->packet_w - bt->rx->packet_r <=
		     SCO_RX_PACKER_BUF_NUM - buf_cnt_rx)) {
			mtk_btcvsd_read_from_bt(bt,
						packet_type,
						packet_length,
						packet_num,
						buf_cnt_rx,
						control);
			bt->rx->rw_cnt++;
		} else {
			bt->rx->xrun = 1;
			dev_warn(bt->dev, "%s(), rx->xrun 1\n", __func__);
		}
	}

	/* tx */
	bt->tx->timeout = 0;
	if ((bt->tx->state == BT_SCO_STATE_RUNNING ||
	     bt->tx->state == BT_SCO_STATE_ENDING) &&
	    bt->tx->trigger_start) {
		if (bt->tx->xrun) {
			/* prepared data is larger then twice
			 * interrupt tx data size
			 */
			if (bt->tx->packet_w - bt->tx->packet_r >=
			    2 * buf_cnt_tx) {
				bt->tx->xrun = 0;
				dev_warn(bt->dev, "%s(), tx->xrun 0\n",
					 __func__);
			}
		}

		if ((!bt->tx->xrun &&
		     (bt->tx->packet_w - bt->tx->packet_r >= buf_cnt_tx)) ||
		    bt->tx->state == BT_SCO_STATE_ENDING) {
			mtk_btcvsd_write_to_bt(bt,
					       packet_type,
					       packet_length,
					       packet_num,
					       buf_cnt_tx);
			bt->tx->rw_cnt++;
		} else {
			bt->tx->xrun = 1;
			dev_warn(bt->dev, "%s(), tx->xrun 1\n", __func__);
		}
	}

	*bt->bt_reg_ctl &= ~BT_CVSD_CLEAR;

	if (bt->rx->state == BT_SCO_STATE_RUNNING ||
	    bt->rx->state == BT_SCO_STATE_ENDING) {
		bt->rx->wait_flag = 1;
		wake_up_interruptible(&bt->rx_wait);
		snd_pcm_period_elapsed(bt->rx->substream);
	}
	if (bt->tx->state == BT_SCO_STATE_RUNNING ||
	    bt->tx->state == BT_SCO_STATE_ENDING) {
		bt->tx->wait_flag = 1;
		wake_up_interruptible(&bt->tx_wait);
		snd_pcm_period_elapsed(bt->tx->substream);
	}

	return IRQ_HANDLED;
irq_handler_exit:
	*bt->bt_reg_ctl &= ~BT_CVSD_CLEAR;
	return IRQ_HANDLED;
}

static int wait_for_bt_irq(struct mtk_btcvsd_snd *bt,
			   struct mtk_btcvsd_snd_stream *bt_stream)
{
	unsigned long long t1, t2;
	/* one interrupt period = 22.5ms */
	unsigned long long timeout_limit = 22500000;
	int max_timeout_trial = 2;
	int ret;

	bt_stream->wait_flag = 0;

	while (max_timeout_trial && !bt_stream->wait_flag) {
		t1 = sched_clock();
		if (bt_stream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			ret = wait_event_interruptible_timeout(bt->tx_wait,
				bt_stream->wait_flag,
				nsecs_to_jiffies(timeout_limit));
		} else {
			ret = wait_event_interruptible_timeout(bt->rx_wait,
				bt_stream->wait_flag,
				nsecs_to_jiffies(timeout_limit));
		}

		t2 = sched_clock();
		t2 = t2 - t1; /* in ns (10^9) */

		if (t2 > timeout_limit) {
			dev_warn(bt->dev, "%s(), stream %d, timeout %llu, limit %llu, ret %d, flag %d\n",
				 __func__, bt_stream->stream,
				 t2, timeout_limit, ret,
				 bt_stream->wait_flag);
		}

		if (ret < 0) {
			/*
			 * error, -ERESTARTSYS if it was interrupted by
			 * a signal
			 */
			dev_warn(bt->dev, "%s(), stream %d, error, trial left %d\n",
				 __func__,
				 bt_stream->stream, max_timeout_trial);

			bt_stream->timeout = 1;
			return ret;
		} else if (ret == 0) {
			/* conidtion is false after timeout */
			max_timeout_trial--;
			dev_warn(bt->dev, "%s(), stream %d, error, timeout, condition is false, trial left %d\n",
				 __func__,
				 bt_stream->stream, max_timeout_trial);

			if (max_timeout_trial <= 0) {
				bt_stream->timeout = 1;
				return -ETIME;
			}
		}
	}

	return 0;
}

static ssize_t mtk_btcvsd_snd_read(struct mtk_btcvsd_snd *bt,
				   char __user *buf,
				   size_t count)
{
	ssize_t read_size = 0, read_count = 0, cur_read_idx, cont;
	unsigned int cur_buf_ofs = 0;
	unsigned long avail;
	unsigned long flags;
	unsigned int packet_size = bt->rx->packet_size;

	while (count) {
		spin_lock_irqsave(&bt->rx_lock, flags);
		/* available data in RX packet buffer */
		avail = (bt->rx->packet_w - bt->rx->packet_r) * packet_size;

		cur_read_idx = (bt->rx->packet_r & SCO_RX_PACKET_MASK) *
			       packet_size;
		spin_unlock_irqrestore(&bt->rx_lock, flags);

		if (!avail) {
			int ret = wait_for_bt_irq(bt, bt->rx);

			if (ret)
				return read_count;

			continue;
		}

		/* count must be multiple of packet_size */
		if (count % packet_size != 0 ||
		    avail % packet_size != 0) {
			dev_warn(bt->dev, "%s(), count %zu or d %lu is not multiple of packet_size %dd\n",
				 __func__, count, avail, packet_size);

			count -= count % packet_size;
			avail -= avail % packet_size;
		}

		if (count > avail)
			read_size = avail;
		else
			read_size = count;

		/* calculate continue space */
		cont = bt->rx->buf_size - cur_read_idx;
		if (read_size > cont)
			read_size = cont;

		if (copy_to_user(buf + cur_buf_ofs,
				 bt->rx_packet_buf + cur_read_idx,
				 read_size)) {
			dev_warn(bt->dev, "%s(), copy_to_user fail\n",
				 __func__);
			return -EFAULT;
		}

		spin_lock_irqsave(&bt->rx_lock, flags);
		bt->rx->packet_r += read_size / packet_size;
		spin_unlock_irqrestore(&bt->rx_lock, flags);

		read_count += read_size;
		cur_buf_ofs += read_size;
		count -= read_size;
	}

	/*
	 * save current timestamp & buffer time in times_tamp and
	 * buf_data_equivalent_time
	 */
	bt->rx->time_stamp = sched_clock();
	bt->rx->buf_data_equivalent_time =
		(unsigned long long)(bt->rx->packet_w - bt->rx->packet_r) *
		SCO_RX_PLC_SIZE * 16 * 1000 / 2 / 64;
	bt->rx->buf_data_equivalent_time += read_count * SCO_RX_PLC_SIZE *
					    16 * 1000 / packet_size / 2 / 64;
	/* return equivalent time(us) to data count */
	bt->rx->buf_data_equivalent_time *= 1000;

	return read_count;
}

static ssize_t mtk_btcvsd_snd_write(struct mtk_btcvsd_snd *bt,
				    char __user *buf,
				    size_t count)
{
	int written_size = count, avail = 0, cur_write_idx, write_size, cont;
	unsigned int cur_buf_ofs = 0;
	unsigned long flags;
	unsigned int packet_size = bt->tx->packet_size;

	/*
	 * save current timestamp & buffer time in time_stamp and
	 * buf_data_equivalent_time
	 */
	bt->tx->time_stamp = sched_clock();
	bt->tx->buf_data_equivalent_time =
		(unsigned long long)(bt->tx->packet_w - bt->tx->packet_r) *
		packet_size * 16 * 1000 / 2 / 64;

	/* return equivalent time(us) to data count */
	bt->tx->buf_data_equivalent_time *= 1000;

	while (count) {
		spin_lock_irqsave(&bt->tx_lock, flags);
		/* free space of TX packet buffer */
		avail = bt->tx->buf_size -
			(bt->tx->packet_w - bt->tx->packet_r) * packet_size;

		cur_write_idx = (bt->tx->packet_w % SCO_TX_PACKER_BUF_NUM) *
				packet_size;
		spin_unlock_irqrestore(&bt->tx_lock, flags);

		if (!avail) {
			int ret = wait_for_bt_irq(bt, bt->rx);

			if (ret)
				return written_size;

			continue;
		}

		/* count must be multiple of bt->tx->packet_size */
		if (count % packet_size != 0 ||
		    avail % packet_size != 0) {
			dev_warn(bt->dev, "%s(), count %zu or avail %d is not multiple of packet_size %d\n",
				 __func__, count, avail, packet_size);
			count -= count % packet_size;
			avail -= avail % packet_size;
		}

		if (count > avail)
			write_size = avail;
		else
			write_size = count;

		/* calculate continue space */
		cont = bt->tx->buf_size - cur_write_idx;
		if (write_size > cont)
			write_size = cont;

		if (copy_from_user(bt->tx_packet_buf +
				   cur_write_idx,
				   buf + cur_buf_ofs,
				   write_size)) {
			dev_warn(bt->dev, "%s(), copy_from_user fail\n",
				 __func__);
			return -EFAULT;
		}

		spin_lock_irqsave(&bt->tx_lock, flags);
		bt->tx->packet_w += write_size / packet_size;
		spin_unlock_irqrestore(&bt->tx_lock, flags);
		cur_buf_ofs += write_size;
		count -= write_size;
	}

	return written_size;
}

static struct mtk_btcvsd_snd_stream *get_bt_stream
	(struct mtk_btcvsd_snd *bt, struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return bt->tx;
	else
		return bt->rx;
}

/* pcm ops */
static const struct snd_pcm_hardware mtk_btcvsd_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_RESUME),
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.buffer_bytes_max = 24 * 1024,
	.period_bytes_max = 24 * 1024,
	.periods_min = 2,
	.periods_max = 16,
	.fifo_size = 0,
};

static int mtk_pcm_btcvsd_open(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);
	int ret;

	dev_dbg(bt->dev, "%s(), stream %d, substream %p\n",
		__func__, substream->stream, substream);

	snd_soc_set_runtime_hwparams(substream, &mtk_btcvsd_hardware);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = mtk_btcvsd_snd_tx_init(bt);
		bt->tx->substream = substream;
	} else {
		ret = mtk_btcvsd_snd_rx_init(bt);
		bt->rx->substream = substream;
	}

	return ret;
}

static int mtk_pcm_btcvsd_close(struct snd_soc_component *component,
				struct snd_pcm_substream *substream)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);
	struct mtk_btcvsd_snd_stream *bt_stream = get_bt_stream(bt, substream);

	dev_dbg(bt->dev, "%s(), stream %d\n", __func__, substream->stream);

	mtk_btcvsd_snd_set_state(bt, bt_stream, BT_SCO_STATE_IDLE);
	bt_stream->substream = NULL;
	return 0;
}

static int mtk_pcm_btcvsd_hw_params(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    params_buffer_bytes(hw_params) % bt->tx->packet_size != 0) {
		dev_warn(bt->dev, "%s(), error, buffer size %d not valid\n",
			 __func__,
			 params_buffer_bytes(hw_params));
		return -EINVAL;
	}

	substream->runtime->dma_bytes = params_buffer_bytes(hw_params);
	return 0;
}

static int mtk_pcm_btcvsd_hw_free(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		btcvsd_tx_clean_buffer(bt);

	return 0;
}

static int mtk_pcm_btcvsd_prepare(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);
	struct mtk_btcvsd_snd_stream *bt_stream = get_bt_stream(bt, substream);

	dev_dbg(bt->dev, "%s(), stream %d\n", __func__, substream->stream);

	mtk_btcvsd_snd_set_state(bt, bt_stream, BT_SCO_STATE_RUNNING);
	return 0;
}

static int mtk_pcm_btcvsd_trigger(struct snd_soc_component *component,
				  struct snd_pcm_substream *substream, int cmd)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);
	struct mtk_btcvsd_snd_stream *bt_stream = get_bt_stream(bt, substream);
	int stream = substream->stream;
	int hw_packet_ptr;

	dev_dbg(bt->dev, "%s(), stream %d, cmd %d\n",
		__func__, substream->stream, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		hw_packet_ptr = stream == SNDRV_PCM_STREAM_PLAYBACK ?
				bt_stream->packet_r : bt_stream->packet_w;
		bt_stream->prev_packet_idx = hw_packet_ptr;
		bt_stream->prev_frame = 0;
		bt_stream->trigger_start = 1;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		bt_stream->trigger_start = 0;
		mtk_btcvsd_snd_set_state(bt, bt_stream, BT_SCO_STATE_ENDING);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t mtk_pcm_btcvsd_pointer(
	struct snd_soc_component *component,
	struct snd_pcm_substream *substream)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);
	struct mtk_btcvsd_snd_stream *bt_stream;
	snd_pcm_uframes_t frame = 0;
	int byte = 0;
	int hw_packet_ptr;
	int packet_diff;
	spinlock_t *lock;	/* spinlock for bt stream control */
	unsigned long flags;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		lock = &bt->tx_lock;
		bt_stream = bt->tx;
	} else {
		lock = &bt->rx_lock;
		bt_stream = bt->rx;
	}

	spin_lock_irqsave(lock, flags);
	hw_packet_ptr = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			bt->tx->packet_r : bt->rx->packet_w;

	/* get packet diff from last time */
	if (hw_packet_ptr >= bt_stream->prev_packet_idx) {
		packet_diff = hw_packet_ptr - bt_stream->prev_packet_idx;
	} else {
		/* integer overflow */
		packet_diff = (INT_MAX - bt_stream->prev_packet_idx) +
			      (hw_packet_ptr - INT_MIN) + 1;
	}
	bt_stream->prev_packet_idx = hw_packet_ptr;

	/* increased bytes */
	byte = packet_diff * bt_stream->packet_size;

	frame = btcvsd_bytes_to_frame(substream, byte);
	frame += bt_stream->prev_frame;
	frame %= substream->runtime->buffer_size;

	bt_stream->prev_frame = frame;

	spin_unlock_irqrestore(lock, flags);

	return frame;
}

static int mtk_pcm_btcvsd_copy(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       int channel, unsigned long pos,
			       void __user *buf, unsigned long count)
{
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(component);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return mtk_btcvsd_snd_write(bt, buf, count);
	else
		return mtk_btcvsd_snd_read(bt, buf, count);
}

/* kcontrol */
static const char *const btsco_band_str[] = {"NB", "WB"};

static const struct soc_enum btcvsd_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(btsco_band_str), btsco_band_str),
};

static int btcvsd_band_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.integer.value[0] = bt->band;
	return 0;
}

static int btcvsd_band_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	bt->band = ucontrol->value.integer.value[0];
	dev_dbg(bt->dev, "%s(), band %d\n", __func__, bt->band);
	return 0;
}

static int btcvsd_loopback_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);
	bool lpbk_en = bt->tx->state == BT_SCO_STATE_LOOPBACK;

	ucontrol->value.integer.value[0] = lpbk_en;
	return 0;
}

static int btcvsd_loopback_set(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (ucontrol->value.integer.value[0]) {
		mtk_btcvsd_snd_set_state(bt, bt->tx, BT_SCO_STATE_LOOPBACK);
		mtk_btcvsd_snd_set_state(bt, bt->rx, BT_SCO_STATE_LOOPBACK);
	} else {
		mtk_btcvsd_snd_set_state(bt, bt->tx, BT_SCO_STATE_RUNNING);
		mtk_btcvsd_snd_set_state(bt, bt->rx, BT_SCO_STATE_RUNNING);
	}
	return 0;
}

static int btcvsd_tx_mute_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (!bt->tx) {
		ucontrol->value.integer.value[0] = 0;
		return 0;
	}

	ucontrol->value.integer.value[0] = bt->tx->mute;
	return 0;
}

static int btcvsd_tx_mute_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (!bt->tx)
		return 0;

	bt->tx->mute = ucontrol->value.integer.value[0];
	return 0;
}

static int btcvsd_rx_irq_received_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (!bt->rx)
		return 0;

	ucontrol->value.integer.value[0] = bt->rx->rw_cnt ? 1 : 0;
	return 0;
}

static int btcvsd_rx_timeout_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (!bt->rx)
		return 0;

	ucontrol->value.integer.value[0] = bt->rx->timeout;
	bt->rx->timeout = 0;
	return 0;
}

static int btcvsd_rx_timestamp_get(struct snd_kcontrol *kcontrol,
				   unsigned int __user *data, unsigned int size)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);
	int ret = 0;
	struct mtk_btcvsd_snd_time_buffer_info time_buffer_info_rx;

	if (size > sizeof(struct mtk_btcvsd_snd_time_buffer_info))
		return -EINVAL;

	get_rx_time_stamp(bt, &time_buffer_info_rx);

	dev_dbg(bt->dev, "%s(), time_stamp_us %llu, data_count_equi_time %llu",
		__func__,
		time_buffer_info_rx.time_stamp_us,
		time_buffer_info_rx.data_count_equi_time);

	if (copy_to_user(data, &time_buffer_info_rx,
			 sizeof(struct mtk_btcvsd_snd_time_buffer_info))) {
		dev_warn(bt->dev, "%s(), copy_to_user fail", __func__);
		ret = -EFAULT;
	}

	return ret;
}

static int btcvsd_tx_irq_received_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	if (!bt->tx)
		return 0;

	ucontrol->value.integer.value[0] = bt->tx->rw_cnt ? 1 : 0;
	return 0;
}

static int btcvsd_tx_timeout_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.integer.value[0] = bt->tx->timeout;
	return 0;
}

static int btcvsd_tx_timestamp_get(struct snd_kcontrol *kcontrol,
				   unsigned int __user *data, unsigned int size)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_btcvsd_snd *bt = snd_soc_component_get_drvdata(cmpnt);
	int ret = 0;
	struct mtk_btcvsd_snd_time_buffer_info time_buffer_info_tx;

	if (size > sizeof(struct mtk_btcvsd_snd_time_buffer_info))
		return -EINVAL;

	get_tx_time_stamp(bt, &time_buffer_info_tx);

	dev_dbg(bt->dev, "%s(), time_stamp_us %llu, data_count_equi_time %llu",
		__func__,
		time_buffer_info_tx.time_stamp_us,
		time_buffer_info_tx.data_count_equi_time);

	if (copy_to_user(data, &time_buffer_info_tx,
			 sizeof(struct mtk_btcvsd_snd_time_buffer_info))) {
		dev_warn(bt->dev, "%s(), copy_to_user fail", __func__);
		ret = -EFAULT;
	}

	return ret;
}

static const struct snd_kcontrol_new mtk_btcvsd_snd_controls[] = {
	SOC_ENUM_EXT("BTCVSD Band", btcvsd_enum[0],
		     btcvsd_band_get, btcvsd_band_set),
	SOC_SINGLE_BOOL_EXT("BTCVSD Loopback Switch", 0,
			    btcvsd_loopback_get, btcvsd_loopback_set),
	SOC_SINGLE_BOOL_EXT("BTCVSD Tx Mute Switch", 0,
			    btcvsd_tx_mute_get, btcvsd_tx_mute_set),
	SOC_SINGLE_BOOL_EXT("BTCVSD Tx Irq Received Switch", 0,
			    btcvsd_tx_irq_received_get, NULL),
	SOC_SINGLE_BOOL_EXT("BTCVSD Tx Timeout Switch", 0,
			    btcvsd_tx_timeout_get, NULL),
	SOC_SINGLE_BOOL_EXT("BTCVSD Rx Irq Received Switch", 0,
			    btcvsd_rx_irq_received_get, NULL),
	SOC_SINGLE_BOOL_EXT("BTCVSD Rx Timeout Switch", 0,
			    btcvsd_rx_timeout_get, NULL),
	SND_SOC_BYTES_TLV("BTCVSD Rx Timestamp",
			  sizeof(struct mtk_btcvsd_snd_time_buffer_info),
			  btcvsd_rx_timestamp_get, NULL),
	SND_SOC_BYTES_TLV("BTCVSD Tx Timestamp",
			  sizeof(struct mtk_btcvsd_snd_time_buffer_info),
			  btcvsd_tx_timestamp_get, NULL),
};

static int mtk_btcvsd_snd_component_probe(struct snd_soc_component *component)
{
	return snd_soc_add_component_controls(component,
		mtk_btcvsd_snd_controls,
		ARRAY_SIZE(mtk_btcvsd_snd_controls));
}

static const struct snd_soc_component_driver mtk_btcvsd_snd_platform = {
	.name		= BTCVSD_SND_NAME,
	.probe		= mtk_btcvsd_snd_component_probe,
	.open		= mtk_pcm_btcvsd_open,
	.close		= mtk_pcm_btcvsd_close,
	.hw_params	= mtk_pcm_btcvsd_hw_params,
	.hw_free	= mtk_pcm_btcvsd_hw_free,
	.prepare	= mtk_pcm_btcvsd_prepare,
	.trigger	= mtk_pcm_btcvsd_trigger,
	.pointer	= mtk_pcm_btcvsd_pointer,
	.copy_user	= mtk_pcm_btcvsd_copy,
};

static int mtk_btcvsd_snd_probe(struct platform_device *pdev)
{
	int ret;
	int irq_id;
	u32 offset[5] = {0, 0, 0, 0, 0};
	struct mtk_btcvsd_snd *btcvsd;
	struct device *dev = &pdev->dev;

	/* init btcvsd private data */
	btcvsd = devm_kzalloc(dev, sizeof(*btcvsd), GFP_KERNEL);
	if (!btcvsd)
		return -ENOMEM;
	platform_set_drvdata(pdev, btcvsd);
	btcvsd->dev = dev;

	/* init tx/rx */
	btcvsd->rx = devm_kzalloc(btcvsd->dev, sizeof(*btcvsd->rx), GFP_KERNEL);
	if (!btcvsd->rx)
		return -ENOMEM;

	btcvsd->tx = devm_kzalloc(btcvsd->dev, sizeof(*btcvsd->tx), GFP_KERNEL);
	if (!btcvsd->tx)
		return -ENOMEM;

	spin_lock_init(&btcvsd->tx_lock);
	spin_lock_init(&btcvsd->rx_lock);

	init_waitqueue_head(&btcvsd->tx_wait);
	init_waitqueue_head(&btcvsd->rx_wait);

	mtk_btcvsd_snd_tx_init(btcvsd);
	mtk_btcvsd_snd_rx_init(btcvsd);

	/* irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id <= 0)
		return irq_id < 0 ? irq_id : -ENXIO;

	ret = devm_request_irq(dev, irq_id, mtk_btcvsd_snd_irq_handler,
			       IRQF_TRIGGER_LOW, "BTCVSD_ISR_Handle",
			       (void *)btcvsd);
	if (ret) {
		dev_err(dev, "could not request_irq for BTCVSD_ISR_Handle\n");
		return ret;
	}

	btcvsd->irq_id = irq_id;

	/* iomap */
	btcvsd->bt_pkv_base = of_iomap(dev->of_node, 0);
	if (!btcvsd->bt_pkv_base) {
		dev_err(dev, "iomap bt_pkv_base fail\n");
		return -EIO;
	}

	btcvsd->bt_sram_bank2_base = of_iomap(dev->of_node, 1);
	if (!btcvsd->bt_sram_bank2_base) {
		dev_err(dev, "iomap bt_sram_bank2_base fail\n");
		ret = -EIO;
		goto unmap_pkv_err;
	}

	btcvsd->infra = syscon_regmap_lookup_by_phandle(dev->of_node,
							"mediatek,infracfg");
	if (IS_ERR(btcvsd->infra)) {
		dev_err(dev, "cannot find infra controller: %ld\n",
			PTR_ERR(btcvsd->infra));
		ret = PTR_ERR(btcvsd->infra);
		goto unmap_bank2_err;
	}

	/* get offset */
	ret = of_property_read_u32_array(dev->of_node, "mediatek,offset",
					 offset,
					 ARRAY_SIZE(offset));
	if (ret) {
		dev_warn(dev, "%s(), get offset fail, ret %d\n", __func__, ret);
		goto unmap_bank2_err;
	}
	btcvsd->infra_misc_offset = offset[0];
	btcvsd->conn_bt_cvsd_mask = offset[1];
	btcvsd->cvsd_mcu_read_offset = offset[2];
	btcvsd->cvsd_mcu_write_offset = offset[3];
	btcvsd->cvsd_packet_indicator = offset[4];

	btcvsd->bt_reg_pkt_r = btcvsd->bt_pkv_base +
			       btcvsd->cvsd_mcu_read_offset;
	btcvsd->bt_reg_pkt_w = btcvsd->bt_pkv_base +
			       btcvsd->cvsd_mcu_write_offset;
	btcvsd->bt_reg_ctl = btcvsd->bt_pkv_base +
			     btcvsd->cvsd_packet_indicator;

	/* init state */
	mtk_btcvsd_snd_set_state(btcvsd, btcvsd->tx, BT_SCO_STATE_IDLE);
	mtk_btcvsd_snd_set_state(btcvsd, btcvsd->rx, BT_SCO_STATE_IDLE);

	ret = devm_snd_soc_register_component(dev, &mtk_btcvsd_snd_platform,
					      NULL, 0);
	if (ret)
		goto unmap_bank2_err;

	return 0;

unmap_bank2_err:
	iounmap(btcvsd->bt_sram_bank2_base);
unmap_pkv_err:
	iounmap(btcvsd->bt_pkv_base);
	return ret;
}

static int mtk_btcvsd_snd_remove(struct platform_device *pdev)
{
	struct mtk_btcvsd_snd *btcvsd = dev_get_drvdata(&pdev->dev);

	iounmap(btcvsd->bt_pkv_base);
	iounmap(btcvsd->bt_sram_bank2_base);
	return 0;
}

static const struct of_device_id mtk_btcvsd_snd_dt_match[] = {
	{ .compatible = "mediatek,mtk-btcvsd-snd", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_btcvsd_snd_dt_match);

static struct platform_driver mtk_btcvsd_snd_driver = {
	.driver = {
		.name = "mtk-btcvsd-snd",
		.of_match_table = mtk_btcvsd_snd_dt_match,
	},
	.probe = mtk_btcvsd_snd_probe,
	.remove = mtk_btcvsd_snd_remove,
};

module_platform_driver(mtk_btcvsd_snd_driver);

MODULE_DESCRIPTION("Mediatek ALSA BT SCO CVSD/MSBC Driver");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_LICENSE("GPL v2");
