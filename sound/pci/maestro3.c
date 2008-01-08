/*
 * Driver for ESS Maestro3/Allegro (ES1988) soundcards.
 * Copyright (c) 2000 by Zach Brown <zab@zabbo.net>
 *                       Takashi Iwai <tiwai@suse.de>
 *
 * Most of the hardware init stuffs are based on maestro3 driver for
 * OSS/Free by Zach Brown.  Many thanks to Zach!
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *
 * ChangeLog:
 * Aug. 27, 2001
 *     - Fixed deadlock on capture
 *     - Added Canyon3D-2 support by Rob Riggs <rob@pangalactic.org>
 *
 */
 
#define CARD_NAME "ESS Maestro3/Allegro/Canyon3D-2"
#define DRIVER_NAME "Maestro3"

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/mpu401.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <asm/byteorder.h>

MODULE_AUTHOR("Zach Brown <zab@zabbo.net>, Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ESS Maestro3 PCI");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ESS,Maestro3 PCI},"
		"{ESS,ES1988},"
		"{ESS,Allegro PCI},"
		"{ESS,Allegro-1 PCI},"
	        "{ESS,Canyon3D-2/LE PCI}}");
#ifndef CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL
MODULE_FIRMWARE("ess/maestro3_assp_kernel.fw");
MODULE_FIRMWARE("ess/maestro3_assp_minisrc.fw");
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* all enabled */
static int external_amp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};
static int amp_gpio[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this soundcard.");
module_param_array(external_amp, bool, NULL, 0444);
MODULE_PARM_DESC(external_amp, "Enable external amp for " CARD_NAME " soundcard.");
module_param_array(amp_gpio, int, NULL, 0444);
MODULE_PARM_DESC(amp_gpio, "GPIO pin number for external amp. (default = -1)");

#define MAX_PLAYBACKS	2
#define MAX_CAPTURES	1
#define NR_DSPS		(MAX_PLAYBACKS + MAX_CAPTURES)


/*
 * maestro3 registers
 */

/* Allegro PCI configuration registers */
#define PCI_LEGACY_AUDIO_CTRL   0x40
#define SOUND_BLASTER_ENABLE    0x00000001
#define FM_SYNTHESIS_ENABLE     0x00000002
#define GAME_PORT_ENABLE        0x00000004
#define MPU401_IO_ENABLE        0x00000008
#define MPU401_IRQ_ENABLE       0x00000010
#define ALIAS_10BIT_IO          0x00000020
#define SB_DMA_MASK             0x000000C0
#define SB_DMA_0                0x00000040
#define SB_DMA_1                0x00000040
#define SB_DMA_R                0x00000080
#define SB_DMA_3                0x000000C0
#define SB_IRQ_MASK             0x00000700
#define SB_IRQ_5                0x00000000
#define SB_IRQ_7                0x00000100
#define SB_IRQ_9                0x00000200
#define SB_IRQ_10               0x00000300
#define MIDI_IRQ_MASK           0x00003800
#define SERIAL_IRQ_ENABLE       0x00004000
#define DISABLE_LEGACY          0x00008000

#define PCI_ALLEGRO_CONFIG      0x50
#define SB_ADDR_240             0x00000004
#define MPU_ADDR_MASK           0x00000018
#define MPU_ADDR_330            0x00000000
#define MPU_ADDR_300            0x00000008
#define MPU_ADDR_320            0x00000010
#define MPU_ADDR_340            0x00000018
#define USE_PCI_TIMING          0x00000040
#define POSTED_WRITE_ENABLE     0x00000080
#define DMA_POLICY_MASK         0x00000700
#define DMA_DDMA                0x00000000
#define DMA_TDMA                0x00000100
#define DMA_PCPCI               0x00000200
#define DMA_WBDMA16             0x00000400
#define DMA_WBDMA4              0x00000500
#define DMA_WBDMA2              0x00000600
#define DMA_WBDMA1              0x00000700
#define DMA_SAFE_GUARD          0x00000800
#define HI_PERF_GP_ENABLE       0x00001000
#define PIC_SNOOP_MODE_0        0x00002000
#define PIC_SNOOP_MODE_1        0x00004000
#define SOUNDBLASTER_IRQ_MASK   0x00008000
#define RING_IN_ENABLE          0x00010000
#define SPDIF_TEST_MODE         0x00020000
#define CLK_MULT_MODE_SELECT_2  0x00040000
#define EEPROM_WRITE_ENABLE     0x00080000
#define CODEC_DIR_IN            0x00100000
#define HV_BUTTON_FROM_GD       0x00200000
#define REDUCED_DEBOUNCE        0x00400000
#define HV_CTRL_ENABLE          0x00800000
#define SPDIF_ENABLE            0x01000000
#define CLK_DIV_SELECT          0x06000000
#define CLK_DIV_BY_48           0x00000000
#define CLK_DIV_BY_49           0x02000000
#define CLK_DIV_BY_50           0x04000000
#define CLK_DIV_RESERVED        0x06000000
#define PM_CTRL_ENABLE          0x08000000
#define CLK_MULT_MODE_SELECT    0x30000000
#define CLK_MULT_MODE_SHIFT     28
#define CLK_MULT_MODE_0         0x00000000
#define CLK_MULT_MODE_1         0x10000000
#define CLK_MULT_MODE_2         0x20000000
#define CLK_MULT_MODE_3         0x30000000
#define INT_CLK_SELECT          0x40000000
#define INT_CLK_MULT_RESET      0x80000000

/* M3 */
#define INT_CLK_SRC_NOT_PCI     0x00100000
#define INT_CLK_MULT_ENABLE     0x80000000

#define PCI_ACPI_CONTROL        0x54
#define PCI_ACPI_D0             0x00000000
#define PCI_ACPI_D1             0xB4F70000
#define PCI_ACPI_D2             0xB4F7B4F7

#define PCI_USER_CONFIG         0x58
#define EXT_PCI_MASTER_ENABLE   0x00000001
#define SPDIF_OUT_SELECT        0x00000002
#define TEST_PIN_DIR_CTRL       0x00000004
#define AC97_CODEC_TEST         0x00000020
#define TRI_STATE_BUFFER        0x00000080
#define IN_CLK_12MHZ_SELECT     0x00000100
#define MULTI_FUNC_DISABLE      0x00000200
#define EXT_MASTER_PAIR_SEL     0x00000400
#define PCI_MASTER_SUPPORT      0x00000800
#define STOP_CLOCK_ENABLE       0x00001000
#define EAPD_DRIVE_ENABLE       0x00002000
#define REQ_TRI_STATE_ENABLE    0x00004000
#define REQ_LOW_ENABLE          0x00008000
#define MIDI_1_ENABLE           0x00010000
#define MIDI_2_ENABLE           0x00020000
#define SB_AUDIO_SYNC           0x00040000
#define HV_CTRL_TEST            0x00100000
#define SOUNDBLASTER_TEST       0x00400000

#define PCI_USER_CONFIG_C       0x5C

#define PCI_DDMA_CTRL           0x60
#define DDMA_ENABLE             0x00000001


/* Allegro registers */
#define HOST_INT_CTRL           0x18
#define SB_INT_ENABLE           0x0001
#define MPU401_INT_ENABLE       0x0002
#define ASSP_INT_ENABLE         0x0010
#define RING_INT_ENABLE         0x0020
#define HV_INT_ENABLE           0x0040
#define CLKRUN_GEN_ENABLE       0x0100
#define HV_CTRL_TO_PME          0x0400
#define SOFTWARE_RESET_ENABLE   0x8000

/*
 * should be using the above defines, probably.
 */
#define REGB_ENABLE_RESET               0x01
#define REGB_STOP_CLOCK                 0x10

#define HOST_INT_STATUS         0x1A
#define SB_INT_PENDING          0x01
#define MPU401_INT_PENDING      0x02
#define ASSP_INT_PENDING        0x10
#define RING_INT_PENDING        0x20
#define HV_INT_PENDING          0x40

#define HARDWARE_VOL_CTRL       0x1B
#define SHADOW_MIX_REG_VOICE    0x1C
#define HW_VOL_COUNTER_VOICE    0x1D
#define SHADOW_MIX_REG_MASTER   0x1E
#define HW_VOL_COUNTER_MASTER   0x1F

#define CODEC_COMMAND           0x30
#define CODEC_READ_B            0x80

#define CODEC_STATUS            0x30
#define CODEC_BUSY_B            0x01

#define CODEC_DATA              0x32

#define RING_BUS_CTRL_A         0x36
#define RAC_PME_ENABLE          0x0100
#define RAC_SDFS_ENABLE         0x0200
#define LAC_PME_ENABLE          0x0400
#define LAC_SDFS_ENABLE         0x0800
#define SERIAL_AC_LINK_ENABLE   0x1000
#define IO_SRAM_ENABLE          0x2000
#define IIS_INPUT_ENABLE        0x8000

#define RING_BUS_CTRL_B         0x38
#define SECOND_CODEC_ID_MASK    0x0003
#define SPDIF_FUNC_ENABLE       0x0010
#define SECOND_AC_ENABLE        0x0020
#define SB_MODULE_INTF_ENABLE   0x0040
#define SSPE_ENABLE             0x0040
#define M3I_DOCK_ENABLE         0x0080

#define SDO_OUT_DEST_CTRL       0x3A
#define COMMAND_ADDR_OUT        0x0003
#define PCM_LR_OUT_LOCAL        0x0000
#define PCM_LR_OUT_REMOTE       0x0004
#define PCM_LR_OUT_MUTE         0x0008
#define PCM_LR_OUT_BOTH         0x000C
#define LINE1_DAC_OUT_LOCAL     0x0000
#define LINE1_DAC_OUT_REMOTE    0x0010
#define LINE1_DAC_OUT_MUTE      0x0020
#define LINE1_DAC_OUT_BOTH      0x0030
#define PCM_CLS_OUT_LOCAL       0x0000
#define PCM_CLS_OUT_REMOTE      0x0040
#define PCM_CLS_OUT_MUTE        0x0080
#define PCM_CLS_OUT_BOTH        0x00C0
#define PCM_RLF_OUT_LOCAL       0x0000
#define PCM_RLF_OUT_REMOTE      0x0100
#define PCM_RLF_OUT_MUTE        0x0200
#define PCM_RLF_OUT_BOTH        0x0300
#define LINE2_DAC_OUT_LOCAL     0x0000
#define LINE2_DAC_OUT_REMOTE    0x0400
#define LINE2_DAC_OUT_MUTE      0x0800
#define LINE2_DAC_OUT_BOTH      0x0C00
#define HANDSET_OUT_LOCAL       0x0000
#define HANDSET_OUT_REMOTE      0x1000
#define HANDSET_OUT_MUTE        0x2000
#define HANDSET_OUT_BOTH        0x3000
#define IO_CTRL_OUT_LOCAL       0x0000
#define IO_CTRL_OUT_REMOTE      0x4000
#define IO_CTRL_OUT_MUTE        0x8000
#define IO_CTRL_OUT_BOTH        0xC000

#define SDO_IN_DEST_CTRL        0x3C
#define STATUS_ADDR_IN          0x0003
#define PCM_LR_IN_LOCAL         0x0000
#define PCM_LR_IN_REMOTE        0x0004
#define PCM_LR_RESERVED         0x0008
#define PCM_LR_IN_BOTH          0x000C
#define LINE1_ADC_IN_LOCAL      0x0000
#define LINE1_ADC_IN_REMOTE     0x0010
#define LINE1_ADC_IN_MUTE       0x0020
#define MIC_ADC_IN_LOCAL        0x0000
#define MIC_ADC_IN_REMOTE       0x0040
#define MIC_ADC_IN_MUTE         0x0080
#define LINE2_DAC_IN_LOCAL      0x0000
#define LINE2_DAC_IN_REMOTE     0x0400
#define LINE2_DAC_IN_MUTE       0x0800
#define HANDSET_IN_LOCAL        0x0000
#define HANDSET_IN_REMOTE       0x1000
#define HANDSET_IN_MUTE         0x2000
#define IO_STATUS_IN_LOCAL      0x0000
#define IO_STATUS_IN_REMOTE     0x4000

#define SPDIF_IN_CTRL           0x3E
#define SPDIF_IN_ENABLE         0x0001

#define GPIO_DATA               0x60
#define GPIO_DATA_MASK          0x0FFF
#define GPIO_HV_STATUS          0x3000
#define GPIO_PME_STATUS         0x4000

#define GPIO_MASK               0x64
#define GPIO_DIRECTION          0x68
#define GPO_PRIMARY_AC97        0x0001
#define GPI_LINEOUT_SENSE       0x0004
#define GPO_SECONDARY_AC97      0x0008
#define GPI_VOL_DOWN            0x0010
#define GPI_VOL_UP              0x0020
#define GPI_IIS_CLK             0x0040
#define GPI_IIS_LRCLK           0x0080
#define GPI_IIS_DATA            0x0100
#define GPI_DOCKING_STATUS      0x0100
#define GPI_HEADPHONE_SENSE     0x0200
#define GPO_EXT_AMP_SHUTDOWN    0x1000

#define GPO_EXT_AMP_M3		1	/* default m3 amp */
#define GPO_EXT_AMP_ALLEGRO	8	/* default allegro amp */

/* M3 */
#define GPO_M3_EXT_AMP_SHUTDN   0x0002

#define ASSP_INDEX_PORT         0x80
#define ASSP_MEMORY_PORT        0x82
#define ASSP_DATA_PORT          0x84

#define MPU401_DATA_PORT        0x98
#define MPU401_STATUS_PORT      0x99

#define CLK_MULT_DATA_PORT      0x9C

#define ASSP_CONTROL_A          0xA2
#define ASSP_0_WS_ENABLE        0x01
#define ASSP_CTRL_A_RESERVED1   0x02
#define ASSP_CTRL_A_RESERVED2   0x04
#define ASSP_CLK_49MHZ_SELECT   0x08
#define FAST_PLU_ENABLE         0x10
#define ASSP_CTRL_A_RESERVED3   0x20
#define DSP_CLK_36MHZ_SELECT    0x40

#define ASSP_CONTROL_B          0xA4
#define RESET_ASSP              0x00
#define RUN_ASSP                0x01
#define ENABLE_ASSP_CLOCK       0x00
#define STOP_ASSP_CLOCK         0x10
#define RESET_TOGGLE            0x40

#define ASSP_CONTROL_C          0xA6
#define ASSP_HOST_INT_ENABLE    0x01
#define FM_ADDR_REMAP_DISABLE   0x02
#define HOST_WRITE_PORT_ENABLE  0x08

#define ASSP_HOST_INT_STATUS    0xAC
#define DSP2HOST_REQ_PIORECORD  0x01
#define DSP2HOST_REQ_I2SRATE    0x02
#define DSP2HOST_REQ_TIMER      0x04

/* AC97 registers */
/* XXX fix this crap up */
/*#define AC97_RESET              0x00*/

#define AC97_VOL_MUTE_B         0x8000
#define AC97_VOL_M              0x1F
#define AC97_LEFT_VOL_S         8

#define AC97_MASTER_VOL         0x02
#define AC97_LINE_LEVEL_VOL     0x04
#define AC97_MASTER_MONO_VOL    0x06
#define AC97_PC_BEEP_VOL        0x0A
#define AC97_PC_BEEP_VOL_M      0x0F
#define AC97_SROUND_MASTER_VOL  0x38
#define AC97_PC_BEEP_VOL_S      1

/*#define AC97_PHONE_VOL          0x0C
#define AC97_MIC_VOL            0x0E*/
#define AC97_MIC_20DB_ENABLE    0x40

/*#define AC97_LINEIN_VOL         0x10
#define AC97_CD_VOL             0x12
#define AC97_VIDEO_VOL          0x14
#define AC97_AUX_VOL            0x16*/
#define AC97_PCM_OUT_VOL        0x18
/*#define AC97_RECORD_SELECT      0x1A*/
#define AC97_RECORD_MIC         0x00
#define AC97_RECORD_CD          0x01
#define AC97_RECORD_VIDEO       0x02
#define AC97_RECORD_AUX         0x03
#define AC97_RECORD_MONO_MUX    0x02
#define AC97_RECORD_DIGITAL     0x03
#define AC97_RECORD_LINE        0x04
#define AC97_RECORD_STEREO      0x05
#define AC97_RECORD_MONO        0x06
#define AC97_RECORD_PHONE       0x07

/*#define AC97_RECORD_GAIN        0x1C*/
#define AC97_RECORD_VOL_M       0x0F

/*#define AC97_GENERAL_PURPOSE    0x20*/
#define AC97_POWER_DOWN_CTRL    0x26
#define AC97_ADC_READY          0x0001
#define AC97_DAC_READY          0x0002
#define AC97_ANALOG_READY       0x0004
#define AC97_VREF_ON            0x0008
#define AC97_PR0                0x0100
#define AC97_PR1                0x0200
#define AC97_PR2                0x0400
#define AC97_PR3                0x0800
#define AC97_PR4                0x1000

#define AC97_RESERVED1          0x28

#define AC97_VENDOR_TEST        0x5A

#define AC97_CLOCK_DELAY        0x5C
#define AC97_LINEOUT_MUX_SEL    0x0001
#define AC97_MONO_MUX_SEL       0x0002
#define AC97_CLOCK_DELAY_SEL    0x1F
#define AC97_DAC_CDS_SHIFT      6
#define AC97_ADC_CDS_SHIFT      11

#define AC97_MULTI_CHANNEL_SEL  0x74

/*#define AC97_VENDOR_ID1         0x7C
#define AC97_VENDOR_ID2         0x7E*/

/*
 * ASSP control regs
 */
#define DSP_PORT_TIMER_COUNT    0x06

#define DSP_PORT_MEMORY_INDEX   0x80

#define DSP_PORT_MEMORY_TYPE    0x82
#define MEMTYPE_INTERNAL_CODE   0x0002
#define MEMTYPE_INTERNAL_DATA   0x0003
#define MEMTYPE_MASK            0x0003

#define DSP_PORT_MEMORY_DATA    0x84

#define DSP_PORT_CONTROL_REG_A  0xA2
#define DSP_PORT_CONTROL_REG_B  0xA4
#define DSP_PORT_CONTROL_REG_C  0xA6

#define REV_A_CODE_MEMORY_BEGIN         0x0000
#define REV_A_CODE_MEMORY_END           0x0FFF
#define REV_A_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_A_CODE_MEMORY_LENGTH        (REV_A_CODE_MEMORY_END - REV_A_CODE_MEMORY_BEGIN + 1)

#define REV_B_CODE_MEMORY_BEGIN         0x0000
#define REV_B_CODE_MEMORY_END           0x0BFF
#define REV_B_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_B_CODE_MEMORY_LENGTH        (REV_B_CODE_MEMORY_END - REV_B_CODE_MEMORY_BEGIN + 1)

#define REV_A_DATA_MEMORY_BEGIN         0x1000
#define REV_A_DATA_MEMORY_END           0x2FFF
#define REV_A_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_A_DATA_MEMORY_LENGTH        (REV_A_DATA_MEMORY_END - REV_A_DATA_MEMORY_BEGIN + 1)

#define REV_B_DATA_MEMORY_BEGIN         0x1000
#define REV_B_DATA_MEMORY_END           0x2BFF
#define REV_B_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_B_DATA_MEMORY_LENGTH        (REV_B_DATA_MEMORY_END - REV_B_DATA_MEMORY_BEGIN + 1)


#define NUM_UNITS_KERNEL_CODE          16
#define NUM_UNITS_KERNEL_DATA           2

#define NUM_UNITS_KERNEL_CODE_WITH_HSP 16
#define NUM_UNITS_KERNEL_DATA_WITH_HSP  5

/*
 * Kernel data layout
 */

#define DP_SHIFT_COUNT                  7

#define KDATA_BASE_ADDR                 0x1000
#define KDATA_BASE_ADDR2                0x1080

#define KDATA_TASK0                     (KDATA_BASE_ADDR + 0x0000)
#define KDATA_TASK1                     (KDATA_BASE_ADDR + 0x0001)
#define KDATA_TASK2                     (KDATA_BASE_ADDR + 0x0002)
#define KDATA_TASK3                     (KDATA_BASE_ADDR + 0x0003)
#define KDATA_TASK4                     (KDATA_BASE_ADDR + 0x0004)
#define KDATA_TASK5                     (KDATA_BASE_ADDR + 0x0005)
#define KDATA_TASK6                     (KDATA_BASE_ADDR + 0x0006)
#define KDATA_TASK7                     (KDATA_BASE_ADDR + 0x0007)
#define KDATA_TASK_ENDMARK              (KDATA_BASE_ADDR + 0x0008)

#define KDATA_CURRENT_TASK              (KDATA_BASE_ADDR + 0x0009)
#define KDATA_TASK_SWITCH               (KDATA_BASE_ADDR + 0x000A)

#define KDATA_INSTANCE0_POS3D           (KDATA_BASE_ADDR + 0x000B)
#define KDATA_INSTANCE1_POS3D           (KDATA_BASE_ADDR + 0x000C)
#define KDATA_INSTANCE2_POS3D           (KDATA_BASE_ADDR + 0x000D)
#define KDATA_INSTANCE3_POS3D           (KDATA_BASE_ADDR + 0x000E)
#define KDATA_INSTANCE4_POS3D           (KDATA_BASE_ADDR + 0x000F)
#define KDATA_INSTANCE5_POS3D           (KDATA_BASE_ADDR + 0x0010)
#define KDATA_INSTANCE6_POS3D           (KDATA_BASE_ADDR + 0x0011)
#define KDATA_INSTANCE7_POS3D           (KDATA_BASE_ADDR + 0x0012)
#define KDATA_INSTANCE8_POS3D           (KDATA_BASE_ADDR + 0x0013)
#define KDATA_INSTANCE_POS3D_ENDMARK    (KDATA_BASE_ADDR + 0x0014)

#define KDATA_INSTANCE0_SPKVIRT         (KDATA_BASE_ADDR + 0x0015)
#define KDATA_INSTANCE_SPKVIRT_ENDMARK  (KDATA_BASE_ADDR + 0x0016)

#define KDATA_INSTANCE0_SPDIF           (KDATA_BASE_ADDR + 0x0017)
#define KDATA_INSTANCE_SPDIF_ENDMARK    (KDATA_BASE_ADDR + 0x0018)

#define KDATA_INSTANCE0_MODEM           (KDATA_BASE_ADDR + 0x0019)
#define KDATA_INSTANCE_MODEM_ENDMARK    (KDATA_BASE_ADDR + 0x001A)

#define KDATA_INSTANCE0_SRC             (KDATA_BASE_ADDR + 0x001B)
#define KDATA_INSTANCE1_SRC             (KDATA_BASE_ADDR + 0x001C)
#define KDATA_INSTANCE_SRC_ENDMARK      (KDATA_BASE_ADDR + 0x001D)

#define KDATA_INSTANCE0_MINISRC         (KDATA_BASE_ADDR + 0x001E)
#define KDATA_INSTANCE1_MINISRC         (KDATA_BASE_ADDR + 0x001F)
#define KDATA_INSTANCE2_MINISRC         (KDATA_BASE_ADDR + 0x0020)
#define KDATA_INSTANCE3_MINISRC         (KDATA_BASE_ADDR + 0x0021)
#define KDATA_INSTANCE_MINISRC_ENDMARK  (KDATA_BASE_ADDR + 0x0022)

#define KDATA_INSTANCE0_CPYTHRU         (KDATA_BASE_ADDR + 0x0023)
#define KDATA_INSTANCE1_CPYTHRU         (KDATA_BASE_ADDR + 0x0024)
#define KDATA_INSTANCE_CPYTHRU_ENDMARK  (KDATA_BASE_ADDR + 0x0025)

#define KDATA_CURRENT_DMA               (KDATA_BASE_ADDR + 0x0026)
#define KDATA_DMA_SWITCH                (KDATA_BASE_ADDR + 0x0027)
#define KDATA_DMA_ACTIVE                (KDATA_BASE_ADDR + 0x0028)

#define KDATA_DMA_XFER0                 (KDATA_BASE_ADDR + 0x0029)
#define KDATA_DMA_XFER1                 (KDATA_BASE_ADDR + 0x002A)
#define KDATA_DMA_XFER2                 (KDATA_BASE_ADDR + 0x002B)
#define KDATA_DMA_XFER3                 (KDATA_BASE_ADDR + 0x002C)
#define KDATA_DMA_XFER4                 (KDATA_BASE_ADDR + 0x002D)
#define KDATA_DMA_XFER5                 (KDATA_BASE_ADDR + 0x002E)
#define KDATA_DMA_XFER6                 (KDATA_BASE_ADDR + 0x002F)
#define KDATA_DMA_XFER7                 (KDATA_BASE_ADDR + 0x0030)
#define KDATA_DMA_XFER8                 (KDATA_BASE_ADDR + 0x0031)
#define KDATA_DMA_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0032)

#define KDATA_I2S_SAMPLE_COUNT          (KDATA_BASE_ADDR + 0x0033)
#define KDATA_I2S_INT_METER             (KDATA_BASE_ADDR + 0x0034)
#define KDATA_I2S_ACTIVE                (KDATA_BASE_ADDR + 0x0035)

#define KDATA_TIMER_COUNT_RELOAD        (KDATA_BASE_ADDR + 0x0036)
#define KDATA_TIMER_COUNT_CURRENT       (KDATA_BASE_ADDR + 0x0037)

#define KDATA_HALT_SYNCH_CLIENT         (KDATA_BASE_ADDR + 0x0038)
#define KDATA_HALT_SYNCH_DMA            (KDATA_BASE_ADDR + 0x0039)
#define KDATA_HALT_ACKNOWLEDGE          (KDATA_BASE_ADDR + 0x003A)

#define KDATA_ADC1_XFER0                (KDATA_BASE_ADDR + 0x003B)
#define KDATA_ADC1_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x003C)
#define KDATA_ADC1_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x003D)
#define KDATA_ADC1_RIGHT_VOLUME  		(KDATA_BASE_ADDR + 0x003E)
#define KDATA_ADC1_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x003F)
#define KDATA_ADC1_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0040)

#define KDATA_ADC2_XFER0                (KDATA_BASE_ADDR + 0x0041)
#define KDATA_ADC2_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x0042)
#define KDATA_ADC2_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0043)
#define KDATA_ADC2_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x0044)
#define KDATA_ADC2_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x0045)
#define KDATA_ADC2_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0046)

#define KDATA_CD_XFER0					(KDATA_BASE_ADDR + 0x0047)					
#define KDATA_CD_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x0048)
#define KDATA_CD_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0049)
#define KDATA_CD_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x004A)
#define KDATA_CD_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x004B)
#define KDATA_CD_RIGHT_SUR_VOL			(KDATA_BASE_ADDR + 0x004C)

#define KDATA_MIC_XFER0					(KDATA_BASE_ADDR + 0x004D)
#define KDATA_MIC_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x004E)
#define KDATA_MIC_VOLUME				(KDATA_BASE_ADDR + 0x004F)
#define KDATA_MIC_SUR_VOL				(KDATA_BASE_ADDR + 0x0050)

#define KDATA_I2S_XFER0                 (KDATA_BASE_ADDR + 0x0051)
#define KDATA_I2S_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0052)

#define KDATA_CHI_XFER0                 (KDATA_BASE_ADDR + 0x0053)
#define KDATA_CHI_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0054)

#define KDATA_SPDIF_XFER                (KDATA_BASE_ADDR + 0x0055)
#define KDATA_SPDIF_CURRENT_FRAME       (KDATA_BASE_ADDR + 0x0056)
#define KDATA_SPDIF_FRAME0              (KDATA_BASE_ADDR + 0x0057)
#define KDATA_SPDIF_FRAME1              (KDATA_BASE_ADDR + 0x0058)
#define KDATA_SPDIF_FRAME2              (KDATA_BASE_ADDR + 0x0059)

#define KDATA_SPDIF_REQUEST             (KDATA_BASE_ADDR + 0x005A)
#define KDATA_SPDIF_TEMP                (KDATA_BASE_ADDR + 0x005B)

#define KDATA_SPDIFIN_XFER0             (KDATA_BASE_ADDR + 0x005C)
#define KDATA_SPDIFIN_XFER_ENDMARK      (KDATA_BASE_ADDR + 0x005D)
#define KDATA_SPDIFIN_INT_METER         (KDATA_BASE_ADDR + 0x005E)

#define KDATA_DSP_RESET_COUNT           (KDATA_BASE_ADDR + 0x005F)
#define KDATA_DEBUG_OUTPUT              (KDATA_BASE_ADDR + 0x0060)

#define KDATA_KERNEL_ISR_LIST           (KDATA_BASE_ADDR + 0x0061)

#define KDATA_KERNEL_ISR_CBSR1          (KDATA_BASE_ADDR + 0x0062)
#define KDATA_KERNEL_ISR_CBER1          (KDATA_BASE_ADDR + 0x0063)
#define KDATA_KERNEL_ISR_CBCR           (KDATA_BASE_ADDR + 0x0064)
#define KDATA_KERNEL_ISR_AR0            (KDATA_BASE_ADDR + 0x0065)
#define KDATA_KERNEL_ISR_AR1            (KDATA_BASE_ADDR + 0x0066)
#define KDATA_KERNEL_ISR_AR2            (KDATA_BASE_ADDR + 0x0067)
#define KDATA_KERNEL_ISR_AR3            (KDATA_BASE_ADDR + 0x0068)
#define KDATA_KERNEL_ISR_AR4            (KDATA_BASE_ADDR + 0x0069)
#define KDATA_KERNEL_ISR_AR5            (KDATA_BASE_ADDR + 0x006A)
#define KDATA_KERNEL_ISR_BRCR           (KDATA_BASE_ADDR + 0x006B)
#define KDATA_KERNEL_ISR_PASR           (KDATA_BASE_ADDR + 0x006C)
#define KDATA_KERNEL_ISR_PAER           (KDATA_BASE_ADDR + 0x006D)

#define KDATA_CLIENT_SCRATCH0           (KDATA_BASE_ADDR + 0x006E)
#define KDATA_CLIENT_SCRATCH1           (KDATA_BASE_ADDR + 0x006F)
#define KDATA_KERNEL_SCRATCH            (KDATA_BASE_ADDR + 0x0070)
#define KDATA_KERNEL_ISR_SCRATCH        (KDATA_BASE_ADDR + 0x0071)

#define KDATA_OUEUE_LEFT                (KDATA_BASE_ADDR + 0x0072)
#define KDATA_QUEUE_RIGHT               (KDATA_BASE_ADDR + 0x0073)

#define KDATA_ADC1_REQUEST              (KDATA_BASE_ADDR + 0x0074)
#define KDATA_ADC2_REQUEST              (KDATA_BASE_ADDR + 0x0075)
#define KDATA_CD_REQUEST				(KDATA_BASE_ADDR + 0x0076)
#define KDATA_MIC_REQUEST				(KDATA_BASE_ADDR + 0x0077)

#define KDATA_ADC1_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0078)
#define KDATA_ADC2_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0079)
#define KDATA_CD_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007A)
#define KDATA_MIC_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007B)
#define KDATA_MIC_SYNC_COUNTER			(KDATA_BASE_ADDR + 0x007C)

/*
 * second 'segment' (?) reserved for mixer
 * buffers..
 */

#define KDATA_MIXER_WORD0               (KDATA_BASE_ADDR2 + 0x0000)
#define KDATA_MIXER_WORD1               (KDATA_BASE_ADDR2 + 0x0001)
#define KDATA_MIXER_WORD2               (KDATA_BASE_ADDR2 + 0x0002)
#define KDATA_MIXER_WORD3               (KDATA_BASE_ADDR2 + 0x0003)
#define KDATA_MIXER_WORD4               (KDATA_BASE_ADDR2 + 0x0004)
#define KDATA_MIXER_WORD5               (KDATA_BASE_ADDR2 + 0x0005)
#define KDATA_MIXER_WORD6               (KDATA_BASE_ADDR2 + 0x0006)
#define KDATA_MIXER_WORD7               (KDATA_BASE_ADDR2 + 0x0007)
#define KDATA_MIXER_WORD8               (KDATA_BASE_ADDR2 + 0x0008)
#define KDATA_MIXER_WORD9               (KDATA_BASE_ADDR2 + 0x0009)
#define KDATA_MIXER_WORDA               (KDATA_BASE_ADDR2 + 0x000A)
#define KDATA_MIXER_WORDB               (KDATA_BASE_ADDR2 + 0x000B)
#define KDATA_MIXER_WORDC               (KDATA_BASE_ADDR2 + 0x000C)
#define KDATA_MIXER_WORDD               (KDATA_BASE_ADDR2 + 0x000D)
#define KDATA_MIXER_WORDE               (KDATA_BASE_ADDR2 + 0x000E)
#define KDATA_MIXER_WORDF               (KDATA_BASE_ADDR2 + 0x000F)

#define KDATA_MIXER_XFER0               (KDATA_BASE_ADDR2 + 0x0010)
#define KDATA_MIXER_XFER1               (KDATA_BASE_ADDR2 + 0x0011)
#define KDATA_MIXER_XFER2               (KDATA_BASE_ADDR2 + 0x0012)
#define KDATA_MIXER_XFER3               (KDATA_BASE_ADDR2 + 0x0013)
#define KDATA_MIXER_XFER4               (KDATA_BASE_ADDR2 + 0x0014)
#define KDATA_MIXER_XFER5               (KDATA_BASE_ADDR2 + 0x0015)
#define KDATA_MIXER_XFER6               (KDATA_BASE_ADDR2 + 0x0016)
#define KDATA_MIXER_XFER7               (KDATA_BASE_ADDR2 + 0x0017)
#define KDATA_MIXER_XFER8               (KDATA_BASE_ADDR2 + 0x0018)
#define KDATA_MIXER_XFER9               (KDATA_BASE_ADDR2 + 0x0019)
#define KDATA_MIXER_XFER_ENDMARK        (KDATA_BASE_ADDR2 + 0x001A)

#define KDATA_MIXER_TASK_NUMBER         (KDATA_BASE_ADDR2 + 0x001B)
#define KDATA_CURRENT_MIXER             (KDATA_BASE_ADDR2 + 0x001C)
#define KDATA_MIXER_ACTIVE              (KDATA_BASE_ADDR2 + 0x001D)
#define KDATA_MIXER_BANK_STATUS         (KDATA_BASE_ADDR2 + 0x001E)
#define KDATA_DAC_LEFT_VOLUME	        (KDATA_BASE_ADDR2 + 0x001F)
#define KDATA_DAC_RIGHT_VOLUME          (KDATA_BASE_ADDR2 + 0x0020)

#define MAX_INSTANCE_MINISRC            (KDATA_INSTANCE_MINISRC_ENDMARK - KDATA_INSTANCE0_MINISRC)
#define MAX_VIRTUAL_DMA_CHANNELS        (KDATA_DMA_XFER_ENDMARK - KDATA_DMA_XFER0)
#define MAX_VIRTUAL_MIXER_CHANNELS      (KDATA_MIXER_XFER_ENDMARK - KDATA_MIXER_XFER0)
#define MAX_VIRTUAL_ADC1_CHANNELS       (KDATA_ADC1_XFER_ENDMARK - KDATA_ADC1_XFER0)

/*
 * client data area offsets
 */
#define CDATA_INSTANCE_READY            0x00

#define CDATA_HOST_SRC_ADDRL            0x01
#define CDATA_HOST_SRC_ADDRH            0x02
#define CDATA_HOST_SRC_END_PLUS_1L      0x03
#define CDATA_HOST_SRC_END_PLUS_1H      0x04
#define CDATA_HOST_SRC_CURRENTL         0x05
#define CDATA_HOST_SRC_CURRENTH         0x06

#define CDATA_IN_BUF_CONNECT            0x07
#define CDATA_OUT_BUF_CONNECT           0x08

#define CDATA_IN_BUF_BEGIN              0x09
#define CDATA_IN_BUF_END_PLUS_1         0x0A
#define CDATA_IN_BUF_HEAD               0x0B
#define CDATA_IN_BUF_TAIL               0x0C
#define CDATA_OUT_BUF_BEGIN             0x0D
#define CDATA_OUT_BUF_END_PLUS_1        0x0E
#define CDATA_OUT_BUF_HEAD              0x0F
#define CDATA_OUT_BUF_TAIL              0x10

#define CDATA_DMA_CONTROL               0x11
#define CDATA_RESERVED                  0x12

#define CDATA_FREQUENCY                 0x13
#define CDATA_LEFT_VOLUME               0x14
#define CDATA_RIGHT_VOLUME              0x15
#define CDATA_LEFT_SUR_VOL              0x16
#define CDATA_RIGHT_SUR_VOL             0x17

#define CDATA_HEADER_LEN                0x18

#define SRC3_DIRECTION_OFFSET           CDATA_HEADER_LEN
#define SRC3_MODE_OFFSET                (CDATA_HEADER_LEN + 1)
#define SRC3_WORD_LENGTH_OFFSET         (CDATA_HEADER_LEN + 2)
#define SRC3_PARAMETER_OFFSET           (CDATA_HEADER_LEN + 3)
#define SRC3_COEFF_ADDR_OFFSET          (CDATA_HEADER_LEN + 8)
#define SRC3_FILTAP_ADDR_OFFSET         (CDATA_HEADER_LEN + 10)
#define SRC3_TEMP_INBUF_ADDR_OFFSET     (CDATA_HEADER_LEN + 16)
#define SRC3_TEMP_OUTBUF_ADDR_OFFSET    (CDATA_HEADER_LEN + 17)

#define MINISRC_IN_BUFFER_SIZE   ( 0x50 * 2 )
#define MINISRC_OUT_BUFFER_SIZE  ( 0x50 * 2 * 2)
#define MINISRC_TMP_BUFFER_SIZE  ( 112 + ( MINISRC_BIQUAD_STAGE * 3 + 4 ) * 2 * 2 )
#define MINISRC_BIQUAD_STAGE    2
#define MINISRC_COEF_LOC          0x175

#define DMACONTROL_BLOCK_MASK           0x000F
#define  DMAC_BLOCK0_SELECTOR           0x0000
#define  DMAC_BLOCK1_SELECTOR           0x0001
#define  DMAC_BLOCK2_SELECTOR           0x0002
#define  DMAC_BLOCK3_SELECTOR           0x0003
#define  DMAC_BLOCK4_SELECTOR           0x0004
#define  DMAC_BLOCK5_SELECTOR           0x0005
#define  DMAC_BLOCK6_SELECTOR           0x0006
#define  DMAC_BLOCK7_SELECTOR           0x0007
#define  DMAC_BLOCK8_SELECTOR           0x0008
#define  DMAC_BLOCK9_SELECTOR           0x0009
#define  DMAC_BLOCKA_SELECTOR           0x000A
#define  DMAC_BLOCKB_SELECTOR           0x000B
#define  DMAC_BLOCKC_SELECTOR           0x000C
#define  DMAC_BLOCKD_SELECTOR           0x000D
#define  DMAC_BLOCKE_SELECTOR           0x000E
#define  DMAC_BLOCKF_SELECTOR           0x000F
#define DMACONTROL_PAGE_MASK            0x00F0
#define  DMAC_PAGE0_SELECTOR            0x0030
#define  DMAC_PAGE1_SELECTOR            0x0020
#define  DMAC_PAGE2_SELECTOR            0x0010
#define  DMAC_PAGE3_SELECTOR            0x0000
#define DMACONTROL_AUTOREPEAT           0x1000
#define DMACONTROL_STOPPED              0x2000
#define DMACONTROL_DIRECTION            0x0100

/*
 * an arbitrary volume we set the internal
 * volume settings to so that the ac97 volume
 * range is a little less insane.  0x7fff is 
 * max.
 */
#define ARB_VOLUME ( 0x6800 )

/*
 */

struct m3_list {
	int curlen;
	int mem_addr;
	int max;
};

struct m3_dma {

	int number;
	struct snd_pcm_substream *substream;

	struct assp_instance {
		unsigned short code, data;
	} inst;

	int running;
	int opened;

	unsigned long buffer_addr;
	int dma_size;
	int period_size;
	unsigned int hwptr;
	int count;

	int index[3];
	struct m3_list *index_list[3];

        int in_lists;
	
	struct list_head list;

};
    
struct snd_m3 {
	
	struct snd_card *card;

	unsigned long iobase;

	int irq;
	unsigned int allegro_flag : 1;

	struct snd_ac97 *ac97;

	struct snd_pcm *pcm;

	struct pci_dev *pci;

	int dacs_active;
	int timer_users;

	struct m3_list  msrc_list;
	struct m3_list  mixer_list;
	struct m3_list  adc1_list;
	struct m3_list  dma_list;

	/* for storing reset state..*/
	u8 reset_state;

	int external_amp;
	int amp_gpio;	/* gpio pin #  for external amp, -1 = default */
	unsigned int hv_config;		/* hardware-volume config bits */
	unsigned irda_workaround :1;	/* avoid to touch 0x10 on GPIO_DIRECTION
					   (e.g. for IrDA on Dell Inspirons) */
	unsigned is_omnibook :1;	/* Do HP OmniBook GPIO magic? */

	/* midi */
	struct snd_rawmidi *rmidi;

	/* pcm streams */
	int num_substreams;
	struct m3_dma *substreams;

	spinlock_t reg_lock;
	spinlock_t ac97_lock;

	struct snd_kcontrol *master_switch;
	struct snd_kcontrol *master_volume;
	struct tasklet_struct hwvol_tq;

#ifdef CONFIG_PM
	u16 *suspend_mem;
#endif

	const struct firmware *assp_kernel_image;
	const struct firmware *assp_minisrc_image;
};

/*
 * pci ids
 */
static struct pci_device_id snd_m3_ids[] = {
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_ALLEGRO_1, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_ALLEGRO, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_CANYON3D_2LE, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_CANYON3D_2, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_MAESTRO3, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_MAESTRO3_1, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_MAESTRO3_HW, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_MAESTRO3_2, PCI_ANY_ID, PCI_ANY_ID,
	 PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, 0},
	{0,},
};

