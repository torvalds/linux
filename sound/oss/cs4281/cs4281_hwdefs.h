//****************************************************************************
//
// HWDEFS.H - Definitions of the registers and data structures used by the
//            CS4281
//
// Copyright (c) 1999,2000,2001 Crystal Semiconductor Corp.
//
//****************************************************************************

#ifndef _H_HWDEFS
#define _H_HWDEFS

//****************************************************************************
//
// The following define the offsets of the registers located in the PCI
// configuration space of the CS4281 part.
//
//****************************************************************************
#define PCICONFIG_DEVID_VENID                   0x00000000L
#define PCICONFIG_STATUS_COMMAND                0x00000004L
#define PCICONFIG_CLASS_REVISION                0x00000008L
#define PCICONFIG_LATENCY_TIMER                 0x0000000CL
#define PCICONFIG_BA0                           0x00000010L
#define PCICONFIG_BA1                           0x00000014L
#define PCICONFIG_SUBSYSID_SUBSYSVENID          0x0000002CL
#define PCICONFIG_INTERRUPT                     0x0000003CL

//****************************************************************************
//
// The following define the offsets of the registers accessed via base address
// register zero on the CS4281 part.
//
//****************************************************************************
#define BA0_HISR                                0x00000000L
#define BA0_HICR                                0x00000008L
#define BA0_HIMR                                0x0000000CL
#define BA0_IIER                                0x00000010L
#define BA0_HDSR0                               0x000000F0L
#define BA0_HDSR1                               0x000000F4L
#define BA0_HDSR2                               0x000000F8L
#define BA0_HDSR3                               0x000000FCL
#define BA0_DCA0                                0x00000110L
#define BA0_DCC0                                0x00000114L
#define BA0_DBA0                                0x00000118L
#define BA0_DBC0                                0x0000011CL
#define BA0_DCA1                                0x00000120L
#define BA0_DCC1                                0x00000124L
#define BA0_DBA1                                0x00000128L
#define BA0_DBC1                                0x0000012CL
#define BA0_DCA2                                0x00000130L
#define BA0_DCC2                                0x00000134L
#define BA0_DBA2                                0x00000138L
#define BA0_DBC2                                0x0000013CL
#define BA0_DCA3                                0x00000140L
#define BA0_DCC3                                0x00000144L
#define BA0_DBA3                                0x00000148L
#define BA0_DBC3                                0x0000014CL
#define BA0_DMR0                                0x00000150L
#define BA0_DCR0                                0x00000154L
#define BA0_DMR1                                0x00000158L
#define BA0_DCR1                                0x0000015CL
#define BA0_DMR2                                0x00000160L
#define BA0_DCR2                                0x00000164L
#define BA0_DMR3                                0x00000168L
#define BA0_DCR3                                0x0000016CL
#define BA0_DLMR                                0x00000170L
#define BA0_DLSR                                0x00000174L
#define BA0_FCR0                                0x00000180L
#define BA0_FCR1                                0x00000184L
#define BA0_FCR2                                0x00000188L
#define BA0_FCR3                                0x0000018CL
#define BA0_FPDR0                               0x00000190L
#define BA0_FPDR1                               0x00000194L
#define BA0_FPDR2                               0x00000198L
#define BA0_FPDR3                               0x0000019CL
#define BA0_FCHS                                0x0000020CL
#define BA0_FSIC0                               0x00000210L
#define BA0_FSIC1                               0x00000214L
#define BA0_FSIC2                               0x00000218L
#define BA0_FSIC3                               0x0000021CL
#define BA0_PCICFG00                            0x00000300L
#define BA0_PCICFG04                            0x00000304L
#define BA0_PCICFG08                            0x00000308L
#define BA0_PCICFG0C                            0x0000030CL
#define BA0_PCICFG10                            0x00000310L
#define BA0_PCICFG14                            0x00000314L
#define BA0_PCICFG18                            0x00000318L
#define BA0_PCICFG1C                            0x0000031CL
#define BA0_PCICFG20                            0x00000320L
#define BA0_PCICFG24                            0x00000324L
#define BA0_PCICFG28                            0x00000328L
#define BA0_PCICFG2C                            0x0000032CL
#define BA0_PCICFG30                            0x00000330L
#define BA0_PCICFG34                            0x00000334L
#define BA0_PCICFG38                            0x00000338L
#define BA0_PCICFG3C                            0x0000033CL
#define BA0_PCICFG40                            0x00000340L
#define BA0_PMCS                                0x00000344L
#define BA0_CWPR                                0x000003E0L
#define BA0_EPPMC                               0x000003E4L
#define BA0_GPIOR                               0x000003E8L
#define BA0_SPMC                                0x000003ECL
#define BA0_CFLR                                0x000003F0L
#define BA0_IISR                                0x000003F4L
#define BA0_TMS                                 0x000003F8L
#define BA0_SSVID                               0x000003FCL
#define BA0_CLKCR1                              0x00000400L
#define BA0_FRR                                 0x00000410L
#define BA0_SLT12O                              0x0000041CL
#define BA0_SERMC                               0x00000420L
#define BA0_SERC1                               0x00000428L
#define BA0_SERC2                               0x0000042CL
#define BA0_SLT12M                              0x0000045CL
#define BA0_ACCTL                               0x00000460L
#define BA0_ACSTS                               0x00000464L
#define BA0_ACOSV                               0x00000468L
#define BA0_ACCAD                               0x0000046CL
#define BA0_ACCDA                               0x00000470L
#define BA0_ACISV                               0x00000474L
#define BA0_ACSAD                               0x00000478L
#define BA0_ACSDA                               0x0000047CL
#define BA0_JSPT                                0x00000480L
#define BA0_JSCTL                               0x00000484L
#define BA0_MIDCR                               0x00000490L
#define BA0_MIDCMD                              0x00000494L
#define BA0_MIDSR                               0x00000494L
#define BA0_MIDWP                               0x00000498L
#define BA0_MIDRP                               0x0000049CL
#define BA0_AODSD1                              0x000004A8L
#define BA0_AODSD2                              0x000004ACL
#define BA0_CFGI                                0x000004B0L
#define BA0_SLT12M2                             0x000004DCL
#define BA0_ACSTS2                              0x000004E4L
#define BA0_ACISV2                              0x000004F4L
#define BA0_ACSAD2                              0x000004F8L
#define BA0_ACSDA2                              0x000004FCL
#define BA0_IOTGP                               0x00000500L
#define BA0_IOTSB                               0x00000504L
#define BA0_IOTFM                               0x00000508L
#define BA0_IOTDMA                              0x0000050CL
#define BA0_IOTAC0                              0x00000500L
#define BA0_IOTAC1                              0x00000504L
#define BA0_IOTAC2                              0x00000508L
#define BA0_IOTAC3                              0x0000050CL
#define BA0_IOTPCP                              0x0000052CL
#define BA0_IOTCC                               0x00000530L
#define BA0_IOTCR                               0x0000058CL
#define BA0_PCPRR                               0x00000600L
#define BA0_PCPGR                               0x00000604L
#define BA0_PCPCR                               0x00000608L
#define BA0_PCPCIEN                             0x00000608L
#define BA0_SBMAR                               0x00000700L
#define BA0_SBMDR                               0x00000704L
#define BA0_SBRR                                0x00000708L
#define BA0_SBRDP                               0x0000070CL
#define BA0_SBWDP                               0x00000710L
#define BA0_SBWBS                               0x00000710L
#define BA0_SBRBS                               0x00000714L
#define BA0_FMSR                                0x00000730L
#define BA0_B0AP                                0x00000730L
#define BA0_FMDP                                0x00000734L
#define BA0_B1AP                                0x00000738L
#define BA0_B1DP                                0x0000073CL
#define BA0_SSPM                                0x00000740L
#define BA0_DACSR                               0x00000744L
#define BA0_ADCSR                               0x00000748L
#define BA0_SSCR                                0x0000074CL
#define BA0_FMLVC                               0x00000754L
#define BA0_FMRVC                               0x00000758L
#define BA0_SRCSA                               0x0000075CL
#define BA0_PPLVC                               0x00000760L
#define BA0_PPRVC                               0x00000764L
#define BA0_PASR                                0x00000768L
#define BA0_CASR                                0x0000076CL

