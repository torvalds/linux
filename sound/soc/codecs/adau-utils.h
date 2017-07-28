#ifndef SOUND_SOC_CODECS_ADAU_PLL_H
#define SOUND_SOC_CODECS_ADAU_PLL_H

int adau_calc_pll_cfg(unsigned int freq_in, unsigned int freq_out,
	uint8_t regs[5]);

#endif
