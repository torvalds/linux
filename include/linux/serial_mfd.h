#ifndef _SERIAL_MFD_H_
#define _SERIAL_MFD_H_

/* HW register offset definition */
#define UART_FOR	0x08
#define UART_PS		0x0C
#define UART_MUL	0x0D
#define UART_DIV	0x0E

#define HSU_GBL_IEN	0x0
#define HSU_GBL_IST	0x4

#define HSU_GBL_INT_BIT_PORT0	0x0
#define HSU_GBL_INT_BIT_PORT1	0x1
#define HSU_GBL_INT_BIT_PORT2	0x2
#define HSU_GBL_INT_BIT_IRI	0x3
#define HSU_GBL_INT_BIT_HDLC	0x4
#define HSU_GBL_INT_BIT_DMA	0x5

#define HSU_GBL_ISR	0x8
#define HSU_GBL_DMASR	0x400
#define HSU_GBL_DMAISR	0x404

#define HSU_PORT_REG_OFFSET	0x80
#define HSU_PORT0_REG_OFFSET	0x80
#define HSU_PORT1_REG_OFFSET	0x100
#define HSU_PORT2_REG_OFFSET	0x180
#define HSU_PORT_REG_LENGTH	0x80

#define HSU_DMA_CHANS_REG_OFFSET	0x500
#define HSU_DMA_CHANS_REG_LENGTH	0x40

#define HSU_CH_SR		0x0	/* channel status reg */
#define HSU_CH_CR		0x4	/* control reg */
#define HSU_CH_DCR		0x8	/* descriptor control reg */
#define HSU_CH_BSR		0x10	/* max fifo buffer size reg */
#define HSU_CH_MOTSR		0x14	/* minimum ocp transfer size */
#define HSU_CH_D0SAR		0x20	/* desc 0 start addr */
#define HSU_CH_D0TSR		0x24	/* desc 0 transfer size */
#define HSU_CH_D1SAR		0x28
#define HSU_CH_D1TSR		0x2C
#define HSU_CH_D2SAR		0x30
#define HSU_CH_D2TSR		0x34
#define HSU_CH_D3SAR		0x38
#define HSU_CH_D3TSR		0x3C

#endif
