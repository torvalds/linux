#ifndef _DT_ROCKCHIP_SDMMC_H
#define _DT_ROCKCHIP_SDMMC_H

#define CONTROLLER_INT_DETECT   (0) 
#define GPIO_INT_DETECT         (1)

#define MMC_NOT_DMA             (0)
#define MMC_USE_DMA             (1)

#define MMC_USE_IDMA            (0)
#define MMC_USE_GDMA            (1)

#define SDMMC_DMA_CHN       	(1)
#define SDIO_DMA_CHN        	(3)
#define EMMC_DMA_CHN			(4)

#define MMC_CAP_4_BIT_DATA      (1 << 0)    /* Can the host do 4 bit transfers */
#define MMC_CAP_MMC_HIGHSPEED   (1 << 1)    /* Can do MMC high-speed timing */
#define MMC_CAP_SD_HIGHSPEED    (1 << 2)    /* Can do SD high-speed timing */
#define MMC_CAP_SDIO_IRQ        (1 << 3)    /* Can signal pending SDIO IRQs */
#define MMC_CAP_SPI             (1 << 4)    /* Talks only SPI protocols */
#define MMC_CAP_NEEDS_POLL      (1 << 5)    /* Needs polling for card-detection */
#define MMC_CAP_8_BIT_DATA      (1 << 6)    /* Can the host do 8 bit transfers */

#define MMC_CAP_NONREMOVABLE    (1 << 8)    /* Nonremovable e.g. eMMC */
#define MMC_CAP_WAIT_WHILE_BUSY (1 << 9)    /* Waits while card is busy */
#define MMC_CAP_ERASE           (1 << 10)   /* Allow erase/trim commands */
#define MMC_CAP_1_8V_DDR        (1 << 11)   /* can support */
                                            /* DDR mode at 1.8V */
#define MMC_CAP_1_2V_DDR        (1 << 12)   /* can support */
                                            /* DDR mode at 1.2V */
#define MMC_CAP_POWER_OFF_CARD  (1 << 13)   /* Can power off after boot */
#define MMC_CAP_BUS_WIDTH_TEST  (1 << 14)   /* CMD14/CMD19 bus width ok */
#define MMC_CAP_UHS_SDR12       (1 << 15)   /* Host supports UHS SDR12 mode */
#define MMC_CAP_UHS_SDR25   (1 << 16)   /* Host supports UHS SDR25 mode */
#define MMC_CAP_UHS_SDR50   (1 << 17)   /* Host supports UHS SDR50 mode */
#define MMC_CAP_UHS_SDR104  (1 << 18)   /* Host supports UHS SDR104 mode */
#define MMC_CAP_UHS_DDR50   (1 << 19)   /* Host supports UHS DDR50 mode */
#define MMC_CAP_DRIVER_TYPE_A   (1 << 23)   /* Host supports Driver Type A */
#define MMC_CAP_DRIVER_TYPE_C   (1 << 24)   /* Host supports Driver Type C */
#define MMC_CAP_DRIVER_TYPE_D   (1 << 25)   /* Host supports Driver Type D */
#define MMC_CAP_CMD23       (1 << 30)   /* CMD23 supported. */
#define MMC_CAP_HW_RESET    (1 << 31)   /* Hardware reset */

#define MMC_VDD_165_195     0x00000080  /* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21       0x00000100  /* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22       0x00000200  /* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23       0x00000400  /* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24       0x00000800  /* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25       0x00001000  /* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26       0x00002000  /* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27       0x00004000  /* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28       0x00008000  /* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29       0x00010000  /* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30       0x00020000  /* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31       0x00040000  /* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32       0x00080000  /* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33       0x00100000  /* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34       0x00200000  /* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35       0x00400000  /* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36       0x00800000  /* VDD voltage 3.5 ~ 3.6 */


#endif
