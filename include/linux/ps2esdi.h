#ifndef _PS2ESDI_H_
#define _PS2ESDI_H_

#define NRML_ESDI_ID 0xddff
#define INTG_ESDI_ID 0xdf9f

#define PRIMARY_IO_BASE 0x3510
#define ALT_IO_BASE 0x3518

#define ESDI_CMD_INT (io_base+0)
#define ESDI_STT_INT (io_base+0)
#define ESDI_CONTROL (io_base+2)
#define ESDI_STATUS  (io_base+2)
#define ESDI_ATTN    (io_base+3)
#define ESDI_INTRPT  (io_base+3)

#define STATUS_ENABLED    0x01
#define STATUS_ALTERNATE  0x02
#define STATUS_BUSY       0x10
#define STATUS_STAT_AVAIL 0x08
#define STATUS_INTR       0x01
#define STATUS_RESET_FAIL 0xea
#define STATUS_CMD_INF	  0x04

#define CTRL_SOFT_RESET   0xe4
#define CTRL_HARD_RESET   0x80
#define CTRL_EOI          0xe2
#define CTRL_ENABLE_DMA   0x02
#define CTRL_ENABLE_INTR  0x01
#define CTRL_DISABLE_INTR  0x00

#define ATT_EOI 0x02

/* bits of word 0 of configuration status block. more info see p.38 of tech ref */
#define CONFIG_IS 0x10 /* Invalid Secondary */
#define CONFIG_ZD 0x08 /* Zero Defect */
#define CONFIG_SF 0x04 /* Skewed Format */
#define CONFIG_FR 0x02 /* Removable */
#define CONFIG_RT 0x01 /* Retries */

#define PORT_SYS_A   0x92
#define PORT_DMA_FN  0x18
#define PORT_DMA_EX  0x1a

#define ON (unsigned char)0x40
#define OFF (unsigned char)~ON
#define LITE_ON outb(inb(PORT_SYS_A) | ON,PORT_SYS_A)
#define LITE_OFF outb((inb(PORT_SYS_A) & OFF),PORT_SYS_A)

#define FAIL 0
#define SUCCES 1

#define INT_CMD_COMPLETE 0x01
#define INT_CMD_ECC      0x03
#define INT_CMD_RETRY    0x05
#define INT_CMD_FORMAT   0x06
#define INT_CMD_ECC_RETRY 0x07
#define INT_CMD_WARNING  0x08
#define INT_CMD_ABORT    0x09
#define INT_RESET        0x0A
#define INT_TRANSFER_REQ 0x0B
#define INT_CMD_FAILED   0x0C
#define INT_DMA_ERR      0x0D
#define INT_CMD_BLK_ERR  0x0E
#define INT_ATTN_ERROR   0x0F

#define DMA_MASK_CHAN 0x90
#define DMA_UNMASK_CHAN 0xA0
#define DMA_WRITE_ADDR 0x20
#define DMA_WRITE_TC 0x40
#define DMA_WRITE_MODE 0x70

#define CMD_GET_DEV_CONFIG 0x09
#define CMD_READ 0x4601
#define CMD_WRITE 0x4602
#define DMA_READ_16 0x4C
#define DMA_WRITE_16 0x44


#define MB 1024*1024
#define SECT_SIZE 512   

#define ERROR 1
#define OK 0

#define HDIO_GETGEO 0x0301

#define FALSE 0
#define TRUE !FALSE

struct ps2esdi_geometry {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	unsigned long start;
};

#endif /* _PS2ESDI_H_ */
