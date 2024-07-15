/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011,2013-2015,2020 The Linux Foundation. All rights reserved.
 *
 * lpass.h - Definitions for the QTi LPASS
 */

#ifndef __LPASS_H__
#define __LPASS_H__

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <dt-bindings/sound/qcom,lpass.h>
#include "lpass-hdmi.h"

#define LPASS_AHBIX_CLOCK_FREQUENCY		131072000
#define LPASS_MAX_PORTS			(LPASS_CDC_DMA_VA_TX8 + 1)
#define LPASS_MAX_MI2S_PORTS			(8)
#define LPASS_MAX_DMA_CHANNELS			(8)
#define LPASS_MAX_HDMI_DMA_CHANNELS		(4)
#define LPASS_MAX_CDC_DMA_CHANNELS		(8)
#define LPASS_MAX_VA_CDC_DMA_CHANNELS		(8)
#define LPASS_CDC_DMA_INTF_ONE_CHANNEL		(0x01)
#define LPASS_CDC_DMA_INTF_TWO_CHANNEL		(0x03)
#define LPASS_CDC_DMA_INTF_FOUR_CHANNEL		(0x0F)
#define LPASS_CDC_DMA_INTF_SIX_CHANNEL		(0x3F)
#define LPASS_CDC_DMA_INTF_EIGHT_CHANNEL	(0xFF)

#define LPASS_ACTIVE_PDS			(4)
#define LPASS_PROXY_PDS			(8)

#define QCOM_REGMAP_FIELD_ALLOC(d, m, f, mf)    \
	do { \
		mf = devm_regmap_field_alloc(d, m, f);     \
		if (IS_ERR(mf))                \
			return -EINVAL;         \
	} while (0)

static inline bool is_cdc_dma_port(int dai_id)
{
	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		return true;
	}
	return false;
}

static inline bool is_rxtx_cdc_dma_port(int dai_id)
{
	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		return true;
	}
	return false;
}

struct lpaif_i2sctl {
	struct regmap_field *loopback;
	struct regmap_field *spken;
	struct regmap_field *spkmode;
	struct regmap_field *spkmono;
	struct regmap_field *micen;
	struct regmap_field *micmode;
	struct regmap_field *micmono;
	struct regmap_field *wssrc;
	struct regmap_field *bitwidth;
};


struct lpaif_dmactl {
	struct regmap_field *intf;
	struct regmap_field *bursten;
	struct regmap_field *wpscnt;
	struct regmap_field *fifowm;
	struct regmap_field *enable;
	struct regmap_field *dyncclk;
	struct regmap_field *burst8;
	struct regmap_field *burst16;
	struct regmap_field *dynburst;
	struct regmap_field *codec_enable;
	struct regmap_field *codec_pack;
	struct regmap_field *codec_intf;
	struct regmap_field *codec_fs_sel;
	struct regmap_field *codec_channel;
	struct regmap_field *codec_fs_delay;
};

/* Both the CPU DAI and platform drivers will access this data */
struct lpass_data {

	/* AHB-I/X bus clocks inside the low-power audio subsystem (LPASS) */
	struct clk *ahbix_clk;

	/* MI2S system clock */
	struct clk *mi2s_osr_clk[LPASS_MAX_MI2S_PORTS];

	/* MI2S bit clock (derived from system clock by a divider */
	struct clk *mi2s_bit_clk[LPASS_MAX_MI2S_PORTS];

	struct clk *codec_mem0;
	struct clk *codec_mem1;
	struct clk *codec_mem2;
	struct clk *va_mem0;

	/* MI2S SD lines to use for playback/capture */
	unsigned int mi2s_playback_sd_mode[LPASS_MAX_MI2S_PORTS];
	unsigned int mi2s_capture_sd_mode[LPASS_MAX_MI2S_PORTS];

	/* The state of MI2S prepare dai_ops was called */
	bool mi2s_was_prepared[LPASS_MAX_MI2S_PORTS];

	int hdmi_port_enable;
	int codec_dma_enable;

