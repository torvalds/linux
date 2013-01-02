#ifndef _OMAP2_MCSPI_H
#define _OMAP2_MCSPI_H

#define OMAP2_MCSPI_REV 0
#define OMAP3_MCSPI_REV 1
#define OMAP4_MCSPI_REV 2

#define OMAP4_MCSPI_REG_OFFSET 0x100

#define MCSPI_PINDIR_D0_IN_D1_OUT	0
#define MCSPI_PINDIR_D0_OUT_D1_IN	1

struct omap2_mcspi_platform_config {
	unsigned short	num_cs;
	unsigned int regs_offset;
	unsigned int pin_dir:1;
};

struct omap2_mcspi_dev_attr {
	unsigned short num_chipselect;
};

struct omap2_mcspi_device_config {
	unsigned turbo_mode:1;
};

#endif
