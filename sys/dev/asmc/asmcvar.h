/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007, 2008 Rui Paulo <rpaulo@FreeBSD.org>
 * All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#define ASMC_MAXFANS	6

struct asmc_softc {
	device_t 		sc_dev;
	struct mtx 		sc_mtx;
	int 			sc_nfan;
	int16_t			sms_rest_x;
	int16_t			sms_rest_y;
	int16_t			sms_rest_z;
	struct sysctl_oid 	*sc_fan_tree[ASMC_MAXFANS+1];
	struct sysctl_oid 	*sc_temp_tree;
	struct sysctl_oid 	*sc_sms_tree;
	struct sysctl_oid 	*sc_light_tree;
	struct asmc_model 	*sc_model;
	int 			sc_rid_port;
	int 			sc_rid_irq;
	struct resource 	*sc_ioport;
	struct resource 	*sc_irq;
	void 			*sc_cookie;
	int 			sc_sms_intrtype;
	struct taskqueue 	*sc_sms_tq;
	struct task 		sc_sms_task;
	uint8_t			sc_sms_intr_works;
};

/*
 * Data port.
 */
#define ASMC_DATAPORT_READ(sc)	bus_read_1(sc->sc_ioport, 0x00)
#define ASMC_DATAPORT_WRITE(sc, val) \
	bus_write_1(sc->sc_ioport, 0x00, val)
#define ASMC_STATUS_MASK 	0x0f

/*
 * Command port.
 */
#define ASMC_CMDPORT_READ(sc)	bus_read_1(sc->sc_ioport, 0x04)
#define ASMC_CMDPORT_WRITE(sc, val) \
	bus_write_1(sc->sc_ioport, 0x04, val)
#define ASMC_CMDREAD		0x10
#define ASMC_CMDWRITE		0x11

/*
 * Interrupt port.
 */
#define ASMC_INTPORT_READ(sc)	bus_read_1(sc->sc_ioport, 0x1f)


/* Number of keys */
#define ASMC_NKEYS		"#KEY"	/* RO; 4 bytes */

/*
 * Fan control via SMC.
 */
#define ASMC_KEY_FANCOUNT	"FNum"	/* RO; 1 byte */
#define ASMC_KEY_FANMANUAL	"FS! "	/* RW; 2 bytes */
#define ASMC_KEY_FANID		"F%dID"	/* RO; 16 bytes */
#define ASMC_KEY_FANSPEED	"F%dAc"	/* RO; 2 bytes */
#define ASMC_KEY_FANMINSPEED	"F%dMn"	/* RO; 2 bytes */
#define ASMC_KEY_FANMAXSPEED	"F%dMx"	/* RO; 2 bytes */
#define ASMC_KEY_FANSAFESPEED	"F%dSf"	/* RO; 2 bytes */
#define ASMC_KEY_FANTARGETSPEED	"F%dTg"	/* RW; 2 bytes */

/*
 * Sudden Motion Sensor (SMS).
 */
#define ASMC_SMS_INIT1		0xe0
#define ASMC_SMS_INIT2		0xf8
#define ASMC_KEY_SMS		"MOCN"	/* RW; 2 bytes */
#define ASMC_KEY_SMS_X		"MO_X"	/* RO; 2 bytes */
#define ASMC_KEY_SMS_Y		"MO_Y"	/* RO; 2 bytes */
#define ASMC_KEY_SMS_Z		"MO_Z"	/* RO; 2 bytes */
#define ASMC_KEY_SMS_LOW	"MOLT"	/* RW; 2 bytes */
#define ASMC_KEY_SMS_HIGH	"MOHT"	/* RW; 2 bytes */
#define ASMC_KEY_SMS_LOW_INT	"MOLD"	/* RW; 1 byte */
#define ASMC_KEY_SMS_HIGH_INT	"MOHD"	/* RW; 1 byte */
#define ASMC_KEY_SMS_FLAG	"MSDW"	/* RW; 1 byte */
#define ASMC_SMS_INTFF		0x60	/* Free fall Interrupt */
#define ASMC_SMS_INTHA		0x6f	/* High Acceleration Interrupt */
#define ASMC_SMS_INTSH		0x80	/* Shock Interrupt */

