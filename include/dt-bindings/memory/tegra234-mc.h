/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved. */

#ifndef DT_BINDINGS_MEMORY_TEGRA234_MC_H
#define DT_BINDINGS_MEMORY_TEGRA234_MC_H

/* special clients */
#define TEGRA234_SID_INVALID		0x00
#define TEGRA234_SID_PASSTHROUGH	0x7f

/* NISO0 stream IDs */
#define TEGRA234_SID_APE	0x02
#define TEGRA234_SID_HDA	0x03
#define TEGRA234_SID_PCIE0	0x12
#define TEGRA234_SID_PCIE4	0x13
#define TEGRA234_SID_PCIE5	0x14
#define TEGRA234_SID_PCIE6	0x15
#define TEGRA234_SID_PCIE9	0x1f

/* NISO1 stream IDs */
#define TEGRA234_SID_SDMMC4	0x02
#define TEGRA234_SID_PCIE1	0x05
#define TEGRA234_SID_PCIE2	0x06
#define TEGRA234_SID_PCIE3	0x07
#define TEGRA234_SID_PCIE7	0x08
#define TEGRA234_SID_PCIE8	0x09
#define TEGRA234_SID_PCIE10	0x0b
#define TEGRA234_SID_BPMP	0x10

/*
 * memory client IDs
 */

/* High-definition audio (HDA) read clients */
#define TEGRA234_MEMORY_CLIENT_HDAR 0x15
/* PCIE6 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE6AR 0x28
/* PCIE6 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE6AW 0x29
/* PCIE7 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE7AR 0x2a
/* PCIE7 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE7AW 0x30
/* PCIE8 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE8AR 0x32
/* High-definition audio (HDA) write clients */
#define TEGRA234_MEMORY_CLIENT_HDAW 0x35
/* PCIE8 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE8AW 0x3b
/* PCIE9 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE9AR 0x3c
/* PCIE6r1 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE6AR1 0x3d
/* PCIE9 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE9AW 0x3e
/* PCIE10 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE10AR 0x3f
/* PCIE10 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE10AW 0x40
/* PCIE10r1 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE10AR1 0x48
/* PCIE7r1 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE7AR1 0x49
/* sdmmcd memory read client */
#define TEGRA234_MEMORY_CLIENT_SDMMCRAB 0x63
/* sdmmcd memory write client */
#define TEGRA234_MEMORY_CLIENT_SDMMCWAB 0x67
/* BPMP read client */
#define TEGRA234_MEMORY_CLIENT_BPMPR 0x93
/* BPMP write client */
#define TEGRA234_MEMORY_CLIENT_BPMPW 0x94
/* BPMPDMA read client */
#define TEGRA234_MEMORY_CLIENT_BPMPDMAR 0x95
/* BPMPDMA write client */
#define TEGRA234_MEMORY_CLIENT_BPMPDMAW 0x96
/* APEDMA read client */
#define TEGRA234_MEMORY_CLIENT_APEDMAR 0x9f
/* APEDMA write client */
#define TEGRA234_MEMORY_CLIENT_APEDMAW 0xa0
/* PCIE0 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE0R 0xd8
/* PCIE0 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE0W 0xd9
/* PCIE1 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE1R 0xda
/* PCIE1 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE1W 0xdb
/* PCIE2 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE2AR 0xdc
/* PCIE2 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE2AW 0xdd
/* PCIE3 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE3R 0xde
/* PCIE3 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE3W 0xdf
/* PCIE4 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE4R 0xe0
/* PCIE4 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE4W 0xe1
/* PCIE5 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE5R 0xe2
/* PCIE5 write clients */
#define TEGRA234_MEMORY_CLIENT_PCIE5W 0xe3
/* PCIE5r1 read clients */
#define TEGRA234_MEMORY_CLIENT_PCIE5R1 0xef

#endif
