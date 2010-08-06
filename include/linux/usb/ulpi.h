/*
 * ulpi.h -- ULPI defines and function prorotypes
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * version 2 of that License.
 */

#ifndef __LINUX_USB_ULPI_H
#define __LINUX_USB_ULPI_H

/*-------------------------------------------------------------------------*/

/*
 * Macros for Set and Clear
 * See ULPI 1.1 specification to find the registers with Set and Clear offsets
 */
#define ULPI_SET(a)				(a + 1)
#define ULPI_CLR(a)				(a + 2)

/*-------------------------------------------------------------------------*/

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
#define ULPI_CARCIT_CTRL			0x19
#define ULPI_CARCIT_INT_DELAY			0x1c
#define ULPI_CARCIT_INT_EN			0x1d
#define ULPI_CARCIT_INT_STS			0x20
#define ULPI_CARCIT_INT_LATCH			0x21
#define ULPI_CARCIT_PLS_CTRL			0x22
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

/*-------------------------------------------------------------------------*/

/* Function Control */
#define ULPI_FUNC_CTRL_XCVRSEL			(1 << 0)
#define  ULPI_FUNC_CTRL_XCVRSEL_MASK		(3 << 0)
#define  ULPI_FUNC_CTRL_HIGH_SPEED		(0 << 0)
#define  ULPI_FUNC_CTRL_FULL_SPEED		(1 << 0)
#define  ULPI_FUNC_CTRL_LOW_SPEED		(2 << 0)
#define  ULPI_FUNC_CTRL_FS4LS			(3 << 0)
#define ULPI_FUNC_CTRL_TERMSELECT		(1 << 2)
#define ULPI_FUNC_CTRL_OPMODE			(1 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_MASK		(3 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NORMAL		(0 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NONDRIVING	(1 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_DISABLE_NRZI	(2 << 3)
#define  ULPI_FUNC_CTRL_OPMODE_NOSYNC_NOEOP	(3 << 3)
#define ULPI_FUNC_CTRL_RESET			(1 << 5)
#define ULPI_FUNC_CTRL_SUSPENDM			(1 << 6)

/* Interface Control */
#define ULPI_IFC_CTRL_6_PIN_SERIAL_MODE		(1 << 0)
#define ULPI_IFC_CTRL_3_PIN_SERIAL_MODE		(1 << 1)
#define ULPI_IFC_CTRL_CARKITMODE		(1 << 2)
#define ULPI_IFC_CTRL_CLOCKSUSPENDM		(1 << 3)
#define ULPI_IFC_CTRL_AUTORESUME		(1 << 4)
#define ULPI_IFC_CTRL_EXTERNAL_VBUS		(1 << 5)
#define ULPI_IFC_CTRL_PASSTHRU			(1 << 6)
#define ULPI_IFC_CTRL_PROTECT_IFC_DISABLE	(1 << 7)

/* OTG Control */
#define ULPI_OTG_CTRL_ID_PULLUP			(1 << 0)
#define ULPI_OTG_CTRL_DP_PULLDOWN		(1 << 1)
#define ULPI_OTG_CTRL_DM_PULLDOWN		(1 << 2)
#define ULPI_OTG_CTRL_DISCHRGVBUS		(1 << 3)
#define ULPI_OTG_CTRL_CHRGVBUS			(1 << 4)
#define ULPI_OTG_CTRL_DRVVBUS			(1 << 5)
#define ULPI_OTG_CTRL_DRVVBUS_EXT		(1 << 6)
#define ULPI_OTG_CTRL_EXTVBUSIND		(1 << 7)

/* USB Interrupt Enable Rising,
 * USB Interrupt Enable Falling,
 * USB Interrupt Status and
 * USB Interrupt Latch
 */
#define ULPI_INT_HOST_DISCONNECT		(1 << 0)
#define ULPI_INT_VBUS_VALID			(1 << 1)
#define ULPI_INT_SESS_VALID			(1 << 2)
#define ULPI_INT_SESS_END			(1 << 3)
#define ULPI_INT_IDGRD				(1 << 4)

/* Debug */
#define ULPI_DEBUG_LINESTATE0			(1 << 0)
#define ULPI_DEBUG_LINESTATE1			(1 << 1)

/* Carkit Control */
#define ULPI_CARKIT_CTRL_CARKITPWR		(1 << 0)
#define ULPI_CARKIT_CTRL_IDGNDDRV		(1 << 1)
#define ULPI_CARKIT_CTRL_TXDEN			(1 << 2)
#define ULPI_CARKIT_CTRL_RXDEN			(1 << 3)
#define ULPI_CARKIT_CTRL_SPKLEFTEN		(1 << 4)
#define ULPI_CARKIT_CTRL_SPKRIGHTEN		(1 << 5)
#define ULPI_CARKIT_CTRL_MICEN			(1 << 6)

/* Carkit Interrupt Enable */
#define ULPI_CARKIT_INT_EN_IDFLOAT_RISE		(1 << 0)
#define ULPI_CARKIT_INT_EN_IDFLOAT_FALL		(1 << 1)
#define ULPI_CARKIT_INT_EN_CARINTDET		(1 << 2)
#define ULPI_CARKIT_INT_EN_DP_RISE		(1 << 3)
#define ULPI_CARKIT_INT_EN_DP_FALL		(1 << 4)

/* Carkit Interrupt Status and
 * Carkit Interrupt Latch
 */
#define ULPI_CARKIT_INT_IDFLOAT			(1 << 0)
#define ULPI_CARKIT_INT_CARINTDET		(1 << 1)
#define ULPI_CARKIT_INT_DP			(1 << 2)

/* Carkit Pulse Control*/
#define ULPI_CARKIT_PLS_CTRL_TXPLSEN		(1 << 0)
#define ULPI_CARKIT_PLS_CTRL_RXPLSEN		(1 << 1)
#define ULPI_CARKIT_PLS_CTRL_SPKRLEFT_BIASEN	(1 << 2)
#define ULPI_CARKIT_PLS_CTRL_SPKRRIGHT_BIASEN	(1 << 3)

/*-------------------------------------------------------------------------*/

struct otg_transceiver *otg_ulpi_create(struct otg_io_access_ops *ops,
					unsigned int flags);

#endif /* __LINUX_USB_ULPI_H */