/*
 * Keyboard backlight.
 */
#define ASMC_KEY_LIGHTLEFT	"ALV0"	/* RO; 6 bytes */
#define ASMC_KEY_LIGHTRIGHT	"ALV1"	/* RO; 6 bytes */
#define ASMC_KEY_LIGHTVALUE	"LKSB"	/* WO; 2 bytes */

/*
 * Clamshell.
 */
#define ASMC_KEY_CLAMSHELL	"MSLD"	/* RO; 1 byte */

/*
 * Interrupt keys.
 */
#define ASMC_KEY_INTOK		"NTOK"	/* WO; 1 byte */

/*
 * Temperatures.
 *
 * First for MacBook, second for MacBook Pro, third for Intel Mac Mini,
 * fourth the Mac Pro 8-core and finally the MacBook Air.
 *
 */
/* maximum array size for temperatures including the last NULL */
#define ASMC_TEMP_MAX		80
#define ASMC_MB_TEMPS		{ "TB0T", "TN0P", "TN1P", "Th0H", "Th1H", \
				  "TM0P", NULL }
#define ASMC_MB_TEMPNAMES	{ "enclosure", "northbridge1", \
				  "northbridge2", "heatsink1", \
				  "heatsink2", "memory", }
#define ASMC_MB_TEMPDESCS	{ "Enclosure Bottomside", \
				  "Northbridge Point 1", \
				  "Northbridge Point 2", "Heatsink 1", \
				  "Heatsink 2", "Memory Bank A", }

#define ASMC_MB31_TEMPS		{ "TB0T", "TN0P",  "Th0H", "Th1H", \
				  "TM0P", NULL }

#define ASMC_MB31_TEMPNAMES	{ "enclosure", "northbridge1", \
				  "heatsink1", "heatsink2", \
				  "memory", }

#define ASMC_MB31_TEMPDESCS	{ "Enclosure Bottomside", \
				  "Northbridge Point 1", \
				  "Heatsink 1","Heatsink 2" \
				  "Memory Bank A", }

#define ASMC_MBP_TEMPS		{ "TB0T", "Th0H", "Th1H", "Tm0P",	\
				  "TG0H", "TG0P", "TG0T", NULL }

#define ASMC_MBP_TEMPNAMES	{ "enclosure", "heatsink1", \
				  "heatsink2", "memory", "graphics", \
				  "graphicssink", "unknown", }

#define ASMC_MBP_TEMPDESCS	{ "Enclosure Bottomside", \
				  "Heatsink 1", "Heatsink 2", \
				  "Memory Controller", \
				  "Graphics Chip", "Graphics Heatsink", \
				  "Unknown", }

#define ASMC_MBP4_TEMPS		{ "TB0T", "Th0H", "Th1H", "Th2H", "Tm0P", \
				  "TG0H", "TG0D", "TC0D", "TC0P", "Ts0P", \
				  "TTF0", "TW0P", NULL }

#define ASMC_MBP4_TEMPNAMES	{ "enclosure", "heatsink1", "heatsink2", \
				  "heatsink3", "memory", "graphicssink", \
				  "graphics", "cpu", "cpu2", "unknown1", \
				  "unknown2", "wireless", }

#define ASMC_MBP4_TEMPDESCS	{ "Enclosure Bottomside", \
				  "Main Heatsink 1", "Main Heatsink 2", \
				  "Main Heatsink 3", \
				  "Memory Controller", \
				  "Graphics Chip Heatsink", \
				  "Graphics Chip Diode", \
				  "CPU Temperature Diode", "CPU Point 2", \
				  "Unknown", "Unknown", \
				  "Wireless Module", }

#define ASMC_MBP5_TEMPS		{ "TB0T", "TB1T", "TB2T", "TB3T", "TC0D", \
				  "TC0F", "TC0P", "TG0D", "TG0F", "TG0H", \
				  "TG0P", "TG0T", "TG1H", "TN0D", "TN0P", \
				  "TTF0", "Th2H", "Tm0P", "Ts0P", "Ts0S", \
				  NULL }

