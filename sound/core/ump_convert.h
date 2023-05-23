// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef __UMP_CONVERT_H
#define __UMP_CONVERT_H

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

int snd_ump_convert_init(struct snd_ump_endpoint *ump);
void snd_ump_convert_free(struct snd_ump_endpoint *ump);
int snd_ump_convert_from_ump(struct snd_ump_endpoint *ump,
			     const u32 *data, unsigned char *dst,
			     unsigned char *group_ret);
void snd_ump_convert_to_ump(struct snd_ump_endpoint *ump,
			    unsigned char group, unsigned char c);
void snd_ump_reset_convert_to_ump(struct snd_ump_endpoint *ump,
				  unsigned char group);
#endif /* __UMP_CONVERT_H */
