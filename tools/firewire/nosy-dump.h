#ifndef __nosy_dump_h__
#define __nosy_dump_h__

#define array_length(array) (sizeof(array) / sizeof(array[0]))

#define ACK_NO_ACK   0x0
#define ACK_DONE(a)  ((a >> 2) == 0)
#define ACK_BUSY(a)  ((a >> 2) == 1)
#define ACK_ERROR(a) ((a >> 2) == 3)

#include <stdint.h>

struct phy_packet {
	uint32_t timestamp;
	union {
		struct {
			uint32_t zero:24;
			uint32_t phy_id:6;
			uint32_t identifier:2;
		} common, link_on;

		struct {
			uint32_t zero:16;
			uint32_t gap_count:6;
			uint32_t set_gap_count:1;
			uint32_t set_root:1;
			uint32_t root_id:6;
			uint32_t identifier:2;
		} phy_config;

		struct {
			uint32_t more_packets:1;
			uint32_t initiated_reset:1;
			uint32_t port2:2;
			uint32_t port1:2;
			uint32_t port0:2;
			uint32_t power_class:3;
			uint32_t contender:1;
			uint32_t phy_delay:2;
			uint32_t phy_speed:2;
			uint32_t gap_count:6;
			uint32_t link_active:1;
			uint32_t extended:1;
			uint32_t phy_id:6;
			uint32_t identifier:2;
		} self_id;

		struct {
			uint32_t more_packets:1;
			uint32_t reserved1:1;
			uint32_t porth:2;
			uint32_t portg:2;
			uint32_t portf:2;
			uint32_t porte:2;
			uint32_t portd:2;
			uint32_t portc:2;
			uint32_t portb:2;
			uint32_t porta:2;
			uint32_t reserved0:2;
			uint32_t sequence:3;
			uint32_t extended:1;
			uint32_t phy_id:6;
			uint32_t identifier:2;
		} ext_self_id;
	};
	uint32_t inverted;
	uint32_t ack;
};

#define TCODE_PHY_PACKET 0x10

#define PHY_PACKET_CONFIGURATION 0x00
#define PHY_PACKET_LINK_ON 0x01
#define PHY_PACKET_SELF_ID 0x02

struct link_packet {
	uint32_t timestamp;
	union {
		struct {
			uint32_t priority:4;
			uint32_t tcode:4;
			uint32_t rt:2;
			uint32_t tlabel:6;
			uint32_t destination:16;

			uint32_t offset_high:16;
			uint32_t source:16;

			uint32_t offset_low;
		} common;

		struct {
			uint32_t common[3];
			uint32_t crc;
		} read_quadlet;

		struct {
			uint32_t common[3];
			uint32_t data;
			uint32_t crc;
		} read_quadlet_response;

		struct {
			uint32_t common[3];
			uint32_t extended_tcode:16;
			uint32_t data_length:16;
			uint32_t crc;
		} read_block;

		struct {
			uint32_t common[3];
			uint32_t extended_tcode:16;
			uint32_t data_length:16;
			uint32_t crc;
			uint32_t data[0];
			/* crc and ack follows. */
		} read_block_response;

		struct {
			uint32_t common[3];
			uint32_t data;
			uint32_t crc;
		} write_quadlet;

		struct {
			uint32_t common[3];
			uint32_t extended_tcode:16;
			uint32_t data_length:16;
			uint32_t crc;
			uint32_t data[0];
			/* crc and ack follows. */
		} write_block;

		struct {
			uint32_t common[3];
			uint32_t crc;
		} write_response;

		struct {
			uint32_t common[3];
			uint32_t data;
			uint32_t crc;
		} cycle_start;

		struct {
			uint32_t sy:4;
			uint32_t tcode:4;
			uint32_t channel:6;
			uint32_t tag:2;
			uint32_t data_length:16;

			uint32_t crc;
		} iso_data;
	};
};

struct subaction {
	uint32_t ack;
	size_t length;
	struct list link;
	struct link_packet packet;
};

struct link_transaction {
	int request_node, response_node, tlabel;
	struct subaction *request, *response;
	struct list request_list, response_list;
	struct list link;
};

int decode_fcp(struct link_transaction *t);

#endif /* __nosy_dump_h__ */