#define ASMC_MBP5_TEMPNAMES	{ "enclosure_bottom_0", "enclosure_bottom_1", \
				  "enclosure_bottom_2", "enclosure_bottom_3", \
				  "cpu_diode", "cpu", \
				  "cpu_pin", "gpu_diode", \
				  "gpu", "gpu_heatsink", \
				  "gpu_pin", "gpu_transistor", \
				  "gpu_2_heatsink", "northbridge_diode", \
				  "northbridge_pin", "unknown", \
				  "heatsink_2", "memory_controller", \
				  "pci_express_slot_pin", "pci_express_slot_unk" }

#define ASMC_MBP5_TEMPDESCS	{ "Enclosure Bottom 0", "Enclosure Bottom 1", \
				  "Enclosure Bottom 2", "Enclosure Bottom 3", \
				  "CPU Diode", "CPU ???", \
				  "CPU Pin", "GPU Diode", \
				  "GPU ???", "GPU Heatsink", \
				  "GPU Pin", "GPU Transistor", \
				  "GPU 2 Heatsink", "Northbridge Diode", \
				  "Northbridge Pin", "Unknown", \
				  "Heatsink 2", "Memory Controller", \
				  "PCI Express Slot Pin", "PCI Express Slot (unk)" }

#define ASMC_MBP81_TEMPS	{ "TB0T", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TCFC", "TCGC", "TCSA", "TM0S", "TMBS", \
				  "TP0P", "TPCD", "TW0P", "Th1H", "Ts0P", \
				  "Ts0S", NULL }

#define ASMC_MBP81_TEMPNAMES	{ "enclosure", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TCFC", "TCGC", "TCSA", "TM0S", "TMBS", \
				  "TP0P", "TPCD", "wireless", "Th1H", "Ts0P", \
				  "Ts0S" }

#define ASMC_MBP81_TEMPDESCS	{ "Enclosure Bottomside", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TCFC", "TCGC", "TCSA", "TM0S", "TMBS", \
				  "TP0P", "TPCD", "TW0P", "Th1H", "Ts0P", \
				  "Ts0S" }

#define ASMC_MBP82_TEMPS	{ "TB0T", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TC3C", "TC4C", "TCFC", "TCGC", "TCSA", \
				  "TCTD", "TG0D", "TG0P", "THSP", "TM0S", \
				  "TMBS", "TP0P", "TPCD", "TW0P", "Th1H", \
				  "Th2H", "Tm0P", "Ts0P", "Ts0S", NULL }

#define ASMC_MBP82_TEMPNAMES	{ "enclosure", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TC3C", "TC4C", "TCFC", "TCGC", "TCSA", \
				  "TCTD", "graphics", "TG0P", "THSP", "TM0S", \
				  "TMBS", "TP0P", "TPCD", "wireless", "Th1H", \
				  "Th2H", "memory", "Ts0P", "Ts0S" }

#define ASMC_MBP82_TEMPDESCS	{ "Enclosure Bottomside", "TB1T", "TB2T", "TC0C", "TC0D", \
				  "TC0E", "TC0F", "TC0P", "TC1C", "TC2C", \
				  "TC3C", "TC4C", "TCFC", "TCGC", "TCSA", \
				  "TCTD", "TG0D", "TG0P", "THSP", "TM0S", \
				  "TMBS", "TP0P", "TPCD", "TW0P", "Th1H", \
				  "Th2H", "Tm0P", "Ts0P", "Ts0S" }

#define ASMC_MBP9_TEMPS		{ "Ts0P", "Ts0S", "TA0P", "TB1T", "TB2T", \
				  "TB0T", "TC1C", "TC2C", "TC0E", "TC0F", \
				  "TC0J", "TC0P", "TCFC", "TCGC", "TCSA", \
				  "TCTD", "TCXC", "TG1D", "TM0P", "TM0S", \
				  "TPCD", NULL }

#define ASMC_MBP9_TEMPNAMES	{ "Ts0P", "Ts0S", "TA0P", "TB1T", "TB2T", \
				  "TB0T", "TC1C", "TC2C", "TC0E", "TC0F", \
				  "TC0J", "TC0P", "TCFC", "TCGC", "TCSA", \
				  "TCTD", "TCXC", "TG1D", "TM0P", "TM0S", \
				  "TPCD" }

