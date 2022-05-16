#ifndef DT_BINDINGS_MEMORY_TEGRA186_MC_H
#define DT_BINDINGS_MEMORY_TEGRA186_MC_H

/* special clients */
#define TEGRA186_SID_INVALID		0x00
#define TEGRA186_SID_PASSTHROUGH	0x7f

/* host1x clients */
#define TEGRA186_SID_HOST1X		0x01
#define TEGRA186_SID_CSI		0x02
#define TEGRA186_SID_VIC		0x03
#define TEGRA186_SID_VI			0x04
#define TEGRA186_SID_ISP		0x05
#define TEGRA186_SID_NVDEC		0x06
#define TEGRA186_SID_NVENC		0x07
#define TEGRA186_SID_NVJPG		0x08
#define TEGRA186_SID_NVDISPLAY		0x09
#define TEGRA186_SID_TSEC		0x0a
#define TEGRA186_SID_TSECB		0x0b
#define TEGRA186_SID_SE			0x0c
#define TEGRA186_SID_SE1		0x0d
#define TEGRA186_SID_SE2		0x0e
#define TEGRA186_SID_SE3		0x0f

/* GPU clients */
#define TEGRA186_SID_GPU		0x10

/* other SoC clients */
#define TEGRA186_SID_AFI		0x11
#define TEGRA186_SID_HDA		0x12
#define TEGRA186_SID_ETR		0x13
#define TEGRA186_SID_EQOS		0x14
#define TEGRA186_SID_UFSHC		0x15
#define TEGRA186_SID_AON		0x16
#define TEGRA186_SID_SDMMC4		0x17
#define TEGRA186_SID_SDMMC3		0x18
#define TEGRA186_SID_SDMMC2		0x19
#define TEGRA186_SID_SDMMC1		0x1a
#define TEGRA186_SID_XUSB_HOST		0x1b
#define TEGRA186_SID_XUSB_DEV		0x1c
#define TEGRA186_SID_SATA		0x1d
#define TEGRA186_SID_APE		0x1e
#define TEGRA186_SID_SCE		0x1f

/* GPC DMA clients */
#define TEGRA186_SID_GPCDMA_0		0x20
#define TEGRA186_SID_GPCDMA_1		0x21
#define TEGRA186_SID_GPCDMA_2		0x22
#define TEGRA186_SID_GPCDMA_3		0x23
#define TEGRA186_SID_GPCDMA_4		0x24
#define TEGRA186_SID_GPCDMA_5		0x25
#define TEGRA186_SID_GPCDMA_6		0x26
#define TEGRA186_SID_GPCDMA_7		0x27

/* APE DMA clients */
#define TEGRA186_SID_APE_1		0x28
#define TEGRA186_SID_APE_2		0x29

/* camera RTCPU */
#define TEGRA186_SID_RCE		0x2a

/* camera RTCPU on host1x address space */
#define TEGRA186_SID_RCE_1X		0x2b

/* APE DMA clients */
#define TEGRA186_SID_APE_3		0x2c

/* camera RTCPU running on APE */
#define TEGRA186_SID_APE_CAM		0x2d
#define TEGRA186_SID_APE_CAM_1X		0x2e

/*
 * The BPMP has its SID value hardcoded in the firmware. Changing it requires
 * considerable effort.
 */
#define TEGRA186_SID_BPMP		0x32

/* for SMMU tests */
#define TEGRA186_SID_SMMU_TEST		0x33

/* host1x virtualization channels */
#define TEGRA186_SID_HOST1X_CTX0	0x38
#define TEGRA186_SID_HOST1X_CTX1	0x39
#define TEGRA186_SID_HOST1X_CTX2	0x3a
#define TEGRA186_SID_HOST1X_CTX3	0x3b
#define TEGRA186_SID_HOST1X_CTX4	0x3c
#define TEGRA186_SID_HOST1X_CTX5	0x3d
#define TEGRA186_SID_HOST1X_CTX6	0x3e
#define TEGRA186_SID_HOST1X_CTX7	0x3f

/* host1x command buffers */
#define TEGRA186_SID_HOST1X_VM0		0x40
#define TEGRA186_SID_HOST1X_VM1		0x41
#define TEGRA186_SID_HOST1X_VM2		0x42
#define TEGRA186_SID_HOST1X_VM3		0x43
#define TEGRA186_SID_HOST1X_VM4		0x44
#define TEGRA186_SID_HOST1X_VM5		0x45
#define TEGRA186_SID_HOST1X_VM6		0x46
#define TEGRA186_SID_HOST1X_VM7		0x47

