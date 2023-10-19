#ifndef DT_BINDINGS_MEMORY_TEGRA194_MC_H
#define DT_BINDINGS_MEMORY_TEGRA194_MC_H

/* special clients */
#define TEGRA194_SID_INVALID		0x00
#define TEGRA194_SID_PASSTHROUGH	0x7f

/* host1x clients */
#define TEGRA194_SID_HOST1X		0x01
#define TEGRA194_SID_CSI		0x02
#define TEGRA194_SID_VIC		0x03
#define TEGRA194_SID_VI			0x04
#define TEGRA194_SID_ISP		0x05
#define TEGRA194_SID_NVDEC		0x06
#define TEGRA194_SID_NVENC		0x07
#define TEGRA194_SID_NVJPG		0x08
#define TEGRA194_SID_NVDISPLAY		0x09
#define TEGRA194_SID_TSEC		0x0a
#define TEGRA194_SID_TSECB		0x0b
#define TEGRA194_SID_SE			0x0c
#define TEGRA194_SID_SE1		0x0d
#define TEGRA194_SID_SE2		0x0e
#define TEGRA194_SID_SE3		0x0f

/* GPU clients */
#define TEGRA194_SID_GPU		0x10

/* other SoC clients */
#define TEGRA194_SID_AFI		0x11
#define TEGRA194_SID_HDA		0x12
#define TEGRA194_SID_ETR		0x13
#define TEGRA194_SID_EQOS		0x14
#define TEGRA194_SID_UFSHC		0x15
#define TEGRA194_SID_AON		0x16
#define TEGRA194_SID_SDMMC4		0x17
#define TEGRA194_SID_SDMMC3		0x18
#define TEGRA194_SID_SDMMC2		0x19
#define TEGRA194_SID_SDMMC1		0x1a
#define TEGRA194_SID_XUSB_HOST		0x1b
#define TEGRA194_SID_XUSB_DEV		0x1c
#define TEGRA194_SID_SATA		0x1d
#define TEGRA194_SID_APE		0x1e
#define TEGRA194_SID_SCE		0x1f

/* GPC DMA clients */
#define TEGRA194_SID_GPCDMA_0		0x20
#define TEGRA194_SID_GPCDMA_1		0x21
#define TEGRA194_SID_GPCDMA_2		0x22
#define TEGRA194_SID_GPCDMA_3		0x23
#define TEGRA194_SID_GPCDMA_4		0x24
#define TEGRA194_SID_GPCDMA_5		0x25
#define TEGRA194_SID_GPCDMA_6		0x26
#define TEGRA194_SID_GPCDMA_7		0x27

/* APE DMA clients */
#define TEGRA194_SID_APE_1		0x28
#define TEGRA194_SID_APE_2		0x29

/* camera RTCPU */
#define TEGRA194_SID_RCE		0x2a

/* camera RTCPU on host1x address space */
#define TEGRA194_SID_RCE_1X		0x2b

/* APE DMA clients */
#define TEGRA194_SID_APE_3		0x2c

/* camera RTCPU running on APE */
#define TEGRA194_SID_APE_CAM		0x2d
#define TEGRA194_SID_APE_CAM_1X		0x2e

#define TEGRA194_SID_RCE_RM		0x2f
#define TEGRA194_SID_VI_FALCON		0x30
#define TEGRA194_SID_ISP_FALCON		0x31

/*
 * The BPMP has its SID value hardcoded in the firmware. Changing it requires
 * considerable effort.
 */
#define TEGRA194_SID_BPMP		0x32

/* for SMMU tests */
#define TEGRA194_SID_SMMU_TEST		0x33

/* host1x virtualization channels */
#define TEGRA194_SID_HOST1X_CTX0	0x38
#define TEGRA194_SID_HOST1X_CTX1	0x39
#define TEGRA194_SID_HOST1X_CTX2	0x3a
#define TEGRA194_SID_HOST1X_CTX3	0x3b
#define TEGRA194_SID_HOST1X_CTX4	0x3c
#define TEGRA194_SID_HOST1X_CTX5	0x3d
#define TEGRA194_SID_HOST1X_CTX6	0x3e
#define TEGRA194_SID_HOST1X_CTX7	0x3f

