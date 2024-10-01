/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __Q6DSP_AUDIO_CLOCKS_H__
#define __Q6DSP_AUDIO_CLOCKS_H__

struct q6dsp_clk_init {
	int clk_id;
	int q6dsp_clk_id;
	char *name;
	int rate;
};

#define Q6DSP_VOTE_CLK(id, blkid, n) {			\
		.clk_id	= id,				\
		.q6dsp_clk_id = blkid,			\
		.name = n,				\
	}

struct q6dsp_clk_desc {
	const struct q6dsp_clk_init *clks;
	size_t num_clks;
	int (*lpass_set_clk)(struct device *dev, int clk_id, int attr,
			      int root_clk, unsigned int freq);
	int (*lpass_vote_clk)(struct device *dev, uint32_t hid, const char *n, uint32_t *h);
	int (*lpass_unvote_clk)(struct device *dev, uint32_t hid, uint32_t h);
};

int q6dsp_clock_dev_probe(struct platform_device *pdev);

#endif  /* __Q6DSP_AUDIO_CLOCKS_H__ */
