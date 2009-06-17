/*
 * SDIO Classes, Interface Types, Manufacturer IDs, etc.
 */

#ifndef MMC_SDIO_IDS_H
#define MMC_SDIO_IDS_H

/*
 * Standard SDIO Function Interfaces
 */

#define SDIO_CLASS_NONE		0x00	/* Not a SDIO standard interface */
#define SDIO_CLASS_UART		0x01	/* standard UART interface */
#define SDIO_CLASS_BT_A		0x02	/* Type-A BlueTooth std interface */
#define SDIO_CLASS_BT_B		0x03	/* Type-B BlueTooth std interface */
#define SDIO_CLASS_GPS		0x04	/* GPS standard interface */
#define SDIO_CLASS_CAMERA	0x05	/* Camera standard interface */
#define SDIO_CLASS_PHS		0x06	/* PHS standard interface */
#define SDIO_CLASS_WLAN		0x07	/* WLAN interface */
#define SDIO_CLASS_ATA		0x08	/* Embedded SDIO-ATA std interface */

/*
 * Vendors and devices.  Sort key: vendor first, device next.
 */

#define SDIO_VENDOR_ID_MARVELL			0x02df
#define SDIO_DEVICE_ID_MARVELL_LIBERTAS		0x9103
#define SDIO_DEVICE_ID_MARVELL_8688WLAN		0x9104
#define SDIO_DEVICE_ID_MARVELL_8688BT		0x9105

#define SDIO_VENDOR_ID_SIANO			0x039a
#define SDIO_DEVICE_ID_SIANO_NOVA_B0		0x0201
#define SDIO_DEVICE_ID_SIANO_NICE		0x0202
#define SDIO_DEVICE_ID_SIANO_VEGA_A0		0x0300
#define SDIO_DEVICE_ID_SIANO_VENICE		0x0301
#define SDIO_DEVICE_ID_SIANO_NOVA_A0		0x1100
#define SDIO_DEVICE_ID_SIANO_STELLAR 		0x5347

#endif