MODULE_DEVICE_TABLE(pci, snd_m3_ids);

static struct snd_pci_quirk m3_amp_quirk_list[] __devinitdata = {
	SND_PCI_QUIRK(0x10f7, 0x833e, "Panasonic CF-28", 0x0d),
	SND_PCI_QUIRK(0x10f7, 0x833d, "Panasonic CF-72", 0x0d),
	SND_PCI_QUIRK(0x1033, 0x80f1, "NEC LM800J/7", 0x03),
	SND_PCI_QUIRK(0x1509, 0x1740, "LEGEND ZhaoYang 3100CF", 0x03),
	{ } /* END */
};

static struct snd_pci_quirk m3_irda_quirk_list[] __devinitdata = {
	SND_PCI_QUIRK(0x1028, 0x00b0, "Dell Inspiron 4000", 1),
	SND_PCI_QUIRK(0x1028, 0x00a4, "Dell Inspiron 8000", 1),
	SND_PCI_QUIRK(0x1028, 0x00e6, "Dell Inspiron 8100", 1),
	{ } /* END */
};

/* hardware volume quirks */
static struct snd_pci_quirk m3_hv_quirk_list[] __devinitdata = {
	/* Allegro chips */
	SND_PCI_QUIRK(0x0E11, 0x002E, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x0E11, 0x0094, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x0E11, 0xB112, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x0E11, 0xB114, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x103C, 0x0012, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x103C, 0x0018, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x103C, 0x001C, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x103C, 0x001D, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x103C, 0x001E, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x107B, 0x3350, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x10F7, 0x8338, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x10F7, 0x833C, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x10F7, 0x833D, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x10F7, 0x833E, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x10F7, 0x833F, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x13BD, 0x1018, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x13BD, 0x1019, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x13BD, 0x101A, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x14FF, 0x0F03, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x14FF, 0x0F04, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x14FF, 0x0F05, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x156D, 0xB400, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x156D, 0xB795, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x156D, 0xB797, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x156D, 0xC700, NULL, HV_CTRL_ENABLE | HV_BUTTON_FROM_GD),
	SND_PCI_QUIRK(0x1033, 0x80F1, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x103C, 0x001A, NULL, /* HP OmniBook 6100 */
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x107B, 0x340A, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x107B, 0x3450, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x109F, 0x3134, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x109F, 0x3161, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0x3280, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0x3281, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0xC002, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0xC003, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x1509, 0x1740, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x1610, 0x0010, NULL,
		      HV_CTRL_ENABLE | HV_BUTTON_FROM_GD | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x1042, 0x1042, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x107B, 0x9500, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x14FF, 0x0F06, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x1558, 0x8586, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x161F, 0x2011, NULL, HV_CTRL_ENABLE),
	/* Maestro3 chips */
	SND_PCI_QUIRK(0x103C, 0x000E, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x103C, 0x0010, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x103C, 0x0011, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x103C, 0x001B, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x104D, 0x80A6, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x104D, 0x80AA, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x107B, 0x5300, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x110A, 0x1998, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x13BD, 0x1015, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x13BD, 0x101C, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x13BD, 0x1802, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x1599, 0x0715, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x5643, 0x5643, NULL, HV_CTRL_ENABLE),
	SND_PCI_QUIRK(0x144D, 0x3260, NULL, HV_CTRL_ENABLE | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0x3261, NULL, HV_CTRL_ENABLE | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0xC000, NULL, HV_CTRL_ENABLE | REDUCED_DEBOUNCE),
	SND_PCI_QUIRK(0x144D, 0xC001, NULL, HV_CTRL_ENABLE | REDUCED_DEBOUNCE),
	{ } /* END */
};

