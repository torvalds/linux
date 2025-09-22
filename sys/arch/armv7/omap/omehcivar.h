/* $OpenBSD: omehcivar.h,v 1.2 2019/05/06 03:45:58 mlarkin Exp $ */

/*
 * Misc
 */
#define OMAP_HS_USB_PORTS                           3

/*
 * USB TTL Module
 */
#define	OMAP_USBTLL_REVISION                        0x0000
#define	OMAP_USBTLL_SYSCONFIG                       0x0010
#define	OMAP_USBTLL_SYSSTATUS                       0x0014
#define	OMAP_USBTLL_IRQSTATUS                       0x0018
#define	OMAP_USBTLL_IRQENABLE                       0x001C
#define	OMAP_USBTLL_TLL_SHARED_CONF                 0x0030
#define	OMAP_USBTLL_TLL_CHANNEL_CONF(i)             (0x0040 + (0x04 * (i)))
#define	OMAP_USBTLL_SAR_CNTX(i)                     (0x0400 + (0x04 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_ID_LO(i)            (0x0800 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_ID_HI(i)            (0x0801 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_PRODUCT_ID_LO(i)           (0x0802 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_PRODUCT_ID_HI(i)           (0x0803 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL(i)           (0x0804 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL_SET(i)       (0x0805 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL_CLR(i)       (0x0806 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL(i)          (0x0807 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL_SET(i)      (0x0808 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL_CLR(i)      (0x0809 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL(i)                (0x080A + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL_SET(i)            (0x080B + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL_CLR(i)            (0x080C + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE(i)         (0x080D + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE_SET(i)     (0x080E + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE_CLR(i)     (0x080F + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL(i)         (0x0810 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL_SET(i)     (0x0811 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL_CLR(i)     (0x0812 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_STATUS(i)          (0x0813 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_LATCH(i)           (0x0814 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_DEBUG(i)                   (0x0815 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER(i)        (0x0816 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER_SET(i)    (0x0817 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER_CLR(i)    (0x0818 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_EXTENDED_SET_ACCESS(i)     (0x082F + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN(i)        (0x0830 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN_SET(i)    (0x0831 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN_CLR(i)    (0x0832 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_STATUS(i)    (0x0833 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_LATCH(i)     (0x0834 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS(i)            (0x0835 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS_SET(i)        (0x0836 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS_CLR(i)        (0x0837 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_LATCH_NOCLR(i)     (0x0838 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN(i)           (0x083B + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN_SET(i)       (0x083C + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN_CLR(i)       (0x083D + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_STATUS(i)       (0x083E + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_LATCH(i)        (0x083F + (0x100 * (i)))


/*
 * USB Host Module
 */

/* UHH */
#define	OMAP_USBHOST_UHH_REVISION                   0x0000
#define	OMAP_USBHOST_UHH_SYSCONFIG                  0x0010
#define	OMAP_USBHOST_UHH_SYSSTATUS                  0x0014
#define	OMAP_USBHOST_UHH_HOSTCONFIG                 0x0040
#define	OMAP_USBHOST_UHH_DEBUG_CSR                  0x0044

/* EHCI */
#define	OMAP_USBHOST_HCCAPBASE                      0x0000
#define	OMAP_USBHOST_HCSPARAMS                      0x0004
#define	OMAP_USBHOST_HCCPARAMS                      0x0008
#define	OMAP_USBHOST_USBCMD                         0x0010
#define	OMAP_USBHOST_USBSTS                         0x0014
#define	OMAP_USBHOST_USBINTR                        0x0018
#define	OMAP_USBHOST_FRINDEX                        0x001C
#define	OMAP_USBHOST_CTRLDSSEGMENT                  0x0020
#define	OMAP_USBHOST_PERIODICLISTBASE               0x0024
#define	OMAP_USBHOST_ASYNCLISTADDR                  0x0028
#define	OMAP_USBHOST_CONFIGFLAG                     0x0050
#define	OMAP_USBHOST_PORTSC(i)                      (0x0054 + (0x04 * (i)))
#define	OMAP_USBHOST_INSNREG00                      0x0090
#define	OMAP_USBHOST_INSNREG01                      0x0094
#define	OMAP_USBHOST_INSNREG02                      0x0098
#define	OMAP_USBHOST_INSNREG03                      0x009C
#define	OMAP_USBHOST_INSNREG04                      0x00A0
#define	OMAP_USBHOST_INSNREG05_UTMI                 0x00A4
#define	OMAP_USBHOST_INSNREG05_ULPI                 0x00A4
#define	OMAP_USBHOST_INSNREG06                      0x00A8
#define	OMAP_USBHOST_INSNREG07                      0x00AC
#define	OMAP_USBHOST_INSNREG08                      0x00B0

