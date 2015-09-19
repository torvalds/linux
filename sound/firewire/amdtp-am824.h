#ifndef SOUND_FIREWIRE_AMDTP_AM824_H_INCLUDED
#define SOUND_FIREWIRE_AMDTP_AM824_H_INCLUDED

#include "amdtp-stream.h"

int amdtp_am824_init(struct amdtp_stream *s, struct fw_unit *unit,
		     enum amdtp_stream_direction dir, enum cip_flags flags);
#endif