/* HP Omnibook quirks */
static struct snd_pci_quirk m3_omnibook_quirk_list[] __devinitdata = {
	SND_PCI_QUIRK_ID(0x103c, 0x0010), /* HP OmniBook 6000 */
	SND_PCI_QUIRK_ID(0x103c, 0x0011), /* HP OmniBook 500 */
	{ } /* END */
};

/*
 * lowlevel functions
 */

static inline void snd_m3_outw(struct snd_m3 *chip, u16 value, unsigned long reg)
{
	outw(value, chip->iobase + reg);
}

static inline u16 snd_m3_inw(struct snd_m3 *chip, unsigned long reg)
{
	return inw(chip->iobase + reg);
}

static inline void snd_m3_outb(struct snd_m3 *chip, u8 value, unsigned long reg)
{
	outb(value, chip->iobase + reg);
}

static inline u8 snd_m3_inb(struct snd_m3 *chip, unsigned long reg)
{
	return inb(chip->iobase + reg);
}

/*
 * access 16bit words to the code or data regions of the dsp's memory.
 * index addresses 16bit words.
 */
static u16 snd_m3_assp_read(struct snd_m3 *chip, u16 region, u16 index)
{
	snd_m3_outw(chip, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
	snd_m3_outw(chip, index, DSP_PORT_MEMORY_INDEX);
	return snd_m3_inw(chip, DSP_PORT_MEMORY_DATA);
}

static void snd_m3_assp_write(struct snd_m3 *chip, u16 region, u16 index, u16 data)
{
	snd_m3_outw(chip, region & MEMTYPE_MASK, DSP_PORT_MEMORY_TYPE);
	snd_m3_outw(chip, index, DSP_PORT_MEMORY_INDEX);
	snd_m3_outw(chip, data, DSP_PORT_MEMORY_DATA);
}

static void snd_m3_assp_halt(struct snd_m3 *chip)
{
	chip->reset_state = snd_m3_inb(chip, DSP_PORT_CONTROL_REG_B) & ~REGB_STOP_CLOCK;
	msleep(10);
	snd_m3_outb(chip, chip->reset_state & ~REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}

static void snd_m3_assp_continue(struct snd_m3 *chip)
{
	snd_m3_outb(chip, chip->reset_state | REGB_ENABLE_RESET, DSP_PORT_CONTROL_REG_B);
}


/*
 * This makes me sad. the maestro3 has lists
 * internally that must be packed.. 0 terminates,
 * apparently, or maybe all unused entries have
 * to be 0, the lists have static lengths set
 * by the binary code images.
 */

static int snd_m3_add_list(struct snd_m3 *chip, struct m3_list *list, u16 val)
{
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  list->mem_addr + list->curlen,
			  val);
	return list->curlen++;
}

static void snd_m3_remove_list(struct snd_m3 *chip, struct m3_list *list, int index)
{
	u16  val;
	int lastindex = list->curlen - 1;

	if (index != lastindex) {
		val = snd_m3_assp_read(chip, MEMTYPE_INTERNAL_DATA,
				       list->mem_addr + lastindex);
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  list->mem_addr + index,
				  val);
	}

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  list->mem_addr + lastindex,
			  0);

	list->curlen--;
}