	/* low-power audio interface (LPAIF) registers */
	void __iomem *lpaif;
	void __iomem *hdmiif;
	void __iomem *rxtx_lpaif;
	void __iomem *va_lpaif;

	u32 rxtx_cdc_dma_lpm_buf;
	u32 va_cdc_dma_lpm_buf;

	/* regmap backed by the low-power audio interface (LPAIF) registers */
	struct regmap *lpaif_map;
	struct regmap *hdmiif_map;
	struct regmap *rxtx_lpaif_map;
	struct regmap *va_lpaif_map;

	/* interrupts from the low-power audio interface (LPAIF) */
	int lpaif_irq;
	int hdmiif_irq;
	int rxtxif_irq;
	int vaif_irq;

	/* SOC specific variations in the LPASS IP integration */
	const struct lpass_variant *variant;

	/* bit map to keep track of static channel allocations */
	unsigned long dma_ch_bit_map;
	unsigned long hdmi_dma_ch_bit_map;
	unsigned long rxtx_dma_ch_bit_map;
	unsigned long va_dma_ch_bit_map;

	/* used it for handling interrupt per dma channel */
	struct snd_pcm_substream *substream[LPASS_MAX_DMA_CHANNELS];
	struct snd_pcm_substream *hdmi_substream[LPASS_MAX_HDMI_DMA_CHANNELS];
	struct snd_pcm_substream *rxtx_substream[LPASS_MAX_CDC_DMA_CHANNELS];
	struct snd_pcm_substream *va_substream[LPASS_MAX_CDC_DMA_CHANNELS];

	/* SOC specific clock list */
	struct clk_bulk_data *clks;
	int num_clks;

	/* Regmap fields of I2SCTL & DMACTL registers bitfields */
	struct lpaif_i2sctl *i2sctl;
	struct lpaif_dmactl *rd_dmactl;
	struct lpaif_dmactl *wr_dmactl;
	struct lpaif_dmactl *hdmi_rd_dmactl;

	/* Regmap fields of CODEC DMA CTRL registers */
	struct lpaif_dmactl *rxtx_rd_dmactl;
	struct lpaif_dmactl *rxtx_wr_dmactl;
	struct lpaif_dmactl *va_wr_dmactl;

	/* Regmap fields of HDMI_CTRL registers*/
	struct regmap_field *hdmitx_legacy_en;
	struct regmap_field *hdmitx_parity_calc_en;
	struct regmap_field *hdmitx_ch_msb[LPASS_MAX_HDMI_DMA_CHANNELS];
	struct regmap_field *hdmitx_ch_lsb[LPASS_MAX_HDMI_DMA_CHANNELS];
	struct lpass_hdmi_tx_ctl *tx_ctl;
	struct lpass_vbit_ctrl *vbit_ctl;
	struct lpass_hdmitx_dmactl *hdmi_tx_dmactl[LPASS_MAX_HDMI_DMA_CHANNELS];
	struct lpass_dp_metadata_ctl *meta_ctl;
	struct lpass_sstream_ctl *sstream_ctl;
};

/* Vairant data per each SOC */
struct lpass_variant {
	u32	irq_reg_base;
	u32	irq_reg_stride;
	u32	irq_ports;
	u32	rdma_reg_base;
	u32	rdma_reg_stride;
	u32	rdma_channels;
	u32	hdmi_rdma_reg_base;
	u32	hdmi_rdma_reg_stride;
	u32	hdmi_rdma_channels;
	u32	wrdma_reg_base;
	u32	wrdma_reg_stride;
	u32	wrdma_channels;
	u32	rxtx_irq_reg_base;
	u32	rxtx_irq_reg_stride;
	u32	rxtx_irq_ports;
	u32	rxtx_rdma_reg_base;
	u32	rxtx_rdma_reg_stride;
	u32	rxtx_rdma_channels;
	u32	rxtx_wrdma_reg_base;
	u32	rxtx_wrdma_reg_stride;
	u32	rxtx_wrdma_channels;
	u32	va_irq_reg_base;
	u32	va_irq_reg_stride;
	u32	va_irq_ports;
	u32	va_rdma_reg_base;
	u32	va_rdma_reg_stride;
	u32	va_rdma_channels;
	u32	va_wrdma_reg_base;
	u32	va_wrdma_reg_stride;
	u32	va_wrdma_channels;
	u32	i2sctrl_reg_base;
	u32	i2sctrl_reg_stride;
	u32	i2s_ports;