//****************************************************************************
//
// The following define the offsets of the AC97 shadow registers, which appear
// as a virtual extension to the base address register zero memory range.
//
//****************************************************************************
#define AC97_REG_OFFSET_MASK                    0x0000007EL
#define AC97_CODEC_NUMBER_MASK                  0x00003000L

#define BA0_AC97_RESET                          0x00001000L
#define BA0_AC97_MASTER_VOLUME                  0x00001002L
#define BA0_AC97_HEADPHONE_VOLUME               0x00001004L
#define BA0_AC97_MASTER_VOLUME_MONO             0x00001006L
#define BA0_AC97_MASTER_TONE                    0x00001008L
#define BA0_AC97_PC_BEEP_VOLUME                 0x0000100AL
#define BA0_AC97_PHONE_VOLUME                   0x0000100CL
#define BA0_AC97_MIC_VOLUME                     0x0000100EL
#define BA0_AC97_LINE_IN_VOLUME                 0x00001010L
#define BA0_AC97_CD_VOLUME                      0x00001012L
#define BA0_AC97_VIDEO_VOLUME                   0x00001014L
#define BA0_AC97_AUX_VOLUME                     0x00001016L
#define BA0_AC97_PCM_OUT_VOLUME                 0x00001018L
#define BA0_AC97_RECORD_SELECT                  0x0000101AL
#define BA0_AC97_RECORD_GAIN                    0x0000101CL
#define BA0_AC97_RECORD_GAIN_MIC                0x0000101EL
#define BA0_AC97_GENERAL_PURPOSE                0x00001020L
#define BA0_AC97_3D_CONTROL                     0x00001022L
#define BA0_AC97_MODEM_RATE                     0x00001024L
#define BA0_AC97_POWERDOWN                      0x00001026L
#define BA0_AC97_EXT_AUDIO_ID                   0x00001028L
#define BA0_AC97_EXT_AUDIO_POWER                0x0000102AL
#define BA0_AC97_PCM_FRONT_DAC_RATE             0x0000102CL
#define BA0_AC97_PCM_SURR_DAC_RATE              0x0000102EL
#define BA0_AC97_PCM_LFE_DAC_RATE               0x00001030L
#define BA0_AC97_PCM_LR_ADC_RATE                0x00001032L
#define BA0_AC97_MIC_ADC_RATE                   0x00001034L
#define BA0_AC97_6CH_VOL_C_LFE                  0x00001036L
#define BA0_AC97_6CH_VOL_SURROUND               0x00001038L
#define BA0_AC97_RESERVED_3A                    0x0000103AL
#define BA0_AC97_EXT_MODEM_ID                   0x0000103CL
#define BA0_AC97_EXT_MODEM_POWER                0x0000103EL
#define BA0_AC97_LINE1_CODEC_RATE               0x00001040L
#define BA0_AC97_LINE2_CODEC_RATE               0x00001042L
#define BA0_AC97_HANDSET_CODEC_RATE             0x00001044L
#define BA0_AC97_LINE1_CODEC_LEVEL              0x00001046L
#define BA0_AC97_LINE2_CODEC_LEVEL              0x00001048L
#define BA0_AC97_HANDSET_CODEC_LEVEL            0x0000104AL
#define BA0_AC97_GPIO_PIN_CONFIG                0x0000104CL
#define BA0_AC97_GPIO_PIN_TYPE                  0x0000104EL
#define BA0_AC97_GPIO_PIN_STICKY                0x00001050L
#define BA0_AC97_GPIO_PIN_WAKEUP                0x00001052L
#define BA0_AC97_GPIO_PIN_STATUS                0x00001054L
#define BA0_AC97_MISC_MODEM_AFE_STAT            0x00001056L
#define BA0_AC97_RESERVED_58                    0x00001058L
#define BA0_AC97_CRYSTAL_REV_N_FAB_ID           0x0000105AL
#define BA0_AC97_TEST_AND_MISC_CTRL             0x0000105CL
#define BA0_AC97_AC_MODE                        0x0000105EL
#define BA0_AC97_MISC_CRYSTAL_CONTROL           0x00001060L
#define BA0_AC97_LINE1_HYPRID_CTRL              0x00001062L
#define BA0_AC97_VENDOR_RESERVED_64             0x00001064L
#define BA0_AC97_VENDOR_RESERVED_66             0x00001066L
#define BA0_AC97_SPDIF_CONTROL                  0x00001068L
#define BA0_AC97_VENDOR_RESERVED_6A             0x0000106AL
#define BA0_AC97_VENDOR_RESERVED_6C             0x0000106CL
#define BA0_AC97_VENDOR_RESERVED_6E             0x0000106EL
#define BA0_AC97_VENDOR_RESERVED_70             0x00001070L
#define BA0_AC97_VENDOR_RESERVED_72             0x00001072L
#define BA0_AC97_VENDOR_RESERVED_74             0x00001074L
#define BA0_AC97_CAL_ADDRESS                    0x00001076L
#define BA0_AC97_CAL_DATA                       0x00001078L
#define BA0_AC97_VENDOR_RESERVED_7A             0x0000107AL
#define BA0_AC97_VENDOR_ID1                     0x0000107CL
#define BA0_AC97_VENDOR_ID2                     0x0000107EL

//****************************************************************************
//
// The following define the offsets of the registers and memories accessed via
// base address register one on the CS4281 part.
//
//****************************************************************************

