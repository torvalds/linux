/*
 * Copyright 2015 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOC_IMX_REVISION_H__
#define __SOC_IMX_REVISION_H__

#define IMX_CHIP_REVISION_1_0		0x10
#define IMX_CHIP_REVISION_1_1		0x11
#define IMX_CHIP_REVISION_1_2		0x12
#define IMX_CHIP_REVISION_1_3		0x13
#define IMX_CHIP_REVISION_1_4		0x14
#define IMX_CHIP_REVISION_1_5		0x15
#define IMX_CHIP_REVISION_2_0		0x20
#define IMX_CHIP_REVISION_2_1		0x21
#define IMX_CHIP_REVISION_2_2		0x22
#define IMX_CHIP_REVISION_2_3		0x23
#define IMX_CHIP_REVISION_3_0		0x30
#define IMX_CHIP_REVISION_3_1		0x31
#define IMX_CHIP_REVISION_3_2		0x32
#define IMX_CHIP_REVISION_3_3		0x33
#define IMX_CHIP_REVISION_UNKNOWN	0xff

int mx27_revision(void);
int mx31_revision(void);
int mx35_revision(void);
int mx51_revision(void);
int mx53_revision(void);

unsigned int imx_get_soc_revision(void);
void imx_print_silicon_rev(const char *cpu, int srev);

#endif /* __SOC_IMX_REVISION_H__ */