static void snd_m3_inc_timer_users(struct snd_m3 *chip)
{
	chip->timer_users++;
	if (chip->timer_users != 1) 
		return;

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_TIMER_COUNT_RELOAD,
			  240);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_TIMER_COUNT_CURRENT,
			  240);

	snd_m3_outw(chip,
		    snd_m3_inw(chip, HOST_INT_CTRL) | CLKRUN_GEN_ENABLE,
		    HOST_INT_CTRL);
}

static void snd_m3_dec_timer_users(struct snd_m3 *chip)
{
	chip->timer_users--;
	if (chip->timer_users > 0)  
		return;

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_TIMER_COUNT_RELOAD,
			  0);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_TIMER_COUNT_CURRENT,
			  0);

	snd_m3_outw(chip,
		    snd_m3_inw(chip, HOST_INT_CTRL) & ~CLKRUN_GEN_ENABLE,
		    HOST_INT_CTRL);
}

/*
 * start/stop
 */

/* spinlock held! */
static int snd_m3_pcm_start(struct snd_m3 *chip, struct m3_dma *s,
			    struct snd_pcm_substream *subs)
{
	if (! s || ! subs)
		return -EINVAL;

	snd_m3_inc_timer_users(chip);
	switch (subs->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		chip->dacs_active++;
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  s->inst.data + CDATA_INSTANCE_READY, 1);
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  KDATA_MIXER_TASK_NUMBER,
				  chip->dacs_active);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  KDATA_ADC1_REQUEST, 1);
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  s->inst.data + CDATA_INSTANCE_READY, 1);
		break;
	}
	return 0;
}

/* spinlock held! */
static int snd_m3_pcm_stop(struct snd_m3 *chip, struct m3_dma *s,
			   struct snd_pcm_substream *subs)
{
	if (! s || ! subs)
		return -EINVAL;

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_INSTANCE_READY, 0);
	snd_m3_dec_timer_users(chip);
	switch (subs->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		chip->dacs_active--;
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  KDATA_MIXER_TASK_NUMBER, 
				  chip->dacs_active);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  KDATA_ADC1_REQUEST, 0);
		break;
	}
	return 0;
}

static int
snd_m3_pcm_trigger(struct snd_pcm_substream *subs, int cmd)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);
	struct m3_dma *s = subs->runtime->private_data;
	int err = -EINVAL;

	snd_assert(s != NULL, return -ENXIO);

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (s->running)
			err = -EBUSY;
		else {
			s->running = 1;
			err = snd_m3_pcm_start(chip, s, subs);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (! s->running)
			err = 0; /* should return error? */
		else {
			s->running = 0;
			err = snd_m3_pcm_stop(chip, s, subs);
		}
		break;
	}
	spin_unlock(&chip->reg_lock);
	return err;
}

/*
 * setup
 */
static void 
snd_m3_pcm_setup1(struct snd_m3 *chip, struct m3_dma *s, struct snd_pcm_substream *subs)
{
	int dsp_in_size, dsp_out_size, dsp_in_buffer, dsp_out_buffer;
	struct snd_pcm_runtime *runtime = subs->runtime;

	if (subs->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dsp_in_size = MINISRC_IN_BUFFER_SIZE - (0x20 * 2);
		dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x20 * 2);
	} else {
		dsp_in_size = MINISRC_IN_BUFFER_SIZE - (0x10 * 2);
		dsp_out_size = MINISRC_OUT_BUFFER_SIZE - (0x10 * 2);
	}
	dsp_in_buffer = s->inst.data + (MINISRC_TMP_BUFFER_SIZE / 2);
	dsp_out_buffer = dsp_in_buffer + (dsp_in_size / 2) + 1;

	s->dma_size = frames_to_bytes(runtime, runtime->buffer_size);
	s->period_size = frames_to_bytes(runtime, runtime->period_size);
	s->hwptr = 0;
	s->count = 0;

#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)

	/* host dma buffer pointers */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_ADDRL,
			  LO(s->buffer_addr));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_ADDRH,
			  HI(s->buffer_addr));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_END_PLUS_1L,
			  LO(s->buffer_addr + s->dma_size));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_END_PLUS_1H,
			  HI(s->buffer_addr + s->dma_size));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_CURRENTL,
			  LO(s->buffer_addr));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_HOST_SRC_CURRENTH,
			  HI(s->buffer_addr));
#undef LO
#undef HI

	/* dsp buffers */

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_IN_BUF_BEGIN,
			  dsp_in_buffer);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_IN_BUF_END_PLUS_1,
			  dsp_in_buffer + (dsp_in_size / 2));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_IN_BUF_HEAD,
			  dsp_in_buffer);
    
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_IN_BUF_TAIL,
			  dsp_in_buffer);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_OUT_BUF_BEGIN,
			  dsp_out_buffer);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_OUT_BUF_END_PLUS_1,
			  dsp_out_buffer + (dsp_out_size / 2));

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_OUT_BUF_HEAD,
			  dsp_out_buffer);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_OUT_BUF_TAIL,
			  dsp_out_buffer);
}

static void snd_m3_pcm_setup2(struct snd_m3 *chip, struct m3_dma *s,
			      struct snd_pcm_runtime *runtime)
{
	u32 freq;

	/* 
	 * put us in the lists if we're not already there
	 */
	if (! s->in_lists) {
		s->index[0] = snd_m3_add_list(chip, s->index_list[0],
					      s->inst.data >> DP_SHIFT_COUNT);
		s->index[1] = snd_m3_add_list(chip, s->index_list[1],
					      s->inst.data >> DP_SHIFT_COUNT);
		s->index[2] = snd_m3_add_list(chip, s->index_list[2],
					      s->inst.data >> DP_SHIFT_COUNT);
		s->in_lists = 1;
	}

	/* write to 'mono' word */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 1, 
			  runtime->channels == 2 ? 0 : 1);
	/* write to '8bit' word */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 2, 
			  snd_pcm_format_width(runtime->format) == 16 ? 0 : 1);

	/* set up dac/adc rate */
	freq = ((runtime->rate << 15) + 24000 ) / 48000;
	if (freq) 
		freq--;

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_FREQUENCY,
			  freq);
}