//****************************************************************************
//
// The following defines are for the flags in the PCI device ID/vendor ID
// register.
//
//****************************************************************************
#define PDV_VENID_MASK                          0x0000FFFFL
#define PDV_DEVID_MASK                          0xFFFF0000L
#define PDV_VENID_SHIFT                         0L
#define PDV_DEVID_SHIFT                         16L
#define VENID_CIRRUS_LOGIC                      0x1013L
#define DEVID_CS4281                            0x6005L

//****************************************************************************
//
// The following defines are for the flags in the PCI status and command
// register.
//
//****************************************************************************
#define PSC_IO_SPACE_ENABLE                     0x00000001L
#define PSC_MEMORY_SPACE_ENABLE                 0x00000002L
#define PSC_BUS_MASTER_ENABLE                   0x00000004L
#define PSC_SPECIAL_CYCLES                      0x00000008L
#define PSC_MWI_ENABLE                          0x00000010L
#define PSC_VGA_PALETTE_SNOOP                   0x00000020L
#define PSC_PARITY_RESPONSE                     0x00000040L
#define PSC_WAIT_CONTROL                        0x00000080L
#define PSC_SERR_ENABLE                         0x00000100L
#define PSC_FAST_B2B_ENABLE                     0x00000200L
#define PSC_UDF_MASK                            0x007F0000L
#define PSC_FAST_B2B_CAPABLE                    0x00800000L
#define PSC_PARITY_ERROR_DETECTED               0x01000000L
#define PSC_DEVSEL_TIMING_MASK                  0x06000000L
#define PSC_TARGET_ABORT_SIGNALLED              0x08000000L
#define PSC_RECEIVED_TARGET_ABORT               0x10000000L
#define PSC_RECEIVED_MASTER_ABORT               0x20000000L
#define PSC_SIGNALLED_SERR                      0x40000000L
#define PSC_DETECTED_PARITY_ERROR               0x80000000L
#define PSC_UDF_SHIFT                           16L
#define PSC_DEVSEL_TIMING_SHIFT                 25L

//****************************************************************************
//
// The following defines are for the flags in the PCI class/revision ID
// register.
//
//****************************************************************************
#define PCR_REVID_MASK                          0x000000FFL
#define PCR_INTERFACE_MASK                      0x0000FF00L
#define PCR_SUBCLASS_MASK                       0x00FF0000L
#define PCR_CLASS_MASK                          0xFF000000L
#define PCR_REVID_SHIFT                         0L
#define PCR_INTERFACE_SHIFT                     8L
#define PCR_SUBCLASS_SHIFT                      16L
#define PCR_CLASS_SHIFT                         24L

//****************************************************************************
//
// The following defines are for the flags in the PCI latency timer register.
//
//****************************************************************************
#define PLT_CACHE_LINE_SIZE_MASK                0x000000FFL
#define PLT_LATENCY_TIMER_MASK                  0x0000FF00L
#define PLT_HEADER_TYPE_MASK                    0x00FF0000L
#define PLT_BIST_MASK                           0xFF000000L
#define PLT_CACHE_LINE_SIZE_SHIFT               0L
#define PLT_LATENCY_TIMER_SHIFT                 8L
#define PLT_HEADER_TYPE_SHIFT                   16L
#define PLT_BIST_SHIFT                          24L

//****************************************************************************
//
// The following defines are for the flags in the PCI base address registers.
//
//****************************************************************************
#define PBAR_MEMORY_SPACE_INDICATOR             0x00000001L
#define PBAR_LOCATION_TYPE_MASK                 0x00000006L
#define PBAR_NOT_PREFETCHABLE                   0x00000008L
#define PBAR_ADDRESS_MASK                       0xFFFFFFF0L
#define PBAR_LOCATION_TYPE_SHIFT                1L

//****************************************************************************
//
// The following defines are for the flags in the PCI subsystem ID/subsystem
// vendor ID register.
//
//****************************************************************************
#define PSS_SUBSYSTEM_VENDOR_ID_MASK            0x0000FFFFL
#define PSS_SUBSYSTEM_ID_MASK                   0xFFFF0000L
#define PSS_SUBSYSTEM_VENDOR_ID_SHIFT           0L
#define PSS_SUBSYSTEM_ID_SHIFT                  16L

//****************************************************************************
//
// The following defines are for the flags in the PCI interrupt register.
//
//****************************************************************************
#define PI_LINE_MASK                            0x000000FFL
#define PI_PIN_MASK                             0x0000FF00L
#define PI_MIN_GRANT_MASK                       0x00FF0000L
#define PI_MAX_LATENCY_MASK                     0xFF000000L
#define PI_LINE_SHIFT                           0L
#define PI_PIN_SHIFT                            8L
#define PI_MIN_GRANT_SHIFT                      16L
#define PI_MAX_LATENCY_SHIFT                    24L

//****************************************************************************
//
// The following defines are for the flags in the host interrupt status
// register.
//
//****************************************************************************
#define HISR_HVOLMASK                            0x00000003L
#define HISR_VDNI                                0x00000001L
#define HISR_VUPI                                0x00000002L
#define HISR_GP1I                                0x00000004L
#define HISR_GP3I                                0x00000008L
#define HISR_GPSI                                0x00000010L
#define HISR_GPPI                                0x00000020L
#define HISR_DMAI                                0x00040000L
#define HISR_FIFOI                               0x00100000L
#define HISR_HVOL                                0x00200000L
#define HISR_MIDI                                0x00400000L
#define HISR_SBINT                               0x00800000L
#define HISR_INTENA                              0x80000000L
#define HISR_DMA_MASK                            0x00000F00L
#define HISR_FIFO_MASK                           0x0000F000L
#define HISR_DMA_SHIFT                           8L
#define HISR_FIFO_SHIFT                          12L
#define HISR_FIFO0                               0x00001000L
#define HISR_FIFO1                               0x00002000L
#define HISR_FIFO2                               0x00004000L
#define HISR_FIFO3                               0x00008000L
#define HISR_DMA0                                0x00000100L
#define HISR_DMA1                                0x00000200L
#define HISR_DMA2                                0x00000400L
#define HISR_DMA3                                0x00000800L
#define HISR_RESERVED                            0x40000000L

//****************************************************************************
//
// The following defines are for the flags in the host interrupt control
// register.
//
//****************************************************************************
#define HICR_IEV                                 0x00000001L
#define HICR_CHGM                                0x00000002L

