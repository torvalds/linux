#ifndef __nosy_dump_h__
#define __nosy_dump_h__

#define array_length(array) (sizeof(array) / sizeof(array[0]))

#define TCODE_WRITE_QUADLET         0x0
#define TCODE_WRITE_BLOCK           0x1
#define TCODE_WRITE_RESPONSE        0x2
#define TCODE_READ_QUADLET          0x4
#define TCODE_READ_BLOCK            0x5
#define TCODE_READ_QUADLET_RESPONSE 0x6
#define TCODE_READ_BLOCK_RESPONSE   0x7
#define TCODE_CYCLE_START           0x8
#define TCODE_LOCK_REQUEST          0x9
#define TCODE_ISO_DATA              0xa
#define TCODE_LOCK_RESPONSE         0xb
#define TCODE_PHY_PACKET            0x10

#define ACK_NO_ACK               0x0
#define ACK_COMPLETE             0x1
#define ACK_PENDING              0x2
#define ACK_BUSY_X               0x4
#define ACK_BUSY_A               0x5
#define ACK_BUSY_B               0x6
#define ACK_DATA_ERROR           0xd
#define ACK_TYPE_ERROR           0xe 

#define ACK_DONE(a)  ((a >> 2) == 0)
#define ACK_BUSY(a)  ((a >> 2) == 1)
#define ACK_ERROR(a) ((a >> 2) == 3)

#define SPEED_100                0x0
#define SPEED_200                0x1
#define SPEED_400                0x2

struct phy_packet {
  unsigned long timestamp;

  union {
    struct {
      unsigned int zero : 24;
      unsigned int phy_id : 6;
      unsigned int identifier : 2;
    } common, link_on;
    struct {
      unsigned int zero : 16;
      unsigned int gap_count : 6;
      unsigned int set_gap_count : 1;
      unsigned int set_root : 1;
      unsigned int root_id : 6;
      unsigned int identifier : 2;
    } phy_config;
    struct {
      unsigned int more_packets : 1;
      unsigned int initiated_reset : 1;
      unsigned int port2 : 2;
      unsigned int port1 : 2;
      unsigned int port0 : 2;
      unsigned int power_class : 3;
      unsigned int contender : 1;
      unsigned int phy_delay : 2;
      unsigned int phy_speed : 2;
      unsigned int gap_count : 6;
      unsigned int link_active : 1;
      unsigned int extended : 1;
      unsigned int phy_id : 6;
      unsigned int identifier : 2;
    } self_id;

    struct {
      unsigned int more_packets : 1;
      unsigned int reserved1 : 1;
      unsigned int porth : 2;
      unsigned int portg : 2;
      unsigned int portf : 2;
      unsigned int porte : 2;
      unsigned int portd : 2;
      unsigned int portc : 2;
      unsigned int portb : 2;
      unsigned int porta : 2;
      unsigned int reserved0 : 2;
      unsigned int sequence : 3;
      unsigned int extended : 1;
      unsigned int phy_id : 6;
      unsigned int identifier : 2;
    } ext_self_id;
  };

  unsigned long inverted;
  unsigned long ack;
};

#define PHY_PACKET_CONFIGURATION 0x00
#define PHY_PACKET_LINK_ON 0x01
#define PHY_PACKET_SELF_ID 0x02

struct link_packet {
  unsigned long timestamp;

  union {
    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned long offset_low;
    } common;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned long offset_low;

      unsigned long crc;
    } read_quadlet;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int reserved0 : 12;
      unsigned int rcode : 4;
      unsigned int source : 16;

      unsigned long reserved1;

      unsigned long data;
 
      unsigned long crc; 
    } read_quadlet_response;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned long offset_low;

      unsigned int extended_tcode : 16;
      unsigned int data_length : 16;

      unsigned long crc;
    } read_block;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int reserved0 : 12;
      unsigned int rcode : 4;
      unsigned int source : 16;

      unsigned long reserved1;

      unsigned int extended_tcode : 16;
      unsigned int data_length : 16;

      unsigned long crc; 

      unsigned long data[0];

      /* crc and ack follows. */

    } read_block_response;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned long offset_low;

      unsigned long data;
 
      unsigned long crc; 

    } write_quadlet;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned int offset_low : 32;

      unsigned int extended_tcode : 16;
      unsigned int data_length : 16;
 
      unsigned long crc; 
      unsigned long data[0];

      /* crc and ack follows. */

    } write_block;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int reserved0 : 12;
      unsigned int rcode : 4;
      unsigned int source : 16;

      unsigned long reserved1;

      unsigned long crc; 
    } write_response;

    struct {
      unsigned int priority : 4;
      unsigned int tcode : 4;
      unsigned int rt : 2;
      unsigned int tlabel : 6;
      unsigned int destination : 16;

      unsigned int offset_high : 16;
      unsigned int source : 16;

      unsigned long offset_low;

      unsigned long data;

      unsigned long crc; 
    } cycle_start;

    struct {
      unsigned int sy : 4;
      unsigned int tcode : 4;
      unsigned int channel : 6;
      unsigned int tag : 2;
      unsigned int data_length : 16;

      unsigned long crc;
    } iso_data;

  };

};

struct subaction {
  unsigned long ack;
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
