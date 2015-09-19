#ifndef SOUND_FIREWIRE_AMDTP_AM824_H_INCLUDED
#define SOUND_FIREWIRE_AMDTP_AM824_H_INCLUDED

#include <sound/pcm.h>
#include <sound/rawmidi.h>

#include "amdtp-stream.h"

int amdtp_am824_set_parameters(struct amdtp_stream *s, unsigned int rate,
			       unsigned int pcm_channels,
			       unsigned int midi_ports,
			       bool double_pcm_frames);

void amdtp_am824_set_pcm_position(struct amdtp_stream *s, unsigned int index,
				 unsigned int position);

void amdtp_am824_set_midi_position(struct amdtp_stream *s,
				   unsigned int position);

int amdtp_am824_add_pcm_hw_constraints(struct amdtp_stream *s,
				       struct snd_pcm_runtime *runtime);

void amdtp_am824_midi_trigger(struct amdtp_stream *s, unsigned int port,
			      struct snd_rawmidi_substream *midi);

int amdtp_am824_init(struct amdtp_stream *s, struct fw_unit *unit,
		     enum amdtp_stream_direction dir, enum cip_flags flags);
#endif
