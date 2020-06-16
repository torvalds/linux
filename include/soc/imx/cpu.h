/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __IMX_CPU_H__
#define __IMX_CPU_H__

#define MXC_CPU_MX1		1
#define MXC_CPU_MX21		21
#define MXC_CPU_MX25		25
#define MXC_CPU_MX27		27
#define MXC_CPU_MX31		31
#define MXC_CPU_MX35		35
#define MXC_CPU_MX51		51
#define MXC_CPU_MX53		53
#define MXC_CPU_IMX6SL		0x60
#define MXC_CPU_IMX6DL		0x61
#define MXC_CPU_IMX6SX		0x62
#define MXC_CPU_IMX6Q		0x63
#define MXC_CPU_IMX6UL		0x64
#define MXC_CPU_IMX6ULL		0x65
/* virtual cpu id for i.mx6ulz */
#define MXC_CPU_IMX6ULZ		0x6b
#define MXC_CPU_IMX6SLL		0x67
#define MXC_CPU_IMX7D		0x72
#define MXC_CPU_IMX7ULP		0xff

#define MXC_CPU_VFx10		0x010
#define MXC_CPU_VF500		0x500
#define MXC_CPU_VF510		(MXC_CPU_VF500 | MXC_CPU_VFx10)
#define MXC_CPU_VF600		0x600
#define MXC_CPU_VF610		(MXC_CPU_VF600 | MXC_CPU_VFx10)

#ifndef __ASSEMBLY__
extern unsigned int __mxc_cpu_type;
#endif

#endif
