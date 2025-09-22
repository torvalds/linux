/*	$OpenBSD: est.c,v 1.55 2023/01/30 10:49:05 jsg Exp $ */
/*
 * Copyright (c) 2003 Michael Eriksson.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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


/*
 * This is a driver for Intel's Enhanced SpeedStep, as implemented in
 * Pentium M processors.
 *
 * Reference documentation:
 *
 * - IA-32 Intel Architecture Software Developer's Manual, Volume 3:
 *   System Programming Guide.
 *   Section 13.14, Enhanced Intel SpeedStep technology.
 *   Table B-2, MSRs in Pentium M Processors.
 *   http://www.intel.com/design/pentium4/manuals/245472.htm
 *
 * - Intel Pentium M Processor Datasheet.
 *   Table 5, Voltage and Current Specifications.
 *   http://www.intel.com/design/mobile/datashts/252612.htm
 *
 * - Intel Pentium M Processor on 90 nm Process with 2-MB L2 Cache Datasheet
 *   Table 3-4, Voltage and Current Specifications.
 *   http://www.intel.com/design/mobile/datashts/302189.htm
 *
 * - Linux cpufreq patches, speedstep-centrino.c.
 *   Encoding of MSR_PERF_CTL and MSR_PERF_STATUS.
 *   http://www.codemonkey.org.uk/projects/cpufreq/cpufreq-2.4.22-pre6-1.gz
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/bus.h>

#include "acpicpu.h"

#if NACPICPU > 0
#include <dev/acpi/acpidev.h>
#endif

/* Convert MHz and mV into IDs for passing to the MSR. */
#define ID16(MHz, mV, bus_clk) \
    { ((((MHz * 100 + 50) / bus_clk) << 8) | ((mV ? mV - 700 : 0) >> 4)), \
	 MHz }

struct est_op {
	uint16_t ctrl;
	uint16_t mhz;
	uint16_t pct;
};

/* Ultra Low Voltage Intel Pentium M processor 900 MHz */
static struct est_op pm130_900_ulv[] = {
	ID16( 900, 1004, BUS100),
	ID16( 800,  988, BUS100),
	ID16( 600,  844, BUS100),
};

/* Ultra Low Voltage Intel Pentium M processor 1.00 GHz */
static struct est_op pm130_1000_ulv[] = {
	ID16(1000, 1004, BUS100),
	ID16( 900,  988, BUS100),
	ID16( 800,  972, BUS100),
	ID16( 600,  844, BUS100),
};

/* Ultra Low Voltage Intel Pentium M processor 1.10 GHz */
static struct est_op pm130_1100_ulv[] = {
	ID16(1100, 1004, BUS100),
	ID16(1000,  988, BUS100),
	ID16( 900,  972, BUS100),
	ID16( 800,  956, BUS100),
	ID16( 600,  844, BUS100),
};

/* Low Voltage Intel Pentium M processor 1.10 GHz */
static struct est_op pm130_1100_lv[] = {
	ID16(1100, 1180, BUS100),
	ID16(1000, 1164, BUS100),
	ID16( 900, 1100, BUS100),
	ID16( 800, 1020, BUS100),
	ID16( 600,  956, BUS100),
};

/* Low Voltage Intel Pentium M processor 1.20 GHz */
static struct est_op pm130_1200_lv[] = {
	ID16(1200, 1180, BUS100),
	ID16(1100, 1164, BUS100),
	ID16(1000, 1100, BUS100),
	ID16( 900, 1020, BUS100),
	ID16( 800, 1004, BUS100),
	ID16( 600,  956, BUS100),
};