//****************************************************************************
//
// The following defines are for the flags in the DMA Mode Register n
// (DMRn)
//
//****************************************************************************
#define DMRn_TR_MASK                             0x0000000CL
#define DMRn_TR_SHIFT                            2L
#define DMRn_AUTO                                0x00000010L
#define DMRn_TR_READ                             0x00000008L
#define DMRn_TR_WRITE                            0x00000004L
#define DMRn_TYPE_MASK                           0x000000C0L
#define DMRn_TYPE_SHIFT                          6L
#define DMRn_SIZE8                               0x00010000L
#define DMRn_MONO                                0x00020000L
#define DMRn_BEND                                0x00040000L
#define DMRn_USIGN                               0x00080000L
#define DMRn_SIZE20                              0x00100000L
#define DMRn_SWAPC                               0x00400000L
#define DMRn_CBC                                 0x01000000L
#define DMRn_TBC                                 0x02000000L
#define DMRn_POLL                                0x10000000L
#define DMRn_DMA                                 0x20000000L
#define DMRn_FSEL_MASK                           0xC0000000L
#define DMRn_FSEL_SHIFT                          30L
#define DMRn_FSEL0                               0x00000000L
#define DMRn_FSEL1                               0x40000000L
#define DMRn_FSEL2                               0x80000000L
#define DMRn_FSEL3                               0xC0000000L

//****************************************************************************
//
// The following defines are for the flags in the DMA Command Register n
// (DCRn)
//
//****************************************************************************
#define DCRn_HTCIE                               0x00020000L
#define DCRn_TCIE                                0x00010000L
#define DCRn_MSK                                 0x00000001L

//****************************************************************************
//
// The following defines are for the flags in the FIFO Control 
// register n.(FCRn)
//
//****************************************************************************
#define FCRn_OF_MASK                            0x0000007FL
#define FCRn_OF_SHIFT                           0L
#define FCRn_SZ_MASK                            0x00007F00L
#define FCRn_SZ_SHIFT                           8L
#define FCRn_LS_MASK                            0x001F0000L
#define FCRn_LS_SHIFT                           16L
#define FCRn_RS_MASK                            0x1F000000L
#define FCRn_RS_SHIFT                           24L
#define FCRn_FEN                                0x80000000L
#define FCRn_PSH                                0x20000000L
#define FCRn_DACZ                               0x40000000L

//****************************************************************************
//
// The following defines are for the flags in the serial port Power Management
// control register.(SPMC)
//
//****************************************************************************
#define SPMC_RSTN                               0x00000001L
#define SPMC_ASYN                               0x00000002L
#define SPMC_WUP1                               0x00000004L
#define SPMC_WUP2                               0x00000008L
#define SPMC_ASDI2E                             0x00000100L
#define SPMC_ESSPD                              0x00000200L
#define SPMC_GISPEN                             0x00004000L
#define SPMC_GIPPEN                             0x00008000L

//****************************************************************************
//
// The following defines are for the flags in the Configuration Load register.
// (CFLR)
//
//****************************************************************************
#define CFLR_CLOCK_SOURCE_MASK                  0x00000003L
#define CFLR_CLOCK_SOURCE_AC97                  0x00000001L

#define CFLR_CB0_MASK                            0x000000FFL
#define CFLR_CB1_MASK                            0x0000FF00L
#define CFLR_CB2_MASK                            0x00FF0000L
#define CFLR_CB3_MASK                            0xFF000000L
#define CFLR_CB0_SHIFT                           0L
#define CFLR_CB1_SHIFT                           8L
#define CFLR_CB2_SHIFT                           16L
#define CFLR_CB3_SHIFT                           24L

#define IOTCR_DMA0                              0x00000000L
#define IOTCR_DMA1                              0x00000400L
#define IOTCR_DMA2                              0x00000800L
#define IOTCR_DMA3                              0x00000C00L
#define IOTCR_CCLS                              0x00000100L
#define IOTCR_PCPCI                             0x00000200L
#define IOTCR_DDMA                              0x00000300L

#define SBWBS_WBB                               0x00000080L

//****************************************************************************
//
// The following defines are for the flags in the SRC Slot Assignment Register
// (SRCSA)
//
//****************************************************************************
#define SRCSA_PLSS_MASK                         0x0000001FL
#define SRCSA_PLSS_SHIFT                        0L
#define SRCSA_PRSS_MASK                         0x00001F00L
#define SRCSA_PRSS_SHIFT                        8L
#define SRCSA_CLSS_MASK                         0x001F0000L
#define SRCSA_CLSS_SHIFT                        16L
#define SRCSA_CRSS_MASK                         0x1F000000L
#define SRCSA_CRSS_SHIFT                        24L

//****************************************************************************
//
// The following defines are for the flags in the Sound System Power Management
// register.(SSPM)
//
//****************************************************************************
#define SSPM_FPDN                               0x00000080L
#define SSPM_MIXEN                              0x00000040L
#define SSPM_CSRCEN                             0x00000020L
#define SSPM_PSRCEN                             0x00000010L
#define SSPM_JSEN                               0x00000008L
#define SSPM_ACLEN                              0x00000004L
#define SSPM_FMEN                               0x00000002L

//****************************************************************************
//
// The following defines are for the flags in the Sound System Control
// Register. (SSCR)
//
//****************************************************************************
#define SSCR_SB                                 0x00000004L
#define SSCR_HVC                                0x00000008L
#define SSCR_LPFIFO                             0x00000040L
#define SSCR_LPSRC                              0x00000080L
#define SSCR_XLPSRC                             0x00000100L
#define SSCR_MVMD                               0x00010000L
#define SSCR_MVAD                               0x00020000L
#define SSCR_MVLD                               0x00040000L
#define SSCR_MVCS                               0x00080000L

//****************************************************************************
//
// The following defines are for the flags in the Clock Control Register 1. 
// (CLKCR1)
//
//****************************************************************************
#define CLKCR1_DLLSS_MASK                       0x0000000CL
#define CLKCR1_DLLSS_SHIFT                      2L
#define CLKCR1_DLLP                             0x00000010L
#define CLKCR1_SWCE                             0x00000020L
#define CLKCR1_DLLOS                            0x00000040L
#define CLKCR1_CKRA                             0x00010000L
#define CLKCR1_CKRN                             0x00020000L
#define CLKCR1_DLLRDY                           0x01000000L
#define CLKCR1_CLKON                            0x02000000L

//****************************************************************************
//
// The following defines are for the flags in the Sound Blaster Read Buffer
// Status.(SBRBS)
//
//****************************************************************************
#define SBRBS_RD_MASK                           0x0000007FL
#define SBRBS_RD_SHIFT                          0L
#define SBRBS_RBF                               0x00000080L

//****************************************************************************
//
// The following defines are for the flags in the serial port master control
// register.(SERMC)
//
//****************************************************************************
#define SERMC_MSPE                              0x00000001L
#define SERMC_PTC_MASK                          0x0000000EL
#define SERMC_PTC_SHIFT                         1L
#define SERMC_PTC_AC97                          0x00000002L
#define SERMC_PLB                               0x00000010L
#define SERMC_PXLB                              0x00000020L
#define SERMC_LOFV                              0x00080000L
#define SERMC_SLB                               0x00100000L
#define SERMC_SXLB                              0x00200000L
#define SERMC_ODSEN1                            0x01000000L
#define SERMC_ODSEN2                            0x02000000L

