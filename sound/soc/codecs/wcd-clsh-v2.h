/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _WCD_CLSH_V2_H_
#define _WCD_CLSH_V2_H_
#include <sound/soc.h>

enum wcd_clsh_event {
	WCD_CLSH_EVENT_PRE_DAC = 1,
	WCD_CLSH_EVENT_POST_PA,
};

/*
 * Basic states for Class H state machine.
 * represented as a bit mask within a u8 data type
 * bit 0: EAR mode
 * bit 1: HPH Left mode
 * bit 2: HPH Right mode
 * bit 3: Lineout mode
 */
#define	WCD_CLSH_STATE_IDLE	0
#define	WCD_CLSH_STATE_EAR	BIT(0)
#define	WCD_CLSH_STATE_HPHL	BIT(1)
#define	WCD_CLSH_STATE_HPHR	BIT(2)
#define	WCD_CLSH_STATE_LO	BIT(3)
#define	WCD_CLSH_STATE_AUX	BIT(4)
#define WCD_CLSH_STATE_MAX	4
#define WCD_CLSH_V3_STATE_MAX	5
#define NUM_CLSH_STATES_V2	BIT(WCD_CLSH_STATE_MAX)
#define NUM_CLSH_STATES_V3	BIT(WCD_CLSH_V3_STATE_MAX)

enum wcd_clsh_mode {
	CLS_H_NORMAL = 0, /* Class-H Default */
	CLS_H_HIFI, /* Class-H HiFi */
	CLS_H_LP, /* Class-H Low Power */
	CLS_AB, /* Class-AB */
	CLS_H_LOHIFI, /* LoHIFI */
	CLS_H_ULP, /* Ultra Low power */
	CLS_AB_HIFI, /* Class-AB */
	CLS_AB_LP, /* Class-AB Low Power */
	CLS_AB_LOHIFI, /* Class-AB Low HIFI */
	CLS_NONE, /* None of the above modes */
};

enum wcd_codec_version {
	WCD9335  = 0,
	WCD934X  = 1,
	/* New CLSH after this */
	WCD937X  = 2,
	WCD938X  = 3,
	WCD939X  = 4,
};
struct wcd_clsh_ctrl;

extern struct wcd_clsh_ctrl *wcd_clsh_ctrl_alloc(
				struct snd_soc_component *comp,
				int version);
extern void wcd_clsh_ctrl_free(struct wcd_clsh_ctrl *ctrl);
extern int wcd_clsh_ctrl_get_state(struct wcd_clsh_ctrl *ctrl);
extern int wcd_clsh_ctrl_set_state(struct wcd_clsh_ctrl *ctrl,
				   enum wcd_clsh_event clsh_event,
				   int nstate,
				   enum wcd_clsh_mode mode);
extern void wcd_clsh_set_hph_mode(struct wcd_clsh_ctrl *ctrl,
				  int mode);

#endif /* _WCD_CLSH_V2_H_ */
