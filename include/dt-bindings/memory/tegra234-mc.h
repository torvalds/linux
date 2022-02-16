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

/* NISO1 stream IDs */
#define TEGRA234_SID_SDMMC4	0x02
#define TEGRA234_SID_BPMP	0x10

/*
 * memory client IDs
 */

/* High-definition audio (HDA) read clients */
#define TEGRA234_MEMORY_CLIENT_HDAR 0x15
/* High-definition audio (HDA) write clients */
#define TEGRA234_MEMORY_CLIENT_HDAW 0x35
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

#endif
