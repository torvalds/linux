/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SOUND_FIREWIRE_FCP_H_INCLUDED
#define SOUND_FIREWIRE_FCP_H_INCLUDED

#define	AVC_PLUG_INFO_BUF_BYTES	4

struct fw_unit;

/*
 * AV/C Digital Interface Command Set General Specification 4.2
 * (Sep 2004, 1394TA)
 */
enum avc_general_plug_dir {
	AVC_GENERAL_PLUG_DIR_IN		= 0,
	AVC_GENERAL_PLUG_DIR_OUT	= 1,
	AVC_GENERAL_PLUG_DIR_COUNT
};
int avc_general_set_sig_fmt(struct fw_unit *unit, unsigned int rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_sig_fmt(struct fw_unit *unit, unsigned int *rate,
			    enum avc_general_plug_dir dir,
			    unsigned short plug);
int avc_general_get_plug_info(struct fw_unit *unit, unsigned int subunit_type,
			      unsigned int subunit_id, unsigned int subfunction,
			      u8 info[AVC_PLUG_INFO_BUF_BYTES]);

int fcp_avc_transaction(struct fw_unit *unit,
			const void *command, unsigned int command_size,
			void *response, unsigned int response_size,
			unsigned int response_match_bytes);
void fcp_bus_reset(struct fw_unit *unit);

#endif