/* host1x command buffers */
#define TEGRA194_SID_HOST1X_VM0		0x40
#define TEGRA194_SID_HOST1X_VM1		0x41
#define TEGRA194_SID_HOST1X_VM2		0x42
#define TEGRA194_SID_HOST1X_VM3		0x43
#define TEGRA194_SID_HOST1X_VM4		0x44
#define TEGRA194_SID_HOST1X_VM5		0x45
#define TEGRA194_SID_HOST1X_VM6		0x46
#define TEGRA194_SID_HOST1X_VM7		0x47

/* SE data buffers */
#define TEGRA194_SID_SE_VM0		0x48
#define TEGRA194_SID_SE_VM1		0x49
#define TEGRA194_SID_SE_VM2		0x4a
#define TEGRA194_SID_SE_VM3		0x4b
#define TEGRA194_SID_SE_VM4		0x4c
#define TEGRA194_SID_SE_VM5		0x4d
#define TEGRA194_SID_SE_VM6		0x4e
#define TEGRA194_SID_SE_VM7		0x4f

#define TEGRA194_SID_MIU		0x50

#define TEGRA194_SID_NVDLA0		0x51
#define TEGRA194_SID_NVDLA1		0x52

#define TEGRA194_SID_PVA0		0x53
#define TEGRA194_SID_PVA1		0x54
#define TEGRA194_SID_NVENC1		0x55
#define TEGRA194_SID_PCIE0		0x56
#define TEGRA194_SID_PCIE1		0x57
#define TEGRA194_SID_PCIE2		0x58
#define TEGRA194_SID_PCIE3		0x59
#define TEGRA194_SID_PCIE4		0x5a
#define TEGRA194_SID_PCIE5		0x5b
#define TEGRA194_SID_NVDEC1		0x5c

#define TEGRA194_SID_XUSB_VF0		0x5d
#define TEGRA194_SID_XUSB_VF1		0x5e
#define TEGRA194_SID_XUSB_VF2		0x5f
#define TEGRA194_SID_XUSB_VF3		0x60

#define TEGRA194_SID_RCE_VM3		0x61
#define TEGRA194_SID_VI_VM2		0x62
#define TEGRA194_SID_VI_VM3		0x63
#define TEGRA194_SID_RCE_SERVER		0x64

/*
 * memory client IDs
 */