//****************************************************************************
//
// The following defines are for the flags in the General Purpose I/O Register. 
// (GPIOR)
//
//****************************************************************************
#define GPIOR_VDNS                              0x00000001L
#define GPIOR_VUPS                              0x00000002L
#define GPIOR_GP1S                              0x00000004L
#define GPIOR_GP3S                              0x00000008L
#define GPIOR_GPSS                              0x00000010L
#define GPIOR_GPPS                              0x00000020L
#define GPIOR_GP1D                              0x00000400L
#define GPIOR_GP3D                              0x00000800L
#define GPIOR_VDNLT                             0x00010000L
#define GPIOR_VDNPO                             0x00020000L
#define GPIOR_VDNST                             0x00040000L
#define GPIOR_VDNW                              0x00080000L
#define GPIOR_VUPLT                             0x00100000L
#define GPIOR_VUPPO                             0x00200000L
#define GPIOR_VUPST                             0x00400000L
#define GPIOR_VUPW                              0x00800000L
#define GPIOR_GP1OE                             0x01000000L
#define GPIOR_GP1PT                             0x02000000L
#define GPIOR_GP1ST                             0x04000000L
#define GPIOR_GP1W                              0x08000000L
#define GPIOR_GP3OE                             0x10000000L
#define GPIOR_GP3PT                             0x20000000L
#define GPIOR_GP3ST                             0x40000000L
#define GPIOR_GP3W                              0x80000000L

//****************************************************************************
//
// The following defines are for the flags in the clock control register 1.
//
//****************************************************************************
#define CLKCR1_PLLSS_MASK                       0x0000000CL
#define CLKCR1_PLLSS_SERIAL                     0x00000000L
#define CLKCR1_PLLSS_CRYSTAL                    0x00000004L
#define CLKCR1_PLLSS_PCI                        0x00000008L
#define CLKCR1_PLLSS_RESERVED                   0x0000000CL
#define CLKCR1_PLLP                             0x00000010L
#define CLKCR1_SWCE                             0x00000020L
#define CLKCR1_PLLOS                            0x00000040L

//****************************************************************************
//
// The following defines are for the flags in the feature reporting register.
//
//****************************************************************************
#define FRR_FAB_MASK                            0x00000003L
#define FRR_MASK_MASK                           0x0000001CL
#define FRR_ID_MASK                             0x00003000L
#define FRR_FAB_SHIFT                           0L
#define FRR_MASK_SHIFT                          2L
#define FRR_ID_SHIFT                            12L

//****************************************************************************
//
// The following defines are for the flags in the serial port 1 configuration
// register.
//
//****************************************************************************
#define SERC1_VALUE                             0x00000003L
#define SERC1_SO1EN                             0x00000001L
#define SERC1_SO1F_MASK                         0x0000000EL
#define SERC1_SO1F_CS423X                       0x00000000L
#define SERC1_SO1F_AC97                         0x00000002L
#define SERC1_SO1F_DAC                          0x00000004L
#define SERC1_SO1F_SPDIF                        0x00000006L

//****************************************************************************
//
// The following defines are for the flags in the serial port 2 configuration
// register.
//
//****************************************************************************
#define SERC2_VALUE                             0x00000003L
#define SERC2_SI1EN                             0x00000001L
#define SERC2_SI1F_MASK                         0x0000000EL
#define SERC2_SI1F_CS423X                       0x00000000L
#define SERC2_SI1F_AC97                         0x00000002L
#define SERC2_SI1F_ADC                          0x00000004L
#define SERC2_SI1F_SPDIF                        0x00000006L

//****************************************************************************
//
// The following defines are for the flags in the AC97 control register.
//
//****************************************************************************
#define ACCTL_ESYN                              0x00000002L
#define ACCTL_VFRM                              0x00000004L
#define ACCTL_DCV                               0x00000008L
#define ACCTL_CRW                               0x00000010L
#define ACCTL_TC                                0x00000040L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status register.
//
//****************************************************************************
#define ACSTS_CRDY                              0x00000001L
#define ACSTS_VSTS                              0x00000002L

//****************************************************************************
//
// The following defines are for the flags in the AC97 output slot valid
// register.
//
//****************************************************************************
#define ACOSV_SLV3                              0x00000001L
#define ACOSV_SLV4                              0x00000002L
#define ACOSV_SLV5                              0x00000004L
#define ACOSV_SLV6                              0x00000008L
#define ACOSV_SLV7                              0x00000010L
#define ACOSV_SLV8                              0x00000020L
#define ACOSV_SLV9                              0x00000040L
#define ACOSV_SLV10                             0x00000080L
#define ACOSV_SLV11                             0x00000100L
#define ACOSV_SLV12                             0x00000200L

//****************************************************************************
//
// The following defines are for the flags in the AC97 command address
// register.
//
//****************************************************************************
#define ACCAD_CI_MASK                           0x0000007FL
#define ACCAD_CI_SHIFT                          0L

//****************************************************************************
//
// The following defines are for the flags in the AC97 command data register.
//
//****************************************************************************
#define ACCDA_CD_MASK                           0x0000FFFFL
#define ACCDA_CD_SHIFT                          0L

//****************************************************************************
//
// The following defines are for the flags in the AC97 input slot valid
// register.
//
//****************************************************************************
#define ACISV_ISV3                              0x00000001L
#define ACISV_ISV4                              0x00000002L
#define ACISV_ISV5                              0x00000004L
#define ACISV_ISV6                              0x00000008L
#define ACISV_ISV7                              0x00000010L
#define ACISV_ISV8                              0x00000020L
#define ACISV_ISV9                              0x00000040L
#define ACISV_ISV10                             0x00000080L
#define ACISV_ISV11                             0x00000100L
#define ACISV_ISV12                             0x00000200L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status address
// register.
//
//****************************************************************************
#define ACSAD_SI_MASK                           0x0000007FL
#define ACSAD_SI_SHIFT                          0L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status data register.
//
//****************************************************************************
#define ACSDA_SD_MASK                           0x0000FFFFL
#define ACSDA_SD_SHIFT                          0L

//****************************************************************************
//
// The following defines are for the flags in the I/O trap address and control
// registers (all 12).
//
//****************************************************************************
#define IOTAC_SA_MASK                           0x0000FFFFL
#define IOTAC_MSK_MASK                          0x000F0000L
#define IOTAC_IODC_MASK                         0x06000000L
#define IOTAC_IODC_16_BIT                       0x00000000L
#define IOTAC_IODC_10_BIT                       0x02000000L
#define IOTAC_IODC_12_BIT                       0x04000000L
#define IOTAC_WSPI                              0x08000000L
#define IOTAC_RSPI                              0x10000000L
#define IOTAC_WSE                               0x20000000L
#define IOTAC_WE                                0x40000000L
#define IOTAC_RE                                0x80000000L
#define IOTAC_SA_SHIFT                          0L
#define IOTAC_MSK_SHIFT                         16L