/* Low Voltage Intel Pentium M processor 1.30 GHz */
static struct est_op pm130_1300_lv[] = {
	ID16(1300, 1180, BUS100),
	ID16(1200, 1164, BUS100),
	ID16(1100, 1100, BUS100),
	ID16(1000, 1020, BUS100),
	ID16( 900, 1004, BUS100),
	ID16( 800,  988, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 1.30 GHz */
static struct est_op pm130_1300[] = {
	ID16(1300, 1388, BUS100),
	ID16(1200, 1356, BUS100),
	ID16(1000, 1292, BUS100),
	ID16( 800, 1260, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 1.40 GHz */
static struct est_op pm130_1400[] = {
	ID16(1400, 1484, BUS100),
	ID16(1200, 1436, BUS100),
	ID16(1000, 1308, BUS100),
	ID16( 800, 1180, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 1.50 GHz */
static struct est_op pm130_1500[] = {
	ID16(1500, 1484, BUS100),
	ID16(1400, 1452, BUS100),
	ID16(1200, 1356, BUS100),
	ID16(1000, 1228, BUS100),
	ID16( 800, 1116, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 1.60 GHz */
static struct est_op pm130_1600[] = {
	ID16(1600, 1484, BUS100),
	ID16(1400, 1420, BUS100),
	ID16(1200, 1276, BUS100),
	ID16(1000, 1164, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 1.70 GHz */
static struct est_op pm130_1700[] = {
	ID16(1700, 1484, BUS100),
	ID16(1400, 1308, BUS100),
	ID16(1200, 1228, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1004, BUS100),
	ID16( 600,  956, BUS100),
};

/* Intel Pentium M processor 723 1.0 GHz */
static struct est_op pm90_n723[] = {
	ID16(1000,  940, BUS100),
	ID16( 900,  908, BUS100),
	ID16( 800,  876, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #G */
static struct est_op pm90_n733g[] = {
	ID16(1100,  956, BUS100),
	ID16(1000,  940, BUS100),
	ID16( 900,  908, BUS100),
	ID16( 800,  876, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #H */
static struct est_op pm90_n733h[] = {
	ID16(1100,  940, BUS100),
	ID16(1000,  924, BUS100),
	ID16( 900,  892, BUS100),
	ID16( 800,  876, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #I */
static struct est_op pm90_n733i[] = {
	ID16(1100,  924, BUS100),
	ID16(1000,  908, BUS100),
	ID16( 900,  892, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #J */
static struct est_op pm90_n733j[] = {
	ID16(1100,  908, BUS100),
	ID16(1000,  892, BUS100),
	ID16( 900,  876, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #K */
static struct est_op pm90_n733k[] = {
	ID16(1100,  892, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 733 1.1 GHz, VID #L */
static struct est_op pm90_n733l[] = {
	ID16(1100,  876, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #G */
static struct est_op pm90_n753g[] = {
	ID16(1200,  956, BUS100),
	ID16(1100,  940, BUS100),
	ID16(1000,  908, BUS100),
	ID16( 900,  892, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #H */
static struct est_op pm90_n753h[] = {
	ID16(1200,  940, BUS100),
	ID16(1100,  924, BUS100),
	ID16(1000,  908, BUS100),
	ID16( 900,  876, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #I */
static struct est_op pm90_n753i[] = {
	ID16(1200,  924, BUS100),
	ID16(1100,  908, BUS100),
	ID16(1000,  892, BUS100),
	ID16( 900,  876, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #J */
static struct est_op pm90_n753j[] = {
	ID16(1200,  908, BUS100),
	ID16(1100,  892, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #K */
static struct est_op pm90_n753k[] = {
	ID16(1200,  892, BUS100),
	ID16(1100,  892, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 753 1.2 GHz, VID #L */
static struct est_op pm90_n753l[] = {
	ID16(1200,  876, BUS100),
	ID16(1100,  876, BUS100),
	ID16(1000,  860, BUS100),
	ID16( 900,  844, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #G */
static struct est_op pm90_n773g[] = {
	ID16(1300,  956, BUS100),
	ID16(1200,  940, BUS100),
	ID16(1100,  924, BUS100),
	ID16(1000,  908, BUS100),
	ID16( 900,  876, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #H */
static struct est_op pm90_n773h[] = {
	ID16(1300,  940, BUS100),
	ID16(1200,  924, BUS100),
	ID16(1100,  908, BUS100),
	ID16(1000,  892, BUS100),
	ID16( 900,  876, BUS100),
	ID16( 800,  860, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #I */
static struct est_op pm90_n773i[] = {
	ID16(1300,  924, BUS100),
	ID16(1200,  908, BUS100),
	ID16(1100,  892, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #J */
static struct est_op pm90_n773j[] = {
	ID16(1300,  908, BUS100),
	ID16(1200,  908, BUS100),
	ID16(1100,  892, BUS100),
	ID16(1000,  876, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #K */
static struct est_op pm90_n773k[] = {
	ID16(1300,  892, BUS100),
	ID16(1200,  892, BUS100),
	ID16(1100,  876, BUS100),
	ID16(1000,  860, BUS100),
	ID16( 900,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 773 1.3 GHz, VID #L */
static struct est_op pm90_n773l[] = {
	ID16(1300,  876, BUS100),
	ID16(1200,  876, BUS100),
	ID16(1100,  860, BUS100),
	ID16(1000,  860, BUS100),
	ID16( 900,  844, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  812, BUS100),
};

/* Intel Pentium M processor 738 1.4 GHz */
static struct est_op pm90_n738[] = {
	ID16(1400, 1116, BUS100),
	ID16(1300, 1116, BUS100),
	ID16(1200, 1100, BUS100),
	ID16(1100, 1068, BUS100),
	ID16(1000, 1052, BUS100),
	ID16( 900, 1036, BUS100),
	ID16( 800, 1020, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 758 1.5 GHz */
static struct est_op pm90_n758[] = {
	ID16(1500, 1116, BUS100),
	ID16(1400, 1116, BUS100),
	ID16(1300, 1100, BUS100),
	ID16(1200, 1084, BUS100),
	ID16(1100, 1068, BUS100),
	ID16(1000, 1052, BUS100),
	ID16( 900, 1036, BUS100),
	ID16( 800, 1020, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 778 1.6 GHz */
static struct est_op pm90_n778[] = {
	ID16(1600, 1116, BUS100),
	ID16(1500, 1116, BUS100),
	ID16(1400, 1100, BUS100),
	ID16(1300, 1184, BUS100),
	ID16(1200, 1068, BUS100),
	ID16(1100, 1052, BUS100),
	ID16(1000, 1052, BUS100),
	ID16( 900, 1036, BUS100),
	ID16( 800, 1020, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 710 1.4 GHz, 533 MHz FSB */
static struct est_op pm90_n710[] = {
	ID16(1400, 1340, BUS133),
	ID16(1200, 1228, BUS133),
	ID16(1000, 1148, BUS133),
	ID16( 800, 1068, BUS133),
	ID16( 600,  998, BUS133),
};

/* Intel Pentium M processor 715 1.5 GHz, VID #A */
static struct est_op pm90_n715a[] = {
	ID16(1500, 1340, BUS100),
	ID16(1200, 1228, BUS100),
	ID16(1000, 1148, BUS100),
	ID16( 800, 1068, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 715 1.5 GHz, VID #B */
static struct est_op pm90_n715b[] = {
	ID16(1500, 1324, BUS100),
	ID16(1200, 1212, BUS100),
	ID16(1000, 1148, BUS100),
	ID16( 800, 1068, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 715 1.5 GHz, VID #C */
static struct est_op pm90_n715c[] = {
	ID16(1500, 1308, BUS100),
	ID16(1200, 1212, BUS100),
	ID16(1000, 1132, BUS100),
	ID16( 800, 1068, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 715 1.5 GHz, VID #D */
static struct est_op pm90_n715d[] = {
	ID16(1500, 1276, BUS100),
	ID16(1200, 1180, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 725 1.6 GHz, VID #A */
static struct est_op pm90_n725a[] = {
	ID16(1600, 1340, BUS100),
	ID16(1400, 1276, BUS100),
	ID16(1200, 1212, BUS100),
	ID16(1000, 1132, BUS100),
	ID16( 800, 1068, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 725 1.6 GHz, VID #B */
static struct est_op pm90_n725b[] = {
	ID16(1600, 1324, BUS100),
	ID16(1400, 1260, BUS100),
	ID16(1200, 1196, BUS100),
	ID16(1000, 1132, BUS100),
	ID16( 800, 1068, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 725 1.6 GHz, VID #C */
static struct est_op pm90_n725c[] = {
	ID16(1600, 1308, BUS100),
	ID16(1400, 1244, BUS100),
	ID16(1200, 1180, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 725 1.6 GHz, VID #D */
static struct est_op pm90_n725d[] = {
	ID16(1600, 1276, BUS100),
	ID16(1400, 1228, BUS100),
	ID16(1200, 1164, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 730 1.6 GHz, 533 MHz FSB */
static struct est_op pm90_n730[] = {
	ID16(1600, 1308, BUS133),
	ID16(1333, 1260, BUS133),
	ID16(1200, 1212, BUS133),
	ID16(1067, 1180, BUS133),
	ID16( 800,  988, BUS133),
};

/* Intel Pentium M processor 735 1.7 GHz, VID #A */
static struct est_op pm90_n735a[] = {
	ID16(1700, 1340, BUS100),
	ID16(1400, 1244, BUS100),
	ID16(1200, 1180, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 735 1.7 GHz, VID #B */
static struct est_op pm90_n735b[] = {
	ID16(1700, 1324, BUS100),
	ID16(1400, 1244, BUS100),
	ID16(1200, 1180, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 735 1.7 GHz, VID #C */
static struct est_op pm90_n735c[] = {
	ID16(1700, 1308, BUS100),
	ID16(1400, 1228, BUS100),
	ID16(1200, 1164, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 735 1.7 GHz, VID #D */
static struct est_op pm90_n735d[] = {
	ID16(1700, 1276, BUS100),
	ID16(1400, 1212, BUS100),
	ID16(1200, 1148, BUS100),
	ID16(1000, 1100, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 740 1.73 GHz, 533 MHz FSB */
static struct est_op pm90_n740[] = {
	ID16(1733, 1356, BUS133),
	ID16(1333, 1212, BUS133),
	ID16(1067, 1100, BUS133),
	ID16( 800,  988, BUS133),
};

/* Intel Pentium M processor 745 1.8 GHz, VID #A */
static struct est_op pm90_n745a[] = {
	ID16(1800, 1340, BUS100),
	ID16(1600, 1292, BUS100),
	ID16(1400, 1228, BUS100),
	ID16(1200, 1164, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 745 1.8 GHz, VID #B */
static struct est_op pm90_n745b[] = {
	ID16(1800, 1324, BUS100),
	ID16(1600, 1276, BUS100),
	ID16(1400, 1212, BUS100),
	ID16(1200, 1164, BUS100),
	ID16(1000, 1116, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 745 1.8 GHz, VID #C */
static struct est_op pm90_n745c[] = {
	ID16(1800, 1308, BUS100),
	ID16(1600, 1260, BUS100),
	ID16(1400, 1212, BUS100),
	ID16(1200, 1148, BUS100),
	ID16(1000, 1100, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 745 1.8 GHz, VID #D */
static struct est_op pm90_n745d[] = {
	ID16(1800, 1276, BUS100),
	ID16(1600, 1228, BUS100),
	ID16(1400, 1180, BUS100),
	ID16(1200, 1132, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 750 1.86 GHz, 533 MHz FSB */
/* values extracted from \_PR\NPSS (via _PSS) SDST ACPI table */
static struct est_op pm90_n750[] = {
	ID16(1867, 1308, BUS133),
	ID16(1600, 1228, BUS133),
	ID16(1333, 1148, BUS133),
	ID16(1067, 1068, BUS133),
	ID16( 800,  988, BUS133),
};

/* Intel Pentium M processor 755 2.0 GHz, VID #A */
static struct est_op pm90_n755a[] = {
	ID16(2000, 1340, BUS100),
	ID16(1800, 1292, BUS100),
	ID16(1600, 1244, BUS100),
	ID16(1400, 1196, BUS100),
	ID16(1200, 1148, BUS100),
	ID16(1000, 1100, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 755 2.0 GHz, VID #B */
static struct est_op pm90_n755b[] = {
	ID16(2000, 1324, BUS100),
	ID16(1800, 1276, BUS100),
	ID16(1600, 1228, BUS100),
	ID16(1400, 1180, BUS100),
	ID16(1200, 1132, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 755 2.0 GHz, VID #C */
static struct est_op pm90_n755c[] = {
	ID16(2000, 1308, BUS100),
	ID16(1800, 1276, BUS100),
	ID16(1600, 1228, BUS100),
	ID16(1400, 1180, BUS100),
	ID16(1200, 1132, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 755 2.0 GHz, VID #D */
static struct est_op pm90_n755d[] = {
	ID16(2000, 1276, BUS100),
	ID16(1800, 1244, BUS100),
	ID16(1600, 1196, BUS100),
	ID16(1400, 1164, BUS100),
	ID16(1200, 1116, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 760 2.0 GHz, 533 MHz FSB */
static struct est_op pm90_n760[] = {
	ID16(2000, 1356, BUS133),
	ID16(1600, 1244, BUS133),
	ID16(1333, 1164, BUS133),
	ID16(1067, 1084, BUS133),
	ID16( 800,  988, BUS133),
};

/* Intel Pentium M processor 765 2.1 GHz, VID #A */
static struct est_op pm90_n765a[] = {
	ID16(2100, 1340, BUS100),
	ID16(1800, 1276, BUS100),
	ID16(1600, 1228, BUS100),
	ID16(1400, 1180, BUS100),
	ID16(1200, 1132, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 765 2.1 GHz, VID #B */
static struct est_op pm90_n765b[] = {
	ID16(2100, 1324, BUS100),
	ID16(1800, 1260, BUS100),
	ID16(1600, 1212, BUS100),
	ID16(1400, 1180, BUS100),
	ID16(1200, 1132, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 765 2.1 GHz, VID #C */
static struct est_op pm90_n765c[] = {
	ID16(2100, 1308, BUS100),
	ID16(1800, 1244, BUS100),
	ID16(1600, 1212, BUS100),
	ID16(1400, 1164, BUS100),
	ID16(1200, 1116, BUS100),
	ID16(1000, 1084, BUS100),
	ID16( 800, 1036, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 765 2.1 GHz, VID #E */
static struct est_op pm90_n765e[] = {
	ID16(2100, 1356, BUS100),
	ID16(1800, 1292, BUS100),
	ID16(1600, 1244, BUS100),
	ID16(1400, 1196, BUS100),
	ID16(1200, 1148, BUS100),
	ID16(1000, 1100, BUS100),
	ID16( 800, 1052, BUS100),
	ID16( 600,  988, BUS100),
};

/* Intel Pentium M processor 770 2.13 GHz */
static struct est_op pm90_n770[] = {
	ID16(2133, 1356, BUS133),
	ID16(1867, 1292, BUS133),
	ID16(1600, 1212, BUS133),
	ID16(1333, 1148, BUS133),
	ID16(1067, 1068, BUS133),
	ID16( 800,  988, BUS133),
};

/*
 * VIA C7-M 500 MHz FSB, 400 MHz FSB, and ULV variants.
 * Data from the "VIA C7-M Processor BIOS Writer's Guide (v2.17)" datasheet.
 */

/* 1.00GHz Centaur C7-M ULV */
static struct est_op C7M_770_ULV[] = {
	ID16(1000,  844, BUS100),
	ID16( 800,  796, BUS100),
	ID16( 600,  796, BUS100),
	ID16( 400,  796, BUS100),
};

/* 1.00GHz Centaur C7-M ULV */
static struct est_op C7M_779_ULV[] = {
	ID16(1000,  796, BUS100),
	ID16( 800,  796, BUS100),
	ID16( 600,  796, BUS100),
	ID16( 400,  796, BUS100),
};

/* 1.20GHz Centaur C7-M ULV */
static struct est_op C7M_772_ULV[] = {
	ID16(1200,  844, BUS100),
	ID16(1000,  844, BUS100),
	ID16( 800,  828, BUS100),
	ID16( 600,  796, BUS100),
	ID16( 400,  796, BUS100),
};

/* 1.50GHz Centaur C7-M ULV */
static struct est_op C7M_775_ULV[] = {
	ID16(1500,  956, BUS100),
	ID16(1400,  940, BUS100),
	ID16(1000,  860, BUS100),
	ID16( 800,  828, BUS100),
	ID16( 600,  796, BUS100),
	ID16( 400,  796, BUS100),
};

/* 1.20GHz Centaur C7-M 400 MHz FSB */
static struct est_op C7M_771[] = {
	ID16(1200,  860, BUS100),
	ID16(1000,  860, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};

/* 1.50GHz Centaur C7-M 400 MHz FSB */
static struct est_op C7M_754[] = {
	ID16(1500, 1004, BUS100),
	ID16(1400,  988, BUS100),
	ID16(1000,  940, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};

/* 1.60GHz Centaur C7-M 400 MHz FSB */
static struct est_op C7M_764[] = {
	ID16(1600, 1084, BUS100),
	ID16(1400, 1052, BUS100),
	ID16(1000, 1004, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};

/* 1.80GHz Centaur C7-M 400 MHz FSB */
static struct est_op C7M_784[] = {
	ID16(1800, 1148, BUS100),
	ID16(1600, 1100, BUS100),
	ID16(1400, 1052, BUS100),
	ID16(1000, 1004, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};

/* 2.00GHz Centaur C7-M 400 MHz FSB */
static struct est_op C7M_794[] = {
	ID16(2000, 1148, BUS100),
	ID16(1800, 1132, BUS100),
	ID16(1600, 1100, BUS100),
	ID16(1400, 1052, BUS100),
	ID16(1000, 1004, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};

/* 1.60GHz Centaur C7-M 533 MHz FSB */
static struct est_op C7M_765[] = {
	ID16(1600, 1084, BUS133),
	ID16(1467, 1052, BUS133),
	ID16(1200, 1004, BUS133),
	ID16( 800,  844, BUS133),
	ID16( 667,  844, BUS133),
	ID16( 533,  844, BUS133),
};

/* 2.00GHz Centaur C7-M 533 MHz FSB */
static struct est_op C7M_785[] = {
	ID16(1867, 1148, BUS133),
	ID16(1600, 1100, BUS133),
	ID16(1467, 1052, BUS133),
	ID16(1200, 1004, BUS133),
	ID16( 800,  844, BUS133),
	ID16( 667,  844, BUS133),
	ID16( 533,  844, BUS133),
};

/* 2.00GHz Centaur C7-M 533 MHz FSB */
static struct est_op C7M_795[] = {
	ID16(2000, 1148, BUS133),
	ID16(1867, 1132, BUS133),
	ID16(1600, 1100, BUS133),
	ID16(1467, 1052, BUS133),
	ID16(1200, 1004, BUS133),
	ID16( 800,  844, BUS133),
	ID16( 667,  844, BUS133),
	ID16( 533,  844, BUS133),
};

/* 1.00GHz VIA Eden 90nm 'Esther' */
static struct est_op eden90_1000[] = {
	ID16(1000,  844, BUS100),
	ID16( 800,  844, BUS100),
	ID16( 600,  844, BUS100),
	ID16( 400,  844, BUS100),
};


struct fqlist {
	int vendor: 5;
	unsigned bus_clk : 1;
	unsigned n : 5;
	struct est_op *table;
};

#define ENTRY(ven, bus_clk, tab) \
	{ CPUVENDOR_##ven, bus_clk == BUS133 ? 1 : 0, nitems(tab), tab }

#define BUS_CLK(fqp) ((fqp)->bus_clk ? BUS133 : BUS100)

static struct fqlist est_cpus[] = {
	ENTRY(INTEL, BUS100, pm130_900_ulv),
	ENTRY(INTEL, BUS100, pm130_1000_ulv),
	ENTRY(INTEL, BUS100, pm130_1100_ulv),
	ENTRY(INTEL, BUS100, pm130_1100_lv),
	ENTRY(INTEL, BUS100, pm130_1200_lv),
	ENTRY(INTEL, BUS100, pm130_1300_lv),
	ENTRY(INTEL, BUS100, pm130_1300),
	ENTRY(INTEL, BUS100, pm130_1400),
	ENTRY(INTEL, BUS100, pm130_1500),
	ENTRY(INTEL, BUS100, pm130_1600),
	ENTRY(INTEL, BUS100, pm130_1700),

	ENTRY(INTEL, BUS100, pm90_n723),
	ENTRY(INTEL, BUS100, pm90_n733g),
	ENTRY(INTEL, BUS100, pm90_n733h),
	ENTRY(INTEL, BUS100, pm90_n733i),
	ENTRY(INTEL, BUS100, pm90_n733j),
	ENTRY(INTEL, BUS100, pm90_n733k),
	ENTRY(INTEL, BUS100, pm90_n733l),
	ENTRY(INTEL, BUS100, pm90_n753g),
	ENTRY(INTEL, BUS100, pm90_n753h),
	ENTRY(INTEL, BUS100, pm90_n753i),
	ENTRY(INTEL, BUS100, pm90_n753j),
	ENTRY(INTEL, BUS100, pm90_n753k),
	ENTRY(INTEL, BUS100, pm90_n753l),
	ENTRY(INTEL, BUS100, pm90_n773g),
	ENTRY(INTEL, BUS100, pm90_n773h),
	ENTRY(INTEL, BUS100, pm90_n773i),
	ENTRY(INTEL, BUS100, pm90_n773j),
	ENTRY(INTEL, BUS100, pm90_n773k),
	ENTRY(INTEL, BUS100, pm90_n773l),
	ENTRY(INTEL, BUS100, pm90_n738),
	ENTRY(INTEL, BUS100, pm90_n758),
	ENTRY(INTEL, BUS100, pm90_n778),

	ENTRY(INTEL, BUS133, pm90_n710),
	ENTRY(INTEL, BUS100, pm90_n715a),
	ENTRY(INTEL, BUS100, pm90_n715b),
	ENTRY(INTEL, BUS100, pm90_n715c),
	ENTRY(INTEL, BUS100, pm90_n715d),
	ENTRY(INTEL, BUS100, pm90_n725a),
	ENTRY(INTEL, BUS100, pm90_n725b),
	ENTRY(INTEL, BUS100, pm90_n725c),
	ENTRY(INTEL, BUS100, pm90_n725d),
	ENTRY(INTEL, BUS133, pm90_n730),
	ENTRY(INTEL, BUS100, pm90_n735a),
	ENTRY(INTEL, BUS100, pm90_n735b),
	ENTRY(INTEL, BUS100, pm90_n735c),
	ENTRY(INTEL, BUS100, pm90_n735d),
	ENTRY(INTEL, BUS133, pm90_n740),
	ENTRY(INTEL, BUS100, pm90_n745a),
	ENTRY(INTEL, BUS100, pm90_n745b),
	ENTRY(INTEL, BUS100, pm90_n745c),
	ENTRY(INTEL, BUS100, pm90_n745d),
	ENTRY(INTEL, BUS133, pm90_n750),
	ENTRY(INTEL, BUS100, pm90_n755a),
	ENTRY(INTEL, BUS100, pm90_n755b),
	ENTRY(INTEL, BUS100, pm90_n755c),
	ENTRY(INTEL, BUS100, pm90_n755d),
	ENTRY(INTEL, BUS133, pm90_n760),
	ENTRY(INTEL, BUS100, pm90_n765a),
	ENTRY(INTEL, BUS100, pm90_n765b),
	ENTRY(INTEL, BUS100, pm90_n765c),
	ENTRY(INTEL, BUS100, pm90_n765e),
	ENTRY(INTEL, BUS133, pm90_n770),

	ENTRY(VIA,   BUS100, C7M_770_ULV),
	ENTRY(VIA,   BUS100, C7M_779_ULV),
	ENTRY(VIA,   BUS100, C7M_772_ULV),
	ENTRY(VIA,   BUS100, C7M_771),
	ENTRY(VIA,   BUS100, C7M_775_ULV),
	ENTRY(VIA,   BUS100, C7M_754),
	ENTRY(VIA,   BUS100, C7M_764),
	ENTRY(VIA,   BUS133, C7M_765),
	ENTRY(VIA,   BUS100, C7M_784),
	ENTRY(VIA,   BUS133, C7M_785),
	ENTRY(VIA,   BUS100, C7M_794),
	ENTRY(VIA,   BUS133, C7M_795),

	ENTRY(VIA,   BUS100, eden90_1000),
};

#define MSR2MHZ(msr, bus) \
	(((((int) (msr) >> 8) & 0xff) * (bus) + 50) / 100)
#define MSR2MV(msr) \
	(((int) (msr) & 0xff) * 16 + 700)

static struct fqlist *est_fqlist;

extern int setperf_prio;
extern int perflevel;

void p4_get_bus_clock(struct cpu_info *);
void p3_get_bus_clock(struct cpu_info *);
void cyrix3_get_bus_clock(struct cpu_info *);

#if NACPICPU > 0
struct fqlist * est_acpi_init(void);
void est_acpi_pss_changed(struct acpicpu_pss *, int);

struct fqlist *
est_acpi_init(void)
{
	struct acpicpu_pss *pss;
	struct fqlist *acpilist;
	int nstates, i;
	int high, low;

	if ((nstates = acpicpu_fetch_pss(&pss)) == 0)
		goto nolist;

	high = pss[0].pss_core_freq;
	low = pss[nstates - 1].pss_core_freq;
	if (high - low <= 0)
		goto nolist;

	if ((acpilist = malloc(sizeof(struct fqlist), M_DEVBUF, M_NOWAIT))
	    == NULL)
		goto nolist;

	if ((acpilist->table = mallocarray(nstates, sizeof(struct est_op),
	    M_DEVBUF, M_NOWAIT)) == NULL)
		goto notable;

	acpilist->n = nstates;

	for (i = 0; i < nstates; i++) {
		acpilist->table[i].mhz = pss[i].pss_core_freq;
		acpilist->table[i].ctrl = pss[i].pss_ctrl;
		acpilist->table[i].pct =
		    (pss[i].pss_core_freq - low) * 100 / (high - low);
	}

	acpicpu_set_notify(est_acpi_pss_changed);

	return acpilist;

notable:
	free(acpilist, M_DEVBUF, sizeof(*acpilist));
	acpilist = NULL;
nolist:
	return NULL;
}

void
est_acpi_pss_changed(struct acpicpu_pss *pss, int npss)
{
	struct fqlist *acpilist;
	int needtran = 1, i;
	int high, low;
	u_int64_t msr;
	u_int16_t cur;

	msr = rdmsr(MSR_PERF_STATUS);
	cur = msr & 0xffff;

	high = pss[0].pss_core_freq;
	low = pss[npss - 1].pss_core_freq;
	if (high - low <= 0) {
		printf("est_acpi_pss_changed: new est state has no "
		    "speed step\n");
		return;
	}

	if ((acpilist = malloc(sizeof(struct fqlist), M_DEVBUF, M_NOWAIT))
	    == NULL) {
		printf("est_acpi_pss_changed: cannot allocate memory for new "
		    "est state\n");
		return;
	}

	if ((acpilist->table = mallocarray(npss, sizeof(struct est_op),
	    M_DEVBUF, M_NOWAIT)) == NULL) {
		printf("est_acpi_pss_changed: cannot allocate memory for new "
		    "operating points\n");
		free(acpilist, M_DEVBUF, sizeof(*acpilist));
		return;
	}

	for (i = 0; i < npss; i++) {
		acpilist->table[i].mhz = pss[i].pss_core_freq;
		acpilist->table[i].ctrl = pss[i].pss_ctrl;
		acpilist->table[i].pct =
		    (pss[i].pss_core_freq - low) * 100 / (high - low);
		if (pss[i].pss_ctrl == cur)
			needtran = 0;
	}

	free(est_fqlist->table, M_DEVBUF, npss * sizeof(struct est_op));
	free(est_fqlist, M_DEVBUF, sizeof *est_fqlist);
	est_fqlist = acpilist;

	if (needtran) {
		est_setperf(perflevel);
	}
}
#endif

void
est_init(struct cpu_info *ci, int vendor)
{
	const char *cpu_device = ci->ci_dev->dv_xname;
	int i, low, high;
	u_int64_t msr;
	u_int16_t idhi, idlo, cur;
	u_int8_t crhi, crlo, crcur;
	struct fqlist *fql;
	struct fqlist *fake_fqlist;
	struct est_op *fake_table;

	if (setperf_prio > 3)
		return;

#if NACPICPU > 0
	est_fqlist = est_acpi_init();
#endif

	/* bus_clock is only used if we can't get values from ACPI */
	if (est_fqlist == NULL) {
		if (vendor == CPUVENDOR_VIA)
			cyrix3_get_bus_clock(ci);
		else if (ci->ci_family == 0xf)
			p4_get_bus_clock(ci);
		else if (ci->ci_family == 6)
			p3_get_bus_clock(ci);
	}

	/*
	 * Interpreting the values of PERF_STATUS is not valid
	 * on recent processors so don't do it on anything unknown
	 */
	if (est_fqlist == NULL && bus_clock != 0) {
		msr = rdmsr(MSR_PERF_STATUS);
		idhi = (msr >> 32) & 0xffff;
		idlo = (msr >> 48) & 0xffff;
		cur = msr & 0xffff;
		crhi = (idhi  >> 8) & 0xff;
		crlo = (idlo  >> 8) & 0xff;
		crcur = (cur >> 8) & 0xff;
		/*
		 * Find an entry which matches (vendor, bus_clock, idhi, idlo)
		 */
		for (i = 0; i < nitems(est_cpus); i++) {
			fql = &est_cpus[i];
			if (vendor == fql->vendor && bus_clock == BUS_CLK(fql)
			    && idhi == fql->table[0].ctrl
			    && idlo == fql->table[fql->n - 1].ctrl) {
				est_fqlist = fql;
				break;
			}
		}
	}

	if (est_fqlist == NULL && bus_clock != 0) {
		if (crhi == 0 || crcur == 0 || crlo > crhi ||
		    crcur < crlo || crcur > crhi) {
			/*
			 * Do complain about other weirdness, because we first
			 * want to know about it, before we decide what to do
			 * with it.
			 */
			printf("%s: EST: strange msr value 0x%016llx\n",
			    cpu_device, msr);
			return;
		}
		if (crlo == 0 || crhi == crlo) {
			/*
			 * Don't complain about these cases, and silently
			 * disable EST: - A lowest clock ratio of 0, which
			 * seems to happen on all Pentium 4's that report EST.
			 * - An equal highest and lowest clock ratio, which
			 * happens on at least the Core 2 Duo X6800, maybe on 
			 * newer models too.
			 */
			return;
		}

		printf("%s: unknown Enhanced SpeedStep CPU, msr 0x%016llx\n",
		    cpu_device, msr);
		/*
		 * Generate a fake table with the power states we know.
		 */

		if ((fake_fqlist = malloc(sizeof(struct fqlist), M_DEVBUF,
		    M_NOWAIT)) == NULL) {
			printf("%s: EST: cannot allocate memory for fake "
			    "list\n", cpu_device);
			return;
		}


		if ((fake_table = mallocarray(3, sizeof(struct est_op),
		    M_DEVBUF, M_NOWAIT)) == NULL) {
			free(fake_fqlist, M_DEVBUF, sizeof(struct fqlist));
			printf("%s: EST: cannot allocate memory for fake "
			    "table\n", cpu_device);
			return;
		}
		fake_table[0].ctrl = idhi;
		fake_table[0].mhz = MSR2MHZ(idhi, bus_clock);
		if (cur == idhi || cur == idlo) {
			printf("%s: using only highest and lowest power "
			       "states\n", cpu_device);

			fake_table[0].pct = 51;

			fake_table[1].ctrl = idlo;
			fake_table[1].mhz = MSR2MHZ(idlo, bus_clock);
			fake_table[1].pct = 0;
			fake_fqlist->n = 2;
		} else {
			printf("%s: using only highest, current and lowest "
			    "power states\n", cpu_device);

			fake_table[0].pct = 67;

			fake_table[1].ctrl = cur;
			fake_table[1].mhz = MSR2MHZ(cur, bus_clock);
			fake_table[1].pct = 34;

			fake_table[2].ctrl = idlo;
			fake_table[2].mhz = MSR2MHZ(idlo, bus_clock);
			fake_table[2].pct = 0;
			fake_fqlist->n = 3;
		}

		fake_fqlist->vendor = vendor;
		fake_fqlist->table = fake_table;
		est_fqlist = fake_fqlist;
	}

	if (est_fqlist == NULL)
		return;

	if (est_fqlist->n < 2)
		goto nospeedstep;

	low = est_fqlist->table[est_fqlist->n - 1].mhz;
	high = est_fqlist->table[0].mhz;
	if (low == high)
		goto nospeedstep;

	perflevel = (cpuspeed - low) * 100 / (high - low);

	printf("%s: Enhanced SpeedStep %d MHz", cpu_device, cpuspeed);

	/*
	 * OK, tell the user the available frequencies.
	 */
	printf(": speeds: ");
	for (i = 0; i < est_fqlist->n; i++)
		printf("%d%s", est_fqlist->table[i].mhz, i < est_fqlist->n - 1
		    ?  ", " : " MHz\n");

	cpu_setperf = est_setperf;
	setperf_prio = 3;

	return;

nospeedstep:
	/*
	 * While est_fqlist can point into the static est_cpus[],
	 * it can't fail in that case and therefore can't reach here.
	 */
	free(est_fqlist->table, M_DEVBUF, 0);
	free(est_fqlist, M_DEVBUF, sizeof(*est_fqlist));
}

void
est_setperf(int level)
{
	int i;
	uint64_t msr;

	if (est_fqlist == NULL)
		return;

	for (i = 0; i < est_fqlist->n; i++) {
		if (level >= est_fqlist->table[i].pct)
			break;
	}

	msr = rdmsr(MSR_PERF_CTL);
	msr &= ~0xffffULL;
	msr |= est_fqlist->table[i].ctrl;

	wrmsr(MSR_PERF_CTL, msr);
	cpuspeed = est_fqlist->table[i].mhz;
}
