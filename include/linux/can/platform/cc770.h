#ifndef _CAN_PLATFORM_CC770_H_
#define _CAN_PLATFORM_CC770_H_

/* CPU Interface Register (0x02) */
#define CPUIF_CEN	0x01	/* Clock Out Enable */
#define CPUIF_MUX	0x04	/* Multiplex */
#define CPUIF_SLP	0x08	/* Sleep */
#define CPUIF_PWD	0x10	/* Power Down Mode */
#define CPUIF_DMC	0x20	/* Divide Memory Clock */
#define CPUIF_DSC	0x40	/* Divide System Clock */
#define CPUIF_RST	0x80	/* Hardware Reset Status */

/* Clock Out Register (0x1f) */
#define CLKOUT_CD_MASK  0x0f	/* Clock Divider mask */
#define CLKOUT_SL_MASK	0x30	/* Slew Rate mask */
#define CLKOUT_SL_SHIFT	4

/* Bus Configuration Register (0x2f) */
#define BUSCFG_DR0	0x01	/* Disconnect RX0 Input / Select RX input */
#define BUSCFG_DR1	0x02	/* Disconnect RX1 Input / Silent mode */
#define BUSCFG_DT1	0x08	/* Disconnect TX1 Output */
#define BUSCFG_POL	0x20	/* Polarity dominant or recessive */
#define BUSCFG_CBY	0x40	/* Input Comparator Bypass */

struct cc770_platform_data {
	u32 osc_freq;	/* CAN bus oscillator frequency in Hz */

	u8 cir;		/* CPU Interface Register */
	u8 cor;		/* Clock Out Register */
	u8 bcr;		/* Bus Configuration Register */
};

#endif	/* !_CAN_PLATFORM_CC770_H_ */