//****************************************************************************
//
// The following defines are for the flags in the PC/PCI master enable
// register.
//
//****************************************************************************
#define PCPCIEN_EN                              0x00000001L

//****************************************************************************
//
// The following defines are for the flags in the joystick poll/trigger
// register.
//
//****************************************************************************
#define JSPT_CAX                                0x00000001L
#define JSPT_CAY                                0x00000002L
#define JSPT_CBX                                0x00000004L
#define JSPT_CBY                                0x00000008L
#define JSPT_BA1                                0x00000010L
#define JSPT_BA2                                0x00000020L
#define JSPT_BB1                                0x00000040L
#define JSPT_BB2                                0x00000080L

//****************************************************************************
//
// The following defines are for the flags in the joystick control register.
// The TBF bit has been moved from MIDSR register to JSCTL register bit 8.
//
//****************************************************************************
#define JSCTL_SP_MASK                           0x00000003L
#define JSCTL_SP_SLOW                           0x00000000L
#define JSCTL_SP_MEDIUM_SLOW                    0x00000001L
#define JSCTL_SP_MEDIUM_FAST                    0x00000002L
#define JSCTL_SP_FAST                           0x00000003L
#define JSCTL_ARE                               0x00000004L
#define JSCTL_TBF                               0x00000100L


//****************************************************************************
//
// The following defines are for the flags in the MIDI control register.
//
//****************************************************************************
#define MIDCR_TXE                               0x00000001L
#define MIDCR_RXE                               0x00000002L
#define MIDCR_RIE                               0x00000004L
#define MIDCR_TIE                               0x00000008L
#define MIDCR_MLB                               0x00000010L
#define MIDCR_MRST                              0x00000020L

//****************************************************************************
//
// The following defines are for the flags in the MIDI status register.
//
//****************************************************************************
#define MIDSR_RBE                               0x00000080L
#define MIDSR_RDA                               0x00008000L

//****************************************************************************
//
// The following defines are for the flags in the MIDI write port register.
//
//****************************************************************************
#define MIDWP_MWD_MASK                          0x000000FFL
#define MIDWP_MWD_SHIFT                         0L

//****************************************************************************
//
// The following defines are for the flags in the MIDI read port register.
//
//****************************************************************************
#define MIDRP_MRD_MASK                          0x000000FFL
#define MIDRP_MRD_SHIFT                         0L

//****************************************************************************
//
// The following defines are for the flags in the configuration interface
// register.
//
//****************************************************************************
#define CFGI_CLK                                0x00000001L
#define CFGI_DOUT                               0x00000002L
#define CFGI_DIN_EEN                            0x00000004L
#define CFGI_EELD                               0x00000008L

//****************************************************************************
//
// The following defines are for the flags in the subsystem ID and vendor ID
// register.
//
//****************************************************************************
#define SSVID_VID_MASK                          0x0000FFFFL
#define SSVID_SID_MASK                          0xFFFF0000L
#define SSVID_VID_SHIFT                         0L
#define SSVID_SID_SHIFT                         16L

//****************************************************************************
//
// The following defines are for the flags in the GPIO pin interface register.
//
//****************************************************************************
#define GPIOR_VOLDN                             0x00000001L
#define GPIOR_VOLUP                             0x00000002L
#define GPIOR_SI2D                              0x00000004L
#define GPIOR_SI2OE                             0x00000008L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status register 2.
//
//****************************************************************************
#define ACSTS2_CRDY                             0x00000001L
#define ACSTS2_VSTS                             0x00000002L

//****************************************************************************
//
// The following defines are for the flags in the AC97 input slot valid
// register 2.
//
//****************************************************************************
#define ACISV2_ISV3                             0x00000001L
#define ACISV2_ISV4                             0x00000002L
#define ACISV2_ISV5                             0x00000004L
#define ACISV2_ISV6                             0x00000008L
#define ACISV2_ISV7                             0x00000010L
#define ACISV2_ISV8                             0x00000020L
#define ACISV2_ISV9                             0x00000040L
#define ACISV2_ISV10                            0x00000080L
#define ACISV2_ISV11                            0x00000100L
#define ACISV2_ISV12                            0x00000200L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status address
// register 2.
//
//****************************************************************************
#define ACSAD2_SI_MASK                          0x0000007FL
#define ACSAD2_SI_SHIFT                         0L

//****************************************************************************
//
// The following defines are for the flags in the AC97 status data register 2.
//
//****************************************************************************
#define ACSDA2_SD_MASK                          0x0000FFFFL
#define ACSDA2_SD_SHIFT                         0L

//****************************************************************************
//
// The following defines are for the flags in the I/O trap control register.
//
//****************************************************************************
#define IOTCR_ITD                               0x00000001L
#define IOTCR_HRV                               0x00000002L
#define IOTCR_SRV                               0x00000004L
#define IOTCR_DTI                               0x00000008L
#define IOTCR_DFI                               0x00000010L
#define IOTCR_DDP                               0x00000020L
#define IOTCR_JTE                               0x00000040L
#define IOTCR_PPE                               0x00000080L

//****************************************************************************
//
// The following defines are for the flags in the I/O trap address and control
// registers for Hardware Master Volume.  
//
//****************************************************************************
#define IOTGP_SA_MASK                           0x0000FFFFL
#define IOTGP_MSK_MASK                          0x000F0000L
#define IOTGP_IODC_MASK                         0x06000000L
#define IOTGP_IODC_16_BIT                       0x00000000L
#define IOTGP_IODC_10_BIT                       0x02000000L
#define IOTGP_IODC_12_BIT                       0x04000000L
#define IOTGP_WSPI                              0x08000000L
#define IOTGP_RSPI                              0x10000000L
#define IOTGP_WSE                               0x20000000L
#define IOTGP_WE                                0x40000000L
#define IOTGP_RE                                0x80000000L
#define IOTGP_SA_SHIFT                          0L
#define IOTGP_MSK_SHIFT                         16L

//****************************************************************************
//
// The following defines are for the flags in the I/O trap address and control
// registers for Sound Blaster
//
//****************************************************************************
#define IOTSB_SA_MASK                           0x0000FFFFL
#define IOTSB_MSK_MASK                          0x000F0000L
#define IOTSB_IODC_MASK                         0x06000000L
#define IOTSB_IODC_16_BIT                       0x00000000L
#define IOTSB_IODC_10_BIT                       0x02000000L
#define IOTSB_IODC_12_BIT                       0x04000000L
#define IOTSB_WSPI                              0x08000000L
#define IOTSB_RSPI                              0x10000000L
#define IOTSB_WSE                               0x20000000L
#define IOTSB_WE                                0x40000000L
#define IOTSB_RE                                0x80000000L
#define IOTSB_SA_SHIFT                          0L
#define IOTSB_MSK_SHIFT                         16L