	/* I2SCTL Register fields */
	struct reg_field loopback;
	struct reg_field spken;
	struct reg_field spkmode;
	struct reg_field spkmono;
	struct reg_field micen;
	struct reg_field micmode;
	struct reg_field micmono;
	struct reg_field wssrc;
	struct reg_field bitwidth;

	u32	hdmi_irq_reg_base;
	u32	hdmi_irq_reg_stride;
	u32	hdmi_irq_ports;

	/* HDMI specific controls */
	u32	hdmi_tx_ctl_addr;
	u32	hdmi_legacy_addr;
	u32	hdmi_vbit_addr;
	u32	hdmi_ch_lsb_addr;
	u32	hdmi_ch_msb_addr;
	u32	ch_stride;
	u32	hdmi_parity_addr;
	u32	hdmi_dmactl_addr;
	u32	hdmi_dma_stride;
	u32	hdmi_DP_addr;
	u32	hdmi_sstream_addr;

	/* HDMI SSTREAM CTRL fields  */
	struct reg_field sstream_en;
	struct reg_field dma_sel;
	struct reg_field auto_bbit_en;
	struct reg_field layout;
	struct reg_field layout_sp;
	struct reg_field set_sp_on_en;
	struct reg_field dp_audio;
	struct reg_field dp_staffing_en;
	struct reg_field dp_sp_b_hw_en;

	/* HDMI DP METADATA CTL fields */
	struct reg_field mute;
	struct reg_field as_sdp_cc;
	struct reg_field as_sdp_ct;
	struct reg_field aif_db4;
	struct reg_field frequency;
	struct reg_field mst_index;
	struct reg_field dptx_index;

	/* HDMI TX CTRL fields */
	struct reg_field soft_reset;
	struct reg_field force_reset;

	/* HDMI TX DMA CTRL */
	struct reg_field use_hw_chs;
	struct reg_field use_hw_usr;
	struct reg_field hw_chs_sel;
	struct reg_field hw_usr_sel;

	/* HDMI VBIT CTRL */
	struct reg_field replace_vbit;
	struct reg_field vbit_stream;

	/* HDMI TX LEGACY */
	struct reg_field legacy_en;

	/* HDMI TX PARITY */
	struct reg_field calc_en;

	/* HDMI CH LSB */
	struct reg_field lsb_bits;

	/* HDMI CH MSB */
	struct reg_field msb_bits;

	struct reg_field hdmi_rdma_bursten;
	struct reg_field hdmi_rdma_wpscnt;
	struct reg_field hdmi_rdma_fifowm;
	struct reg_field hdmi_rdma_enable;
	struct reg_field hdmi_rdma_dyncclk;
	struct reg_field hdmi_rdma_burst8;
	struct reg_field hdmi_rdma_burst16;
	struct reg_field hdmi_rdma_dynburst;

	/* RD_DMA Register fields */
	struct reg_field rdma_intf;
	struct reg_field rdma_bursten;
	struct reg_field rdma_wpscnt;
	struct reg_field rdma_fifowm;
	struct reg_field rdma_enable;
	struct reg_field rdma_dyncclk;

	/* WR_DMA Register fields */
	struct reg_field wrdma_intf;
	struct reg_field wrdma_bursten;
	struct reg_field wrdma_wpscnt;
	struct reg_field wrdma_fifowm;
	struct reg_field wrdma_enable;
	struct reg_field wrdma_dyncclk;