#define OMAP_USBHOST_INSNREG04_DISABLE_UNSUSPEND   (1 << 5)

#define OMAP_USBHOST_INSNREG05_ULPI_CONTROL_SHIFT   31
#define OMAP_USBHOST_INSNREG05_ULPI_PORTSEL_SHIFT   24
#define OMAP_USBHOST_INSNREG05_ULPI_OPSEL_SHIFT     22
#define OMAP_USBHOST_INSNREG05_ULPI_REGADD_SHIFT    16
#define OMAP_USBHOST_INSNREG05_ULPI_EXTREGADD_SHIFT 8
#define OMAP_USBHOST_INSNREG05_ULPI_WRDATA_SHIFT    0





/* TLL Register Set */
#define	TLL_SYSCONFIG_CACTIVITY                 (1UL << 8)
#define	TLL_SYSCONFIG_SIDLE_SMART_IDLE          (2UL << 3)
#define	TLL_SYSCONFIG_SIDLE_NO_IDLE             (1UL << 3)
#define	TLL_SYSCONFIG_SIDLE_FORCED_IDLE         (0UL << 3)
#define	TLL_SYSCONFIG_ENAWAKEUP                 (1UL << 2)
#define	TLL_SYSCONFIG_SOFTRESET                 (1UL << 1)
#define	TLL_SYSCONFIG_AUTOIDLE                  (1UL << 0)

#define	TLL_SYSSTATUS_RESETDONE                 (1UL << 0)

#define TLL_SHARED_CONF_USB_90D_DDR_EN          (1UL << 6)
#define TLL_SHARED_CONF_USB_180D_SDR_EN         (1UL << 5)
#define TLL_SHARED_CONF_USB_DIVRATIO_MASK       (7UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_128        (7UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_64         (6UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_32         (5UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_16         (4UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_8          (3UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_4          (2UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_2          (1UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_1          (0UL << 2)
#define TLL_SHARED_CONF_FCLK_REQ                (1UL << 1)
#define TLL_SHARED_CONF_FCLK_IS_ON              (1UL << 0)

#define TLL_CHANNEL_CONF_DRVVBUS                (1UL << 16)
#define TLL_CHANNEL_CONF_CHRGVBUS               (1UL << 15)
#define TLL_CHANNEL_CONF_ULPINOBITSTUFF         (1UL << 11)
#define TLL_CHANNEL_CONF_ULPIAUTOIDLE           (1UL << 10)
#define TLL_CHANNEL_CONF_UTMIAUTOIDLE           (1UL << 9)
#define TLL_CHANNEL_CONF_ULPIDDRMODE            (1UL << 8)
#define TLL_CHANNEL_CONF_ULPIOUTCLKMODE         (1UL << 7)
#define TLL_CHANNEL_CONF_TLLFULLSPEED           (1UL << 6)
#define TLL_CHANNEL_CONF_TLLCONNECT             (1UL << 5)
#define TLL_CHANNEL_CONF_TLLATTACH              (1UL << 4)
#define TLL_CHANNEL_CONF_UTMIISADEV             (1UL << 3)
#define TLL_CHANNEL_CONF_CHANEN                 (1UL << 0)


/* UHH Register Set */
#define UHH_SYSCONFIG_MIDLEMODE_MASK            (3UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_SMARTSTANDBY    (2UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_NOSTANDBY       (1UL << 12)
#define UHH_SYSCONFIG_MIDLEMODE_FORCESTANDBY    (0UL << 12)
#define UHH_SYSCONFIG_CLOCKACTIVITY             (1UL << 8)
#define UHH_SYSCONFIG_SIDLEMODE_MASK            (3UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_SMARTIDLE       (2UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_NOIDLE          (1UL << 3)
#define UHH_SYSCONFIG_SIDLEMODE_FORCEIDLE       (0UL << 3)
#define UHH_SYSCONFIG_ENAWAKEUP                 (1UL << 2)
#define UHH_SYSCONFIG_SOFTRESET                 (1UL << 1)
#define UHH_SYSCONFIG_AUTOIDLE                  (1UL << 0)

