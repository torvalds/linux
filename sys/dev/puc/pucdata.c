/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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

/*
 * PCI "universal" communications card driver configuration data (used to
 * match/attach the cards).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/ic/ns16550.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/puc/puc_bus.h>
#include <dev/puc/puc_cfg.h>
#include <dev/puc/puc_bfe.h>

static puc_config_f puc_config_advantech;
static puc_config_f puc_config_amc;
static puc_config_f puc_config_diva;
static puc_config_f puc_config_exar;
static puc_config_f puc_config_exar_pcie;
static puc_config_f puc_config_icbook;
static puc_config_f puc_config_moxa;
static puc_config_f puc_config_oxford_pci954;
static puc_config_f puc_config_oxford_pcie;
static puc_config_f puc_config_quatech;
static puc_config_f puc_config_syba;
static puc_config_f puc_config_siig;
static puc_config_f puc_config_sunix;
static puc_config_f puc_config_timedia;
static puc_config_f puc_config_titan;

const struct puc_cfg puc_pci_devices[] = {
	{   0x0009, 0x7168, 0xffff, 0,
	    "Sunix SUN1889",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x103c, 0x1048, 0x103c, 0x1049,
	    "HP Diva Serial [GSP] Multiport UART - Tosca Console",
	    DEFAULT_RCLK,
	    PUC_PORT_3S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x103c, 0x1048, 0x103c, 0x104a,
	    "HP Diva Serial [GSP] Multiport UART - Tosca Secondary",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x103c, 0x1048, 0x103c, 0x104b,
	    "HP Diva Serial [GSP] Multiport UART - Maestro SP2",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x103c, 0x1048, 0x103c, 0x1223,
	    "HP Diva Serial [GSP] Multiport UART - Superdome Console",
	    DEFAULT_RCLK,
	    PUC_PORT_3S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x103c, 0x1048, 0x103c, 0x1226,
	    "HP Diva Serial [GSP] Multiport UART - Keystone SP2",
	    DEFAULT_RCLK,
	    PUC_PORT_3S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x103c, 0x1048, 0x103c, 0x1282,
	    "HP Diva Serial [GSP] Multiport UART - Everest SP2",
	    DEFAULT_RCLK,
	    PUC_PORT_3S, 0x10, 0, -1,
	    .config_function = puc_config_diva
	},

	{   0x10b5, 0x1076, 0x10b5, 0x1076,
	    "VScom PCI-800",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	{   0x10b5, 0x1077, 0x10b5, 0x1077,
	    "VScom PCI-400",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x10b5, 0x1103, 0x10b5, 0x1103,
	    "VScom PCI-200",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x18, 4, 0,
	},

	/*
	 * Boca Research Turbo Serial 658 (8 serial port) card.
	 * Appears to be the same as Chase Research PLC PCI-FAST8
	 * and Perle PCI-FAST8 Multi-Port serial cards.
	 */
	{   0x10b5, 0x9050, 0x12e0, 0x0021,
	    "Boca Research Turbo Serial 658",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	{   0x10b5, 0x9050, 0x12e0, 0x0031,
	    "Boca Research Turbo Serial 654",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	/*
	 * Dolphin Peripherals 4035 (dual serial port) card.  PLX 9050, with
	 * a seemingly-lame EEPROM setup that puts the Dolphin IDs
	 * into the subsystem fields, and claims that it's a
	 * network/misc (0x02/0x80) device.
	 */
	{   0x10b5, 0x9050, 0xd84d, 0x6808,
	    "Dolphin Peripherals 4035",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x18, 4, 0,
	},

	/*
	 * Dolphin Peripherals 4014 (dual parallel port) card.  PLX 9050, with
	 * a seemingly-lame EEPROM setup that puts the Dolphin IDs
	 * into the subsystem fields, and claims that it's a
	 * network/misc (0x02/0x80) device.
	 */
	{   0x10b5, 0x9050, 0xd84d, 0x6810,
	    "Dolphin Peripherals 4014",
	    0,
	    PUC_PORT_2P, 0x20, 4, 0,
	},

	{   0x10e8, 0x818e, 0xffff, 0,
	    "Applied Micro Circuits 8 Port UART",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x14, -1, -1,
	    .config_function = puc_config_amc
	},

	/*
	 * The following members of the Digi International Neo series are
	 * based on Exar PCI chips, f. e. the 8 port variants on XR17V258IV.
	 * Accordingly, the PCIe versions of these cards incorporate a PLX
	 * PCIe-PCI-bridge.
	 */

	{   0x114f, 0x00b0, 0xffff, 0,
	    "Digi Neo PCI 4 Port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x114f, 0x00b1, 0xffff, 0,
	    "Digi Neo PCI 8 Port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x114f, 0x00f0, 0xffff, 0,
	    "Digi Neo PCIe 8 Port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x114f, 0x00f1, 0xffff, 0,
	    "Digi Neo PCIe 4 Port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x114f, 0x00f2, 0xffff, 0,
	    "Digi Neo PCIe 4 Port RJ45",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x114f, 0x00f3, 0xffff, 0,
	    "Digi Neo PCIe 8 Port RJ45",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x11fe, 0x8010, 0xffff, 0,
	    "Comtrol RocketPort 550/8 RJ11 part A",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8011, 0xffff, 0,
	    "Comtrol RocketPort 550/8 RJ11 part B",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8012, 0xffff, 0,
	    "Comtrol RocketPort 550/8 Octa part A",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8013, 0xffff, 0,
	    "Comtrol RocketPort 550/8 Octa part B",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8014, 0xffff, 0,
	    "Comtrol RocketPort 550/4 RJ45",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8015, 0xffff, 0,
	    "Comtrol RocketPort 550/Quad",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8016, 0xffff, 0,
	    "Comtrol RocketPort 550/16 part A",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8017, 0xffff, 0,
	    "Comtrol RocketPort 550/16 part B",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_12S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8018, 0xffff, 0,
	    "Comtrol RocketPort 550/8 part A",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x11fe, 0x8019, 0xffff, 0,
	    "Comtrol RocketPort 550/8 part B",
	    DEFAULT_RCLK * 4,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	/*
	 * IBM SurePOS 300 Series (481033H) serial ports
	 * Details can be found on the IBM RSS websites
	 */

	{   0x1014, 0x0297, 0xffff, 0,
	    "IBM SurePOS 300 Series (481033H) serial ports",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0
	},

	/*
	 * SIIG Boards.
	 *
	 * SIIG provides documentation for their boards at:
	 * <URL:http://www.siig.com/downloads.asp>
	 */

	{   0x131f, 0x1010, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C550 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1011, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C650 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1012, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C850 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1021, 0xffff, 0,
	    "SIIG Cyber Parallel Dual PCI (10x family)",
	    0,
	    PUC_PORT_2P, 0x18, 8, 0,
	},

	{   0x131f, 0x1030, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C550 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x18, 4, 0,
	},

	{   0x131f, 0x1031, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C650 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x18, 4, 0,
	},

	{   0x131f, 0x1032, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C850 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x18, 4, 0,
	},

	{   0x131f, 0x1034, 0xffff, 0,	/* XXX really? */
	    "SIIG Cyber 2S1P PCI 16C550 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1035, 0xffff, 0,	/* XXX really? */
	    "SIIG Cyber 2S1P PCI 16C650 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1036, 0xffff, 0,	/* XXX really? */
	    "SIIG Cyber 2S1P PCI 16C850 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x18, 4, 0,
	},

	{   0x131f, 0x1050, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C550 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x18, 4, 0,
	},

	{   0x131f, 0x1051, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C650 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x18, 4, 0,
	},

	{   0x131f, 0x1052, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C850 (10x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x18, 4, 0,
	},

	{   0x131f, 0x2010, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C550 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2011, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2012, 0xffff, 0,
	    "SIIG Cyber I/O PCI 16C850 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2021, 0xffff, 0,
	    "SIIG Cyber Parallel Dual PCI (20x family)",
	    0,
	    PUC_PORT_2P, 0x10, 8, 0,
	},

	{   0x131f, 0x2030, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C550 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x131f, 0x2031, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x131f, 0x2032, 0xffff, 0,
	    "SIIG Cyber Serial Dual PCI 16C850 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x131f, 0x2040, 0xffff, 0,
	    "SIIG Cyber 2P1S PCI 16C550 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S2P, 0x10, -1, 0,
	    .config_function = puc_config_siig
	},

	{   0x131f, 0x2041, 0xffff, 0,
	    "SIIG Cyber 2P1S PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S2P, 0x10, -1, 0,
	    .config_function = puc_config_siig
	},

	{   0x131f, 0x2042, 0xffff, 0,
	    "SIIG Cyber 2P1S PCI 16C850 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_1S2P, 0x10, -1, 0,
	    .config_function = puc_config_siig
	},

	{   0x131f, 0x2050, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C550 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x131f, 0x2051, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x131f, 0x2052, 0xffff, 0,
	    "SIIG Cyber 4S PCI 16C850 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x131f, 0x2060, 0xffff, 0,
	    "SIIG Cyber 2S1P PCI 16C550 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2061, 0xffff, 0,
	    "SIIG Cyber 2S1P PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2062, 0xffff, 0,
	    "SIIG Cyber 2S1P PCI 16C850 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x131f, 0x2081, 0xffff, 0,
	    "SIIG PS8000 8S PCI 16C650 (20x family)",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x10, -1, -1,
	    .config_function = puc_config_siig
	},

	{   0x135c, 0x0010, 0xffff, 0,
	    "Quatech QSC-100",
	    -3,	/* max 8x clock rate */
	    PUC_PORT_4S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0020, 0xffff, 0,
	    "Quatech DSC-100",
	    -1, /* max 2x clock rate */
	    PUC_PORT_2S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0030, 0xffff, 0,
	    "Quatech DSC-200/300",
	    -1, /* max 2x clock rate */
	    PUC_PORT_2S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0040, 0xffff, 0,
	    "Quatech QSC-200/300",
	    -3, /* max 8x clock rate */
	    PUC_PORT_4S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0050, 0xffff, 0,
	    "Quatech ESC-100D",
	    -3, /* max 8x clock rate */
	    PUC_PORT_8S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0060, 0xffff, 0,
	    "Quatech ESC-100M",
	    -3, /* max 8x clock rate */
	    PUC_PORT_8S, 0x14, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0170, 0xffff, 0,
	    "Quatech QSCLP-100",
	    -1, /* max 2x clock rate */
	    PUC_PORT_4S, 0x18, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x0180, 0xffff, 0,
	    "Quatech DSCLP-100",
	    -1, /* max 3x clock rate */
	    PUC_PORT_2S, 0x18, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x01b0, 0xffff, 0,
	    "Quatech DSCLP-200/300",
	    -1, /* max 2x clock rate */
	    PUC_PORT_2S, 0x18, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x135c, 0x01e0, 0xffff, 0,
	    "Quatech ESCLP-100",
	    -3, /* max 8x clock rate */
	    PUC_PORT_8S, 0x10, 0, 8,
	    .config_function = puc_config_quatech
	},

	{   0x1393, 0x1024, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-102E/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x1393, 0x1025, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-102EL/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x1393, 0x1040, 0xffff, 0,
	    "Moxa Technologies, Smartio C104H/PCI",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x1393, 0x1041, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-104UL/PCI",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x1393, 0x1042, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-104JU/PCI",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x1393, 0x1043, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-104EL/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x1393, 0x1045, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-104EL-A/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x1393, 0x1120, 0xffff, 0,
	    "Moxa Technologies, CP-112UL",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x18, 0, 8,
	},

	{   0x1393, 0x1141, 0xffff, 0,
	    "Moxa Technologies, Industio CP-114",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x18, 0, 8,
	},

	{   0x1393, 0x1144, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-114EL/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x1393, 0x1182, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-118EL-A/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x1393, 0x1680, 0xffff, 0,
	    "Moxa Technologies, C168H/PCI",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	{   0x1393, 0x1681, 0xffff, 0,
	    "Moxa Technologies, C168U/PCI",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	{   0x1393, 0x1682, 0xffff, 0,
	    "Moxa Technologies, CP-168EL/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	{   0x1393, 0x1683, 0xffff, 0,
	    "Moxa Technologies, Smartio CP-168EL-A/PCIe",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x14, 0, -1,
	    .config_function = puc_config_moxa
	},

	{   0x13a8, 0x0152, 0xffff, 0,
	    "Exar XR17C/D152",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x13a8, 0x0154, 0xffff, 0,
	    "Exar XR17C154",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x13a8, 0x0158, 0xffff, 0,
	    "Exar XR17C158",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x13a8, 0x0258, 0xffff, 0,
	    "Exar XR17V258IV",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar
	},

	{   0x13a8, 0x0352, 0xffff, 0,
	    "Exar XR17V352",
	    125000000,
	    PUC_PORT_2S, 0x10, 0, -1,
	    .config_function = puc_config_exar_pcie
	},

	/* The XR17V358 uses the 125MHz PCIe clock as its reference clock. */
	{   0x13a8, 0x0358, 0xffff, 0,
	    "Exar XR17V358",
	    125000000,
	    PUC_PORT_8S, 0x10, 0, -1,
	    .config_function = puc_config_exar_pcie
	},

	/*
	 * The Advantech PCI-1602 Rev. A use the first two ports of an Oxford
	 * Semiconductor OXuPCI954.  Note these boards have a hardware bug in
	 * that they drive the RS-422/485 transmitters after power-on until a
	 * driver initalizes the UARTs.
	 */
	{   0x13fe, 0x1600, 0x1602, 0x0002,
	    "Advantech PCI-1602 Rev. A",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	    .config_function = puc_config_advantech
	},

	/* Advantech PCI-1602 Rev. B1/PCI-1603 are also based on OXuPCI952. */
	{   0x13fe, 0xa102, 0x13fe, 0xa102,
	    "Advantech 2-port PCI (PCI-1602 Rev. B1/PCI-1603)",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 4, 0,
	    .config_function = puc_config_advantech
	},

	{   0x1407, 0x0100, 0xffff, 0,
	    "Lava Computers Dual Serial",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1407, 0x0101, 0xffff, 0,
	    "Lava Computers Quatro A",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1407, 0x0102, 0xffff, 0,
	    "Lava Computers Quatro B",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1407, 0x0120, 0xffff, 0,
	    "Lava Computers Quattro-PCI A",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1407, 0x0121, 0xffff, 0,
	    "Lava Computers Quattro-PCI B",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1407, 0x0180, 0xffff, 0,
	    "Lava Computers Octo A",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x1407, 0x0181, 0xffff, 0,
	    "Lava Computers Octo B",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x1409, 0x7268, 0xffff, 0,
	    "Sunix SUN1888",
	    0,
	    PUC_PORT_2P, 0x10, 0, 8,
	},

	{   0x1409, 0x7168, 0xffff, 0,
	    NULL,
	    DEFAULT_RCLK * 8,
	    PUC_PORT_NONSTANDARD, 0x10, -1, -1,
	    .config_function = puc_config_timedia
	},

	/*
	 * Boards with an Oxford Semiconductor chip.
	 *
	 * Oxford Semiconductor provides documentation for their chip at:
	 * <URL:http://www.plxtech.com/products/uart/>
	 *
	 * As sold by Kouwell <URL:http://www.kouwell.com/>.
	 * I/O Flex PCI I/O Card Model-223 with 4 serial and 1 parallel ports.
	 */
	{
	    0x1415, 0x9501, 0x10fc, 0xc070,
	    "I-O DATA RSA-PCI2/R",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x1415, 0x9501, 0x131f, 0x2050,
	    "SIIG Cyber 4 PCI 16550",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9501, 0x131f, 0x2051,
	    "SIIG Cyber 4S PCI 16C650 (20x family)",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9501, 0x131f, 0x2052,
	    "SIIG Quartet Serial 850",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9501, 0x14db, 0x2150,
	    "Kuroutoshikou SERIAL4P-LPPCI2",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9501, 0xffff, 0,
	    "Oxford Semiconductor OX16PCI954 UARTs",
	    0,
	    PUC_PORT_4S, 0x10, 0, 8,
	    .config_function = puc_config_oxford_pci954
	},

	{   0x1415, 0x950a, 0x131f, 0x2030,
	    "SIIG Cyber 2S PCIe",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x1415, 0x950a, 0x131f, 0x2032,
	    "SIIG Cyber Serial Dual PCI 16C850",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x950a, 0x131f, 0x2061,
	    "SIIG Cyber 2SP1 PCIe",
	    DEFAULT_RCLK * 10,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x1415, 0x950a, 0xffff, 0,
	    "Oxford Semiconductor OX16PCI954 UARTs",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9511, 0xffff, 0,
	    "Oxford Semiconductor OX9160/OX16PCI954 UARTs (function 1)",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1415, 0x9521, 0xffff, 0,
	    "Oxford Semiconductor OX16PCI952 UARTs",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x1415, 0x9538, 0xffff, 0,
	    "Oxford Semiconductor OX16PCI958 UARTs",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x18, 0, 8,
	},

	/*
	 * Perle boards use Oxford Semiconductor chips, but they store the
	 * Oxford Semiconductor device ID as a subvendor device ID and use
	 * their own device IDs.
	 */

	{   0x155f, 0x0331, 0xffff, 0,
	    "Perle Ultraport4 Express",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x155f, 0xB012, 0xffff, 0,
	    "Perle Speed2 LE",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x155f, 0xB022, 0xffff, 0,
	    "Perle Speed2 LE",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x155f, 0xB004, 0xffff, 0,
	    "Perle Speed4 LE",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x155f, 0xB008, 0xffff, 0,
	    "Perle Speed8 LE",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x10, 0, 8,
	},


	/*
	 * Oxford Semiconductor PCI Express Expresso family
	 *
	 * Found in many 'native' PCI Express serial boards such as:
	 *
	 * eMegatech MP954ER4 (4 port) and MP958ER8 (8 port)
	 * <URL:http://www.emegatech.com.tw/pdrs232pcie.html>
	 *
	 * Lindy 51189 (4 port)
	 * <URL:http://www.lindy.com> <URL:http://tinyurl.com/lindy-51189>
	 *
	 * StarTech.com PEX4S952 (4 port) and PEX8S952 (8 port)
	 * <URL:http://www.startech.com>
	 */

	{   0x1415, 0xc11b, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe952 1S1P",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc138, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe952 UARTs",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc158, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe952 UARTs",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc15d, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe952 UARTs (function 1)",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc208, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe954 UARTs",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc20d, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe954 UARTs (function 1)",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc308, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe958 UARTs",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x1415, 0xc30d, 0xffff, 0,
	    "Oxford Semiconductor OXPCIe958 UARTs (function 1)",
	    DEFAULT_RCLK * 0x22,
	    PUC_PORT_NONSTANDARD, 0x10, 0, -1,
	    .config_function = puc_config_oxford_pcie
	},

	{   0x14d2, 0x8010, 0xffff, 0,
	    "VScom PCI-100L",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_1S, 0x14, 0, 0,
	},

	{   0x14d2, 0x8020, 0xffff, 0,
	    "VScom PCI-200L",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x14, 4, 0,
	},

	{   0x14d2, 0x8028, 0xffff, 0,
	    "VScom 200Li",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x20, 0, 8,
	},

	/*
	 * VScom (Titan?) PCI-800L.  More modern variant of the
	 * PCI-800.  Uses 6 discrete 16550 UARTs, plus another
	 * two of them obviously implemented as macro cells in
	 * the ASIC.  This causes the weird port access pattern
	 * below, where two of the IO port ranges each access
	 * one of the ASIC UARTs, and a block of IO addresses
	 * access the external UARTs.
	 */
	{   0x14d2, 0x8080, 0xffff, 0,
	    "Titan VScom PCI-800L",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, 0x14, -1, -1,
	    .config_function = puc_config_titan
	},

	/*
	 * VScom PCI-800H. Uses 8 16950 UART, behind a PCI chips that offers
	 * 4 com port on PCI device 0 and 4 on PCI device 1. PCI device 0 has
	 * device ID 3 and PCI device 1 device ID 4.
	 */
	{   0x14d2, 0xa003, 0xffff, 0,
	    "Titan PCI-800H",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x14d2, 0xa004, 0xffff, 0,
	    "Titan PCI-800H",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x14d2, 0xa005, 0xffff, 0,
	    "Titan PCI-200H",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x14d2, 0xe020, 0xffff, 0,
	    "Titan VScom PCI-200HV2",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x14d2, 0xa007, 0xffff, 0,
	    "Titan VScom PCIex-800H",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x14d2, 0xa008, 0xffff, 0,
	    "Titan VScom PCIex-800H",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x14db, 0x2130, 0xffff, 0,
	    "Avlab Technology, PCI IO 2S",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x14db, 0x2150, 0xffff, 0,
	    "Avlab Low Profile PCI 4 Serial",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x14db, 0x2152, 0xffff, 0,
	    "Avlab Low Profile PCI 4 Serial",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x1592, 0x0781, 0xffff, 0,
	    "Syba Tech Ltd. PCI-4S2P-550-ECP",
	    DEFAULT_RCLK,
	    PUC_PORT_4S1P, 0x10, 0, -1,
	    .config_function = puc_config_syba
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0002,
	    "Sunix SER5xxxx 2-port serial",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0004,
	    "Sunix SER5xxxx 4-port serial",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S, 0x10, 0, 8,
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0008,
	    "Sunix SER5xxxx 8-port serial",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_8S, -1, -1, -1,
	    .config_function = puc_config_sunix
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0101,
	    "Sunix MIO5xxxx 1-port serial and 1284 Printer port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_1S1P, -1, -1, -1,
	    .config_function = puc_config_sunix
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0102,
	    "Sunix MIO5xxxx 2-port serial and 1284 Printer port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_2S1P, -1, -1, -1,
	    .config_function = puc_config_sunix
	},

	{   0x1fd4, 0x1999, 0x1fd4, 0x0104,
	    "Sunix MIO5xxxx 4-port serial and 1284 Printer port",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_4S1P, -1, -1, -1,
	    .config_function = puc_config_sunix
	},

	{   0x5372, 0x6872, 0xffff, 0,
	    "Feasso PCI FPP-02 2S1P",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x5372, 0x6873, 0xffff, 0,
	    "Sun 1040 PCI Quad Serial",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x6666, 0x0001, 0xffff, 0,
	    "Decision Computer Inc, PCCOM 4-port serial",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x1c, 0, 8,
	},

	{   0x6666, 0x0002, 0xffff, 0,
	    "Decision Computer Inc, PCCOM 8-port serial",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x1c, 0, 8,
	},

	{   0x6666, 0x0004, 0xffff, 0,
	    "PCCOM dual port RS232/422/485",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x1c, 0, 8,
	},

	{   0x9710, 0x9815, 0xffff, 0,
	    "NetMos NM9815 Dual 1284 Printer port",
	    0,
	    PUC_PORT_2P, 0x10, 8, 0,
	},

	/*
	 * This is more specific than the generic NM9835 entry, and is placed
	 * here to _prevent_ puc(4) from claiming this single port card.
	 *
	 * uart(4) will claim this device.
	 */
	{   0x9710, 0x9835, 0x1000, 1,
	    "NetMos NM9835 based 1-port serial",
	    DEFAULT_RCLK,
	    PUC_PORT_1S, 0x10, 4, 0,
	},

	{   0x9710, 0x9835, 0x1000, 2,
	    "NetMos NM9835 based 2-port serial",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x9710, 0x9835, 0xffff, 0,
	    "NetMos NM9835 Dual UART and 1284 Printer port",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x9710, 0x9845, 0x1000, 0x0006,
	    "NetMos NM9845 6 Port UART",
	    DEFAULT_RCLK,
	    PUC_PORT_6S, 0x10, 4, 0,
	},

	{   0x9710, 0x9845, 0xffff, 0,
	    "NetMos NM9845 Quad UART and 1284 Printer port",
	    DEFAULT_RCLK,
	    PUC_PORT_4S1P, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3002,
	    "NetMos NM9865 Dual UART",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3003,
	    "NetMos NM9865 Triple UART",
	    DEFAULT_RCLK,
	    PUC_PORT_3S, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3004,
	    "NetMos NM9865 Quad UART",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3011,
	    "NetMos NM9865 Single UART and 1284 Printer port",
	    DEFAULT_RCLK,
	    PUC_PORT_1S1P, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3012,
	    "NetMos NM9865 Dual UART and 1284 Printer port",
	    DEFAULT_RCLK,
	    PUC_PORT_2S1P, 0x10, 4, 0,
	},

	{   0x9710, 0x9865, 0xa000, 0x3020,
	    "NetMos NM9865 Dual 1284 Printer port",
	    DEFAULT_RCLK,
	    PUC_PORT_2P, 0x10, 4, 0,
	},

	{   0xb00c, 0x021c, 0xffff, 0,
	    "IC Book Labs Gunboat x4 Lite",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x031c, 0xffff, 0,
	    "IC Book Labs Gunboat x4 Pro",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x041c, 0xffff, 0,
	    "IC Book Labs Ironclad x8 Lite",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x051c, 0xffff, 0,
	    "IC Book Labs Ironclad x8 Pro",
	    DEFAULT_RCLK,
	    PUC_PORT_8S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x081c, 0xffff, 0,
	    "IC Book Labs Dreadnought x16 Pro",
	    DEFAULT_RCLK * 8,
	    PUC_PORT_16S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x091c, 0xffff, 0,
	    "IC Book Labs Dreadnought x16 Lite",
	    DEFAULT_RCLK,
	    PUC_PORT_16S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{   0xb00c, 0x0a1c, 0xffff, 0,
	    "IC Book Labs Gunboat x2 Low Profile",
	    DEFAULT_RCLK,
	    PUC_PORT_2S, 0x10, 0, 8,
	},

	{   0xb00c, 0x0b1c, 0xffff, 0,
	    "IC Book Labs Gunboat x4 Low Profile",
	    DEFAULT_RCLK,
	    PUC_PORT_4S, 0x10, 0, 8,
	    .config_function = puc_config_icbook
	},

	{ 0xffff, 0, 0xffff, 0, NULL, 0 }
};

static int
puc_config_advantech(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res __unused)
{
	const struct puc_cfg *cfg;
	struct resource *cres;
	struct puc_bar *bar;
	device_t cdev, dev;
	bus_size_t off;
	int base, crtype, fixed, high, i, oxpcie;
	uint8_t acr, func, mask;

	if (cmd != PUC_CFG_SETUP)
		return (ENXIO);

	base = fixed = oxpcie = 0;
	crtype = SYS_RES_IOPORT;
	acr = mask = 0x0;
	func = high = 1;
	off = 0x60;

	cfg = sc->sc_cfg;
	switch (cfg->subvendor) {
	case 0x13fe:
		switch (cfg->device) {
		case 0xa102:
			high = 0;
			break;
		default:
			break;
		}
	default:
		break;
	}
	if (fixed == 1)
		goto setup;

	dev = sc->sc_dev;
	cdev = pci_find_dbsf(pci_get_domain(dev), pci_get_bus(dev),
	    pci_get_slot(dev), func);
	if (cdev == NULL) {
		device_printf(dev, "could not find config function\n");
		return (ENXIO);
	}

	i = PCIR_BAR(0);
	cres = bus_alloc_resource_any(cdev, crtype, &i, RF_ACTIVE);
	if (cres == NULL) {
		device_printf(dev, "could not allocate config resource\n");
		return (ENXIO);
	}

	if (oxpcie == 0) {
		mask = bus_read_1(cres, off);
		if (pci_get_function(dev) == 1)
			base = 4;
	}

 setup:
	for (i = 0; i < sc->sc_nports; ++i) {
		device_printf(dev, "port %d: ", i);
		bar = puc_get_bar(sc, cfg->rid + i * cfg->d_rid);
		if (bar == NULL) {
			printf("could not get BAR\n");
			continue;
		}

		if (fixed == 0) {
			if ((mask & (1 << (base + i))) == 0) {
				acr = 0;
				printf("RS-232\n");
			} else {
				acr = (high == 1 ? 0x18 : 0x10);
				printf("RS-422/RS-485, active-%s auto-DTR\n",
				    high == 1 ? "high" : "low");
			}
		}

		bus_write_1(bar->b_res, REG_SPR, REG_ACR);
		bus_write_1(bar->b_res, REG_ICR, acr);
	}

	bus_release_resource(cdev, crtype, rman_get_rid(cres), cres);
	return (0);
}

static int
puc_config_amc(struct puc_softc *sc __unused, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{

	switch (cmd) {
	case PUC_CFG_GET_OFS:
		*res = 8 * (port & 1);
		return (0);
	case PUC_CFG_GET_RID:
		*res = 0x14 + (port >> 1) * 4;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_diva(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	const struct puc_cfg *cfg = sc->sc_cfg;

	if (cmd == PUC_CFG_GET_OFS) {
		if (cfg->subdevice == 0x1282)		/* Everest SP */
			port <<= 1;
		else if (cfg->subdevice == 0x104b)	/* Maestro SP2 */
			port = (port == 3) ? 4 : port;
		*res = port * 8 + ((port > 2) ? 0x18 : 0);
		return (0);
	}
	return (ENXIO);
}

static int
puc_config_exar(struct puc_softc *sc __unused, enum puc_cfg_cmd cmd,
    int port, intptr_t *res)
{

	if (cmd == PUC_CFG_GET_OFS) {
		*res = port * 0x200;
		return (0);
	}
	return (ENXIO);
}

static int
puc_config_exar_pcie(struct puc_softc *sc __unused, enum puc_cfg_cmd cmd,
    int port, intptr_t *res)
{

	if (cmd == PUC_CFG_GET_OFS) {
		*res = port * 0x400;
		return (0);
	}
	return (ENXIO);
}

static int
puc_config_icbook(struct puc_softc *sc __unused, enum puc_cfg_cmd cmd,
    int port __unused, intptr_t *res)
{

	if (cmd == PUC_CFG_GET_ILR) {
		*res = PUC_ILR_DIGI;
		return (0);
	}
	return (ENXIO);
}

static int
puc_config_moxa(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	const struct puc_cfg *cfg = sc->sc_cfg;

	if (cmd == PUC_CFG_GET_OFS) {
		if (port == 3 && (cfg->device == 0x1045 ||
		    cfg->device == 0x1144))
			port = 7;
		*res = port * 0x200;

		return 0;
	}
	return (ENXIO);
}

static int
puc_config_quatech(struct puc_softc *sc, enum puc_cfg_cmd cmd,
    int port __unused, intptr_t *res)
{
	const struct puc_cfg *cfg = sc->sc_cfg;
	struct puc_bar *bar;
	uint8_t v0, v1;

	switch (cmd) {
	case PUC_CFG_SETUP:
		/*
		 * Check if the scratchpad register is enabled or if the
		 * interrupt status and options registers are active.
		 */
		bar = puc_get_bar(sc, cfg->rid);
		if (bar == NULL)
			return (ENXIO);
		bus_write_1(bar->b_res, REG_LCR, LCR_DLAB);
		bus_write_1(bar->b_res, REG_SPR, 0);
		v0 = bus_read_1(bar->b_res, REG_SPR);
		bus_write_1(bar->b_res, REG_SPR, 0x80 + -cfg->clock);
		v1 = bus_read_1(bar->b_res, REG_SPR);
		bus_write_1(bar->b_res, REG_LCR, 0);
		sc->sc_cfg_data = (v0 << 8) | v1;
		if (v0 == 0 && v1 == 0x80 + -cfg->clock) {
			/*
			 * The SPR register echoed the two values written
			 * by us.  This means that the SPAD jumper is set.
			 */
			device_printf(sc->sc_dev, "warning: extra features "
			    "not usable -- SPAD compatibility enabled\n");
			return (0);
		}
		if (v0 != 0) {
			/*
			 * The first value doesn't match.  This can only mean
			 * that the SPAD jumper is not set and that a non-
			 * standard fixed clock multiplier jumper is set.
			 */
			if (bootverbose)
				device_printf(sc->sc_dev, "fixed clock rate "
				    "multiplier of %d\n", 1 << v0);
			if (v0 < -cfg->clock)
				device_printf(sc->sc_dev, "warning: "
				    "suboptimal fixed clock rate multiplier "
				    "setting\n");
			return (0);
		}
		/*
		 * The first value matched, but the second didn't.  We know
		 * that the SPAD jumper is not set.  We also know that the
		 * clock rate multiplier is software controlled *and* that
		 * we just programmed it to the maximum allowed.
		 */
		if (bootverbose)
			device_printf(sc->sc_dev, "clock rate multiplier of "
			    "%d selected\n", 1 << -cfg->clock);
		return (0);
	case PUC_CFG_GET_CLOCK:
		v0 = (sc->sc_cfg_data >> 8) & 0xff;
		v1 = sc->sc_cfg_data & 0xff;
		if (v0 == 0 && v1 == 0x80 + -cfg->clock) {
			/*
			 * XXX With the SPAD jumper applied, there's no
			 * easy way of knowing if there's also a clock
			 * rate multiplier jumper installed.  Let's hope
			 * not ...
			 */
			*res = DEFAULT_RCLK;
		} else if (v0 == 0) {
			/*
			 * No clock rate multiplier jumper installed,
			 * so we programmed the board with the maximum
			 * multiplier allowed as given to us in the
			 * clock field of the config record (negated).
			 */
			*res = DEFAULT_RCLK << -cfg->clock;
		} else
			*res = DEFAULT_RCLK << v0;
		return (0);
	case PUC_CFG_GET_ILR:
		v0 = (sc->sc_cfg_data >> 8) & 0xff;
		v1 = sc->sc_cfg_data & 0xff;
		*res = (v0 == 0 && v1 == 0x80 + -cfg->clock) ?
		    PUC_ILR_NONE : PUC_ILR_QUATECH;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_syba(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	static int base[] = { 0x251, 0x3f0, 0 };
	const struct puc_cfg *cfg = sc->sc_cfg;
	struct puc_bar *bar;
	int efir, idx, ofs;
	uint8_t v;

	switch (cmd) {
	case PUC_CFG_SETUP:
		bar = puc_get_bar(sc, cfg->rid);
		if (bar == NULL)
			return (ENXIO);

		/* configure both W83877TFs */
		bus_write_1(bar->b_res, 0x250, 0x89);
		bus_write_1(bar->b_res, 0x3f0, 0x87);
		bus_write_1(bar->b_res, 0x3f0, 0x87);
		idx = 0;
		while (base[idx] != 0) {
			efir = base[idx];
			bus_write_1(bar->b_res, efir, 0x09);
			v = bus_read_1(bar->b_res, efir + 1);
			if ((v & 0x0f) != 0x0c)
				return (ENXIO);
			bus_write_1(bar->b_res, efir, 0x16);
			v = bus_read_1(bar->b_res, efir + 1);
			bus_write_1(bar->b_res, efir, 0x16);
			bus_write_1(bar->b_res, efir + 1, v | 0x04);
			bus_write_1(bar->b_res, efir, 0x16);
			bus_write_1(bar->b_res, efir + 1, v & ~0x04);
			ofs = base[idx] & 0x300;
			bus_write_1(bar->b_res, efir, 0x23);
			bus_write_1(bar->b_res, efir + 1, (ofs + 0x78) >> 2);
			bus_write_1(bar->b_res, efir, 0x24);
			bus_write_1(bar->b_res, efir + 1, (ofs + 0xf8) >> 2);
			bus_write_1(bar->b_res, efir, 0x25);
			bus_write_1(bar->b_res, efir + 1, (ofs + 0xe8) >> 2);
			bus_write_1(bar->b_res, efir, 0x17);
			bus_write_1(bar->b_res, efir + 1, 0x03);
			bus_write_1(bar->b_res, efir, 0x28);
			bus_write_1(bar->b_res, efir + 1, 0x43);
			idx++;
		}
		bus_write_1(bar->b_res, 0x250, 0xaa);
		bus_write_1(bar->b_res, 0x3f0, 0xaa);
		return (0);
	case PUC_CFG_GET_OFS:
		switch (port) {
		case 0:
			*res = 0x2f8;
			return (0);
		case 1:
			*res = 0x2e8;
			return (0);
		case 2:
			*res = 0x3f8;
			return (0);
		case 3:
			*res = 0x3e8;
			return (0);
		case 4:
			*res = 0x278;
			return (0);
		}
		break;
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_siig(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	const struct puc_cfg *cfg = sc->sc_cfg;

	switch (cmd) {
	case PUC_CFG_GET_OFS:
		if (cfg->ports == PUC_PORT_8S) {
			*res = (port > 4) ? 8 * (port - 4) : 0;
			return (0);
		}
		break;
	case PUC_CFG_GET_RID:
		if (cfg->ports == PUC_PORT_8S) {
			*res = 0x10 + ((port > 4) ? 0x10 : 4 * port);
			return (0);
		}
		if (cfg->ports == PUC_PORT_2S1P) {
			switch (port) {
			case 0: *res = 0x10; return (0);
			case 1: *res = 0x14; return (0);
			case 2: *res = 0x1c; return (0);
			}
		}
		break;
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_timedia(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	static const uint16_t dual[] = {
	    0x0002, 0x4036, 0x4037, 0x4038, 0x4078, 0x4079, 0x4085,
	    0x4088, 0x4089, 0x5037, 0x5078, 0x5079, 0x5085, 0x6079,
	    0x7079, 0x8079, 0x8137, 0x8138, 0x8237, 0x8238, 0x9079,
	    0x9137, 0x9138, 0x9237, 0x9238, 0xA079, 0xB079, 0xC079,
	    0xD079, 0
	};
	static const uint16_t quad[] = {
	    0x4055, 0x4056, 0x4095, 0x4096, 0x5056, 0x8156, 0x8157,
	    0x8256, 0x8257, 0x9056, 0x9156, 0x9157, 0x9158, 0x9159,
	    0x9256, 0x9257, 0xA056, 0xA157, 0xA158, 0xA159, 0xB056,
	    0xB157, 0
	};
	static const uint16_t octa[] = {
	    0x4065, 0x4066, 0x5065, 0x5066, 0x8166, 0x9066, 0x9166,
	    0x9167, 0x9168, 0xA066, 0xA167, 0xA168, 0
	};
	static const struct {
		int ports;
		const uint16_t *ids;
	} subdevs[] = {
	    { 2, dual },
	    { 4, quad },
	    { 8, octa },
	    { 0, NULL }
	};
	static char desc[64];
	int dev, id;
	uint16_t subdev;

	switch (cmd) {
	case PUC_CFG_GET_CLOCK:
		if (port < 2)
			*res = DEFAULT_RCLK * 8;
		else
			*res = DEFAULT_RCLK;
		return (0);
	case PUC_CFG_GET_DESC:
		snprintf(desc, sizeof(desc),
		    "Timedia technology %d Port Serial", (int)sc->sc_cfg_data);
		*res = (intptr_t)desc;
		return (0);
	case PUC_CFG_GET_NPORTS:
		subdev = pci_get_subdevice(sc->sc_dev);
		dev = 0;
		while (subdevs[dev].ports != 0) {
			id = 0;
			while (subdevs[dev].ids[id] != 0) {
				if (subdev == subdevs[dev].ids[id]) {
					sc->sc_cfg_data = subdevs[dev].ports;
					*res = sc->sc_cfg_data;
					return (0);
				}
				id++;
			}
			dev++;
		}
		return (ENXIO);
	case PUC_CFG_GET_OFS:
		*res = (port == 1 || port == 3) ? 8 : 0;
		return (0);
	case PUC_CFG_GET_RID:
		*res = 0x10 + ((port > 3) ? port - 2 : port >> 1) * 4;
		return (0);
	case PUC_CFG_GET_TYPE:
		*res = PUC_TYPE_SERIAL;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_oxford_pci954(struct puc_softc *sc, enum puc_cfg_cmd cmd,
    int port __unused, intptr_t *res)
{

	switch (cmd) {
	case PUC_CFG_GET_CLOCK:
		/*
		 * OXu16PCI954 use a 14.7456 MHz clock by default while
		 * OX16PCI954 and OXm16PCI954 employ a 1.8432 MHz one.
		 */
		if (pci_get_revid(sc->sc_dev) == 1)
			*res = DEFAULT_RCLK * 8;
		else
			*res = DEFAULT_RCLK;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_oxford_pcie(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	const struct puc_cfg *cfg = sc->sc_cfg;
	int idx;
	struct puc_bar *bar;
	uint8_t value;

	switch (cmd) {
	case PUC_CFG_SETUP:
		device_printf(sc->sc_dev, "%d UARTs detected\n",
			sc->sc_nports);

		/* Set UARTs to enhanced mode */
		bar = puc_get_bar(sc, cfg->rid);
		if (bar == NULL)
			return (ENXIO);
		for (idx = 0; idx < sc->sc_nports; idx++) {
			value = bus_read_1(bar->b_res, 0x1000 + (idx << 9) +
			    0x92);
			bus_write_1(bar->b_res, 0x1000 + (idx << 9) + 0x92,
			    value | 0x10);
		}
		return (0);
	case PUC_CFG_GET_LEN:
		*res = 0x200;
		return (0);
	case PUC_CFG_GET_NPORTS:
		/*
		 * Check if we are being called from puc_bfe_attach()
		 * or puc_bfe_probe().  If puc_bfe_probe(), we cannot
		 * puc_get_bar(), so we return a value of 16.  This has
		 * cosmetic side-effects at worst; in PUC_CFG_GET_DESC,
		 * sc->sc_cfg_data will not contain the true number of
		 * ports in PUC_CFG_GET_DESC, but we are not implementing
		 * that call for this device family anyway.
		 *
		 * The check is for initialization of sc->sc_bar[idx],
		 * which is only done in puc_bfe_attach().
		 */
		idx = 0;
		do {
			if (sc->sc_bar[idx++].b_rid != -1) {
				sc->sc_cfg_data = 16;
				*res = sc->sc_cfg_data;
				return (0);
			}
		} while (idx < PUC_PCI_BARS);

		bar = puc_get_bar(sc, cfg->rid);
		if (bar == NULL)
			return (ENXIO);

		value = bus_read_1(bar->b_res, 0x04);
		if (value == 0)
			return (ENXIO);

		sc->sc_cfg_data = value;
		*res = sc->sc_cfg_data;
		return (0);
	case PUC_CFG_GET_OFS:
		*res = 0x1000 + (port << 9);
		return (0);
	case PUC_CFG_GET_TYPE:
		*res = PUC_TYPE_SERIAL;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_sunix(struct puc_softc *sc, enum puc_cfg_cmd cmd, int port,
    intptr_t *res)
{
	int error;

	switch (cmd) {
	case PUC_CFG_GET_OFS:
		error = puc_config(sc, PUC_CFG_GET_TYPE, port, res);
		if (error != 0)
			return (error);
		*res = (*res == PUC_TYPE_SERIAL) ? (port & 3) * 8 : 0;
		return (0);
	case PUC_CFG_GET_RID:
		error = puc_config(sc, PUC_CFG_GET_TYPE, port, res);
		if (error != 0)
			return (error);
		*res = (*res == PUC_TYPE_SERIAL && port <= 3) ? 0x10 : 0x14;
		return (0);
	default:
		break;
	}
	return (ENXIO);
}

static int
puc_config_titan(struct puc_softc *sc __unused, enum puc_cfg_cmd cmd,
    int port, intptr_t *res)
{

	switch (cmd) {
	case PUC_CFG_GET_OFS:
		*res = (port < 3) ? 0 : (port - 2) << 3;
		return (0);
	case PUC_CFG_GET_RID:
		*res = 0x14 + ((port >= 2) ? 0x0c : port << 2);
		return (0);
	default:
		break;
	}
	return (ENXIO);
}