	/* CDC RXTX RD_DMA */
	struct reg_field rxtx_rdma_intf;
	struct reg_field rxtx_rdma_bursten;
	struct reg_field rxtx_rdma_wpscnt;
	struct reg_field rxtx_rdma_fifowm;
	struct reg_field rxtx_rdma_enable;
	struct reg_field rxtx_rdma_dyncclk;
	struct reg_field rxtx_rdma_burst8;
	struct reg_field rxtx_rdma_burst16;
	struct reg_field rxtx_rdma_dynburst;
	struct reg_field rxtx_rdma_codec_enable;
	struct reg_field rxtx_rdma_codec_pack;
	struct reg_field rxtx_rdma_codec_intf;
	struct reg_field rxtx_rdma_codec_fs_sel;
	struct reg_field rxtx_rdma_codec_ch;
	struct reg_field rxtx_rdma_codec_fs_delay;

	/* CDC RXTX WR_DMA */
	struct reg_field rxtx_wrdma_intf;
	struct reg_field rxtx_wrdma_bursten;
	struct reg_field rxtx_wrdma_wpscnt;
	struct reg_field rxtx_wrdma_fifowm;
	struct reg_field rxtx_wrdma_enable;
	struct reg_field rxtx_wrdma_dyncclk;
	struct reg_field rxtx_wrdma_burst8;
	struct reg_field rxtx_wrdma_burst16;
	struct reg_field rxtx_wrdma_dynburst;
	struct reg_field rxtx_wrdma_codec_enable;
	struct reg_field rxtx_wrdma_codec_pack;
	struct reg_field rxtx_wrdma_codec_intf;
	struct reg_field rxtx_wrdma_codec_fs_sel;
	struct reg_field rxtx_wrdma_codec_ch;
	struct reg_field rxtx_wrdma_codec_fs_delay;

	/* CDC VA WR_DMA */
	struct reg_field va_wrdma_intf;
	struct reg_field va_wrdma_bursten;
	struct reg_field va_wrdma_wpscnt;
	struct reg_field va_wrdma_fifowm;
	struct reg_field va_wrdma_enable;
	struct reg_field va_wrdma_dyncclk;
	struct reg_field va_wrdma_burst8;
	struct reg_field va_wrdma_burst16;
	struct reg_field va_wrdma_dynburst;
	struct reg_field va_wrdma_codec_enable;
	struct reg_field va_wrdma_codec_pack;
	struct reg_field va_wrdma_codec_intf;
	struct reg_field va_wrdma_codec_fs_sel;
	struct reg_field va_wrdma_codec_ch;
	struct reg_field va_wrdma_codec_fs_delay;

	/**
	 * on SOCs like APQ8016 the channel control bits start
	 * at different offset to ipq806x
	 **/
	u32	dmactl_audif_start;
	u32	wrdma_channel_start;
	u32	rxtx_wrdma_channel_start;
	u32	va_wrdma_channel_start;

	/* SOC specific initialization like clocks */
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);
	int (*alloc_dma_channel)(struct lpass_data *data, int direction, unsigned int dai_id);
	int (*free_dma_channel)(struct lpass_data *data, int ch, unsigned int dai_id);

	/* SOC specific dais */
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;
	const char * const *dai_osr_clk_names;
	const char * const *dai_bit_clk_names;

	/* SOC specific clocks configuration */
	const char **clk_name;
	int num_clks;
};

struct lpass_pcm_data {
	int dma_ch;
	int i2s_port;
};

/* register the platform driver from the CPU DAI driver */
int asoc_qcom_lpass_platform_register(struct platform_device *pdev);
void asoc_qcom_lpass_cpu_platform_remove(struct platform_device *pdev);
void asoc_qcom_lpass_cpu_platform_shutdown(struct platform_device *pdev);
int asoc_qcom_lpass_cpu_platform_probe(struct platform_device *pdev);
extern const struct snd_soc_dai_ops asoc_qcom_lpass_cpu_dai_ops;
extern const struct snd_soc_dai_ops asoc_qcom_lpass_cpu_dai_ops2;
extern const struct snd_soc_dai_ops asoc_qcom_lpass_cdc_dma_dai_ops;

#endif /* __LPASS_H__ */