#define ASMC_MBP9_TEMPDESCS	{ "Palm Rest", "Memory Proximity", "Airflow 1", \
				  "Battery 1", "Battery 2", "Battery TS_MAX", \
				  "CPU Core 1", "CPU Core 2", "CPU1", "CPU1", \
				  "TC0J", "CPU 1 Proximity", "TCFC", \
				  "PECI GPU", "PECI SA", "TCTD", "PECI CPU", \
				  "GPU Die", "Memory Bank A1", "Memory Module A1", \
				  "PCH Die" }


#define ASMC_MBP112_TEMPS	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E", \
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S", NULL }

#define ASMC_MBP112_TEMPNAMES	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E", \
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S" }

#define ASMC_MBP112_TEMPDESCS	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E", \
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S" }

#define ASMC_MBP113_TEMPS	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E",	\
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TG0D", "TG0P", "TG1D", "TG1F", \
				  "TG1d", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S", NULL }

#define ASMC_MBP113_TEMPNAMES	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E", \
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TG0D", "TG0P", "TG1D", "TG1F", \
				  "TG1d", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S" }

#define ASMC_MBP113_TEMPDESCS	{ "TB0T", "TB1T", "TB2T", "TBXT", "TC0E", \
				  "TC0F", "TC0P", "TC1C", "TC2C", "TC3C", \
				  "TC4C", "TCFC", "TCGC", "TCSA", "TCTD", \
				  "TCXC", "TG0D", "TG0P", "TG1D", "TG1F", \
				  "TG1d", "TH0A", "TH0B", "TH0F", "TH0R", \
				  "TH0V", "TH0a", "TH0b", "TH0c", "TM0P", \
				  "TM0S", "TP0P", "TPCD", "TW0P", "Ta0P", \
				  "TaSP", "Th1H", "Th2H", "Ts0P", "Ts0S", \
				  "Ts1S" }


#define ASMC_MM_TEMPS		{ "TN0P", "TN1P", NULL }
#define ASMC_MM_TEMPNAMES	{ "northbridge1", "northbridge2" }
#define ASMC_MM_TEMPDESCS	{ "Northbridge Point 1", \
				  "Northbridge Point 2" }

#define ASMC_MM21_TEMPS		{ "TA0P", "TC0D", \
				  "TC0H", "TC0P", \
				  "TC1P", "TN0P", \
				  "TN1P", NULL }

#define ASMC_MM21_TEMPNAMES	{ "ambient_air", "cpu_die", \
				  "cpu_heatsink", "cpu_proximity1", \
				  "cpu_proximity2", "northbridge_proximity1", \
				  "northbridge_proximity2", }

#define ASMC_MM21_TEMPDESCS	{ "Ambient Air Temperature" \
				  "CPU Die Core Temperature", \
				  "CPU Heatsink Temperature", \
				  "CPU Proximity 1 Temperature", \
				  "CPU Proximity 2 Temperature", \
				  "Northbridge Proximity 1 Temperature", \
				  "Northbridge Proximity 2 Temperature", }

#define ASMC_MM31_TEMPS		{ "TC0D", "TC0H", \
				  "TC0P", "TH0P", \
				  "TN0D", "TN0P", \
				  "TW0P", NULL }

#define ASMC_MM31_TEMPNAMES	{ "cpu0_die", "cpu0_heatsink", \
				  "cpu0_proximity", "hdd_bay", \
				  "northbridge_die", \
				  "northbridge_proximity", \
				  "wireless_proximity", }

#define ASMC_MM31_TEMPDESCS	{ "CPU0 Die Core Temperature", \
				  "CPU0 Heatsink Temperature", \
				  "CPU0 Proximity Temperature", \
				  "HDD Bay Temperature", \
				  "Northbridge Die Core Temperature", \
				  "Northbridge Proximity Temperature", \
				  "Wireless Module Proximity Temperature", }

