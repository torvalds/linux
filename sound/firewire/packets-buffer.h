#ifndef SOUND_FIREWIRE_PACKETS_BUFFER_H_INCLUDED
#define SOUND_FIREWIRE_PACKETS_BUFFER_H_INCLUDED

#include <linux/dma-mapping.h>
#include <linux/firewire.h>

/**
 * struct iso_packets_buffer - manages a buffer for many packets
 * @iso_buffer: the memory containing the packets
 * @packets: an array, with each element pointing to one packet
 */
struct iso_packets_buffer {
	struct fw_iso_buffer iso_buffer;
	struct {
		void *buffer;
		unsigned int offset;
	} *packets;
};

int iso_packets_buffer_init(struct iso_packets_buffer *b, struct fw_unit *unit,
			    unsigned int count, unsigned int packet_size,
			    enum dma_data_direction direction);
void iso_packets_buffer_destroy(struct iso_packets_buffer *b,
				struct fw_unit *unit);

#endif
