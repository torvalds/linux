/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef _DT_BINDINGS_DRAM_ROCKCHIP_H
#define _DT_BINDINGS_DRAM_ROCKCHIP_H

#define DDR2_DS_FULL			(0x0)
#define DDR2_DS_REDUCE			(0x1 << 1)
#define DDR2_DS_MASK			(0x1 << 1)

#define DDR2_ODT_DIS			(0x0)
#define DDR2_ODT_75ohm			(0x1 << 2)
#define DDR2_ODT_150ohm			(0x1 << 6)
#define DDR2_ODT_50ohm			((0x1 << 6) | (0x1 << 2)) /* optional */
#define DDR2_ODT_MASK			((0x1 << 2) | (0x1 << 6))

#define DDR3_DS_40ohm			(0x0)
#define DDR3_DS_34ohm			(0x1 << 1)
#define DDR3_DS_MASK			((1 << 1) | (1 << 5))

#define DDR3_ODT_DIS			(0x0)
#define DDR3_ODT_60ohm			(0x1 << 2)
#define DDR3_ODT_120ohm			(0x1 << 6)
#define DDR3_ODT_40ohm			((0x1 << 6) | (0x1 << 2))
#define DDR3_ODT_MASK			((0x1 << 2) | (0x1 << 6) | (0x1 << 9))

#define DDR4_DS_34ohm			(0x0)
#define DDR4_DS_48ohm			(0x1 << 1)
#define DDR4_DS_MASK			(0x3 << 1)

#define DDR4_ODT_DIS			(0x0)
#define DDR4_ODT_60ohm			(0x1 << 8)
#define DDR4_ODT_120ohm			(0x2 << 8)
#define DDR4_ODT_40ohm			(0x3 << 8)
#define DDR4_ODT_240ohm			(0x4 << 8)
#define DDR4_ODT_48ohm			(0x5 << 8)
#define DDR4_ODT_80ohm			(0x6 << 8)
#define DDR4_ODT_34ohm			(0x7 << 8)
#define DDR4_ODT_MASK			(0x7 << 8)

#define LP2_DS_34ohm			(0x1)
#define LP2_DS_40ohm			(0x2)
#define LP2_DS_48ohm			(0x3)
#define LP2_DS_60ohm			(0x4)
#define LP2_DS_68_6ohm			(0x5)	/* optional */
#define LP2_DS_80ohm			(0x6)
#define LP2_DS_120ohm			(0x7)	/* optional */
#define LP2_DS_MASK			(0xf)

#define LP3_DS_34ohm			(0x1)
#define LP3_DS_40ohm			(0x2)
#define LP3_DS_48ohm			(0x3)
#define LP3_DS_60ohm			(0x4)
#define LP3_DS_80ohm			(0x6)
#define LP3_DS_34D_40U			(0x9)
#define LP3_DS_40D_48U			(0xa)
#define LP3_DS_34D_48U			(0xb)
#define LP3_DS_MASK			(0xf)

#define LP3_ODT_DIS			(0)
#define LP3_ODT_60ohm			(0x1)
#define LP3_ODT_120ohm			(0x2)
#define LP3_ODT_240ohm			(0x3)
#define LP3_ODT_MASK			(0x3)

#define LP4_PDDS_240ohm			(0x1 << 3)
#define LP4_PDDS_120ohm			(0x2 << 3)
#define LP4_PDDS_80ohm			(0x3 << 3)
#define LP4_PDDS_60ohm			(0x4 << 3)
#define LP4_PDDS_48ohm			(0x5 << 3)
#define LP4_PDDS_40ohm			(0x6 << 3)
#define LP4_PDDS_MASK			(0x7 << 3)

#define LP4_DQ_ODT_DIS			(0x0)
#define LP4_DQ_ODT_240ohm		(0x1)
#define LP4_DQ_ODT_120ohm		(0x2)
#define LP4_DQ_ODT_80ohm		(0x3)
#define LP4_DQ_ODT_60ohm		(0x4)
#define LP4_DQ_ODT_48ohm		(0x5)
#define LP4_DQ_ODT_40ohm		(0x6)
#define LP4_DQ_ODT_MASK			(0x7)

#define LP4_CA_ODT_DIS			(0x0)
#define LP4_CA_ODT_240ohm		(0x1 << 4)
#define LP4_CA_ODT_120ohm		(0x2 << 4)
#define LP4_CA_ODT_80ohm		(0x3 << 4)
#define LP4_CA_ODT_60ohm		(0x4 << 4)
#define LP4_CA_ODT_48ohm		(0x5 << 4)
#define LP4_CA_ODT_40ohm		(0x6 << 4)
#define LP4_CA_ODT_MASK			(0x7 << 4)

#define LP4_VDDQ_2_5			(0)
#define LP4_VDDQ_3			(1)

#define LP4X_VDDQ_0_6			(0)
#define LP4X_VDDQ_0_5			(1)

#define IGNORE_THIS			(0)

#endif /* _DT_BINDINGS_DRAM_ROCKCHIP_H */