#define ASMC_MM41_TEMPS		{ "TA0P", "TC0D", "TC0G", "TC0H", "TC0P", \
				  "TC0p", "TCPG", "TH0G", "TH0P", "TH0p", \
				  "TM0G", "TM0P", "TM0p", "TN0D", "TN0G", \
				  "TN0P", "TN0p", "TN1D", "TN1E", "TN1F", \
				  "TN1G", "TN1S", "TNPG", "TO0P", "TO0p", \
				  "TW0P", "Tm0P", "Tp0C", NULL }

#define ASMC_MM41_TEMPNAMES	{ "TA0P", "TC0D", "TC0G", "TC0H", "TC0P", \
				  "TC0p", "TCPG", "TH0G", "TH0P", "TH0p", \
				  "TM0G", "TM0P", "TM0p", "TN0D", "TN0G", \
				  "TN0P", "TN0p", "TN1D", "TN1E", "TN1F", \
				  "TN1G", "TN1S", "TNPG", "TO0P", "TO0p", \
				  "TW0P", "Tm0P", "Tp0C", NULL }

#define ASMC_MM41_TEMPDESCS	{ "TA0P", "TC0D", "TC0G", "TC0H", "TC0P", \
				  "TC0p", "TCPG", "TH0G", "TH0P", "TH0p", \
				  "TM0G", "TM0P", "TM0p", "TN0D", "TN0G", \
				  "TN0P", "TN0p", "TN1D", "TN1E", "TN1F", \
				  "TN1G", "TN1S", "TNPG", "TO0P", "TO0p", \
				  "TW0P", "Tm0P", "Tp0C", NULL }

#define ASMC_MM52_TEMPS		{ "TA0P", "TA1P", \
				  "TC0D", "TC0P", \
				  "TG0D", "TG1D", \
				  "TG0P", "TG0M", \
				  "TI0P", \
				  "TM0S", "TMBS", \
				  "TM0P", "TP0P", \
				  "TPCD", "Tp0C", \
				  "TW0P", NULL }

#define ASMC_MM52_TEMPNAMES	{ "ambient_air_proximity", "ambient_cpu_pch_wireless_dimm", \
				  "cpu_die", "cpu_proximity", \
				  "gpu_diode1", "gpu_diode2", \
				  "gpu_proximity", "gpu_integrated_switcher", \
				  "thunderbolt_proximity", \
				  "memory_slot1", "memory_slot2", \
				  "memory_proximity", "pch_controller_proximity", \
				  "pch_controller_die", "pwr_supply", \
				  "wireless_proximity", }
 
#define ASMC_MM52_TEMPDESCS	{ "Ambient Air Proximity Temperature", \
				  "Combo Ambient CPU PCH Wireless DIMM Temperature", \
				  "CPU Die Temperature", "CPU Proximity Temperature", \
				  "GPU Diode 1 Temperature" , "GPU Diode 2 Temperature", \
				  "GPU Proximity Temperature", \
				  "Integrated Graphics/GPU Switcher Temperature", \
				  "Thunderbolt Proximity Temperature", \
				  "Memory Slot 1 Temperature", \
				  "Memory Slot 2 Temperature", \
				  "Memory Slots Proximity Temperature", \
				  "Platform Controller Hub Proximity Temperature", \
				  "Platform Controller Hub Die Temperature", \
				  "Power Supply Temperature", \
				  "Wireless Module Proximity Temperature", }

#define ASMC_MP1_TEMPS		{ "TA0P", \
				  "TCAH", "TCBH", \
				  "TC0P", "TC0C", "TC1C", \
				  "TC2C", "TC3C", "THTG", \
				  "TH0P", "TH1P", \
				  "TH2P", "TH3P", \
				  "TM0P", "TM1P", "TM2P", \
				  "TM8P", "TM9P", "TMAP", \
				  "TM0S", "TM1S", "TM2P", "TM3S", \
				  "TM8S", "TM9S", "TMAS", "TMBS", \
				  "TN0H", "TS0C", \
				  "Tp0C", "Tp1C", "Tv0S", "Tv1S", NULL }

