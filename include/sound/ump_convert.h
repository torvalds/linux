// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __SOUND_UMP_CONVERT_H
#define __SOUND_UMP_CONVERT_H

#include <sound/ump_msg.h>

/* context for converting from legacy control messages to UMP packet */
struct ump_cvt_to_ump_bank {
	bool rpn_set;
	bool nrpn_set;
	bool bank_set;
	unsigned char cc_rpn_msb, cc_rpn_lsb;
	unsigned char cc_nrpn_msb, cc_nrpn_lsb;
	unsigned char cc_data_msb, cc_data_lsb;
	unsigned char cc_bank_msb, cc_bank_lsb;
	bool cc_data_msb_set, cc_data_lsb_set;
};

/* context for converting from MIDI1 byte stream to UMP packet */
struct ump_cvt_to_ump {
	/* MIDI1 intermediate buffer */
	unsigned char buf[4];
	int len;
	int cmd_bytes;

	/* UMP output packet */
	u32 ump[4];
	int ump_bytes;

	/* various status */
	unsigned int in_sysex;
	struct ump_cvt_to_ump_bank bank[16];	/* per channel */
};

int snd_ump_convert_from_ump(const u32 *data, unsigned char *dst,
			     unsigned char *group_ret);
void snd_ump_convert_to_ump(struct ump_cvt_to_ump *cvt, unsigned char group,
			    unsigned int protocol, unsigned char c);

/* reset the converter context, called at each open to ump */
static inline void snd_ump_convert_reset(struct ump_cvt_to_ump *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

}

#endif /* __SOUND_UMP_CONVERT_H */
