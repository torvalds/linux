#ifndef __LINUX_ULPI_REGS_H
#define __LINUX_ULPI_REGS_H

/*
 * Macros for Set and Clear
 * See ULPI 1.1 specification to find the registers with Set and Clear offsets
 */
#define ULPI_SET(a)				(a + 1)
#define ULPI_CLR(a)				(a + 2)

/*
 * Register Map
 */
#define ULPI_VENDOR_ID_LOW			0x00
#define ULPI_VENDOR_ID_HIGH			0x01
#define ULPI_PRODUCT_ID_LOW			0x02
#define ULPI_PRODUCT_ID_HIGH			0x03
#define ULPI_FUNC_CTRL				0x04
#define ULPI_IFC_CTRL				0x07
#define ULPI_OTG_CTRL				0x0a
#define ULPI_USB_INT_EN_RISE			0x0d
#define ULPI_USB_INT_EN_FALL			0x10
#define ULPI_USB_INT_STS			0x13
#define ULPI_USB_INT_LATCH			0x14
#define ULPI_DEBUG				0x15
#define ULPI_SCRATCH				0x16
/* Optional Carkit Registers */
#define ULPI_CARKIT_CTRL			0x19
#define ULPI_CARKIT_INT_DELAY			0x1c
#define ULPI_CARKIT_INT_EN			0x1d
#define ULPI_CARKIT_INT_STS			0x20
#define ULPI_CARKIT_INT_LATCH			0x21
#define ULPI_CARKIT_PLS_CTRL			0x22
/* Other Optional Registers */
#define ULPI_TX_POS_WIDTH			0x25
#define ULPI_TX_NEG_WIDTH			0x26
#define ULPI_POLARITY_RECOVERY			0x27
/* Access Extended Register Set */
#define ULPI_ACCESS_EXTENDED			0x2f
/* Vendor Specific */
#define ULPI_VENDOR_SPECIFIC			0x30
/* Extended Registers */
#define ULPI_EXT_VENDOR_SPECIFIC		0x80

/*
 * Register Bits
 */

/* Function Control */
#define ULPI_FUNC_CTRL_XCVRSEL			BIT(0)
#define  ULPI_FUNC_CTRL_XCVRSEL_MASK		0x3
#define  ULPI_FUNC_CTRL_HIGH_SPEED		0x0
#define  ULPI_FUNC_CTRL_FULL_SPEED		0x1
#define  ULPI_FUNC_CTRL_LOW_SPEED		0x2
#define  ULPI_FUNC_CTRL_FS4LS			0x3
#define ULPI_FUNC_CTRL_TERMSELECT		BIT(2)
#define ULPI_FUNC_CTRL_OPMODE			BIT(3)
#define  ULPI_FUNC_CTRL_OPMODE_MASK		(0x3 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NORMAL		(0x0 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NONDRIVING	(0x1 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_DISABLE_NRZI	(0x2 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NOSYNC_NOEOP	(0x3 << 3)
#define ULPI_FUNC_CTRL_RESET			BIT(5)
#define ULPI_FUNC_CTRL_SUSPENDM			BIT(6)

/* Interface Control */
#define ULPI_IFC_CTRL_6_PIN_SERIAL_MODE		BIT(0)
#define ULPI_IFC_CTRL_3_PIN_SERIAL_MODE		BIT(1)
#define ULPI_IFC_CTRL_CARKITMODE		BIT(2)
#define ULPI_IFC_CTRL_CLOCKSUSPENDM		BIT(3)
#define ULPI_IFC_CTRL_AUTORESUME		BIT(4)
#define ULPI_IFC_CTRL_EXTERNAL_VBUS		BIT(5)
#define ULPI_IFC_CTRL_PASSTHRU			BIT(6)
#define ULPI_IFC_CTRL_PROTECT_IFC_DISABLE	BIT(7)

/* OTG Control */
#define ULPI_OTG_CTRL_ID_PULLUP			BIT(0)
#define ULPI_OTG_CTRL_DP_PULLDOWN		BIT(1)
#define ULPI_OTG_CTRL_DM_PULLDOWN		BIT(2)
#define ULPI_OTG_CTRL_DISCHRGVBUS		BIT(3)
#define ULPI_OTG_CTRL_CHRGVBUS			BIT(4)
#define ULPI_OTG_CTRL_DRVVBUS			BIT(5)
#define ULPI_OTG_CTRL_DRVVBUS_EXT		BIT(6)
#define ULPI_OTG_CTRL_EXTVBUSIND		BIT(7)

/* USB Interrupt Enable Rising,
 * USB Interrupt Enable Falling,
 * USB Interrupt Status and
 * USB Interrupt Latch
 */
#define ULPI_INT_HOST_DISCONNECT		BIT(0)
#define ULPI_INT_VBUS_VALID			BIT(1)
#define ULPI_INT_SESS_VALID			BIT(2)
#define ULPI_INT_SESS_END			BIT(3)
#define ULPI_INT_IDGRD				BIT(4)

/* Debug */
#define ULPI_DEBUG_LINESTATE0			BIT(0)
#define ULPI_DEBUG_LINESTATE1			BIT(1)

/* Carkit Control */
#define ULPI_CARKIT_CTRL_CARKITPWR		BIT(0)
#define ULPI_CARKIT_CTRL_IDGNDDRV		BIT(1)
#define ULPI_CARKIT_CTRL_TXDEN			BIT(2)
#define ULPI_CARKIT_CTRL_RXDEN			BIT(3)
#define ULPI_CARKIT_CTRL_SPKLEFTEN		BIT(4)
#define ULPI_CARKIT_CTRL_SPKRIGHTEN		BIT(5)
#define ULPI_CARKIT_CTRL_MICEN			BIT(6)

/* Carkit Interrupt Enable */
#define ULPI_CARKIT_INT_EN_IDFLOAT_RISE		BIT(0)
#define ULPI_CARKIT_INT_EN_IDFLOAT_FALL		BIT(1)
#define ULPI_CARKIT_INT_EN_CARINTDET		BIT(2)
#define ULPI_CARKIT_INT_EN_DP_RISE		BIT(3)
#define ULPI_CARKIT_INT_EN_DP_FALL		BIT(4)

/* Carkit Interrupt Status and
 * Carkit Interrupt Latch
 */
#define ULPI_CARKIT_INT_IDFLOAT			BIT(0)
#define ULPI_CARKIT_INT_CARINTDET		BIT(1)
#define ULPI_CARKIT_INT_DP			BIT(2)

/* Carkit Pulse Control*/
#define ULPI_CARKIT_PLS_CTRL_TXPLSEN		BIT(0)
#define ULPI_CARKIT_PLS_CTRL_RXPLSEN		BIT(1)
#define ULPI_CARKIT_PLS_CTRL_SPKRLEFT_BIASEN	BIT(2)
#define ULPI_CARKIT_PLS_CTRL_SPKRRIGHT_BIASEN	BIT(3)

#endif /* __LINUX_ULPI_REGS_H */