static const struct play_vals {
	u16 addr, val;
} pv[] = {
	{CDATA_LEFT_VOLUME, ARB_VOLUME},
	{CDATA_RIGHT_VOLUME, ARB_VOLUME},
	{SRC3_DIRECTION_OFFSET, 0} ,
	/* +1, +2 are stereo/16 bit */
	{SRC3_DIRECTION_OFFSET + 3, 0x0000}, /* fraction? */
	{SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
	{SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
	{SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
	{SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
	{SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
	{SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
	{SRC3_DIRECTION_OFFSET + 10, 0x8000}, /* round */
	{SRC3_DIRECTION_OFFSET + 11, 0xFF00}, /* higher bute mark */
	{SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
	{SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
	{SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
	{SRC3_DIRECTION_OFFSET + 16, 8}, /* numin */
	{SRC3_DIRECTION_OFFSET + 17, 50*2}, /* numout */
	{SRC3_DIRECTION_OFFSET + 18, MINISRC_BIQUAD_STAGE - 1}, /* numstage */
	{SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
	{SRC3_DIRECTION_OFFSET + 21, 0} /* booster */
};


/* the mode passed should be already shifted and masked */
static void
snd_m3_playback_setup(struct snd_m3 *chip, struct m3_dma *s,
		      struct snd_pcm_substream *subs)
{
	unsigned int i;

	/*
	 * some per client initializers
	 */

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 12,
			  s->inst.data + 40 + 8);

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 19,
			  s->inst.code + MINISRC_COEF_LOC);

	/* enable or disable low pass filter? */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 22,
			  subs->runtime->rate > 45000 ? 0xff : 0);
    
	/* tell it which way dma is going? */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_DMA_CONTROL,
			  DMACONTROL_AUTOREPEAT + DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

	/*
	 * set an armload of static initializers
	 */
	for (i = 0; i < ARRAY_SIZE(pv); i++) 
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  s->inst.data + pv[i].addr, pv[i].val);
}

/*
 *    Native record driver 
 */
static const struct rec_vals {
	u16 addr, val;
} rv[] = {
	{CDATA_LEFT_VOLUME, ARB_VOLUME},
	{CDATA_RIGHT_VOLUME, ARB_VOLUME},
	{SRC3_DIRECTION_OFFSET, 1} ,
	/* +1, +2 are stereo/16 bit */
	{SRC3_DIRECTION_OFFSET + 3, 0x0000}, /* fraction? */
	{SRC3_DIRECTION_OFFSET + 4, 0}, /* first l */
	{SRC3_DIRECTION_OFFSET + 5, 0}, /* first r */
	{SRC3_DIRECTION_OFFSET + 6, 0}, /* second l */
	{SRC3_DIRECTION_OFFSET + 7, 0}, /* second r */
	{SRC3_DIRECTION_OFFSET + 8, 0}, /* delta l */
	{SRC3_DIRECTION_OFFSET + 9, 0}, /* delta r */
	{SRC3_DIRECTION_OFFSET + 10, 0x8000}, /* round */
	{SRC3_DIRECTION_OFFSET + 11, 0xFF00}, /* higher bute mark */
	{SRC3_DIRECTION_OFFSET + 13, 0}, /* temp0 */
	{SRC3_DIRECTION_OFFSET + 14, 0}, /* c fraction */
	{SRC3_DIRECTION_OFFSET + 15, 0}, /* counter */
	{SRC3_DIRECTION_OFFSET + 16, 50},/* numin */
	{SRC3_DIRECTION_OFFSET + 17, 8}, /* numout */
	{SRC3_DIRECTION_OFFSET + 18, 0}, /* numstage */
	{SRC3_DIRECTION_OFFSET + 19, 0}, /* coef */
	{SRC3_DIRECTION_OFFSET + 20, 0}, /* filtertap */
	{SRC3_DIRECTION_OFFSET + 21, 0}, /* booster */
	{SRC3_DIRECTION_OFFSET + 22, 0xff} /* skip lpf */
};

static void
snd_m3_capture_setup(struct snd_m3 *chip, struct m3_dma *s, struct snd_pcm_substream *subs)
{
	unsigned int i;

	/*
	 * some per client initializers
	 */

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + SRC3_DIRECTION_OFFSET + 12,
			  s->inst.data + 40 + 8);

	/* tell it which way dma is going? */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  s->inst.data + CDATA_DMA_CONTROL,
			  DMACONTROL_DIRECTION + DMACONTROL_AUTOREPEAT + 
			  DMAC_PAGE3_SELECTOR + DMAC_BLOCKF_SELECTOR);

	/*
	 * set an armload of static initializers
	 */
	for (i = 0; i < ARRAY_SIZE(rv); i++) 
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  s->inst.data + rv[i].addr, rv[i].val);
}

static int snd_m3_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *hw_params)
{
	struct m3_dma *s = substream->runtime->private_data;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	/* set buffer address */
	s->buffer_addr = substream->runtime->dma_addr;
	if (s->buffer_addr & 0x3) {
		snd_printk(KERN_ERR "oh my, not aligned\n");
		s->buffer_addr = s->buffer_addr & ~0x3;
	}
	return 0;
}

static int snd_m3_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct m3_dma *s;
	
	if (substream->runtime->private_data == NULL)
		return 0;
	s = substream->runtime->private_data;
	snd_pcm_lib_free_pages(substream);
	s->buffer_addr = 0;
	return 0;
}

static int
snd_m3_pcm_prepare(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	struct m3_dma *s = runtime->private_data;

	snd_assert(s != NULL, return -ENXIO);

	if (runtime->format != SNDRV_PCM_FORMAT_U8 &&
	    runtime->format != SNDRV_PCM_FORMAT_S16_LE)
		return -EINVAL;
	if (runtime->rate > 48000 ||
	    runtime->rate < 8000)
		return -EINVAL;

	spin_lock_irq(&chip->reg_lock);

	snd_m3_pcm_setup1(chip, s, subs);

	if (subs->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_m3_playback_setup(chip, s, subs);
	else
		snd_m3_capture_setup(chip, s, subs);

	snd_m3_pcm_setup2(chip, s, runtime);

	spin_unlock_irq(&chip->reg_lock);

	return 0;
}

/*
 * get current pointer
 */
static unsigned int
snd_m3_get_pointer(struct snd_m3 *chip, struct m3_dma *s, struct snd_pcm_substream *subs)
{
	u16 hi = 0, lo = 0;
	int retry = 10;
	u32 addr;

	/*
	 * try and get a valid answer
	 */
	while (retry--) {
		hi =  snd_m3_assp_read(chip, MEMTYPE_INTERNAL_DATA,
				       s->inst.data + CDATA_HOST_SRC_CURRENTH);

		lo = snd_m3_assp_read(chip, MEMTYPE_INTERNAL_DATA,
				      s->inst.data + CDATA_HOST_SRC_CURRENTL);

		if (hi == snd_m3_assp_read(chip, MEMTYPE_INTERNAL_DATA,
					   s->inst.data + CDATA_HOST_SRC_CURRENTH))
			break;
	}
	addr = lo | ((u32)hi<<16);
	return (unsigned int)(addr - s->buffer_addr);
}

static snd_pcm_uframes_t
snd_m3_pcm_pointer(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);
	unsigned int ptr;
	struct m3_dma *s = subs->runtime->private_data;
	snd_assert(s != NULL, return 0);

	spin_lock(&chip->reg_lock);
	ptr = snd_m3_get_pointer(chip, s, subs);
	spin_unlock(&chip->reg_lock);
	return bytes_to_frames(subs->runtime, ptr);
}


/* update pointer */
/* spinlock held! */
static void snd_m3_update_ptr(struct snd_m3 *chip, struct m3_dma *s)
{
	struct snd_pcm_substream *subs = s->substream;
	unsigned int hwptr;
	int diff;

	if (! s->running)
		return;

	hwptr = snd_m3_get_pointer(chip, s, subs);

	/* try to avoid expensive modulo divisions */
	if (hwptr >= s->dma_size)
		hwptr %= s->dma_size;

	diff = s->dma_size + hwptr - s->hwptr;
	if (diff >= s->dma_size)
		diff %= s->dma_size;

	s->hwptr = hwptr;
	s->count += diff;

	if (s->count >= (signed)s->period_size) {

		if (s->count < 2 * (signed)s->period_size)
			s->count -= (signed)s->period_size;
		else
			s->count %= s->period_size;

		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(subs);
		spin_lock(&chip->reg_lock);
	}
}

static void snd_m3_update_hw_volume(unsigned long private_data)
{
	struct snd_m3 *chip = (struct snd_m3 *) private_data;
	int x, val;
	unsigned long flags;

	/* Figure out which volume control button was pushed,
	   based on differences from the default register
	   values. */
	x = inb(chip->iobase + SHADOW_MIX_REG_VOICE) & 0xee;

	/* Reset the volume control registers. */
	outb(0x88, chip->iobase + SHADOW_MIX_REG_VOICE);
	outb(0x88, chip->iobase + HW_VOL_COUNTER_VOICE);
	outb(0x88, chip->iobase + SHADOW_MIX_REG_MASTER);
	outb(0x88, chip->iobase + HW_VOL_COUNTER_MASTER);

	if (!chip->master_switch || !chip->master_volume)
		return;

	/* FIXME: we can't call snd_ac97_* functions since here is in tasklet. */
	spin_lock_irqsave(&chip->ac97_lock, flags);

	val = chip->ac97->regs[AC97_MASTER_VOL];
	switch (x) {
	case 0x88:
		/* mute */
		val ^= 0x8000;
		chip->ac97->regs[AC97_MASTER_VOL] = val;
		outw(val, chip->iobase + CODEC_DATA);
		outb(AC97_MASTER_VOL, chip->iobase + CODEC_COMMAND);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->master_switch->id);
		break;
	case 0xaa:
		/* volume up */
		if ((val & 0x7f) > 0)
			val--;
		if ((val & 0x7f00) > 0)
			val -= 0x0100;
		chip->ac97->regs[AC97_MASTER_VOL] = val;
		outw(val, chip->iobase + CODEC_DATA);
		outb(AC97_MASTER_VOL, chip->iobase + CODEC_COMMAND);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->master_volume->id);
		break;
	case 0x66:
		/* volume down */
		if ((val & 0x7f) < 0x1f)
			val++;
		if ((val & 0x7f00) < 0x1f00)
			val += 0x0100;
		chip->ac97->regs[AC97_MASTER_VOL] = val;
		outw(val, chip->iobase + CODEC_DATA);
		outb(AC97_MASTER_VOL, chip->iobase + CODEC_COMMAND);
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &chip->master_volume->id);
		break;
	}
	spin_unlock_irqrestore(&chip->ac97_lock, flags);
}

static irqreturn_t snd_m3_interrupt(int irq, void *dev_id)
{
	struct snd_m3 *chip = dev_id;
	u8 status;
	int i;

	status = inb(chip->iobase + HOST_INT_STATUS);

	if (status == 0xff)
		return IRQ_NONE;

	if (status & HV_INT_PENDING)
		tasklet_hi_schedule(&chip->hwvol_tq);

	/*
	 * ack an assp int if its running
	 * and has an int pending
	 */
	if (status & ASSP_INT_PENDING) {
		u8 ctl = inb(chip->iobase + ASSP_CONTROL_B);
		if (!(ctl & STOP_ASSP_CLOCK)) {
			ctl = inb(chip->iobase + ASSP_HOST_INT_STATUS);
			if (ctl & DSP2HOST_REQ_TIMER) {
				outb(DSP2HOST_REQ_TIMER, chip->iobase + ASSP_HOST_INT_STATUS);
				/* update adc/dac info if it was a timer int */
				spin_lock(&chip->reg_lock);
				for (i = 0; i < chip->num_substreams; i++) {
					struct m3_dma *s = &chip->substreams[i];
					if (s->running)
						snd_m3_update_ptr(chip, s);
				}
				spin_unlock(&chip->reg_lock);
			}
		}
	}

#if 0 /* TODO: not supported yet */
	if ((status & MPU401_INT_PENDING) && chip->rmidi)
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data, regs);
#endif

	/* ack ints */
	outb(status, chip->iobase + HOST_INT_STATUS);

	return IRQ_HANDLED;
}


/*
 */

static struct snd_pcm_hardware snd_m3_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 /*SNDRV_PCM_INFO_PAUSE |*/
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(512*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(512*1024),
	.periods_min =		1,
	.periods_max =		1024,
};

static struct snd_pcm_hardware snd_m3_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 /*SNDRV_PCM_INFO_PAUSE |*/
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(512*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(512*1024),
	.periods_min =		1,
	.periods_max =		1024,
};


/*
 */

static int
snd_m3_substream_open(struct snd_m3 *chip, struct snd_pcm_substream *subs)
{
	int i;
	struct m3_dma *s;

	spin_lock_irq(&chip->reg_lock);
	for (i = 0; i < chip->num_substreams; i++) {
		s = &chip->substreams[i];
		if (! s->opened)
			goto __found;
	}
	spin_unlock_irq(&chip->reg_lock);
	return -ENOMEM;
__found:
	s->opened = 1;
	s->running = 0;
	spin_unlock_irq(&chip->reg_lock);

	subs->runtime->private_data = s;
	s->substream = subs;

	/* set list owners */
	if (subs->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		s->index_list[0] = &chip->mixer_list;
	} else
		s->index_list[0] = &chip->adc1_list;
	s->index_list[1] = &chip->msrc_list;
	s->index_list[2] = &chip->dma_list;

	return 0;
}

static void
snd_m3_substream_close(struct snd_m3 *chip, struct snd_pcm_substream *subs)
{
	struct m3_dma *s = subs->runtime->private_data;

	if (s == NULL)
		return; /* not opened properly */

	spin_lock_irq(&chip->reg_lock);
	if (s->substream && s->running)
		snd_m3_pcm_stop(chip, s, s->substream); /* does this happen? */
	if (s->in_lists) {
		snd_m3_remove_list(chip, s->index_list[0], s->index[0]);
		snd_m3_remove_list(chip, s->index_list[1], s->index[1]);
		snd_m3_remove_list(chip, s->index_list[2], s->index[2]);
		s->in_lists = 0;
	}
	s->running = 0;
	s->opened = 0;
	spin_unlock_irq(&chip->reg_lock);
}

static int
snd_m3_playback_open(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	int err;

	if ((err = snd_m3_substream_open(chip, subs)) < 0)
		return err;

	runtime->hw = snd_m3_playback;

	return 0;
}

static int
snd_m3_playback_close(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);

	snd_m3_substream_close(chip, subs);
	return 0;
}

static int
snd_m3_capture_open(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);
	struct snd_pcm_runtime *runtime = subs->runtime;
	int err;

	if ((err = snd_m3_substream_open(chip, subs)) < 0)
		return err;

	runtime->hw = snd_m3_capture;

	return 0;
}

static int
snd_m3_capture_close(struct snd_pcm_substream *subs)
{
	struct snd_m3 *chip = snd_pcm_substream_chip(subs);

	snd_m3_substream_close(chip, subs);
	return 0;
}

/*
 * create pcm instance
 */

static struct snd_pcm_ops snd_m3_playback_ops = {
	.open =		snd_m3_playback_open,
	.close =	snd_m3_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_m3_pcm_hw_params,
	.hw_free =	snd_m3_pcm_hw_free,
	.prepare =	snd_m3_pcm_prepare,
	.trigger =	snd_m3_pcm_trigger,
	.pointer =	snd_m3_pcm_pointer,
};

static struct snd_pcm_ops snd_m3_capture_ops = {
	.open =		snd_m3_capture_open,
	.close =	snd_m3_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_m3_pcm_hw_params,
	.hw_free =	snd_m3_pcm_hw_free,
	.prepare =	snd_m3_pcm_prepare,
	.trigger =	snd_m3_pcm_trigger,
	.pointer =	snd_m3_pcm_pointer,
};

static int __devinit
snd_m3_pcm(struct snd_m3 * chip, int device)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(chip->card, chip->card->driver, device,
			  MAX_PLAYBACKS, MAX_CAPTURES, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_m3_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_m3_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strcpy(pcm->name, chip->card->driver);
	chip->pcm = pcm;
	
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 64*1024);

	return 0;
}


/*
 * ac97 interface
 */

/*
 * Wait for the ac97 serial bus to be free.
 * return nonzero if the bus is still busy.
 */
static int snd_m3_ac97_wait(struct snd_m3 *chip)
{
	int i = 10000;

	do {
		if (! (snd_m3_inb(chip, 0x30) & 1))
			return 0;
		cpu_relax();
	} while (i-- > 0);

	snd_printk(KERN_ERR "ac97 serial bus busy\n");
	return 1;
}

static unsigned short
snd_m3_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct snd_m3 *chip = ac97->private_data;
	unsigned long flags;
	unsigned short data = 0xffff;

	if (snd_m3_ac97_wait(chip))
		goto fail;
	spin_lock_irqsave(&chip->ac97_lock, flags);
	snd_m3_outb(chip, 0x80 | (reg & 0x7f), CODEC_COMMAND);
	if (snd_m3_ac97_wait(chip))
		goto fail_unlock;
	data = snd_m3_inw(chip, CODEC_DATA);
fail_unlock:
	spin_unlock_irqrestore(&chip->ac97_lock, flags);
fail:
	return data;
}

static void
snd_m3_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val)
{
	struct snd_m3 *chip = ac97->private_data;
	unsigned long flags;

	if (snd_m3_ac97_wait(chip))
		return;
	spin_lock_irqsave(&chip->ac97_lock, flags);
	snd_m3_outw(chip, val, CODEC_DATA);
	snd_m3_outb(chip, reg & 0x7f, CODEC_COMMAND);
	spin_unlock_irqrestore(&chip->ac97_lock, flags);
}


static void snd_m3_remote_codec_config(int io, int isremote)
{
	isremote = isremote ? 1 : 0;

	outw((inw(io + RING_BUS_CTRL_B) & ~SECOND_CODEC_ID_MASK) | isremote,
	     io + RING_BUS_CTRL_B);
	outw((inw(io + SDO_OUT_DEST_CTRL) & ~COMMAND_ADDR_OUT) | isremote,
	     io + SDO_OUT_DEST_CTRL);
	outw((inw(io + SDO_IN_DEST_CTRL) & ~STATUS_ADDR_IN) | isremote,
	     io + SDO_IN_DEST_CTRL);
}

/* 
 * hack, returns non zero on err 
 */
static int snd_m3_try_read_vendor(struct snd_m3 *chip)
{
	u16 ret;

	if (snd_m3_ac97_wait(chip))
		return 1;

	snd_m3_outb(chip, 0x80 | (AC97_VENDOR_ID1 & 0x7f), 0x30);

	if (snd_m3_ac97_wait(chip))
		return 1;

	ret = snd_m3_inw(chip, 0x32);

	return (ret == 0) || (ret == 0xffff);
}