/* SE data buffers */
#define TEGRA186_SID_SE_VM0		0x48
#define TEGRA186_SID_SE_VM1		0x49
#define TEGRA186_SID_SE_VM2		0x4a
#define TEGRA186_SID_SE_VM3		0x4b
#define TEGRA186_SID_SE_VM4		0x4c
#define TEGRA186_SID_SE_VM5		0x4d
#define TEGRA186_SID_SE_VM6		0x4e
#define TEGRA186_SID_SE_VM7		0x4f

/*
 * memory client IDs
 */

/* Misses from System Memory Management Unit (SMMU) Page Table Cache (PTC) */
#define TEGRA186_MEMORY_CLIENT_PTCR 0x00
/* PCIE reads */
#define TEGRA186_MEMORY_CLIENT_AFIR 0x0e
/* High-definition audio (HDA) reads */
#define TEGRA186_MEMORY_CLIENT_HDAR 0x15
/* Host channel data reads */
#define TEGRA186_MEMORY_CLIENT_HOST1XDMAR 0x16
#define TEGRA186_MEMORY_CLIENT_NVENCSRD 0x1c
/* SATA reads */
#define TEGRA186_MEMORY_CLIENT_SATAR 0x1f
/* Reads from Cortex-A9 4 CPU cores via the L2 cache */
#define TEGRA186_MEMORY_CLIENT_MPCORER 0x27
#define TEGRA186_MEMORY_CLIENT_NVENCSWR 0x2b
/* PCIE writes */
#define TEGRA186_MEMORY_CLIENT_AFIW 0x31
/* High-definition audio (HDA) writes */
#define TEGRA186_MEMORY_CLIENT_HDAW 0x35
/* Writes from Cortex-A9 4 CPU cores via the L2 cache */
#define TEGRA186_MEMORY_CLIENT_MPCOREW 0x39
/* SATA writes */
#define TEGRA186_MEMORY_CLIENT_SATAW 0x3d
/* ISP Read client for Crossbar A */
#define TEGRA186_MEMORY_CLIENT_ISPRA 0x44
/* ISP Write client for Crossbar A */
#define TEGRA186_MEMORY_CLIENT_ISPWA 0x46
/* ISP Write client Crossbar B */
#define TEGRA186_MEMORY_CLIENT_ISPWB 0x47
/* XUSB reads */
#define TEGRA186_MEMORY_CLIENT_XUSB_HOSTR 0x4a
/* XUSB_HOST writes */
#define TEGRA186_MEMORY_CLIENT_XUSB_HOSTW 0x4b
/* XUSB reads */
#define TEGRA186_MEMORY_CLIENT_XUSB_DEVR 0x4c
/* XUSB_DEV writes */
#define TEGRA186_MEMORY_CLIENT_XUSB_DEVW 0x4d
/* TSEC Memory Return Data Client Description */
#define TEGRA186_MEMORY_CLIENT_TSECSRD 0x54
/* TSEC Memory Write Client Description */
#define TEGRA186_MEMORY_CLIENT_TSECSWR 0x55
/* 3D, ltcx reads instance 0 */
#define TEGRA186_MEMORY_CLIENT_GPUSRD 0x58
/* 3D, ltcx writes instance 0 */
#define TEGRA186_MEMORY_CLIENT_GPUSWR 0x59
/* sdmmca memory read client */
#define TEGRA186_MEMORY_CLIENT_SDMMCRA 0x60
/* sdmmcbmemory read client */
#define TEGRA186_MEMORY_CLIENT_SDMMCRAA 0x61
/* sdmmc memory read client */
#define TEGRA186_MEMORY_CLIENT_SDMMCR 0x62
/* sdmmcd memory read client */
#define TEGRA186_MEMORY_CLIENT_SDMMCRAB 0x63
/* sdmmca memory write client */
#define TEGRA186_MEMORY_CLIENT_SDMMCWA 0x64
/* sdmmcb memory write client */
#define TEGRA186_MEMORY_CLIENT_SDMMCWAA 0x65
/* sdmmc memory write client */
#define TEGRA186_MEMORY_CLIENT_SDMMCW 0x66
/* sdmmcd memory write client */
#define TEGRA186_MEMORY_CLIENT_SDMMCWAB 0x67
#define TEGRA186_MEMORY_CLIENT_VICSRD 0x6c
#define TEGRA186_MEMORY_CLIENT_VICSWR 0x6d
/* VI Write client */
#define TEGRA186_MEMORY_CLIENT_VIW 0x72
#define TEGRA186_MEMORY_CLIENT_NVDECSRD 0x78
#define TEGRA186_MEMORY_CLIENT_NVDECSWR 0x79
/* Audio Processing (APE) engine reads */
#define TEGRA186_MEMORY_CLIENT_APER 0x7a
/* Audio Processing (APE) engine writes */
#define TEGRA186_MEMORY_CLIENT_APEW 0x7b
#define TEGRA186_MEMORY_CLIENT_NVJPGSRD 0x7e
#define TEGRA186_MEMORY_CLIENT_NVJPGSWR 0x7f
/* SE Memory Return Data Client Description */
#define TEGRA186_MEMORY_CLIENT_SESRD 0x80
/* SE Memory Write Client Description */
#define TEGRA186_MEMORY_CLIENT_SESWR 0x81
/* ETR reads */
#define TEGRA186_MEMORY_CLIENT_ETRR 0x84
/* ETR writes */
#define TEGRA186_MEMORY_CLIENT_ETRW 0x85
/* TSECB Memory Return Data Client Description */
#define TEGRA186_MEMORY_CLIENT_TSECSRDB 0x86
/* TSECB Memory Write Client Description */
#define TEGRA186_MEMORY_CLIENT_TSECSWRB 0x87
/* 3D, ltcx reads instance 1 */
#define TEGRA186_MEMORY_CLIENT_GPUSRD2 0x88
/* 3D, ltcx writes instance 1 */
#define TEGRA186_MEMORY_CLIENT_GPUSWR2 0x89
/* AXI Switch read client */
#define TEGRA186_MEMORY_CLIENT_AXISR 0x8c
/* AXI Switch write client */
#define TEGRA186_MEMORY_CLIENT_AXISW 0x8d
/* EQOS read client */
#define TEGRA186_MEMORY_CLIENT_EQOSR 0x8e
/* EQOS write client */
#define TEGRA186_MEMORY_CLIENT_EQOSW 0x8f
/* UFSHC read client */
#define TEGRA186_MEMORY_CLIENT_UFSHCR 0x90
/* UFSHC write client */
#define TEGRA186_MEMORY_CLIENT_UFSHCW 0x91
/* NVDISPLAY read client */
#define TEGRA186_MEMORY_CLIENT_NVDISPLAYR 0x92
/* BPMP read client */
#define TEGRA186_MEMORY_CLIENT_BPMPR 0x93
/* BPMP write client */
#define TEGRA186_MEMORY_CLIENT_BPMPW 0x94
/* BPMPDMA read client */
#define TEGRA186_MEMORY_CLIENT_BPMPDMAR 0x95
/* BPMPDMA write client */
#define TEGRA186_MEMORY_CLIENT_BPMPDMAW 0x96
/* AON read client */
#define TEGRA186_MEMORY_CLIENT_AONR 0x97
/* AON write client */
#define TEGRA186_MEMORY_CLIENT_AONW 0x98
/* AONDMA read client */
#define TEGRA186_MEMORY_CLIENT_AONDMAR 0x99
/* AONDMA write client */
#define TEGRA186_MEMORY_CLIENT_AONDMAW 0x9a
/* SCE read client */
#define TEGRA186_MEMORY_CLIENT_SCER 0x9b
/* SCE write client */
#define TEGRA186_MEMORY_CLIENT_SCEW 0x9c
/* SCEDMA read client */
#define TEGRA186_MEMORY_CLIENT_SCEDMAR 0x9d
/* SCEDMA write client */
#define TEGRA186_MEMORY_CLIENT_SCEDMAW 0x9e
/* APEDMA read client */
#define TEGRA186_MEMORY_CLIENT_APEDMAR 0x9f
/* APEDMA write client */
#define TEGRA186_MEMORY_CLIENT_APEDMAW 0xa0
/* NVDISPLAY read client instance 2 */
#define TEGRA186_MEMORY_CLIENT_NVDISPLAYR1 0xa1
#define TEGRA186_MEMORY_CLIENT_VICSRD1 0xa2
#define TEGRA186_MEMORY_CLIENT_NVDECSRD1 0xa3

#endif
