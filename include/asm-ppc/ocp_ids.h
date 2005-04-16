/*
 * ocp_ids.h
 *
 * OCP device ids based on the ideas from PCI
 *
 * The numbers below are almost completely arbitrary, and in fact
 * strings might work better.  -- paulus
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * Vender  device
 * [xxxx]  [xxxx]
 *
 *  Keep in order, please
 */

/* Vendor IDs 0x0001 - 0xFFFF copied from pci_ids.h */

#define	OCP_VENDOR_INVALID	0x0000
#define	OCP_VENDOR_ARM		0x0004
#define OCP_VENDOR_FREESCALE	0x1057
#define OCP_VENDOR_IBM		0x1014
#define OCP_VENDOR_MOTOROLA	OCP_VENDOR_FREESCALE
#define	OCP_VENDOR_XILINX	0x10ee
#define	OCP_VENDOR_UNKNOWN	0xFFFF

/* device identification */

/* define type */
#define OCP_FUNC_INVALID	0x0000

/* system 0x0001 - 0x001F */

/* Timers 0x0020 - 0x002F */

/* Serial 0x0030 - 0x006F*/
#define OCP_FUNC_16550		0x0031
#define OCP_FUNC_IIC		0x0032
#define OCP_FUNC_USB		0x0033
#define OCP_FUNC_PSC_UART	0x0034

/* Memory devices 0x0090 - 0x009F */
#define OCP_FUNC_MAL		0x0090
#define OCP_FUNC_DMA		0x0091

/* Display 0x00A0 - 0x00AF */

/* Sound 0x00B0 - 0x00BF */

/* Mass Storage 0x00C0 - 0xxCF */
#define OCP_FUNC_IDE		0x00C0

/* Misc 0x00D0 - 0x00DF*/
#define OCP_FUNC_GPIO		0x00D0
#define OCP_FUNC_ZMII		0x00D1
#define OCP_FUNC_PERFMON	0x00D2	/* Performance Monitor */
#define OCP_FUNC_RGMII		0x00D3
#define OCP_FUNC_TAH		0x00D4
#define OCP_FUNC_SEC2		0x00D5	/* Crypto/Security 2.0 */

/* Network 0x0200 - 0x02FF */
#define OCP_FUNC_EMAC		0x0200
#define OCP_FUNC_GFAR		0x0201	/* TSEC & FEC */

/* Bridge devices 0xE00 - 0xEFF */
#define OCP_FUNC_OPB		0x0E00

#define OCP_FUNC_UNKNOWN	0xFFFF