static void snd_m3_ac97_reset(struct snd_m3 *chip)
{
	u16 dir;
	int delay1 = 0, delay2 = 0, i;
	int io = chip->iobase;

	if (chip->allegro_flag) {
		/*
		 * the onboard codec on the allegro seems 
		 * to want to wait a very long time before
		 * coming back to life 
		 */
		delay1 = 50;
		delay2 = 800;
	} else {
		/* maestro3 */
		delay1 = 20;
		delay2 = 500;
	}

	for (i = 0; i < 5; i++) {
		dir = inw(io + GPIO_DIRECTION);
		if (!chip->irda_workaround)
			dir |= 0x10; /* assuming pci bus master? */

		snd_m3_remote_codec_config(io, 0);

		outw(IO_SRAM_ENABLE, io + RING_BUS_CTRL_A);
		udelay(20);

		outw(dir & ~GPO_PRIMARY_AC97 , io + GPIO_DIRECTION);
		outw(~GPO_PRIMARY_AC97 , io + GPIO_MASK);
		outw(0, io + GPIO_DATA);
		outw(dir | GPO_PRIMARY_AC97, io + GPIO_DIRECTION);

		schedule_timeout_uninterruptible(msecs_to_jiffies(delay1));

		outw(GPO_PRIMARY_AC97, io + GPIO_DATA);
		udelay(5);
		/* ok, bring back the ac-link */
		outw(IO_SRAM_ENABLE | SERIAL_AC_LINK_ENABLE, io + RING_BUS_CTRL_A);
		outw(~0, io + GPIO_MASK);

		schedule_timeout_uninterruptible(msecs_to_jiffies(delay2));

		if (! snd_m3_try_read_vendor(chip))
			break;

		delay1 += 10;
		delay2 += 100;

		snd_printd("maestro3: retrying codec reset with delays of %d and %d ms\n",
			   delay1, delay2);
	}

#if 0
	/* more gung-ho reset that doesn't
	 * seem to work anywhere :)
	 */
	tmp = inw(io + RING_BUS_CTRL_A);
	outw(RAC_SDFS_ENABLE|LAC_SDFS_ENABLE, io + RING_BUS_CTRL_A);
	msleep(20);
	outw(tmp, io + RING_BUS_CTRL_A);
	msleep(50);
#endif
}

static int __devinit snd_m3_mixer(struct snd_m3 *chip)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	struct snd_ctl_elem_id id;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_m3_ac97_write,
		.read = snd_m3_ac97_read,
	};

	if ((err = snd_ac97_bus(chip->card, 0, &ops, NULL, &pbus)) < 0)
		return err;
	
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	if ((err = snd_ac97_mixer(pbus, &ac97, &chip->ac97)) < 0)
		return err;

	/* seems ac97 PCM needs initialization.. hack hack.. */
	snd_ac97_write(chip->ac97, AC97_PCM, 0x8000 | (15 << 8) | 15);
	schedule_timeout_uninterruptible(msecs_to_jiffies(100));
	snd_ac97_write(chip->ac97, AC97_PCM, 0);

	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, "Master Playback Switch");
	chip->master_switch = snd_ctl_find_id(chip->card, &id);
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, "Master Playback Volume");
	chip->master_volume = snd_ctl_find_id(chip->card, &id);

	return 0;
}


#ifdef CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL

/*
 * DSP Code images
 */

static const u16 assp_kernel_image[] = {
    0x7980, 0x0030, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x00FB, 0x7980, 0x00DD, 0x7980, 0x03B4, 
    0x7980, 0x0332, 0x7980, 0x0287, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0x7980, 0x031A, 0x7980, 0x03B4, 0x7980, 0x022F, 0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0x7980, 0x03B4, 0x7980, 0x03B4, 0x7980, 0x0063, 0x7980, 0x006B, 0x7980, 0x03B4, 0x7980, 0x03B4, 
    0xBF80, 0x2C7C, 0x8806, 0x8804, 0xBE40, 0xBC20, 0xAE09, 0x1000, 0xAE0A, 0x0001, 0x6938, 0xEB08, 
    0x0053, 0x695A, 0xEB08, 0x00D6, 0x0009, 0x8B88, 0x6980, 0xE388, 0x0036, 0xBE30, 0xBC20, 0x6909, 
    0xB801, 0x9009, 0xBE41, 0xBE41, 0x6928, 0xEB88, 0x0078, 0xBE41, 0xBE40, 0x7980, 0x0038, 0xBE41, 
    0xBE41, 0x903A, 0x6938, 0xE308, 0x0056, 0x903A, 0xBE41, 0xBE40, 0xEF00, 0x903A, 0x6939, 0xE308, 
    0x005E, 0x903A, 0xEF00, 0x690B, 0x660C, 0xEF8C, 0x690A, 0x660C, 0x620B, 0x6609, 0xEF00, 0x6910, 
    0x660F, 0xEF04, 0xE388, 0x0075, 0x690E, 0x660F, 0x6210, 0x660D, 0xEF00, 0x690E, 0x660D, 0xEF00, 
    0xAE70, 0x0001, 0xBC20, 0xAE27, 0x0001, 0x6939, 0xEB08, 0x005D, 0x6926, 0xB801, 0x9026, 0x0026, 
    0x8B88, 0x6980, 0xE388, 0x00CB, 0x9028, 0x0D28, 0x4211, 0xE100, 0x007A, 0x4711, 0xE100, 0x00A0, 
    0x7A80, 0x0063, 0xB811, 0x660A, 0x6209, 0xE304, 0x007A, 0x0C0B, 0x4005, 0x100A, 0xBA01, 0x9012, 
    0x0C12, 0x4002, 0x7980, 0x00AF, 0x7A80, 0x006B, 0xBE02, 0x620E, 0x660D, 0xBA10, 0xE344, 0x007A, 
    0x0C10, 0x4005, 0x100E, 0xBA01, 0x9012, 0x0C12, 0x4002, 0x1003, 0xBA02, 0x9012, 0x0C12, 0x4000, 
    0x1003, 0xE388, 0x00BA, 0x1004, 0x7980, 0x00BC, 0x1004, 0xBA01, 0x9012, 0x0C12, 0x4001, 0x0C05, 
    0x4003, 0x0C06, 0x4004, 0x1011, 0xBFB0, 0x01FF, 0x9012, 0x0C12, 0x4006, 0xBC20, 0xEF00, 0xAE26, 
    0x1028, 0x6970, 0xBFD0, 0x0001, 0x9070, 0xE388, 0x007A, 0xAE28, 0x0000, 0xEF00, 0xAE70, 0x0300, 
    0x0C70, 0xB00C, 0xAE5A, 0x0000, 0xEF00, 0x7A80, 0x038A, 0x697F, 0xB801, 0x907F, 0x0056, 0x8B88, 
    0x0CA0, 0xB008, 0xAF71, 0xB000, 0x4E71, 0xE200, 0x00F3, 0xAE56, 0x1057, 0x0056, 0x0CA0, 0xB008, 
    0x8056, 0x7980, 0x03A1, 0x0810, 0xBFA0, 0x1059, 0xE304, 0x03A1, 0x8056, 0x7980, 0x03A1, 0x7A80, 
    0x038A, 0xBF01, 0xBE43, 0xBE59, 0x907C, 0x6937, 0xE388, 0x010D, 0xBA01, 0xE308, 0x010C, 0xAE71, 
    0x0004, 0x0C71, 0x5000, 0x6936, 0x9037, 0xBF0A, 0x109E, 0x8B8A, 0xAF80, 0x8014, 0x4C80, 0xBF0A, 
    0x0560, 0xF500, 0xBF0A, 0x0520, 0xB900, 0xBB17, 0x90A0, 0x6917, 0xE388, 0x0148, 0x0D17, 0xE100, 
    0x0127, 0xBF0C, 0x0578, 0xBF0D, 0x057C, 0x7980, 0x012B, 0xBF0C, 0x0538, 0xBF0D, 0x053C, 0x6900, 
    0xE308, 0x0135, 0x8B8C, 0xBE59, 0xBB07, 0x90A0, 0xBC20, 0x7980, 0x0157, 0x030C, 0x8B8B, 0xB903, 
    0x8809, 0xBEC6, 0x013E, 0x69AC, 0x90AB, 0x69AD, 0x90AB, 0x0813, 0x660A, 0xE344, 0x0144, 0x0309, 
    0x830C, 0xBC20, 0x7980, 0x0157, 0x6955, 0xE388, 0x0157, 0x7C38, 0xBF0B, 0x0578, 0xF500, 0xBF0B, 
    0x0538, 0xB907, 0x8809, 0xBEC6, 0x0156, 0x10AB, 0x90AA, 0x6974, 0xE388, 0x0163, 0xAE72, 0x0540, 
    0xF500, 0xAE72, 0x0500, 0xAE61, 0x103B, 0x7A80, 0x02F6, 0x6978, 0xE388, 0x0182, 0x8B8C, 0xBF0C, 
    0x0560, 0xE500, 0x7C40, 0x0814, 0xBA20, 0x8812, 0x733D, 0x7A80, 0x0380, 0x733E, 0x7A80, 0x0380, 
    0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 0x0814, 0xBA2C, 0x8812, 0x733F, 0x7A80, 0x0380, 0x7340, 
    0x7A80, 0x0380, 0x6975, 0xE388, 0x018E, 0xAE72, 0x0548, 0xF500, 0xAE72, 0x0508, 0xAE61, 0x1041, 
    0x7A80, 0x02F6, 0x6979, 0xE388, 0x01AD, 0x8B8C, 0xBF0C, 0x0560, 0xE500, 0x7C40, 0x0814, 0xBA18, 
    0x8812, 0x7343, 0x7A80, 0x0380, 0x7344, 0x7A80, 0x0380, 0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 
    0x0814, 0xBA24, 0x8812, 0x7345, 0x7A80, 0x0380, 0x7346, 0x7A80, 0x0380, 0x6976, 0xE388, 0x01B9, 
    0xAE72, 0x0558, 0xF500, 0xAE72, 0x0518, 0xAE61, 0x1047, 0x7A80, 0x02F6, 0x697A, 0xE388, 0x01D8, 
    0x8B8C, 0xBF0C, 0x0560, 0xE500, 0x7C40, 0x0814, 0xBA08, 0x8812, 0x7349, 0x7A80, 0x0380, 0x734A, 
    0x7A80, 0x0380, 0x8B8C, 0xBF0C, 0x056C, 0xE500, 0x7C40, 0x0814, 0xBA14, 0x8812, 0x734B, 0x7A80, 
    0x0380, 0x734C, 0x7A80, 0x0380, 0xBC21, 0xAE1C, 0x1090, 0x8B8A, 0xBF0A, 0x0560, 0xE500, 0x7C40, 
    0x0812, 0xB804, 0x8813, 0x8B8D, 0xBF0D, 0x056C, 0xE500, 0x7C40, 0x0815, 0xB804, 0x8811, 0x7A80, 
    0x034A, 0x8B8A, 0xBF0A, 0x0560, 0xE500, 0x7C40, 0x731F, 0xB903, 0x8809, 0xBEC6, 0x01F9, 0x548A, 
    0xBE03, 0x98A0, 0x7320, 0xB903, 0x8809, 0xBEC6, 0x0201, 0x548A, 0xBE03, 0x98A0, 0x1F20, 0x2F1F, 
    0x9826, 0xBC20, 0x6935, 0xE388, 0x03A1, 0x6933, 0xB801, 0x9033, 0xBFA0, 0x02EE, 0xE308, 0x03A1, 
    0x9033, 0xBF00, 0x6951, 0xE388, 0x021F, 0x7334, 0xBE80, 0x5760, 0xBE03, 0x9F7E, 0xBE59, 0x9034, 
    0x697E, 0x0D51, 0x9013, 0xBC20, 0x695C, 0xE388, 0x03A1, 0x735E, 0xBE80, 0x5760, 0xBE03, 0x9F7E, 
    0xBE59, 0x905E, 0x697E, 0x0D5C, 0x9013, 0x7980, 0x03A1, 0x7A80, 0x038A, 0xBF01, 0xBE43, 0x6977, 
    0xE388, 0x024E, 0xAE61, 0x104D, 0x0061, 0x8B88, 0x6980, 0xE388, 0x024E, 0x9071, 0x0D71, 0x000B, 
    0xAFA0, 0x8010, 0xAFA0, 0x8010, 0x0810, 0x660A, 0xE308, 0x0249, 0x0009, 0x0810, 0x660C, 0xE388, 
    0x024E, 0x800B, 0xBC20, 0x697B, 0xE388, 0x03A1, 0xBF0A, 0x109E, 0x8B8A, 0xAF80, 0x8014, 0x4C80, 
    0xE100, 0x0266, 0x697C, 0xBF90, 0x0560, 0x9072, 0x0372, 0x697C, 0xBF90, 0x0564, 0x9073, 0x0473, 
    0x7980, 0x0270, 0x697C, 0xBF90, 0x0520, 0x9072, 0x0372, 0x697C, 0xBF90, 0x0524, 0x9073, 0x0473, 
    0x697C, 0xB801, 0x907C, 0xBF0A, 0x10FD, 0x8B8A, 0xAF80, 0x8010, 0x734F, 0x548A, 0xBE03, 0x9880, 
    0xBC21, 0x7326, 0x548B, 0xBE03, 0x618B, 0x988C, 0xBE03, 0x6180, 0x9880, 0x7980, 0x03A1, 0x7A80, 
    0x038A, 0x0D28, 0x4711, 0xE100, 0x02BE, 0xAF12, 0x4006, 0x6912, 0xBFB0, 0x0C00, 0xE388, 0x02B6, 
    0xBFA0, 0x0800, 0xE388, 0x02B2, 0x6912, 0xBFB0, 0x0C00, 0xBFA0, 0x0400, 0xE388, 0x02A3, 0x6909, 
    0x900B, 0x7980, 0x02A5, 0xAF0B, 0x4005, 0x6901, 0x9005, 0x6902, 0x9006, 0x4311, 0xE100, 0x02ED, 
    0x6911, 0xBFC0, 0x2000, 0x9011, 0x7980, 0x02ED, 0x6909, 0x900B, 0x7980, 0x02B8, 0xAF0B, 0x4005, 
    0xAF05, 0x4003, 0xAF06, 0x4004, 0x7980, 0x02ED, 0xAF12, 0x4006, 0x6912, 0xBFB0, 0x0C00, 0xE388, 
    0x02E7, 0xBFA0, 0x0800, 0xE388, 0x02E3, 0x6912, 0xBFB0, 0x0C00, 0xBFA0, 0x0400, 0xE388, 0x02D4, 
    0x690D, 0x9010, 0x7980, 0x02D6, 0xAF10, 0x4005, 0x6901, 0x9005, 0x6902, 0x9006, 0x4311, 0xE100, 
    0x02ED, 0x6911, 0xBFC0, 0x2000, 0x9011, 0x7980, 0x02ED, 0x690D, 0x9010, 0x7980, 0x02E9, 0xAF10, 
    0x4005, 0xAF05, 0x4003, 0xAF06, 0x4004, 0xBC20, 0x6970, 0x9071, 0x7A80, 0x0078, 0x6971, 0x9070, 
    0x7980, 0x03A1, 0xBC20, 0x0361, 0x8B8B, 0x6980, 0xEF88, 0x0272, 0x0372, 0x7804, 0x9071, 0x0D71, 
    0x8B8A, 0x000B, 0xB903, 0x8809, 0xBEC6, 0x0309, 0x69A8, 0x90AB, 0x69A8, 0x90AA, 0x0810, 0x660A, 
    0xE344, 0x030F, 0x0009, 0x0810, 0x660C, 0xE388, 0x0314, 0x800B, 0xBC20, 0x6961, 0xB801, 0x9061, 
    0x7980, 0x02F7, 0x7A80, 0x038A, 0x5D35, 0x0001, 0x6934, 0xB801, 0x9034, 0xBF0A, 0x109E, 0x8B8A, 
    0xAF80, 0x8014, 0x4880, 0xAE72, 0x0550, 0xF500, 0xAE72, 0x0510, 0xAE61, 0x1051, 0x7A80, 0x02F6, 
    0x7980, 0x03A1, 0x7A80, 0x038A, 0x5D35, 0x0002, 0x695E, 0xB801, 0x905E, 0xBF0A, 0x109E, 0x8B8A, 
    0xAF80, 0x8014, 0x4780, 0xAE72, 0x0558, 0xF500, 0xAE72, 0x0518, 0xAE61, 0x105C, 0x7A80, 0x02F6, 
    0x7980, 0x03A1, 0x001C, 0x8B88, 0x6980, 0xEF88, 0x901D, 0x0D1D, 0x100F, 0x6610, 0xE38C, 0x0358, 
    0x690E, 0x6610, 0x620F, 0x660D, 0xBA0F, 0xE301, 0x037A, 0x0410, 0x8B8A, 0xB903, 0x8809, 0xBEC6, 
    0x036C, 0x6A8C, 0x61AA, 0x98AB, 0x6A8C, 0x61AB, 0x98AD, 0x6A8C, 0x61AD, 0x98A9, 0x6A8C, 0x61A9, 
    0x98AA, 0x7C04, 0x8B8B, 0x7C04, 0x8B8D, 0x7C04, 0x8B89, 0x7C04, 0x0814, 0x660E, 0xE308, 0x0379, 
    0x040D, 0x8410, 0xBC21, 0x691C, 0xB801, 0x901C, 0x7980, 0x034A, 0xB903, 0x8809, 0x8B8A, 0xBEC6, 
    0x0388, 0x54AC, 0xBE03, 0x618C, 0x98AA, 0xEF00, 0xBC20, 0xBE46, 0x0809, 0x906B, 0x080A, 0x906C, 
    0x080B, 0x906D, 0x081A, 0x9062, 0x081B, 0x9063, 0x081E, 0x9064, 0xBE59, 0x881E, 0x8065, 0x8166, 
    0x8267, 0x8368, 0x8469, 0x856A, 0xEF00, 0xBC20, 0x696B, 0x8809, 0x696C, 0x880A, 0x696D, 0x880B, 
    0x6962, 0x881A, 0x6963, 0x881B, 0x6964, 0x881E, 0x0065, 0x0166, 0x0267, 0x0368, 0x0469, 0x056A, 
    0xBE3A, 
};