//****************************************************************************
//
// The following defines are for the flags in the I/O trap address and control
// registers for FM.
//
//****************************************************************************
#define IOTFM_SA_MASK                           0x0000FFFFL
#define IOTFM_MSK_MASK                          0x000F0000L
#define IOTFM_IODC_MASK                         0x06000000L
#define IOTFM_IODC_16_BIT                       0x00000000L
#define IOTFM_IODC_10_BIT                       0x02000000L
#define IOTFM_IODC_12_BIT                       0x04000000L
#define IOTFM_WSPI                              0x08000000L
#define IOTFM_RSPI                              0x10000000L
#define IOTFM_WSE                               0x20000000L
#define IOTFM_WE                                0x40000000L
#define IOTFM_RE                                0x80000000L
#define IOTFM_SA_SHIFT                          0L
#define IOTFM_MSK_SHIFT                         16L

//****************************************************************************
//
// The following defines are for the flags in the PC/PCI request register.
//
//****************************************************************************
#define PCPRR_RDC_MASK                         0x00000007L
#define PCPRR_REQ                              0x00008000L
#define PCPRR_RDC_SHIFT                        0L

//****************************************************************************
//
// The following defines are for the flags in the PC/PCI grant register.
//
//****************************************************************************
#define PCPGR_GDC_MASK                         0x00000007L
#define PCPGR_VL                               0x00008000L
#define PCPGR_GDC_SHIFT                        0L

//****************************************************************************
//
// The following defines are for the flags in the PC/PCI Control Register.
//
//****************************************************************************
#define PCPCR_EN                               0x00000001L