#define ASMC_MP1_TEMPNAMES	{ "ambient", \
				  "cpu_a_heatsink", "cpu_b_heatsink", \
				  "cpu_a_proximity", "cpu_core0", "cpu_core1", \
				  "cpu_core2", "cpu_core3", "THTG", \
				  "hdd_bay0", "hdd_bay1", \
				  "hdd_bay2", "hdd_bay3", \
				  "memory_card_a_proximity0", \
				  "memory_card_a_proximity1", \
				  "memory_card_a_proximity2", \
				  "memory_card_b_proximity0", \
				  "memory_card_b_proximity1", \
				  "memory_card_b_proximity2", \
				  "memory_card_a_slot0", \
				  "memory_card_a_slot1", \
				  "memory_card_a_slot2", \
				  "memory_card_a_slot3", \
				  "memory_card_b_slot0", \
				  "memory_card_b_slot1", \
				  "memory_card_b_slot2", \
				  "memory_card_b_slot3", \
				  "mch_heatsink", "expansion_slots", \
				  "power_supply_loc0", "power_supply_loc1", \
				  "Tv0S", "Tv1S", }

#define ASMC_MP1_TEMPDESCS	{ "Ambient Air", \
				  "CPU A Heatsink", "CPU B Heatsink", \
				  "CPU A Proximity", \
				  "CPU Core 1", "CPU Core 2", \
				  "CPU Core 3", "CPU Core 4", "THTG", \
				  "Hard Drive Bay 1", "Hard Drive Bay 2", \
				  "Hard Drive Bay 3", "Hard Drive Bay 4", \
				  "Memory Riser A, Proximity 1", \
				  "Memory Riser A, Proximity 2", \
				  "Memory Riser A, Proximity 3", \
				  "Memory Riser B, Proximity 1", \
				  "Memory Riser B, Proximity 2", \
				  "Memory Riser B, Proximity 3", \
				  "Memory Riser A, Slot 1", \
				  "Memory Riser A, Slot 2", \
				  "Memory Riser A, Slot 3", \
				  "Memory Riser A, Slot 4", \
				  "Memory Riser B, Slot 1", \
				  "Memory Riser B, Slot 2", \
				  "Memory Riser B, Slot 3", \
				  "Memory Riser B, Slot 4", \
				  "MCH Heatsink", "Expansion Slots", \
				  "Power Supply, Location 1", \
				  "Power Supply, Location 2", \
				  "Tv0S", "Tv1S", }

#define ASMC_MP2_TEMPS		{ "TA0P", "TCAG", "TCAH", "TCBG", "TCBH", \
				  "TC0C", "TC0D", "TC0P", "TC1C", "TC1D", \
				  "TC2C", "TC2D", "TC3C", "TC3D", "THTG", \
				  "TH0P", "TH1P", "TH2P", "TH3P", "TMAP", \
				  "TMAS", "TMBS", "TM0P", "TM0S", "TM1P", \
				  "TM1S", "TM2P", "TM2S", "TM3S", "TM8P", \
				  "TM8S", "TM9P", "TM9S", "TN0H", "TS0C", \
				  NULL }

#define ASMC_MP2_TEMPNAMES	{ "TA0P", "TCAG", "TCAH", "TCBG", "TCBH", \
				  "TC0C", "TC0D", "TC0P", "TC1C", "TC1D", \
				  "TC2C", "TC2D", "TC3C", "TC3D", "THTG", \
				  "TH0P", "TH1P", "TH2P", "TH3P", "TMAP", \
				  "TMAS", "TMBS", "TM0P", "TM0S", "TM1P", \
				  "TM1S", "TM2P", "TM2S", "TM3S", "TM8P", \
				  "TM8S", "TM9P", "TM9S", "TN0H", "TS0C", }

#define ASMC_MP2_TEMPDESCS	{ "TA0P", "TCAG", "TCAH", "TCBG", "TCBH", \
				  "TC0C", "TC0D", "TC0P", "TC1C", "TC1D", \
				  "TC2C", "TC2D", "TC3C", "TC3D", "THTG", \
				  "TH0P", "TH1P", "TH2P", "TH3P", "TMAP", \
				  "TMAS", "TMBS", "TM0P", "TM0S", "TM1P", \
				  "TM1S", "TM2P", "TM2S", "TM3S", "TM8P", \
				  "TM8S", "TM9P", "TM9S", "TN0H", "TS0C", }