/*
 * Mini sample rate converter code image
 * that is to be loaded at 0x400 on the DSP.
 */
static const u16 assp_minisrc_image[] = {

    0xBF80, 0x101E, 0x906E, 0x006E, 0x8B88, 0x6980, 0xEF88, 0x906F, 0x0D6F, 0x6900, 0xEB08, 0x0412, 
    0xBC20, 0x696E, 0xB801, 0x906E, 0x7980, 0x0403, 0xB90E, 0x8807, 0xBE43, 0xBF01, 0xBE47, 0xBE41, 
    0x7A80, 0x002A, 0xBE40, 0x3029, 0xEFCC, 0xBE41, 0x7A80, 0x0028, 0xBE40, 0x3028, 0xEFCC, 0x6907, 
    0xE308, 0x042A, 0x6909, 0x902C, 0x7980, 0x042C, 0x690D, 0x902C, 0x1009, 0x881A, 0x100A, 0xBA01, 
    0x881B, 0x100D, 0x881C, 0x100E, 0xBA01, 0x881D, 0xBF80, 0x00ED, 0x881E, 0x050C, 0x0124, 0xB904, 
    0x9027, 0x6918, 0xE308, 0x04B3, 0x902D, 0x6913, 0xBFA0, 0x7598, 0xF704, 0xAE2D, 0x00FF, 0x8B8D, 
    0x6919, 0xE308, 0x0463, 0x691A, 0xE308, 0x0456, 0xB907, 0x8809, 0xBEC6, 0x0453, 0x10A9, 0x90AD, 
    0x7980, 0x047C, 0xB903, 0x8809, 0xBEC6, 0x0460, 0x1889, 0x6C22, 0x90AD, 0x10A9, 0x6E23, 0x6C22, 
    0x90AD, 0x7980, 0x047C, 0x101A, 0xE308, 0x046F, 0xB903, 0x8809, 0xBEC6, 0x046C, 0x10A9, 0x90A0, 
    0x90AD, 0x7980, 0x047C, 0xB901, 0x8809, 0xBEC6, 0x047B, 0x1889, 0x6C22, 0x90A0, 0x90AD, 0x10A9, 
    0x6E23, 0x6C22, 0x90A0, 0x90AD, 0x692D, 0xE308, 0x049C, 0x0124, 0xB703, 0xB902, 0x8818, 0x8B89, 
    0x022C, 0x108A, 0x7C04, 0x90A0, 0x692B, 0x881F, 0x7E80, 0x055B, 0x692A, 0x8809, 0x8B89, 0x99A0, 
    0x108A, 0x90A0, 0x692B, 0x881F, 0x7E80, 0x055B, 0x692A, 0x8809, 0x8B89, 0x99AF, 0x7B99, 0x0484, 
    0x0124, 0x060F, 0x101B, 0x2013, 0x901B, 0xBFA0, 0x7FFF, 0xE344, 0x04AC, 0x901B, 0x8B89, 0x7A80, 
    0x051A, 0x6927, 0xBA01, 0x9027, 0x7A80, 0x0523, 0x6927, 0xE308, 0x049E, 0x7980, 0x050F, 0x0624, 
    0x1026, 0x2013, 0x9026, 0xBFA0, 0x7FFF, 0xE304, 0x04C0, 0x8B8D, 0x7A80, 0x051A, 0x7980, 0x04B4, 
    0x9026, 0x1013, 0x3026, 0x901B, 0x8B8D, 0x7A80, 0x051A, 0x7A80, 0x0523, 0x1027, 0xBA01, 0x9027, 
    0xE308, 0x04B4, 0x0124, 0x060F, 0x8B89, 0x691A, 0xE308, 0x04EA, 0x6919, 0xE388, 0x04E0, 0xB903, 
    0x8809, 0xBEC6, 0x04DD, 0x1FA0, 0x2FAE, 0x98A9, 0x7980, 0x050F, 0xB901, 0x8818, 0xB907, 0x8809, 
    0xBEC6, 0x04E7, 0x10EE, 0x90A9, 0x7980, 0x050F, 0x6919, 0xE308, 0x04FE, 0xB903, 0x8809, 0xBE46, 
    0xBEC6, 0x04FA, 0x17A0, 0xBE1E, 0x1FAE, 0xBFBF, 0xFF00, 0xBE13, 0xBFDF, 0x8080, 0x99A9, 0xBE47, 
    0x7980, 0x050F, 0xB901, 0x8809, 0xBEC6, 0x050E, 0x16A0, 0x26A0, 0xBFB7, 0xFF00, 0xBE1E, 0x1EA0, 
    0x2EAE, 0xBFBF, 0xFF00, 0xBE13, 0xBFDF, 0x8080, 0x99A9, 0x850C, 0x860F, 0x6907, 0xE388, 0x0516, 
    0x0D07, 0x8510, 0xBE59, 0x881E, 0xBE4A, 0xEF00, 0x101E, 0x901C, 0x101F, 0x901D, 0x10A0, 0x901E, 
    0x10A0, 0x901F, 0xEF00, 0x101E, 0x301C, 0x9020, 0x731B, 0x5420, 0xBE03, 0x9825, 0x1025, 0x201C, 
    0x9025, 0x7325, 0x5414, 0xBE03, 0x8B8E, 0x9880, 0x692F, 0xE388, 0x0539, 0xBE59, 0xBB07, 0x6180, 
    0x9880, 0x8BA0, 0x101F, 0x301D, 0x9021, 0x731B, 0x5421, 0xBE03, 0x982E, 0x102E, 0x201D, 0x902E, 
    0x732E, 0x5415, 0xBE03, 0x9880, 0x692F, 0xE388, 0x054F, 0xBE59, 0xBB07, 0x6180, 0x9880, 0x8BA0, 
    0x6918, 0xEF08, 0x7325, 0x5416, 0xBE03, 0x98A0, 0x732E, 0x5417, 0xBE03, 0x98A0, 0xEF00, 0x8BA0, 
    0xBEC6, 0x056B, 0xBE59, 0xBB04, 0xAA90, 0xBE04, 0xBE1E, 0x99E0, 0x8BE0, 0x69A0, 0x90D0, 0x69A0, 
    0x90D0, 0x081F, 0xB805, 0x881F, 0x8B90, 0x69A0, 0x90D0, 0x69A0, 0x9090, 0x8BD0, 0x8BD8, 0xBE1F, 
    0xEF00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 
};

static const struct firmware assp_kernel = {
	.data = (u8 *)assp_kernel_image,
	.size = sizeof assp_kernel_image
};
static const struct firmware assp_minisrc = {
	.data = (u8 *)assp_minisrc_image,
	.size = sizeof assp_minisrc_image
};

#else /* CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL */

#ifdef __LITTLE_ENDIAN
static inline void snd_m3_convert_from_le(const struct firmware *fw) { }
#else
static void snd_m3_convert_from_le(const struct firmware *fw)
{
	int i;
	u16 *data = (u16 *)fw->data;

	for (i = 0; i < fw->size / 2; ++i)
		le16_to_cpus(&data[i]);
}
#endif

#endif /* CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL */


/*
 * initialize ASSP
 */

#define MINISRC_LPF_LEN 10
static const u16 minisrc_lpf[MINISRC_LPF_LEN] = {
	0X0743, 0X1104, 0X0A4C, 0XF88D, 0X242C,
	0X1023, 0X1AA9, 0X0B60, 0XEFDD, 0X186F
};

static void snd_m3_assp_init(struct snd_m3 *chip)
{
	unsigned int i;
	u16 *data;

	/* zero kernel data */
	for (i = 0; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA, 
				  KDATA_BASE_ADDR + i, 0);

	/* zero mixer data? */
	for (i = 0; i < (REV_B_DATA_MEMORY_UNIT_LENGTH * NUM_UNITS_KERNEL_DATA) / 2; i++)
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  KDATA_BASE_ADDR2 + i, 0);

	/* init dma pointer */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_CURRENT_DMA,
			  KDATA_DMA_XFER0);

	/* write kernel into code memory.. */
	data = (u16 *)chip->assp_kernel_image->data;
	for (i = 0 ; i * 2 < chip->assp_kernel_image->size; i++) {
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_CODE, 
				  REV_B_CODE_MEMORY_BEGIN + i, data[i]);
	}

	/*
	 * We only have this one client and we know that 0x400
	 * is free in our kernel's mem map, so lets just
	 * drop it there.  It seems that the minisrc doesn't
	 * need vectors, so we won't bother with them..
	 */
	data = (u16 *)chip->assp_minisrc_image->data;
	for (i = 0; i * 2 < chip->assp_minisrc_image->size; i++) {
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_CODE, 
				  0x400 + i, data[i]);
	}

	/*
	 * write the coefficients for the low pass filter?
	 */
	for (i = 0; i < MINISRC_LPF_LEN ; i++) {
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_CODE,
				  0x400 + MINISRC_COEF_LOC + i,
				  minisrc_lpf[i]);
	}

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_CODE,
			  0x400 + MINISRC_COEF_LOC + MINISRC_LPF_LEN,
			  0x8000);

	/*
	 * the minisrc is the only thing on
	 * our task list..
	 */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA, 
			  KDATA_TASK0,
			  0x400);

	/*
	 * init the mixer number..
	 */

	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_MIXER_TASK_NUMBER,0);

	/*
	 * EXTREME KERNEL MASTER VOLUME
	 */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_DAC_LEFT_VOLUME, ARB_VOLUME);
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
			  KDATA_DAC_RIGHT_VOLUME, ARB_VOLUME);

	chip->mixer_list.curlen = 0;
	chip->mixer_list.mem_addr = KDATA_MIXER_XFER0;
	chip->mixer_list.max = MAX_VIRTUAL_MIXER_CHANNELS;
	chip->adc1_list.curlen = 0;
	chip->adc1_list.mem_addr = KDATA_ADC1_XFER0;
	chip->adc1_list.max = MAX_VIRTUAL_ADC1_CHANNELS;
	chip->dma_list.curlen = 0;
	chip->dma_list.mem_addr = KDATA_DMA_XFER0;
	chip->dma_list.max = MAX_VIRTUAL_DMA_CHANNELS;
	chip->msrc_list.curlen = 0;
	chip->msrc_list.mem_addr = KDATA_INSTANCE0_MINISRC;
	chip->msrc_list.max = MAX_INSTANCE_MINISRC;
}


static int __devinit snd_m3_assp_client_init(struct snd_m3 *chip, struct m3_dma *s, int index)
{
	int data_bytes = 2 * ( MINISRC_TMP_BUFFER_SIZE / 2 + 
			       MINISRC_IN_BUFFER_SIZE / 2 +
			       1 + MINISRC_OUT_BUFFER_SIZE / 2 + 1 );
	int address, i;

	/*
	 * the revb memory map has 0x1100 through 0x1c00
	 * free.  
	 */

	/*
	 * align instance address to 256 bytes so that its
	 * shifted list address is aligned.
	 * list address = (mem address >> 1) >> 7;
	 */
	data_bytes = ALIGN(data_bytes, 256);
	address = 0x1100 + ((data_bytes/2) * index);

	if ((address + (data_bytes/2)) >= 0x1c00) {
		snd_printk(KERN_ERR "no memory for %d bytes at ind %d (addr 0x%x)\n",
			   data_bytes, index, address);
		return -ENOMEM;
	}

	s->number = index;
	s->inst.code = 0x400;
	s->inst.data = address;

	for (i = data_bytes / 2; i > 0; address++, i--) {
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA,
				  address, 0);
	}

	return 0;
}


/* 
 * this works for the reference board, have to find
 * out about others
 *
 * this needs more magic for 4 speaker, but..
 */
