/*
 * AM824 format in Audio and Music Data Transmission Protocol (IEC 61883-6)
 *
 * Copyright (c) 2015 Takashi Sakamoto <o-takashi@sakamocchi.jp>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include "amdtp-am824.h"

#define CIP_FMT_AM		0x10

/**
 * amdtp_am824_init - initialize an AMDTP stream structure to handle AM824
 *		      data block
 * @s: the AMDTP stream to initialize
 * @unit: the target of the stream
 * @dir: the direction of stream
 * @flags: the packet transmission method to use
 */
int amdtp_am824_init(struct amdtp_stream *s, struct fw_unit *unit,
		     enum amdtp_stream_direction dir, enum cip_flags flags)
{
	return amdtp_stream_init(s, unit, dir, flags, CIP_FMT_AM);
}
EXPORT_SYMBOL_GPL(amdtp_am824_init);
