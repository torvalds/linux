#ifndef __E100_IX__
#define __E100_IX__

#define DRIVER_NAME			"e100-ix"

/* misc useful bits, use this or define your own bits */
#define CB_SET_INDIVIDUAL_ADDR		0x01
#define CB_TRANSMIT			0x04

#define CU_START			0x10
#define CU_RESUME			0x20
#define RU_START			0x01
#define RU_RESUME			0x02
#define RU_SUSPENDED			0x04
#define CU_SUSPENDED			0x40

#define SOFTWARE_RESET			0x00000000

#define ENABLE_INTERRUPTS		0x00
#define DISABLE_INTERRUPTS		0x01

#define MAC_ADDRESS_LEN			6
#define DATA_LEN			1518

#define CSR_COMMAND			0x02
#define CSR_INT_CONTROL			0x03

#define CB_RING_LEN			64
#define RFD_RING_LEN			64

#define E100_VENDOR			0x8086
#define E100_DEVICE			0x1209

/* struct csr - Control/Status Register */
struct csr {
	/* System-Control Block */
	struct {
		u8 status;
		u8 stat_ack;
		u8 cmd_lo;
		u8 cmd_hi;
		u32 gen_ptr;
	} scb;

	/* Device Reset */
	u32 port;
	u16 flash_ctrl;
	u8 eeprom_ctrl_lo;
	u8 eeprom_ctrl_hi;
	u32 mdi_ctrl;
	u32 rx_dma_count;
};

/* struct tcb - Transmit Command Block */
struct tcb {
	u32 tbd_array;
	u16 tcb_byte_count;
	u8 threshold;
	u8 tbd_number;

	/* Transmit Buffer Descriptor */
	struct {
		__le32 buf_addr;
		__le16 size;
		u16 unused;
	} tbd;
};

/* struct cb - Command Block */
struct cb {
	struct cb_status {
		u16 unused1:12;
		u8 u:1;
		u8 ok:1;
		u8 unused2:1;
		u8 c:1;
	} status;

	struct cb_command {
		u16 cmd:3;
		u8 sf:1;
		u8 nc:1;
		u8 zero:3;
		u8 cid:5;
		u8 i:1;
		u8 suspend:1;
		u8 el:1;
	} command;

	u32 link;

	union {
		/* Transmit Command Block */
		struct tcb tcb;

		/* Individual Address Setup */
		u8 ias[8];
	} u;

	struct cb *prev, *next; /* for CBL ring buffer */
	dma_addr_t dma_addr;
	struct sk_buff *skb; /* when CB is of Transmit Command Block type */
};

/* struct rfd - Receive Frame Descriptor */
struct rfd {
	struct rfd_status {
		u16 status:13;
		u8 ok:1;
		u8 zero:1;
		u8 c:1;
	} status;

	struct rfd_command {
		u16 zero1:3;
		u8 sf:1;
		u8 h:1;
		u16 zero2:9;
		u8 suspend:1;
		u8 el:1;
	} command;

	u32 link;

	u32 reserved;

	u16 actual_count:14;
	u8 f:1;
	u8 eol:1;
	u16 size;

	char data[DATA_LEN];

	struct rfd *prev, *next;
	dma_addr_t dma_addr;
};

struct csr_stat_ack {
	u8 fcp:1;
	u8 res:1;
	u8 swi:1;
	u8 mdi:1;
	u8 rnr:1;
	u8 cna:1;
	u8 frame_reception:1;
	u8 cx:1;
};

#endif /* __E100_IX__ */