static void
snd_m3_amp_enable(struct snd_m3 *chip, int enable)
{
	int io = chip->iobase;
	u16 gpo, polarity;

	if (! chip->external_amp)
		return;

	polarity = enable ? 0 : 1;
	polarity = polarity << chip->amp_gpio;
	gpo = 1 << chip->amp_gpio;

	outw(~gpo, io + GPIO_MASK);

	outw(inw(io + GPIO_DIRECTION) | gpo,
	     io + GPIO_DIRECTION);

	outw((GPO_SECONDARY_AC97 | GPO_PRIMARY_AC97 | polarity),
	     io + GPIO_DATA);

	outw(0xffff, io + GPIO_MASK);
}

static int
snd_m3_chip_init(struct snd_m3 *chip)
{
	struct pci_dev *pcidev = chip->pci;
	unsigned long io = chip->iobase;
	u32 n;
	u16 w;
	u8 t; /* makes as much sense as 'n', no? */

	pci_read_config_word(pcidev, PCI_LEGACY_AUDIO_CTRL, &w);
	w &= ~(SOUND_BLASTER_ENABLE|FM_SYNTHESIS_ENABLE|
	       MPU401_IO_ENABLE|MPU401_IRQ_ENABLE|ALIAS_10BIT_IO|
	       DISABLE_LEGACY);
	pci_write_config_word(pcidev, PCI_LEGACY_AUDIO_CTRL, w);

	if (chip->is_omnibook) {
		/*
		 * Volume buttons on some HP OmniBook laptops don't work
		 * correctly. This makes them work for the most part.
		 *
		 * Volume up and down buttons on the laptop side work.
		 * Fn+cursor_up (volme up) works.
		 * Fn+cursor_down (volume down) doesn't work.
		 * Fn+F7 (mute) works acts as volume up.
		 */
		outw(~(GPI_VOL_DOWN|GPI_VOL_UP), io + GPIO_MASK);
		outw(inw(io + GPIO_DIRECTION) & ~(GPI_VOL_DOWN|GPI_VOL_UP), io + GPIO_DIRECTION);
		outw((GPI_VOL_DOWN|GPI_VOL_UP), io + GPIO_DATA);
		outw(0xffff, io + GPIO_MASK);
	}
	pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
	n &= ~(HV_CTRL_ENABLE | REDUCED_DEBOUNCE | HV_BUTTON_FROM_GD);
	n |= chip->hv_config;
	/* For some reason we must always use reduced debounce. */
	n |= REDUCED_DEBOUNCE;
	n |= PM_CTRL_ENABLE | CLK_DIV_BY_49 | USE_PCI_TIMING;
	pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

	outb(RESET_ASSP, chip->iobase + ASSP_CONTROL_B);
	pci_read_config_dword(pcidev, PCI_ALLEGRO_CONFIG, &n);
	n &= ~INT_CLK_SELECT;
	if (!chip->allegro_flag) {
		n &= ~INT_CLK_MULT_ENABLE; 
		n |= INT_CLK_SRC_NOT_PCI;
	}
	n &=  ~( CLK_MULT_MODE_SELECT | CLK_MULT_MODE_SELECT_2 );
	pci_write_config_dword(pcidev, PCI_ALLEGRO_CONFIG, n);

	if (chip->allegro_flag) {
		pci_read_config_dword(pcidev, PCI_USER_CONFIG, &n);
		n |= IN_CLK_12MHZ_SELECT;
		pci_write_config_dword(pcidev, PCI_USER_CONFIG, n);
	}

	t = inb(chip->iobase + ASSP_CONTROL_A);
	t &= ~( DSP_CLK_36MHZ_SELECT  | ASSP_CLK_49MHZ_SELECT);
	t |= ASSP_CLK_49MHZ_SELECT;
	t |= ASSP_0_WS_ENABLE; 
	outb(t, chip->iobase + ASSP_CONTROL_A);

	snd_m3_assp_init(chip); /* download DSP code before starting ASSP below */
	outb(RUN_ASSP, chip->iobase + ASSP_CONTROL_B); 

	outb(0x00, io + HARDWARE_VOL_CTRL);
	outb(0x88, io + SHADOW_MIX_REG_VOICE);
	outb(0x88, io + HW_VOL_COUNTER_VOICE);
	outb(0x88, io + SHADOW_MIX_REG_MASTER);
	outb(0x88, io + HW_VOL_COUNTER_MASTER);

	return 0;
} 

static void
snd_m3_enable_ints(struct snd_m3 *chip)
{
	unsigned long io = chip->iobase;
	unsigned short val;

	/* TODO: MPU401 not supported yet */
	val = ASSP_INT_ENABLE /*| MPU401_INT_ENABLE*/;
	if (chip->hv_config & HV_CTRL_ENABLE)
		val |= HV_INT_ENABLE;
	outw(val, io + HOST_INT_CTRL);
	outb(inb(io + ASSP_CONTROL_C) | ASSP_HOST_INT_ENABLE,
	     io + ASSP_CONTROL_C);
}


/*
 */

static int snd_m3_free(struct snd_m3 *chip)
{
	struct m3_dma *s;
	int i;

	if (chip->substreams) {
		spin_lock_irq(&chip->reg_lock);
		for (i = 0; i < chip->num_substreams; i++) {
			s = &chip->substreams[i];
			/* check surviving pcms; this should not happen though.. */
			if (s->substream && s->running)
				snd_m3_pcm_stop(chip, s, s->substream);
		}
		spin_unlock_irq(&chip->reg_lock);
		kfree(chip->substreams);
	}
	if (chip->iobase) {
		outw(0, chip->iobase + HOST_INT_CTRL); /* disable ints */
	}

#ifdef CONFIG_PM
	vfree(chip->suspend_mem);
#endif

	if (chip->irq >= 0) {
		synchronize_irq(chip->irq);
		free_irq(chip->irq, chip);
	}

	if (chip->iobase)
		pci_release_regions(chip->pci);

#ifndef CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL
	release_firmware(chip->assp_kernel_image);
	release_firmware(chip->assp_minisrc_image);
#endif

	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}


/*
 * APM support
 */
#ifdef CONFIG_PM
static int m3_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_m3 *chip = card->private_data;
	int i, index;

	if (chip->suspend_mem == NULL)
		return 0;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_ac97_suspend(chip->ac97);

	msleep(10); /* give the assp a chance to idle.. */

	snd_m3_assp_halt(chip);

	/* save dsp image */
	index = 0;
	for (i = REV_B_CODE_MEMORY_BEGIN; i <= REV_B_CODE_MEMORY_END; i++)
		chip->suspend_mem[index++] = 
			snd_m3_assp_read(chip, MEMTYPE_INTERNAL_CODE, i);
	for (i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
		chip->suspend_mem[index++] = 
			snd_m3_assp_read(chip, MEMTYPE_INTERNAL_DATA, i);

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

static int m3_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_m3 *chip = card->private_data;
	int i, index;

	if (chip->suspend_mem == NULL)
		return 0;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "maestor3: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	/* first lets just bring everything back. .*/
	snd_m3_outw(chip, 0, 0x54);
	snd_m3_outw(chip, 0, 0x56);

	snd_m3_chip_init(chip);
	snd_m3_assp_halt(chip);
	snd_m3_ac97_reset(chip);

	/* restore dsp image */
	index = 0;
	for (i = REV_B_CODE_MEMORY_BEGIN; i <= REV_B_CODE_MEMORY_END; i++)
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_CODE, i, 
				  chip->suspend_mem[index++]);
	for (i = REV_B_DATA_MEMORY_BEGIN ; i <= REV_B_DATA_MEMORY_END; i++)
		snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA, i, 
				  chip->suspend_mem[index++]);

	/* tell the dma engine to restart itself */
	snd_m3_assp_write(chip, MEMTYPE_INTERNAL_DATA, 
			  KDATA_DMA_ACTIVE, 0);

        /* restore ac97 registers */
	snd_ac97_resume(chip->ac97);

	snd_m3_assp_continue(chip);
	snd_m3_enable_ints(chip);
	snd_m3_amp_enable(chip, 1);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM */


/*
 */

static int snd_m3_dev_free(struct snd_device *device)
{
	struct snd_m3 *chip = device->device_data;
	return snd_m3_free(chip);
}

static int __devinit
snd_m3_create(struct snd_card *card, struct pci_dev *pci,
	      int enable_amp,
	      int amp_gpio,
	      struct snd_m3 **chip_ret)
{
	struct snd_m3 *chip;
	int i, err;
	const struct snd_pci_quirk *quirk;
	static struct snd_device_ops ops = {
		.dev_free =	snd_m3_dev_free,
	};

	*chip_ret = NULL;

	if (pci_enable_device(pci))
		return -EIO;

	/* check, if we can restrict PCI DMA transfers to 28 bits */
	if (pci_set_dma_mask(pci, DMA_28BIT_MASK) < 0 ||
	    pci_set_consistent_dma_mask(pci, DMA_28BIT_MASK) < 0) {
		snd_printk(KERN_ERR "architecture does not support 28bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->ac97_lock);

	switch (pci->device) {
	case PCI_DEVICE_ID_ESS_ALLEGRO:
	case PCI_DEVICE_ID_ESS_ALLEGRO_1:
	case PCI_DEVICE_ID_ESS_CANYON3D_2LE:
	case PCI_DEVICE_ID_ESS_CANYON3D_2:
		chip->allegro_flag = 1;
		break;
	}

	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	chip->external_amp = enable_amp;
	if (amp_gpio >= 0 && amp_gpio <= 0x0f)
		chip->amp_gpio = amp_gpio;
	else {
		quirk = snd_pci_quirk_lookup(pci, m3_amp_quirk_list);
		if (quirk) {
			snd_printdd(KERN_INFO "maestro3: set amp-gpio "
				    "for '%s'\n", quirk->name);
			chip->amp_gpio = quirk->value;
		} else if (chip->allegro_flag)
			chip->amp_gpio = GPO_EXT_AMP_ALLEGRO;
		else /* presumably this is for all 'maestro3's.. */
			chip->amp_gpio = GPO_EXT_AMP_M3;
	}

	quirk = snd_pci_quirk_lookup(pci, m3_irda_quirk_list);
	if (quirk) {
		snd_printdd(KERN_INFO "maestro3: enabled irda workaround "
			    "for '%s'\n", quirk->name);
		chip->irda_workaround = 1;
	}
	quirk = snd_pci_quirk_lookup(pci, m3_hv_quirk_list);
	if (quirk)
		chip->hv_config = quirk->value;
	if (snd_pci_quirk_lookup(pci, m3_omnibook_quirk_list))
		chip->is_omnibook = 1;

	chip->num_substreams = NR_DSPS;
	chip->substreams = kcalloc(chip->num_substreams, sizeof(struct m3_dma),
				   GFP_KERNEL);
	if (chip->substreams == NULL) {
		kfree(chip);
		pci_disable_device(pci);
		return -ENOMEM;
	}

#ifdef CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL
	chip->assp_kernel_image = &assp_kernel;
#else
	err = request_firmware(&chip->assp_kernel_image,
			       "ess/maestro3_assp_kernel.fw", &pci->dev);
	if (err < 0) {
		snd_m3_free(chip);
		return err;
	} else
		snd_m3_convert_from_le(chip->assp_kernel_image);
#endif

#ifdef CONFIG_SND_MAESTRO3_FIRMWARE_IN_KERNEL
	chip->assp_minisrc_image = &assp_minisrc;
#else
	err = request_firmware(&chip->assp_minisrc_image,
			       "ess/maestro3_assp_minisrc.fw", &pci->dev);
	if (err < 0) {
		snd_m3_free(chip);
		return err;
	} else
		snd_m3_convert_from_le(chip->assp_minisrc_image);
#endif

	if ((err = pci_request_regions(pci, card->driver)) < 0) {
		snd_m3_free(chip);
		return err;
	}
	chip->iobase = pci_resource_start(pci, 0);
	
	/* just to be sure */
	pci_set_master(pci);

	snd_m3_chip_init(chip);
	snd_m3_assp_halt(chip);

	snd_m3_ac97_reset(chip);

	snd_m3_amp_enable(chip, 1);

	tasklet_init(&chip->hwvol_tq, snd_m3_update_hw_volume, (unsigned long)chip);

	if (request_irq(pci->irq, snd_m3_interrupt, IRQF_SHARED,
			card->driver, chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_m3_free(chip);
		return -ENOMEM;
	}
	chip->irq = pci->irq;

#ifdef CONFIG_PM
	chip->suspend_mem = vmalloc(sizeof(u16) * (REV_B_CODE_MEMORY_LENGTH + REV_B_DATA_MEMORY_LENGTH));
	if (chip->suspend_mem == NULL)
		snd_printk(KERN_WARNING "can't allocate apm buffer\n");
#endif

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_m3_free(chip);
		return err;
	}

	if ((err = snd_m3_mixer(chip)) < 0)
		return err;

	for (i = 0; i < chip->num_substreams; i++) {
		struct m3_dma *s = &chip->substreams[i];
		if ((err = snd_m3_assp_client_init(chip, s, i)) < 0)
			return err;
	}

	if ((err = snd_m3_pcm(chip, 0)) < 0)
		return err;
    
	snd_m3_enable_ints(chip);
	snd_m3_assp_continue(chip);

	snd_card_set_dev(card, &pci->dev);

	*chip_ret = chip;

	return 0; 
}

/*
 */
static int __devinit
snd_m3_probe(struct pci_dev *pci, const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_m3 *chip;
	int err;

	/* don't pick up modems */
	if (((pci->class >> 8) & 0xffff) != PCI_CLASS_MULTIMEDIA_AUDIO)
		return -ENODEV;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	switch (pci->device) {
	case PCI_DEVICE_ID_ESS_ALLEGRO:
	case PCI_DEVICE_ID_ESS_ALLEGRO_1:
		strcpy(card->driver, "Allegro");
		break;
	case PCI_DEVICE_ID_ESS_CANYON3D_2LE:
	case PCI_DEVICE_ID_ESS_CANYON3D_2:
		strcpy(card->driver, "Canyon3D-2");
		break;
	default:
		strcpy(card->driver, "Maestro3");
		break;
	}

	if ((err = snd_m3_create(card, pci,
				 external_amp[dev],
				 amp_gpio[dev],
				 &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	sprintf(card->shortname, "ESS %s PCI", card->driver);
	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname, chip->iobase, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

#if 0 /* TODO: not supported yet */
	/* TODO enable MIDI IRQ and I/O */
	err = snd_mpu401_uart_new(chip->card, 0, MPU401_HW_MPU401,
				  chip->iobase + MPU401_DATA_PORT,
				  MPU401_INFO_INTEGRATED,
				  chip->irq, 0, &chip->rmidi);
	if (err < 0)
		printk(KERN_WARNING "maestro3: no MIDI support.\n");
#endif

	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_m3_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Maestro3",
	.id_table = snd_m3_ids,
	.probe = snd_m3_probe,
	.remove = __devexit_p(snd_m3_remove),
#ifdef CONFIG_PM
	.suspend = m3_suspend,
	.resume = m3_resume,
#endif
};
	
static int __init alsa_card_m3_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_m3_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_m3_init)
module_exit(alsa_card_m3_exit)
