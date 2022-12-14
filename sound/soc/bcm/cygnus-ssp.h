/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2014-2015 Broadcom Corporation */
#ifndef __CYGNUS_SSP_H__
#define __CYGNUS_SSP_H__

#define CYGNUS_TDM_DAI_MAX_SLOTS 16

#define CYGNUS_MAX_PLAYBACK_PORTS 4
#define CYGNUS_MAX_CAPTURE_PORTS 3
#define CYGNUS_MAX_I2S_PORTS 3
#define CYGNUS_MAX_PORTS  CYGNUS_MAX_PLAYBACK_PORTS
#define CYGNUS_AUIDO_MAX_NUM_CLKS 3

#define CYGNUS_SSP_FRAMEBITS_DIV 1

#define CYGNUS_SSPMODE_I2S 0
#define CYGNUS_SSPMODE_TDM 1
#define CYGNUS_SSPMODE_UNKNOWN -1

#define CYGNUS_SSP_CLKSRC_PLL      0

/* Max string length of our dt property names */
#define PROP_LEN_MAX 40

struct ringbuf_regs {
	unsigned rdaddr;
	unsigned wraddr;
	unsigned baseaddr;
	unsigned endaddr;
	unsigned fmark;   /* freemark for play, fullmark for caputure */
	unsigned period_bytes;
	unsigned buf_size;
};

#define RINGBUF_REG_PLAYBACK(num) ((struct ringbuf_regs) { \
	.rdaddr = SRC_RBUF_ ##num## _RDADDR_OFFSET, \
	.wraddr = SRC_RBUF_ ##num## _WRADDR_OFFSET, \
	.baseaddr = SRC_RBUF_ ##num## _BASEADDR_OFFSET, \
	.endaddr = SRC_RBUF_ ##num## _ENDADDR_OFFSET, \
	.fmark = SRC_RBUF_ ##num## _FREE_MARK_OFFSET, \
	.period_bytes = 0, \
	.buf_size = 0, \
})

#define RINGBUF_REG_CAPTURE(num) ((struct ringbuf_regs)  { \
	.rdaddr = DST_RBUF_ ##num## _RDADDR_OFFSET, \
	.wraddr = DST_RBUF_ ##num## _WRADDR_OFFSET, \
	.baseaddr = DST_RBUF_ ##num## _BASEADDR_OFFSET, \
	.endaddr = DST_RBUF_ ##num## _ENDADDR_OFFSET, \
	.fmark = DST_RBUF_ ##num## _FULL_MARK_OFFSET, \
	.period_bytes = 0, \
	.buf_size = 0, \
})

enum cygnus_audio_port_type {
	PORT_TDM,
	PORT_SPDIF,
};

struct cygnus_ssp_regs {
	u32 i2s_stream_cfg;
	u32 i2s_cfg;
	u32 i2s_cap_stream_cfg;
	u32 i2s_cap_cfg;
	u32 i2s_mclk_cfg;

	u32 bf_destch_ctrl;
	u32 bf_destch_cfg;
	u32 bf_sourcech_ctrl;
	u32 bf_sourcech_cfg;
	u32 bf_sourcech_grp;
};

struct cygnus_track_clk {
	bool cap_en;
	bool play_en;
	bool cap_clk_en;
	bool play_clk_en;
};

struct cygnus_aio_port {
	int portnum;
	int mode;
	bool is_slave;
	int streams_on;   /* will be 0 if both capture and play are off */
	int fsync_width;
	int port_type;

	u32 mclk;
	u32 lrclk;
	u32 bit_per_frame;
	u32 pll_clk_num;

	struct cygnus_audio *cygaud;
	struct cygnus_ssp_regs regs;

	struct ringbuf_regs play_rb_regs;
	struct ringbuf_regs capture_rb_regs;

	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;

	struct cygnus_track_clk clk_trace;
};


struct cygnus_audio {
	struct cygnus_aio_port  portinfo[CYGNUS_MAX_PORTS];

	int irq_num;
	void __iomem *audio;
	struct device *dev;
	void __iomem *i2s_in;

	struct clk *audio_clk[CYGNUS_AUIDO_MAX_NUM_CLKS];
	int active_ports;
	unsigned long vco_rate;
};

extern int cygnus_ssp_get_mode(struct snd_soc_dai *cpu_dai);
extern int cygnus_ssp_add_pll_tweak_controls(struct snd_soc_pcm_runtime *rtd);
extern int cygnus_ssp_set_custom_fsync_width(struct snd_soc_dai *cpu_dai,
						int len);
extern int cygnus_soc_platform_register(struct device *dev,
					struct cygnus_audio *cygaud);
extern int cygnus_soc_platform_unregister(struct device *dev);
extern int cygnus_ssp_set_custom_fsync_width(struct snd_soc_dai *cpu_dai,
	int len);
#endif
