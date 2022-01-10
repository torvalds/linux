/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */

#ifndef DT_BINDINGS_MEMORY_TEGRA234_MC_H
#define DT_BINDINGS_MEMORY_TEGRA234_MC_H

/* special clients */
#define TEGRA234_SID_INVALID		0x00
#define TEGRA234_SID_PASSTHROUGH	0x7f


/* NISO1 stream IDs */
#define TEGRA234_SID_SDMMC4	0x02
#define TEGRA234_SID_BPMP	0x10

/*
 * memory client IDs
 */

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

#endif