/* Misses from System Memory Management Unit (SMMU) Page Table Cache (PTC) */
#define TEGRA194_MEMORY_CLIENT_PTCR 0x00
/* MSS internal memqual MIU7 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU7R 0x01
/* MSS internal memqual MIU7 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU7W 0x02
/* High-definition audio (HDA) read clients */
#define TEGRA194_MEMORY_CLIENT_HDAR 0x15
/* Host channel data read clients */
#define TEGRA194_MEMORY_CLIENT_HOST1XDMAR 0x16
#define TEGRA194_MEMORY_CLIENT_NVENCSRD 0x1c
/* SATA read clients */
#define TEGRA194_MEMORY_CLIENT_SATAR 0x1f
/* Reads from Cortex-A9 4 CPU cores via the L2 cache */
#define TEGRA194_MEMORY_CLIENT_MPCORER 0x27
#define TEGRA194_MEMORY_CLIENT_NVENCSWR 0x2b
/* High-definition audio (HDA) write clients */
#define TEGRA194_MEMORY_CLIENT_HDAW 0x35
/* Writes from Cortex-A9 4 CPU cores via the L2 cache */
#define TEGRA194_MEMORY_CLIENT_MPCOREW 0x39
/* SATA write clients */
#define TEGRA194_MEMORY_CLIENT_SATAW 0x3d
/* ISP read client for Crossbar A */
#define TEGRA194_MEMORY_CLIENT_ISPRA 0x44
/* ISP read client 1 for Crossbar A */
#define TEGRA194_MEMORY_CLIENT_ISPFALR 0x45
/* ISP Write client for Crossbar A */
#define TEGRA194_MEMORY_CLIENT_ISPWA 0x46
/* ISP Write client Crossbar B */
#define TEGRA194_MEMORY_CLIENT_ISPWB 0x47
/* XUSB_HOST read clients */
#define TEGRA194_MEMORY_CLIENT_XUSB_HOSTR 0x4a
/* XUSB_HOST write clients */
#define TEGRA194_MEMORY_CLIENT_XUSB_HOSTW 0x4b
/* XUSB read clients */
#define TEGRA194_MEMORY_CLIENT_XUSB_DEVR 0x4c
/* XUSB_DEV write clients */
#define TEGRA194_MEMORY_CLIENT_XUSB_DEVW 0x4d
/* sdmmca memory read client */
#define TEGRA194_MEMORY_CLIENT_SDMMCRA 0x60
/* sdmmc memory read client */
#define TEGRA194_MEMORY_CLIENT_SDMMCR 0x62
/* sdmmcd memory read client */
#define TEGRA194_MEMORY_CLIENT_SDMMCRAB 0x63
/* sdmmca memory write client */
#define TEGRA194_MEMORY_CLIENT_SDMMCWA 0x64
/* sdmmc memory write client */
#define TEGRA194_MEMORY_CLIENT_SDMMCW 0x66
/* sdmmcd memory write client */
#define TEGRA194_MEMORY_CLIENT_SDMMCWAB 0x67
#define TEGRA194_MEMORY_CLIENT_VICSRD 0x6c
#define TEGRA194_MEMORY_CLIENT_VICSWR 0x6d
/* VI Write client */
#define TEGRA194_MEMORY_CLIENT_VIW 0x72
#define TEGRA194_MEMORY_CLIENT_NVDECSRD 0x78
#define TEGRA194_MEMORY_CLIENT_NVDECSWR 0x79
/* Audio Processing (APE) engine read clients */
#define TEGRA194_MEMORY_CLIENT_APER 0x7a
/* Audio Processing (APE) engine write clients */
#define TEGRA194_MEMORY_CLIENT_APEW 0x7b
#define TEGRA194_MEMORY_CLIENT_NVJPGSRD 0x7e
#define TEGRA194_MEMORY_CLIENT_NVJPGSWR 0x7f
/* AXI AP and DFD-AUX0/1 read clients Both share the same interface on the on MSS */
#define TEGRA194_MEMORY_CLIENT_AXIAPR 0x82
/* AXI AP and DFD-AUX0/1 write clients Both sahre the same interface on MSS */
#define TEGRA194_MEMORY_CLIENT_AXIAPW 0x83
/* ETR read clients */
#define TEGRA194_MEMORY_CLIENT_ETRR 0x84
/* ETR write clients */
#define TEGRA194_MEMORY_CLIENT_ETRW 0x85
/* AXI Switch read client */
#define TEGRA194_MEMORY_CLIENT_AXISR 0x8c
/* AXI Switch write client */
#define TEGRA194_MEMORY_CLIENT_AXISW 0x8d
/* EQOS read client */
#define TEGRA194_MEMORY_CLIENT_EQOSR 0x8e
/* EQOS write client */
#define TEGRA194_MEMORY_CLIENT_EQOSW 0x8f
/* UFSHC read client */
#define TEGRA194_MEMORY_CLIENT_UFSHCR 0x90
/* UFSHC write client */
#define TEGRA194_MEMORY_CLIENT_UFSHCW 0x91
/* NVDISPLAY read client */
#define TEGRA194_MEMORY_CLIENT_NVDISPLAYR 0x92
/* BPMP read client */
#define TEGRA194_MEMORY_CLIENT_BPMPR 0x93
/* BPMP write client */
#define TEGRA194_MEMORY_CLIENT_BPMPW 0x94
/* BPMPDMA read client */
#define TEGRA194_MEMORY_CLIENT_BPMPDMAR 0x95
/* BPMPDMA write client */
#define TEGRA194_MEMORY_CLIENT_BPMPDMAW 0x96
/* AON read client */
#define TEGRA194_MEMORY_CLIENT_AONR 0x97
/* AON write client */
#define TEGRA194_MEMORY_CLIENT_AONW 0x98
/* AONDMA read client */
#define TEGRA194_MEMORY_CLIENT_AONDMAR 0x99
/* AONDMA write client */
#define TEGRA194_MEMORY_CLIENT_AONDMAW 0x9a
/* SCE read client */
#define TEGRA194_MEMORY_CLIENT_SCER 0x9b
/* SCE write client */
#define TEGRA194_MEMORY_CLIENT_SCEW 0x9c
/* SCEDMA read client */
#define TEGRA194_MEMORY_CLIENT_SCEDMAR 0x9d
/* SCEDMA write client */
#define TEGRA194_MEMORY_CLIENT_SCEDMAW 0x9e
/* APEDMA read client */
#define TEGRA194_MEMORY_CLIENT_APEDMAR 0x9f
/* APEDMA write client */
#define TEGRA194_MEMORY_CLIENT_APEDMAW 0xa0
/* NVDISPLAY read client instance 2 */
#define TEGRA194_MEMORY_CLIENT_NVDISPLAYR1 0xa1
#define TEGRA194_MEMORY_CLIENT_VICSRD1 0xa2
#define TEGRA194_MEMORY_CLIENT_NVDECSRD1 0xa3
/* MSS internal memqual MIU0 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU0R 0xa6
/* MSS internal memqual MIU0 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU0W 0xa7
/* MSS internal memqual MIU1 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU1R 0xa8
/* MSS internal memqual MIU1 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU1W 0xa9
/* MSS internal memqual MIU2 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU2R 0xae
/* MSS internal memqual MIU2 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU2W 0xaf
/* MSS internal memqual MIU3 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU3R 0xb0
/* MSS internal memqual MIU3 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU3W 0xb1
/* MSS internal memqual MIU4 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU4R 0xb2
/* MSS internal memqual MIU4 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU4W 0xb3
#define TEGRA194_MEMORY_CLIENT_DPMUR 0xb4
#define TEGRA194_MEMORY_CLIENT_DPMUW 0xb5
#define TEGRA194_MEMORY_CLIENT_NVL0R 0xb6
#define TEGRA194_MEMORY_CLIENT_NVL0W 0xb7
#define TEGRA194_MEMORY_CLIENT_NVL1R 0xb8
#define TEGRA194_MEMORY_CLIENT_NVL1W 0xb9
#define TEGRA194_MEMORY_CLIENT_NVL2R 0xba
#define TEGRA194_MEMORY_CLIENT_NVL2W 0xbb
/* VI FLACON read clients */
#define TEGRA194_MEMORY_CLIENT_VIFALR 0xbc
/* VIFAL write clients */
#define TEGRA194_MEMORY_CLIENT_VIFALW 0xbd
/* DLA0ARDA read clients */
#define TEGRA194_MEMORY_CLIENT_DLA0RDA 0xbe
/* DLA0 Falcon read clients */
#define TEGRA194_MEMORY_CLIENT_DLA0FALRDB 0xbf
/* DLA0 write clients */
#define TEGRA194_MEMORY_CLIENT_DLA0WRA 0xc0
/* DLA0 write clients */
#define TEGRA194_MEMORY_CLIENT_DLA0FALWRB 0xc1
/* DLA1ARDA read clients */
#define TEGRA194_MEMORY_CLIENT_DLA1RDA 0xc2
/* DLA1 Falcon read clients */
#define TEGRA194_MEMORY_CLIENT_DLA1FALRDB 0xc3
/* DLA1 write clients */
#define TEGRA194_MEMORY_CLIENT_DLA1WRA 0xc4
/* DLA1 write clients */
#define TEGRA194_MEMORY_CLIENT_DLA1FALWRB 0xc5
/* PVA0RDA read clients */
#define TEGRA194_MEMORY_CLIENT_PVA0RDA 0xc6
/* PVA0RDB read clients */
#define TEGRA194_MEMORY_CLIENT_PVA0RDB 0xc7
/* PVA0RDC read clients */
#define TEGRA194_MEMORY_CLIENT_PVA0RDC 0xc8
/* PVA0WRA write clients */
#define TEGRA194_MEMORY_CLIENT_PVA0WRA 0xc9
/* PVA0WRB write clients */
#define TEGRA194_MEMORY_CLIENT_PVA0WRB 0xca
/* PVA0WRC write clients */
#define TEGRA194_MEMORY_CLIENT_PVA0WRC 0xcb
/* PVA1RDA read clients */
#define TEGRA194_MEMORY_CLIENT_PVA1RDA 0xcc
/* PVA1RDB read clients */
#define TEGRA194_MEMORY_CLIENT_PVA1RDB 0xcd
/* PVA1RDC read clients */
#define TEGRA194_MEMORY_CLIENT_PVA1RDC 0xce
/* PVA1WRA write clients */
#define TEGRA194_MEMORY_CLIENT_PVA1WRA 0xcf
/* PVA1WRB write clients */
#define TEGRA194_MEMORY_CLIENT_PVA1WRB 0xd0
/* PVA1WRC write clients */
#define TEGRA194_MEMORY_CLIENT_PVA1WRC 0xd1
/* RCE read client */
#define TEGRA194_MEMORY_CLIENT_RCER 0xd2
/* RCE write client */
#define TEGRA194_MEMORY_CLIENT_RCEW 0xd3
/* RCEDMA read client */
#define TEGRA194_MEMORY_CLIENT_RCEDMAR 0xd4
/* RCEDMA write client */
#define TEGRA194_MEMORY_CLIENT_RCEDMAW 0xd5
#define TEGRA194_MEMORY_CLIENT_NVENC1SRD 0xd6
#define TEGRA194_MEMORY_CLIENT_NVENC1SWR 0xd7
/* PCIE0 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE0R 0xd8
/* PCIE0 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE0W 0xd9
/* PCIE1 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE1R 0xda
/* PCIE1 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE1W 0xdb
/* PCIE2 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE2AR 0xdc
/* PCIE2 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE2AW 0xdd
/* PCIE3 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE3R 0xde
/* PCIE3 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE3W 0xdf
/* PCIE4 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE4R 0xe0
/* PCIE4 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE4W 0xe1
/* PCIE5 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE5R 0xe2
/* PCIE5 write clients */
#define TEGRA194_MEMORY_CLIENT_PCIE5W 0xe3
/* ISP read client 1 for Crossbar A */
#define TEGRA194_MEMORY_CLIENT_ISPFALW 0xe4
#define TEGRA194_MEMORY_CLIENT_NVL3R 0xe5
#define TEGRA194_MEMORY_CLIENT_NVL3W 0xe6
#define TEGRA194_MEMORY_CLIENT_NVL4R 0xe7
#define TEGRA194_MEMORY_CLIENT_NVL4W 0xe8
/* DLA0ARDA1 read clients */
#define TEGRA194_MEMORY_CLIENT_DLA0RDA1 0xe9
/* DLA1ARDA1 read clients */
#define TEGRA194_MEMORY_CLIENT_DLA1RDA1 0xea
/* PVA0RDA1 read clients */
#define TEGRA194_MEMORY_CLIENT_PVA0RDA1 0xeb
/* PVA0RDB1 read clients */
#define TEGRA194_MEMORY_CLIENT_PVA0RDB1 0xec
/* PVA1RDA1 read clients */
#define TEGRA194_MEMORY_CLIENT_PVA1RDA1 0xed
/* PVA1RDB1 read clients */
#define TEGRA194_MEMORY_CLIENT_PVA1RDB1 0xee
/* PCIE5r1 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE5R1 0xef
#define TEGRA194_MEMORY_CLIENT_NVENCSRD1 0xf0
#define TEGRA194_MEMORY_CLIENT_NVENC1SRD1 0xf1
/* ISP read client for Crossbar A */
#define TEGRA194_MEMORY_CLIENT_ISPRA1 0xf2
/* PCIE0 read clients */
#define TEGRA194_MEMORY_CLIENT_PCIE0R1 0xf3
#define TEGRA194_MEMORY_CLIENT_NVL0RHP 0xf4
#define TEGRA194_MEMORY_CLIENT_NVL1RHP 0xf5
#define TEGRA194_MEMORY_CLIENT_NVL2RHP 0xf6
#define TEGRA194_MEMORY_CLIENT_NVL3RHP 0xf7
#define TEGRA194_MEMORY_CLIENT_NVL4RHP 0xf8
#define TEGRA194_MEMORY_CLIENT_NVDEC1SRD 0xf9
#define TEGRA194_MEMORY_CLIENT_NVDEC1SRD1 0xfa
#define TEGRA194_MEMORY_CLIENT_NVDEC1SWR 0xfb
/* MSS internal memqual MIU5 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU5R 0xfc
/* MSS internal memqual MIU5 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU5W 0xfd
/* MSS internal memqual MIU6 read clients */
#define TEGRA194_MEMORY_CLIENT_MIU6R 0xfe
/* MSS internal memqual MIU6 write clients */
#define TEGRA194_MEMORY_CLIENT_MIU6W 0xff

#endif
