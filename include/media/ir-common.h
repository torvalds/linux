/*
 *
 * some common structs and functions to handle infrared remotes via
 * input layer ...
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _IR_COMMON
#define _IR_COMMON

#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <media/ir-core.h>

#define RC5_START(x)	(((x)>>12)&3)
#define RC5_TOGGLE(x)	(((x)>>11)&1)
#define RC5_ADDR(x)	(((x)>>6)&31)
#define RC5_INSTR(x)	((x)&63)

struct ir_input_state {
	/* configuration */
	enum ir_type       ir_type;

	/* key info */
	u32                ir_key;      /* ir scancode */
	u32                keycode;     /* linux key code */
	int                keypressed;  /* current state */
};

/* this was saa7134_ir and bttv_ir, moved here for
 * rc5 decoding. */
struct card_ir {
	struct input_dev        *dev;
	struct ir_input_state   ir;
	char                    name[32];
	char                    phys[32];

	/* Usual gpio signalling */

	u32                     mask_keycode;
	u32                     mask_keydown;
	u32                     mask_keyup;
	u32                     polling;
	u32                     last_gpio;
	int			shift_by;
	int			start; // What should RC5_START() be
	int			addr; // What RC5_ADDR() should be.
	int			rc5_key_timeout;
	int			rc5_remote_gap;
	struct work_struct      work;
	struct timer_list       timer;

	/* RC5 gpio */
	u32 rc5_gpio;
	struct timer_list timer_end;	/* timer_end for code completion */
	struct timer_list timer_keyup;	/* timer_end for key release */
	u32 last_rc5;			/* last good rc5 code */
	u32 last_bit;			/* last raw bit seen */
	u32 code;			/* raw code under construction */
	struct timeval base_time;	/* time of last seen code */
	int active;			/* building raw code */

	/* NEC decoding */
	u32			nec_gpio;
	struct tasklet_struct   tlet;
};

/* Routines from ir-functions.c */

int ir_input_init(struct input_dev *dev, struct ir_input_state *ir,
		   const enum ir_type ir_type);
void ir_input_nokey(struct input_dev *dev, struct ir_input_state *ir);
void ir_input_keydown(struct input_dev *dev, struct ir_input_state *ir,
		      u32 ir_key);
u32  ir_extract_bits(u32 data, u32 mask);
int  ir_dump_samples(u32 *samples, int count);
int  ir_decode_biphase(u32 *samples, int count, int low, int high);
int  ir_decode_pulsedistance(u32 *samples, int count, int low, int high);
u32  ir_rc5_decode(unsigned int code);

void ir_rc5_timer_end(unsigned long data);
void ir_rc5_timer_keyup(unsigned long data);

/* scancode->keycode map tables from ir-keymaps.c */

extern struct ir_scancode_table ir_codes_empty_table;
extern struct ir_scancode_table ir_codes_avermedia_table;
extern struct ir_scancode_table ir_codes_avermedia_dvbt_table;
extern struct ir_scancode_table ir_codes_avermedia_m135a_table;
extern struct ir_scancode_table ir_codes_avermedia_cardbus_table;
extern struct ir_scancode_table ir_codes_apac_viewcomp_table;
extern struct ir_scancode_table ir_codes_pixelview_table;
extern struct ir_scancode_table ir_codes_pixelview_new_table;
extern struct ir_scancode_table ir_codes_nebula_table;
extern struct ir_scancode_table ir_codes_dntv_live_dvb_t_table;
extern struct ir_scancode_table ir_codes_iodata_bctv7e_table;
extern struct ir_scancode_table ir_codes_adstech_dvb_t_pci_table;
extern struct ir_scancode_table ir_codes_msi_tvanywhere_table;
extern struct ir_scancode_table ir_codes_cinergy_1400_table;
extern struct ir_scancode_table ir_codes_avertv_303_table;
extern struct ir_scancode_table ir_codes_dntv_live_dvbt_pro_table;
extern struct ir_scancode_table ir_codes_em_terratec_table;
extern struct ir_scancode_table ir_codes_pinnacle_grey_table;
extern struct ir_scancode_table ir_codes_flyvideo_table;
extern struct ir_scancode_table ir_codes_flydvb_table;
extern struct ir_scancode_table ir_codes_cinergy_table;
extern struct ir_scancode_table ir_codes_eztv_table;
extern struct ir_scancode_table ir_codes_avermedia_table;
extern struct ir_scancode_table ir_codes_videomate_tv_pvr_table;
extern struct ir_scancode_table ir_codes_manli_table;
extern struct ir_scancode_table ir_codes_gotview7135_table;
extern struct ir_scancode_table ir_codes_purpletv_table;
extern struct ir_scancode_table ir_codes_pctv_sedna_table;
extern struct ir_scancode_table ir_codes_pv951_table;
extern struct ir_scancode_table ir_codes_rc5_tv_table;
extern struct ir_scancode_table ir_codes_winfast_table;
extern struct ir_scancode_table ir_codes_pinnacle_color_table;
extern struct ir_scancode_table ir_codes_hauppauge_new_table;
extern struct ir_scancode_table ir_codes_rc5_hauppauge_new_table;
extern struct ir_scancode_table ir_codes_npgtech_table;
extern struct ir_scancode_table ir_codes_norwood_table;
extern struct ir_scancode_table ir_codes_proteus_2309_table;
extern struct ir_scancode_table ir_codes_budget_ci_old_table;
extern struct ir_scancode_table ir_codes_asus_pc39_table;
extern struct ir_scancode_table ir_codes_encore_enltv_table;
extern struct ir_scancode_table ir_codes_encore_enltv2_table;
extern struct ir_scancode_table ir_codes_tt_1500_table;
extern struct ir_scancode_table ir_codes_fusionhdtv_mce_table;
extern struct ir_scancode_table ir_codes_behold_table;
extern struct ir_scancode_table ir_codes_behold_columbus_table;
extern struct ir_scancode_table ir_codes_pinnacle_pctv_hd_table;
extern struct ir_scancode_table ir_codes_genius_tvgo_a11mce_table;
extern struct ir_scancode_table ir_codes_powercolor_real_angel_table;
extern struct ir_scancode_table ir_codes_avermedia_a16d_table;
extern struct ir_scancode_table ir_codes_encore_enltv_fm53_table;
extern struct ir_scancode_table ir_codes_real_audio_220_32_keys_table;
extern struct ir_scancode_table ir_codes_msi_tvanywhere_plus_table;
extern struct ir_scancode_table ir_codes_ati_tv_wonder_hd_600_table;
extern struct ir_scancode_table ir_codes_kworld_plus_tv_analog_table;
extern struct ir_scancode_table ir_codes_kaiomy_table;
extern struct ir_scancode_table ir_codes_dm1105_nec_table;
extern struct ir_scancode_table ir_codes_tevii_nec_table;
extern struct ir_scancode_table ir_codes_tbs_nec_table;
extern struct ir_scancode_table ir_codes_evga_indtube_table;
extern struct ir_scancode_table ir_codes_terratec_cinergy_xs_table;
extern struct ir_scancode_table ir_codes_videomate_s350_table;
extern struct ir_scancode_table ir_codes_gadmei_rm008z_table;
extern struct ir_scancode_table ir_codes_nec_terratec_cinergy_xs_table;
#endif