#define UHH_HOSTCONFIG_APP_START_CLK            (1UL << 31)
#define UHH_HOSTCONFIG_P3_CONNECT_STATUS        (1UL << 10)
#define UHH_HOSTCONFIG_P2_CONNECT_STATUS        (1UL << 9)
#define UHH_HOSTCONFIG_P1_CONNECT_STATUS        (1UL << 8)
#define UHH_HOSTCONFIG_ENA_INCR_ALIGN           (1UL << 5)
#define UHH_HOSTCONFIG_ENA_INCR16               (1UL << 4)
#define UHH_HOSTCONFIG_ENA_INCR8                (1UL << 3)
#define UHH_HOSTCONFIG_ENA_INCR4                (1UL << 2)
#define UHH_HOSTCONFIG_AUTOPPD_ON_OVERCUR_EN    (1UL << 1)
#define UHH_HOSTCONFIG_P1_ULPI_BYPASS           (1UL << 0)

/* The following are on rev2 (OMAP44xx) of the EHCI only */
#define UHH_SYSCONFIG_IDLEMODE_MASK             (3UL << 2)
#define UHH_SYSCONFIG_IDLEMODE_NOIDLE           (1UL << 2)
#define UHH_SYSCONFIG_STANDBYMODE_MASK          (3UL << 4)
#define UHH_SYSCONFIG_STANDBYMODE_NOSTDBY       (1UL << 4)

#define UHH_HOSTCONFIG_P1_MODE_MASK             (3UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_ULPI_PHY         (0UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_UTMI_PHY         (1UL << 16)
#define UHH_HOSTCONFIG_P1_MODE_HSIC             (3UL << 16)
#define UHH_HOSTCONFIG_P2_MODE_MASK             (3UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_ULPI_PHY         (0UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_UTMI_PHY         (1UL << 18)
#define UHH_HOSTCONFIG_P2_MODE_HSIC             (3UL << 18)

#define ULPI_FUNC_CTRL_RESET                    (1 << 5)

/*-------------------------------------------------------------------------*/

/*
 * Macros for Set and Clear
 * See ULPI 1.1 specification to find the registers with Set and Clear offsets
 */
#define ULPI_SET(a)                             (a + 1)
#define ULPI_CLR(a)                             (a + 2)

/*-------------------------------------------------------------------------*/

/*
 * Register Map
 */
#define ULPI_VENDOR_ID_LOW                      0x00
#define ULPI_VENDOR_ID_HIGH                     0x01
#define ULPI_PRODUCT_ID_LOW                     0x02
#define ULPI_PRODUCT_ID_HIGH                    0x03
#define ULPI_FUNC_CTRL                          0x04
#define ULPI_IFC_CTRL                           0x07
#define ULPI_OTG_CTRL                           0x0a
#define ULPI_USB_INT_EN_RISE                    0x0d
#define ULPI_USB_INT_EN_FALL                    0x10
#define ULPI_USB_INT_STS                        0x13
#define ULPI_USB_INT_LATCH                      0x14
#define ULPI_DEBUG                              0x15
#define ULPI_SCRATCH                            0x16

/*
 * Values of UHH_REVISION - Note: these are not given in the TRM but taken
 * from the linux OMAP EHCI driver (thanks guys).  It has been verified on
 * a Panda and Beagle board.
 */
#define OMAP_EHCI_REV1  0x00000010      /* OMAP3 */
#define OMAP_EHCI_REV2  0x50700100      /* OMAP4 */

#define EHCI_VENDORID_OMAP3     0x42fa05
#define OMAP_EHCI_HC_DEVSTR    "TI OMAP USB 2.0 controller"

#define EHCI_HCD_OMAP_MODE_UNKNOWN  0
#define EHCI_HCD_OMAP_MODE_PHY      1
#define EHCI_HCD_OMAP_MODE_TLL      2
#define EHCI_HCD_OMAP_MODE_HSIC     3
