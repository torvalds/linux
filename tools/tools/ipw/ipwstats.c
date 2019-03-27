/*-
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <sysexits.h>

static void	get_statistics(const char *);

int
main(int argc, char **argv)
{
	get_statistics((argc > 1) ? argv[1] : "ipw0");

	return EX_OK;
}

struct statistic {
	int index;
	const char *desc;
	int unit;
#define INT		1
#define HEX		2
#define MASK		HEX
#define PERCENTAGE	3
#define BOOL		4
};

/*- 
 * TIM  = Traffic Information Message
 * DTIM = Delivery TIM
 * ATIM = Announcement TIM
 * PSP  = Power Save Poll
 * RTS  = Request To Send
 * CTS  = Clear To Send
 * RSSI = Received Signal Strength Indicator
 */

static const struct statistic tbl[] = {
	{ 1, "Number of frames submitted for transfer", INT },
	{ 2, "Number of frames transmitted", INT },
	{ 3, "Number of unicast frames transmitted", INT },
	{ 4, "Number of unicast frames transmitted at 1Mb/s", INT },
	{ 5, "Number of unicast frames transmitted at 2Mb/s", INT },
	{ 6, "Number of unicast frames transmitted at 5.5Mb/s", INT },
	{ 7, "Number of unicast frames transmitted at 11Mb/s", INT },

	{ 13, "Number of multicast frames transmitted at 1Mb/s", INT },
	{ 14, "Number of multicast frames transmitted at 2Mb/s", INT },
	{ 15, "Number of multicast frames transmitted at 5.5Mb/s", INT },
	{ 16, "Number of multicast frames transmitted at 11Mb/s", INT },

	{ 21, "Number of null frames transmitted", INT },
	{ 22, "Number of RTS frames transmitted", INT },
	{ 23, "Number of CTS frames transmitted", INT },
	{ 24, "Number of ACK frames transmitted", INT },
	{ 25, "Number of association requests transmitted", INT },
	{ 26, "Number of association responses transmitted", INT },
	{ 27, "Number of reassociation requests transmitted", INT },
	{ 28, "Number of reassociation responses transmitted", INT },
	{ 29, "Number of probe requests transmitted", INT },
	{ 30, "Number of probe responses transmitted", INT },
	{ 31, "Number of beacons transmitted", INT },
	{ 32, "Number of ATIM frames transmitted", INT },
	{ 33, "Number of disassociation requests transmitted", INT },
	{ 34, "Number of authentication requests transmitted", INT },
	{ 35, "Number of deauthentication requests transmitted", INT },

	{ 41, "Number of bytes transmitted", INT },
	{ 42, "Number of transmission retries", INT },
	{ 43, "Number of transmission retries at 1Mb/s", INT },
	{ 44, "Number of transmission retries at 2Mb/s", INT },
	{ 45, "Number of transmission retries at 5.5Mb/s", INT },
	{ 46, "Number of transmission retries at 11Mb/s", INT },

	{ 51, "Number of transmission failures", INT },

	{ 54, "Number of transmission aborted due to slow DMA setup", INT },

	{ 56, "Number of disassociation failures", INT },

	{ 58, "Number of spanning tree frames transmitted", INT },
	{ 59, "Number of transmission errors due to missing ACK", INT },

	{ 61, "Number of frames received", INT },
	{ 62, "Number of unicast frames received", INT },
	{ 63, "Number of unicast frames received at 1Mb/s", INT },
	{ 64, "Number of unicast frames received at 2Mb/s", INT },
	{ 65, "Number of unicast frames received at 5.5Mb/s", INT },
	{ 66, "Number of unicast frames received at 11Mb/s", INT },

	{ 71, "Number of multicast frames received", INT },
	{ 72, "Number of multicast frames received at 1Mb/s", INT },
	{ 73, "Number of multicast frames received at 2Mb/s", INT },
	{ 74, "Number of multicast frames received at 5.5Mb/s", INT },
	{ 75, "Number of multicast frames received at 11Mb/s", INT },

	{ 80, "Number of null frames received", INT },
	{ 81, "Number of poll frames received", INT },
	{ 82, "Number of RTS frames received", INT },
	{ 83, "Number of CTS frames received", INT },
	{ 84, "Number of ACK frames received", INT },
	{ 85, "Number of CF-End frames received", INT },
	{ 86, "Number of CF-End + CF-Ack frames received", INT },
	{ 87, "Number of association requests received", INT },
	{ 88, "Number of association responses received", INT },
	{ 89, "Number of reassociation requests received", INT },
	{ 90, "Number of reassociation responses received", INT },
	{ 91, "Number of probe requests received", INT },
	{ 92, "Number of probe responses received", INT },
	{ 93, "Number of beacons received", INT },
	{ 94, "Number of ATIM frames received", INT },
	{ 95, "Number of disassociation requests received", INT },
	{ 96, "Number of authentication requests received", INT },
	{ 97, "Number of deauthentication requests received", INT },

	{ 101, "Number of bytes received", INT },
	{ 102, "Number of frames with a bad CRC received", INT },
	{ 103, "Number of frames with a bad CRC received at 1Mb/s", INT },
	{ 104, "Number of frames with a bad CRC received at 2Mb/s", INT },
	{ 105, "Number of frames with a bad CRC received at 5.5Mb/s", INT },
	{ 106, "Number of frames with a bad CRC received at 11Mb/s", INT },

	{ 112, "Number of duplicated frames received at 1Mb/s", INT },
	{ 113, "Number of duplicated frames received at 2Mb/s", INT },
	{ 114, "Number of duplicated frames received at 5.5Mb/s", INT },
	{ 115, "Number of duplicated frames received at 11Mb/s", INT },

	{ 119, "Number of duplicated frames received", INT },

	{ 123, "Number of frames with a bad protocol received", INT },
	{ 124, "Boot time", INT },
	{ 125, "Number of frames dropped due to no buffer", INT },
	{ 126, "Number of frames dropped due to slow DMA setup", INT },

	{ 128, "Number of frames dropped due to missing fragment", INT },
	{ 129, "Number of frames dropped due to non-seq fragment", INT },
	{ 130, "Number of frames dropped due to missing first frame", INT },
	{ 131, "Number of frames dropped due to incomplete frame", INT },

	{ 137, "Number of PSP adapter suspends", INT },
	{ 138, "Number of PSP beacon timeouts", INT },
	{ 139, "Number of PSP PsPollResponse timeouts", INT },
	{ 140, "Number of PSP mcast frame timeouts", INT },
	{ 141, "Number of PSP DTIM frames received", INT },
	{ 142, "Number of PSP TIM frames received", INT },
	{ 143, "PSP station Id", INT },

	{ 147, "RTC time of last association", INT },
	{ 148, "Percentage of missed beacons", PERCENTAGE },
	{ 149, "Percentage of missed transmission retries", PERCENTAGE },

	{ 151, "Number of access points in access points table", INT },

	{ 153, "Number of associations", INT },
	{ 154, "Number of association failures", INT },
	{ 156, "Number of full scans", INT },
	{ 157, "Card disabled", BOOL },

	{ 160, "RSSI at time of association", INT },
	{ 161, "Number of reassociations due to no probe response", INT },
	{ 162, "Number of reassociations due to poor line quality", INT },
	{ 163, "Number of reassociations due to load", INT },
	{ 164, "Number of reassociations due to access point RSSI level", INT },
	{ 165, "Number of reassociations due to load leveling", INT },

	{ 170, "Number of times authentication failed", INT },
	{ 171, "Number of times authentication response failed", INT },
	{ 172, "Number of entries in association table", INT },
	{ 173, "Average RSSI", INT },

	{ 176, "Self test status", INT },
	{ 177, "Power mode", INT },
	{ 178, "Power index", INT },
	{ 179, "IEEE country code", HEX },
	{ 180, "Channels supported for this country", MASK },
	{ 181, "Number of adapter warm resets", INT },
	{ 182, "Beacon interval", INT },

	{ 184, "Princeton version", INT },
	{ 185, "Antenna diversity disabled", BOOL },
#if notset
	{ 186, "CCA RSSI", INT },
	{ 187, "Number of times EEPROM updated", INT },
#endif
	{ 188, "Beacon intervals between DTIM", INT },
	{ 189, "Current channel", INT },
	{ 190, "RTC time", INT },
	{ 191, "Operating mode", INT },
	{ 192, "Transmission rate", HEX },
	{ 193, "Supported transmission rates", MASK },
	{ 194, "ATIM window", INT },
	{ 195, "Supported basic transmission rates", MASK },
	{ 196, "Adapter highest rate", HEX },
	{ 197, "Access point highest rate", HEX },
	{ 198, "Management frame capability", BOOL },
	{ 199, "Type of authentication", INT },
	{ 200, "Adapter card platform type", INT },
	{ 201, "RTS threshold", INT },
	{ 202, "International mode", BOOL },
	{ 203, "Fragmentation threshold", INT },

	{ 209, "MAC version", INT },
	{ 210, "MAC revision", INT },
	{ 211, "Radio version", INT },
	{ 212, "NIC manufacturing date+time", INT },
	{ 213, "Microcode version", INT },
	{ 214, "RF switch state", INT },

	{ 0, NULL, 0 }
};

static void
get_statistics(const char *iface)
{
	static uint32_t stats[256];
	const struct statistic *stat;
	char oid[32];
	int ifaceno, len;

	if (sscanf(iface, "ipw%u", &ifaceno) != 1)
		errx(EX_DATAERR, "Invalid interface name '%s'", iface);

	len = sizeof stats;
	snprintf(oid, sizeof oid, "dev.ipw.%u.stats", ifaceno);
	if (sysctlbyname(oid, stats, &len, NULL, 0) == -1)
		err(EX_OSERR, "Can't retrieve statistics");

	for (stat = tbl; stat->index != 0; stat++) {
		printf("%-60s[", stat->desc);
		switch (stat->unit) {
		case INT:
			printf("%u", stats[stat->index]);
			break;
		case BOOL:
			printf(stats[stat->index] ? "true" : "false");
			break;
		case PERCENTAGE:
			printf("%u%%", stats[stat->index]);
			break;
		case HEX:
		default:
			printf("0x%08X", stats[stat->index]);
		}
		printf("]\n");
	}
}
