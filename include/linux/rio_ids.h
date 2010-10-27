/*
 * RapidIO devices
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef LINUX_RIO_IDS_H
#define LINUX_RIO_IDS_H

#define RIO_ANY_ID			0xffff

#define RIO_VID_FREESCALE		0x0002
#define RIO_DID_MPC8560			0x0003

#define RIO_VID_TUNDRA			0x000d
#define RIO_DID_TSI500			0x0500
#define RIO_DID_TSI568			0x0568
#define RIO_DID_TSI572			0x0572
#define RIO_DID_TSI574			0x0574
#define RIO_DID_TSI576			0x0578 /* Same ID as Tsi578 */
#define RIO_DID_TSI577			0x0577
#define RIO_DID_TSI578			0x0578

#define RIO_VID_IDT			0x0038
#define RIO_DID_IDT70K200		0x0310
#define RIO_DID_IDTCPS8			0x035c
#define RIO_DID_IDTCPS12		0x035d
#define RIO_DID_IDTCPS16		0x035b
#define RIO_DID_IDTCPS6Q		0x035f
#define RIO_DID_IDTCPS10Q		0x035e

#endif				/* LINUX_RIO_IDS_H */