#define ASMC_MP5_TEMPS		{ "TA0P", "TCAC", "TCAD", "TCAG", "TCAH", \
				  "TCAS", "TCBC", "TCBD", "TCBG", "TCBH", \
				  "TCBS", "TH1F", "TH1P", "TH1V", "TH2F", \
				  "TH2P", "TH2V", "TH3F", "TH3P", "TH3V", \
				  "TH4F", "TH4P", "TH4V", "THPS", "THTG", \
				  "TM1P", "TM2P", "TM2V", "TM3P", "TM3V", \
				  "TM4P", "TM5P", "TM6P", "TM6V", "TM7P", \
				  "TM7V", "TM8P", "TM8V", "TM9V", "TMA1", \
				  "TMA2", "TMA3", "TMA4", "TMB1", "TMB2", \
				  "TMB3", "TMB4", "TMHS", "TMLS", "TMPS", \
				  "TMPV", "TMTG", "TN0D", "TN0H", "TNTG", \
				  "Te1F", "Te1P", "Te1S", "Te2F", "Te2S", \
				  "Te3F", "Te3S", "Te4F", "Te4S", "Te5F", \
				  "Te5S", "TeGG", "TeGP", "TeRG", "TeRP", \
				  "TeRV", "Tp0C", "Tp1C", "TpPS", "TpTG", \
				  NULL }

#define ASMC_MP5_TEMPNAMES	{ "ambient", "TCAC", "TCAD", "TCAG", "TCAH", \
				  "TCAS", "TCBC", "TCBD", "TCBG", "TCBH", \
				  "TCBS", "TH1F", "TH1P", "TH1V", "TH2F", \
				  "TH2P", "TH2V", "TH3F", "TH3P", "TH3V", \
				  "TH4F", "TH4P", "TH4V", "THPS", "THTG", \
				  "TM1P", "TM2P", "TM2V", "TM3P", "TM3V", \
				  "TM4P", "TM5P", "TM6P", "TM6V", "TM7P", \
				  "TM7V", "TM8P", "TM8V", "TM9V", "ram_a1", \
				  "ram_a2", "ram_a3", "ram_a4", "ram_b1", "ram_b2", \
				  "ram_b3", "ram_b4", "TMHS", "TMLS", "TMPS", \
				  "TMPV", "TMTG", "TN0D", "TN0H", "TNTG", \
				  "Te1F", "Te1P", "Te1S", "Te2F", "Te2S", \
				  "Te3F", "Te3S", "Te4F", "Te4S", "Te5F", \
				  "Te5S", "TeGG", "TeGP", "TeRG", "TeRP", \
				  "TeRV", "Tp0C", "Tp1C", "TpPS", "TpTG", }

#define ASMC_MP5_TEMPDESCS	{ "TA0P", "TCAC", "TCAD", "TCAG", "TCAH", \
				  "TCAS", "TCBC", "TCBD", "TCBG", "TCBH", \
				  "TCBS", "TH1F", "TH1P", "TH1V", "TH2F", \
				  "TH2P", "TH2V", "TH3F", "TH3P", "TH3V", \
				  "TH4F", "TH4P", "TH4V", "THPS", "THTG", \
				  "TM1P", "TM2P", "TM2V", "TM3P", "TM3V", \
				  "TM4P", "TM5P", "TM6P", "TM6V", "TM7P", \
				  "TM7V", "TM8P", "TM8V", "TM9V", "TMA1", \
				  "TMA2", "TMA3", "TMA4", "TMB1", "TMB2", \
				  "TMB3", "TMB4", "TMHS", "TMLS", "TMPS", \
				  "TMPV", "TMTG", "TN0D", "TN0H", "TNTG", \
				  "Te1F", "Te1P", "Te1S", "Te2F", "Te2S", \
				  "Te3F", "Te3S", "Te4F", "Te4S", "Te5F", \
				  "Te5S", "TeGG", "TeGP", "TeRG", "TeRP", \
				  "TeRV", "Tp0C", "Tp1C", "TpPS", "TpTG", }