//****************************************************************************
//
// The following defines are for the flags in the debug index register.
//
//****************************************************************************
#define DREG_REGID_MASK                         0x0000007FL
#define DREG_DEBUG                              0x00000080L
#define DREG_RGBK_MASK                          0x00000700L
#define DREG_TRAP                               0x00000800L
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_TRAPX                              0x00001000L
#endif
#endif
#define DREG_REGID_SHIFT                        0L
#define DREG_RGBK_SHIFT                         8L
#define DREG_RGBK_REGID_MASK                    0x0000077FL
#define DREG_REGID_R0                           0x00000010L
#define DREG_REGID_R1                           0x00000011L
#define DREG_REGID_R2                           0x00000012L
#define DREG_REGID_R3                           0x00000013L
#define DREG_REGID_R4                           0x00000014L
#define DREG_REGID_R5                           0x00000015L
#define DREG_REGID_R6                           0x00000016L
#define DREG_REGID_R7                           0x00000017L
#define DREG_REGID_R8                           0x00000018L
#define DREG_REGID_R9                           0x00000019L
#define DREG_REGID_RA                           0x0000001AL
#define DREG_REGID_RB                           0x0000001BL
#define DREG_REGID_RC                           0x0000001CL
#define DREG_REGID_RD                           0x0000001DL
#define DREG_REGID_RE                           0x0000001EL
#define DREG_REGID_RF                           0x0000001FL
#define DREG_REGID_RA_BUS_LOW                   0x00000020L
#define DREG_REGID_RA_BUS_HIGH                  0x00000038L
#define DREG_REGID_YBUS_LOW                     0x00000050L
#define DREG_REGID_YBUS_HIGH                    0x00000058L
#define DREG_REGID_TRAP_0                       0x00000100L
#define DREG_REGID_TRAP_1                       0x00000101L
#define DREG_REGID_TRAP_2                       0x00000102L
#define DREG_REGID_TRAP_3                       0x00000103L
#define DREG_REGID_TRAP_4                       0x00000104L
#define DREG_REGID_TRAP_5                       0x00000105L
#define DREG_REGID_TRAP_6                       0x00000106L
#define DREG_REGID_TRAP_7                       0x00000107L
#define DREG_REGID_INDIRECT_ADDRESS             0x0000010EL
#define DREG_REGID_TOP_OF_STACK                 0x0000010FL
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_8                       0x00000110L
#define DREG_REGID_TRAP_9                       0x00000111L
#define DREG_REGID_TRAP_10                      0x00000112L
#define DREG_REGID_TRAP_11                      0x00000113L
#define DREG_REGID_TRAP_12                      0x00000114L
#define DREG_REGID_TRAP_13                      0x00000115L
#define DREG_REGID_TRAP_14                      0x00000116L
#define DREG_REGID_TRAP_15                      0x00000117L
#define DREG_REGID_TRAP_16                      0x00000118L
#define DREG_REGID_TRAP_17                      0x00000119L
#define DREG_REGID_TRAP_18                      0x0000011AL
#define DREG_REGID_TRAP_19                      0x0000011BL
#define DREG_REGID_TRAP_20                      0x0000011CL
#define DREG_REGID_TRAP_21                      0x0000011DL
#define DREG_REGID_TRAP_22                      0x0000011EL
#define DREG_REGID_TRAP_23                      0x0000011FL
#endif
#endif
#define DREG_REGID_RSA0_LOW                     0x00000200L
#define DREG_REGID_RSA0_HIGH                    0x00000201L
#define DREG_REGID_RSA1_LOW                     0x00000202L
#define DREG_REGID_RSA1_HIGH                    0x00000203L
#define DREG_REGID_RSA2                         0x00000204L
#define DREG_REGID_RSA3                         0x00000205L
#define DREG_REGID_RSI0_LOW                     0x00000206L
#define DREG_REGID_RSI0_HIGH                    0x00000207L
#define DREG_REGID_RSI1                         0x00000208L
#define DREG_REGID_RSI2                         0x00000209L
#define DREG_REGID_SAGUSTATUS                   0x0000020AL
#define DREG_REGID_RSCONFIG01_LOW               0x0000020BL
#define DREG_REGID_RSCONFIG01_HIGH              0x0000020CL
#define DREG_REGID_RSCONFIG23_LOW               0x0000020DL
#define DREG_REGID_RSCONFIG23_HIGH              0x0000020EL
#define DREG_REGID_RSDMA01E                     0x0000020FL
#define DREG_REGID_RSDMA23E                     0x00000210L
#define DREG_REGID_RSD0_LOW                     0x00000211L
#define DREG_REGID_RSD0_HIGH                    0x00000212L
#define DREG_REGID_RSD1_LOW                     0x00000213L
#define DREG_REGID_RSD1_HIGH                    0x00000214L
#define DREG_REGID_RSD2_LOW                     0x00000215L
#define DREG_REGID_RSD2_HIGH                    0x00000216L
#define DREG_REGID_RSD3_LOW                     0x00000217L
#define DREG_REGID_RSD3_HIGH                    0x00000218L
#define DREG_REGID_SRAR_HIGH                    0x0000021AL
#define DREG_REGID_SRAR_LOW                     0x0000021BL
#define DREG_REGID_DMA_STATE                    0x0000021CL
#define DREG_REGID_CURRENT_DMA_STREAM           0x0000021DL
#define DREG_REGID_NEXT_DMA_STREAM              0x0000021EL
#define DREG_REGID_CPU_STATUS                   0x00000300L
#define DREG_REGID_MAC_MODE                     0x00000301L
#define DREG_REGID_STACK_AND_REPEAT             0x00000302L
#define DREG_REGID_INDEX0                       0x00000304L
#define DREG_REGID_INDEX1                       0x00000305L
#define DREG_REGID_DMA_STATE_0_3                0x00000400L
#define DREG_REGID_DMA_STATE_4_7                0x00000404L
#define DREG_REGID_DMA_STATE_8_11               0x00000408L
#define DREG_REGID_DMA_STATE_12_15              0x0000040CL
#define DREG_REGID_DMA_STATE_16_19              0x00000410L
#define DREG_REGID_DMA_STATE_20_23              0x00000414L
#define DREG_REGID_DMA_STATE_24_27              0x00000418L
#define DREG_REGID_DMA_STATE_28_31              0x0000041CL
#define DREG_REGID_DMA_STATE_32_35              0x00000420L
#define DREG_REGID_DMA_STATE_36_39              0x00000424L
#define DREG_REGID_DMA_STATE_40_43              0x00000428L
#define DREG_REGID_DMA_STATE_44_47              0x0000042CL
#define DREG_REGID_DMA_STATE_48_51              0x00000430L
#define DREG_REGID_DMA_STATE_52_55              0x00000434L
#define DREG_REGID_DMA_STATE_56_59              0x00000438L
#define DREG_REGID_DMA_STATE_60_63              0x0000043CL
#define DREG_REGID_DMA_STATE_64_67              0x00000440L
#define DREG_REGID_DMA_STATE_68_71              0x00000444L
#define DREG_REGID_DMA_STATE_72_75              0x00000448L
#define DREG_REGID_DMA_STATE_76_79              0x0000044CL
#define DREG_REGID_DMA_STATE_80_83              0x00000450L
#define DREG_REGID_DMA_STATE_84_87              0x00000454L
#define DREG_REGID_DMA_STATE_88_91              0x00000458L
#define DREG_REGID_DMA_STATE_92_95              0x0000045CL
#define DREG_REGID_TRAP_SELECT                  0x00000500L
#define DREG_REGID_TRAP_WRITE_0                 0x00000500L
#define DREG_REGID_TRAP_WRITE_1                 0x00000501L
#define DREG_REGID_TRAP_WRITE_2                 0x00000502L
#define DREG_REGID_TRAP_WRITE_3                 0x00000503L
#define DREG_REGID_TRAP_WRITE_4                 0x00000504L
#define DREG_REGID_TRAP_WRITE_5                 0x00000505L
#define DREG_REGID_TRAP_WRITE_6                 0x00000506L
#define DREG_REGID_TRAP_WRITE_7                 0x00000507L
#if !defined(NO_CS4612)
#if !defined(NO_CS4615)
#define DREG_REGID_TRAP_WRITE_8                 0x00000510L
#define DREG_REGID_TRAP_WRITE_9                 0x00000511L
#define DREG_REGID_TRAP_WRITE_10                0x00000512L
#define DREG_REGID_TRAP_WRITE_11                0x00000513L
#define DREG_REGID_TRAP_WRITE_12                0x00000514L
#define DREG_REGID_TRAP_WRITE_13                0x00000515L
#define DREG_REGID_TRAP_WRITE_14                0x00000516L
#define DREG_REGID_TRAP_WRITE_15                0x00000517L
#define DREG_REGID_TRAP_WRITE_16                0x00000518L
#define DREG_REGID_TRAP_WRITE_17                0x00000519L
#define DREG_REGID_TRAP_WRITE_18                0x0000051AL
#define DREG_REGID_TRAP_WRITE_19                0x0000051BL
#define DREG_REGID_TRAP_WRITE_20                0x0000051CL
#define DREG_REGID_TRAP_WRITE_21                0x0000051DL
#define DREG_REGID_TRAP_WRITE_22                0x0000051EL
#define DREG_REGID_TRAP_WRITE_23                0x0000051FL
#endif
#endif
#define DREG_REGID_MAC0_ACC0_LOW                0x00000600L
#define DREG_REGID_MAC0_ACC1_LOW                0x00000601L
#define DREG_REGID_MAC0_ACC2_LOW                0x00000602L
#define DREG_REGID_MAC0_ACC3_LOW                0x00000603L
#define DREG_REGID_MAC1_ACC0_LOW                0x00000604L
#define DREG_REGID_MAC1_ACC1_LOW                0x00000605L
#define DREG_REGID_MAC1_ACC2_LOW                0x00000606L
#define DREG_REGID_MAC1_ACC3_LOW                0x00000607L
#define DREG_REGID_MAC0_ACC0_MID                0x00000608L
#define DREG_REGID_MAC0_ACC1_MID                0x00000609L
#define DREG_REGID_MAC0_ACC2_MID                0x0000060AL
#define DREG_REGID_MAC0_ACC3_MID                0x0000060BL
#define DREG_REGID_MAC1_ACC0_MID                0x0000060CL
#define DREG_REGID_MAC1_ACC1_MID                0x0000060DL
#define DREG_REGID_MAC1_ACC2_MID                0x0000060EL
#define DREG_REGID_MAC1_ACC3_MID                0x0000060FL
#define DREG_REGID_MAC0_ACC0_HIGH               0x00000610L
#define DREG_REGID_MAC0_ACC1_HIGH               0x00000611L
#define DREG_REGID_MAC0_ACC2_HIGH               0x00000612L
#define DREG_REGID_MAC0_ACC3_HIGH               0x00000613L
#define DREG_REGID_MAC1_ACC0_HIGH               0x00000614L
#define DREG_REGID_MAC1_ACC1_HIGH               0x00000615L
#define DREG_REGID_MAC1_ACC2_HIGH               0x00000616L
#define DREG_REGID_MAC1_ACC3_HIGH               0x00000617L
#define DREG_REGID_RSHOUT_LOW                   0x00000620L
#define DREG_REGID_RSHOUT_MID                   0x00000628L
#define DREG_REGID_RSHOUT_HIGH                  0x00000630L

//****************************************************************************
//
// The following defines are for the flags in the AC97 S/PDIF Control register.
//
//****************************************************************************
#define SPDIF_CONTROL_SPDIF_EN                 0x00008000L
#define SPDIF_CONTROL_VAL                      0x00004000L
#define SPDIF_CONTROL_COPY                     0x00000004L
#define SPDIF_CONTROL_CC0                      0x00000010L
#define SPDIF_CONTROL_CC1                      0x00000020L
#define SPDIF_CONTROL_CC2                      0x00000040L
#define SPDIF_CONTROL_CC3                      0x00000080L
#define SPDIF_CONTROL_CC4                      0x00000100L
#define SPDIF_CONTROL_CC5                      0x00000200L
#define SPDIF_CONTROL_CC6                      0x00000400L
#define SPDIF_CONTROL_L                        0x00000800L

#endif // _H_HWDEFS
