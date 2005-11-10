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

#define IR_TYPE_RC5     1
#define IR_TYPE_PD      2 /* Pulse distance encoded IR */
#define IR_TYPE_OTHER  99

#define IR_KEYTAB_TYPE	u32
#define IR_KEYTAB_SIZE	128  // enougth for rc5, probably need more some day ...

#define IR_KEYCODE(tab,code)	(((unsigned)code < IR_KEYTAB_SIZE) \
				 ? tab[code] : KEY_RESERVED)

struct ir_input_state {
	/* configuration */
	int                ir_type;
	IR_KEYTAB_TYPE     ir_codes[IR_KEYTAB_SIZE];

	/* key info */
	u32                ir_raw;      /* raw data */
	u32                ir_key;      /* ir key code */
	u32                keycode;     /* linux key code */
	int                keypressed;  /* current state */
};

extern IR_KEYTAB_TYPE ir_codes_rc5_tv[IR_KEYTAB_SIZE];
extern IR_KEYTAB_TYPE ir_codes_winfast[IR_KEYTAB_SIZE];
extern IR_KEYTAB_TYPE ir_codes_empty[IR_KEYTAB_SIZE];
extern IR_KEYTAB_TYPE ir_codes_hauppauge_new[IR_KEYTAB_SIZE];
extern IR_KEYTAB_TYPE ir_codes_pixelview[IR_KEYTAB_SIZE];

void ir_input_init(struct input_dev *dev, struct ir_input_state *ir,
		   int ir_type, IR_KEYTAB_TYPE *ir_codes);
void ir_input_nokey(struct input_dev *dev, struct ir_input_state *ir);
void ir_input_keydown(struct input_dev *dev, struct ir_input_state *ir,
		      u32 ir_key, u32 ir_raw);
u32  ir_extract_bits(u32 data, u32 mask);
int  ir_dump_samples(u32 *samples, int count);
int  ir_decode_biphase(u32 *samples, int count, int low, int high);
int  ir_decode_pulsedistance(u32 *samples, int count, int low, int high);

#endif

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