#define	ASMC_MBA_TEMPS		{ "TB0T", NULL }
#define	ASMC_MBA_TEMPNAMES	{ "enclosure" }
#define	ASMC_MBA_TEMPDESCS	{ "Enclosure Bottom" }

#define	ASMC_MBA3_TEMPS		{ "TB0T", "TB1T", "TB2T", \
				  "TC0D", "TC0E", "TC0P", NULL }

#define	ASMC_MBA3_TEMPNAMES	{ "enclosure", "TB1T", "TB2T", \
				  "TC0D", "TC0E", "TC0P" }

#define	ASMC_MBA3_TEMPDESCS	{ "Enclosure Bottom", "TB1T", "TB2T", \
				  "TC0D", "TC0E", "TC0P" }

#define	ASMC_MBA5_TEMPS		{ "TB0T", "TB1T", "TB2T", "TC0C", \
                         	  "TC0D", "TC0E", "TC0F", "TC0P", \
	                          "TC1C", "TC2C", "TCGC", "TCSA", \
	                          "TCXC", "THSP", "TM0P", "TPCD", \
	                          "Ta0P", "Th1H", "Tm0P", "Tm1P", \
	                          "Ts0P", "Ts0S", NULL }

#define	ASMC_MBA5_TEMPNAMES	{ "enclosure1", "enclosure2", "enclosure3", "TC0C", \
	                          "cpudiode", "cputemp1", "cputemp2", "cpuproximity", \
	                          "cpucore1", "cpucore2", "cpupeci", "pecisa", \
	                          "TCXC", "THSP", "memorybank", "pchdie", \
	                          "Ta0P", "heatpipe", "mainboardproximity1", "mainboardproximity2", \
	                          "palmrest", "memoryproximity" }

#define	ASMC_MBA5_TEMPDESCS	{ "Enclosure Bottom 1", "Enclosure Bottom 2", "Enclosure Bottom 3", "TC0C",\
	                          "CPU Diode", "CPU Temp 1", "CPU Temp 2", "CPU Proximity", \
	                          "CPU Core 1", "CPU Core 2", "CPU Peci Core", "PECI SA", \
	                          "TCXC", "THSP", "Memory Bank A", "PCH Die", \
	                          "Ta0P", "Heatpipe", "Mainboard Proximity 1", "Mainboard Proximity 2", \
	                          "Palm Rest", "Memory Proximity" }

#define	ASMC_MBA7_TEMPS		{ "TB0T", "TB1T", "TB2T", \
                         	  "TC0E", "TC0F", "TC0P", \
	                          "TC1C", "TC2C", \
	                          "TCGC", "TCSA", "TCXC", \
	                          "THSP", "TM0P", "TPCD", \
	                          "TW0P" "Ta0P", "Th1H", \
	                          "Tm0P", "Ts0P", "Ts0S", NULL }

#define	ASMC_MBA7_TEMPNAMES	{ "enclosure1", "enclosure2", "enclosure3", \
	                          "cputemp1", "cputemp2", "cpuproximity", \
	                          "cpucore1", "cpucore2", \
	                          "pecigpu", "pecisa", "pecicpu", \
	                          "thunderboltproximity", "memorybank", "pchdie", \
	                          "wirelessproximity", "airflowproximity", "heatpipe", \
	                          "mainboardproximity", "palmrest", "memoryproximity" }

#define	ASMC_MBA7_TEMPDESCS	{ "Enclosure Bottom 1", "Enclosure Bottom 2", "Enclosure Bottom 3", \
	                          "CPU Temp 1", "CPU Temp 2", "CPU Proximity", \
	                          "CPU Core 1", "CPU Core 2", \
	                          "PECI GPU", "PECI SA", "PECI CPU", \
	                          "Thunderbolt Proximity", "Memory Bank A", "PCH Die", \
	                          "Wireless Proximity", "Airflow Proxmity", "Heatpipe", \
	                          "Mainboard Proximity", "Palm Rest", "Memory Proximity" }
