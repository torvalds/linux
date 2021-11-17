/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LPASS_WSA_MACRO_H__
#define __LPASS_WSA_MACRO_H__

/*
 * Selects compander and smart boost settings
 * for a given speaker mode
 */
enum {
	WSA_MACRO_SPKR_MODE_DEFAULT,
	WSA_MACRO_SPKR_MODE_1, /* COMP Gain = 12dB, Smartboost Max = 5.5V */
};

int wsa_macro_set_spkr_mode(struct snd_soc_component *component, int mode);

#endif /* __LPASS_WSA_MACRO_H__ */
