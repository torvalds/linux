/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 M. Warner Losh.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_sio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/tty.h>
#include <machine/bus.h>
#include <sys/timepps.h>

#include <dev/sio/siovar.h>

#include <isa/isareg.h>
#include <isa/isavar.h>

static	int	sio_isa_attach(device_t dev);
static	int	sio_isa_probe(device_t dev);

static device_method_t sio_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sio_isa_probe),
	DEVMETHOD(device_attach,	sio_isa_attach),
	DEVMETHOD(device_detach,	siodetach),

	{ 0, 0 }
};

static driver_t sio_isa_driver = {
	sio_driver_name,
	sio_isa_methods,
	0,
};

static struct isa_pnp_id sio_ids[] = {
	{0x0005d041, "Standard PC COM port"},	/* PNP0500 */
	{0x0105d041, "16550A-compatible COM port"},	/* PNP0501 */
	{0x0205d041, "Multiport serial device (non-intelligent 16550)"}, /* PNP0502 */
	{0x1005d041, "Generic IRDA-compatible device"},	/* PNP0510 */
	{0x1105d041, "Generic IRDA-compatible device"},	/* PNP0511 */
	/* Devices that do not have a compatid */
	{0x12206804, NULL},     /* ACH2012 - 5634BTS 56K Video Ready Modem */
	{0x7602a904, NULL},	/* AEI0276 - 56K v.90 Fax Modem (LKT) */
	{0x00007905, NULL},	/* AKY0000 - 56K Plug&Play Modem */
	{0x21107905, NULL},	/* AKY1021 - 56K Plug&Play Modem */
	{0x01405407, NULL},	/* AZT4001 - AZT3000 PnP SOUND DEVICE, MODEM */
	{0x56039008, NULL},	/* BDP0356 - Best Data 56x2 */
	{0x56159008, NULL},	/* BDP1556 - B.D. Smart One 56SPS,Voice Modem*/
	{0x36339008, NULL},	/* BDP3336 - Best Data Prods. 336F */
	{0x0014490a, NULL},	/* BRI1400 - Boca 33.6 PnP */
	{0x0015490a, NULL},	/* BRI1500 - Internal Fax Data */
	{0x0034490a, NULL},	/* BRI3400 - Internal ACF Modem */
	{0x0094490a, NULL},	/* BRI9400 - Boca K56Flex PnP */
	{0x00b4490a, NULL},	/* BRIB400 - Boca 56k PnP */
	{0x0010320d, NULL},	/* CIR1000 - Cirrus Logic V34 */
	{0x0030320d, NULL},	/* CIR3000 - Cirrus Logic V43 */
	{0x0100440e, NULL},	/* CRD0001 - Cardinal MVP288IV ? */
	{0x01308c0e, NULL},	/* CTL3001 - Creative Labs Phoneblaster */
	{0x36033610, NULL},     /* DAV0336 - DAVICOM 336PNP MODEM */
	{0x01009416, NULL},	/* ETT0001 - E-Tech Bullet 33k6 PnP */
	{0x0000aa1a, NULL},	/* FUJ0000 - FUJITSU Modem 33600 PNP/I2 */
	{0x1200c31e, NULL},	/* GVC0012 - VF1128HV-R9 (win modem?) */
	{0x0303c31e, NULL},	/* GVC0303 - MaxTech 33.6 PnP D/F/V */
	{0x0505c31e, NULL},	/* GVC0505 - GVC 56k Faxmodem */
	{0x0116c31e, NULL},	/* GVC1601 - Rockwell V.34 Plug & Play Modem */
	{0x0050c31e, NULL},	/* GVC5000 - some GVC modem */
	{0x3800f91e, NULL},	/* GWY0038 - Telepath with v.90 */
	{0x9062f91e, NULL},	/* GWY6290 - Telepath with x2 Technology */
	{0x8100e425, NULL},	/* IOD0081 - I-O DATA DEVICE,INC. IFML-560 */
	{0x71004d24, NULL},     /* IBM0071 - IBM ThinkPad 240 IrDA controller*/
	{0x21002534, NULL},	/* MAE0021 - Jetstream Int V.90 56k Voice Series 2*/
	{0x0000f435, NULL},	/* MOT0000 - Motorola ModemSURFR 33.6 Intern */
	{0x5015f435, NULL},	/* MOT1550 - Motorola ModemSURFR 56K Modem */
	{0xf015f435, NULL},	/* MOT15F0 - Motorola VoiceSURFR 56K Modem */
	{0x6045f435, NULL},	/* MOT4560 - Motorola ? */
	{0x61e7a338, NULL},	/* NECE761 - 33.6Modem */
	{0x0160633a, NULL},	/* NSC6001 - National Semi's IrDA Controller*/
 	{0x08804f3f, NULL},	/* OZO8008 - Zoom  (33.6k Modem) */
	{0x0f804f3f, NULL},	/* OZO800f - Zoom 2812 (56k Modem) */
	{0x39804f3f, NULL},	/* OZO8039 - Zoom 56k flex */
	{0x00914f3f, NULL},	/* OZO9100 - Zoom 2919 (K56 Faxmodem) */
	{0x3024a341, NULL},	/* PMC2430 - Pace 56 Voice Internal Modem */
	{0x1000eb49, NULL},	/* ROK0010 - Rockwell ? */
	{0x1200b23d, NULL},     /* RSS0012 - OMRON ME5614ISA */
	{0x5002734a, NULL},	/* RSS0250 - 5614Jx3(G) Internal Modem */
	{0x6202734a, NULL},	/* RSS0262 - 5614Jx3[G] V90+K56Flex Modem */
	{0x1010104d, NULL},	/* SHP1010 - Rockwell 33600bps Modem */
	{0x10f0a34d, NULL},	/* SMCF010 - SMC IrCC*/
	{0xc100ad4d, NULL},	/* SMM00C1 - Leopard 56k PnP */
	{0x9012b04e, NULL},	/* SUP1290 - Supra ? */
	{0x1013b04e, NULL},	/* SUP1310 - SupraExpress 336i PnP */
	{0x8013b04e, NULL},	/* SUP1380 - SupraExpress 288i PnP Voice */
	{0x8113b04e, NULL},	/* SUP1381 - SupraExpress 336i PnP Voice */
	{0x5016b04e, NULL},	/* SUP1650 - Supra 336i Sp Intl */
	{0x7016b04e, NULL},	/* SUP1670 - Supra 336i V+ Intl */
	{0x7420b04e, NULL},	/* SUP2070 - Supra ? */
	{0x8020b04e, NULL},	/* SUP2080 - Supra ? */
	{0x8420b04e, NULL},	/* SUP2084 - SupraExpress 56i PnP */
	{0x7121b04e, NULL},	/* SUP2171 - SupraExpress 56i Sp? */
	{0x8024b04e, NULL},	/* SUP2480 - Supra ? */
	{0x01007256, NULL},	/* USR0001 - U.S. Robotics Inc., Sportster W */
	{0x02007256, NULL},	/* USR0002 - U.S. Robotics Inc. Sportster 33. */
	{0x04007256, NULL},	/* USR0004 - USR Sportster 14.4k */
	{0x06007256, NULL},	/* USR0006 - USR Sportster 33.6k */
	{0x11007256, NULL},	/* USR0011 - USR ? */
	{0x01017256, NULL},	/* USR0101 - USR ? */
	{0x30207256, NULL},	/* USR2030 - U.S.Robotics Inc. Sportster 560 */
	{0x50207256, NULL},	/* USR2050 - U.S.Robotics Inc. Sportster 33. */
	{0x70207256, NULL},	/* USR2070 - U.S.Robotics Inc. Sportster 560 */
	{0x30307256, NULL},	/* USR3030 - U.S. Robotics 56K FAX INT */
	{0x31307256, NULL},	/* USR3031 - U.S. Robotics 56K FAX INT */
	{0x50307256, NULL},	/* USR3050 - U.S. Robotics 56K FAX INT */
	{0x70307256, NULL},	/* USR3070 - U.S. Robotics 56K Voice INT */
	{0x90307256, NULL},	/* USR3090 - USR ? */
	{0x70917256, NULL},	/* USR9170 - U.S. Robotics 56K FAX INT */
	{0x90917256, NULL},	/* USR9190 - USR 56k Voice INT */
	{0x04f0235c, NULL},	/* WACF004 - Wacom Tablet PC Screen*/
	{0x0300695c, NULL},	/* WCI0003 - Fax/Voice/Modem/Speakphone/Asvd */
	{0x01a0896a, NULL},	/* ZTIA001 - Zoom Internal V90 Faxmodem */
	{0x61f7896a, NULL},	/* ZTIF761 - Zoom ComStar 33.6 */
	{0}
};

static int
sio_isa_probe(dev)
	device_t	dev;
{
	/* Check isapnp ids */
	if (ISA_PNP_PROBE(device_get_parent(dev), dev, sio_ids) == ENXIO)
		return (ENXIO);
	return (sioprobe(dev, 0, 0UL, 0));
}

static int
sio_isa_attach(dev)
	device_t	dev;
{
	return (sioattach(dev, 0, 0UL));
}

DRIVER_MODULE(sio, isa, sio_isa_driver, sio_devclass, 0, 0);
#ifndef COM_NO_ACPI
DRIVER_MODULE(sio, acpi, sio_isa_driver, sio_devclass, 0, 0);
#endif
ISA_PNP_INFO(sio_ids);
