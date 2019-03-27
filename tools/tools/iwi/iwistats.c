/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2005
 *	Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
	get_statistics((argc > 1) ? argv[1] : "iwi0");

	return EX_OK;
}

static const struct statistic {
	int 		index;
	const char	*desc;
} tbl[] = {
	{  1, "Current transmission rate" },
	{  2, "Fragmentation threshold" },
	{  3, "RTS threshold" },
	{  4, "Number of frames submitted for transfer" },
	{  5, "Number of frames transmitted" },
	{  6, "Number of unicast frames transmitted" },
	{  7, "Number of unicast 802.11b frames transmitted at 1Mb/s" },
	{  8, "Number of unicast 802.11b frames transmitted at 2Mb/s" },
	{  9, "Number of unicast 802.11b frames transmitted at 5.5Mb/s" },
	{ 10, "Number of unicast 802.11b frames transmitted at 11Mb/s" },

	{ 19, "Number of unicast 802.11g frames transmitted at 1Mb/s" },
	{ 20, "Number of unicast 802.11g frames transmitted at 2Mb/s" },
	{ 21, "Number of unicast 802.11g frames transmitted at 5.5Mb/s" },
	{ 22, "Number of unicast 802.11g frames transmitted at 6Mb/s" },
	{ 23, "Number of unicast 802.11g frames transmitted at 9Mb/s" },
	{ 24, "Number of unicast 802.11g frames transmitted at 11Mb/s" },
	{ 25, "Number of unicast 802.11g frames transmitted at 12Mb/s" },
	{ 26, "Number of unicast 802.11g frames transmitted at 18Mb/s" },
	{ 27, "Number of unicast 802.11g frames transmitted at 24Mb/s" },
	{ 28, "Number of unicast 802.11g frames transmitted at 36Mb/s" },
	{ 29, "Number of unicast 802.11g frames transmitted at 48Mb/s" },
	{ 30, "Number of unicast 802.11g frames transmitted at 54Mb/s" },
	{ 31, "Number of multicast frames transmitted" },
	{ 32, "Number of multicast 802.11b frames transmitted at 1Mb/s" },
	{ 33, "Number of multicast 802.11b frames transmitted at 2Mb/s" },
	{ 34, "Number of multicast 802.11b frames transmitted at 5.5Mb/s" },
	{ 35, "Number of multicast 802.11b frames transmitted at 11Mb/s" },

	{ 44, "Number of multicast 802.11g frames transmitted at 1Mb/s" },
	{ 45, "Number of multicast 802.11g frames transmitted at 2Mb/s" },
	{ 46, "Number of multicast 802.11g frames transmitted at 5.5Mb/s" },
	{ 47, "Number of multicast 802.11g frames transmitted at 6Mb/s" },
	{ 48, "Number of multicast 802.11g frames transmitted at 9Mb/s" },
	{ 49, "Number of multicast 802.11g frames transmitted at 11Mb/s" },
	{ 50, "Number of multicast 802.11g frames transmitted at 12Mb/s" },
	{ 51, "Number of multicast 802.11g frames transmitted at 18Mb/s" },
	{ 52, "Number of multicast 802.11g frames transmitted at 24Mb/s" },
	{ 53, "Number of multicast 802.11g frames transmitted at 36Mb/s" },
	{ 54, "Number of multicast 802.11g frames transmitted at 48Mb/s" },
	{ 55, "Number of multicast 802.11g frames transmitted at 54Mb/s" },
	{ 56, "Number of transmission retries" },
	{ 57, "Number of transmission failures" },
	{ 58, "Number of CRC errors" },

	{ 61, "Number of full scans" },
	{ 62, "Number of partial scans" },

	{ 64, "Number of bytes transmitted" },
	{ 65, "Current RSSI" },
	{ 66, "Number of beacons received" },
	{ 67, "Number of beacons missed" },

	{ -1, NULL }
};

static void
get_statistics(const char *iface)
{
	static uint32_t stats[256];
	const struct statistic *stat;
	char oid[32];
	size_t len;
	int ifaceno;

	if (sscanf(iface, "iwi%u", &ifaceno) != 1)
		errx(EX_DATAERR, "Invalid interface name '%s'", iface);

	len = sizeof(stats);
	(void)snprintf(oid, sizeof(oid), "dev.iwi.%u.stats", ifaceno);
	if (sysctlbyname(oid, stats, &len, NULL, 0) == -1)
		err(EX_OSERR, "Can't retrieve statistics");

	for (stat = tbl; stat->index != -1; stat++)
		(void)printf("%-60s[%u]\n", stat->desc, stats[stat->index]);
}
