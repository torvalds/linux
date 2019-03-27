/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 EMC Corp.
 * All rights reserved.
 *
 * Copyright (C) 2012-2013 Intel Corporation
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

#include <sys/param.h>
#include <sys/ioccom.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/endian.h>

#include "nvmecontrol.h"

/*
 * Intel specific log pages from
 * http://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/ssd-dc-p3700-spec.pdf
 *
 * Though the version as of this date has a typo for the size of log page 0xca,
 * offset 147: it is only 1 byte, not 6.
 */
static void
print_intel_temp_stats(const struct nvme_controller_data *cdata __unused, void *buf, uint32_t size __unused)
{
	struct intel_log_temp_stats	*temp = buf;

	printf("Intel Temperature Log\n");
	printf("=====================\n");

	printf("Current:                        ");
	print_temp(temp->current);
	printf("Overtemp Last Flags             %#jx\n", (uintmax_t)temp->overtemp_flag_last);
	printf("Overtemp Lifetime Flags         %#jx\n", (uintmax_t)temp->overtemp_flag_life);
	printf("Max Temperature                 ");
	print_temp(temp->max_temp);
	printf("Min Temperature                 ");
	print_temp(temp->min_temp);
	printf("Max Operating Temperature       ");
	print_temp(temp->max_oper_temp);
	printf("Min Operating Temperature       ");
	print_temp(temp->min_oper_temp);
	printf("Estimated Temperature Offset:   %ju C/K\n", (uintmax_t)temp->est_offset);
}

/*
 * Format from Table 22, section 5.7 IO Command Latency Statistics.
 * Read and write stats pages have identical encoding.
 */
static void
print_intel_read_write_lat_log(const struct nvme_controller_data *cdata __unused, void *buf, uint32_t size __unused)
{
	const char *walker = buf;
	int i;

	printf("Major:                         %d\n", le16dec(walker + 0));
	printf("Minor:                         %d\n", le16dec(walker + 2));
	for (i = 0; i < 32; i++)
		printf("%4dus-%4dus:                 %ju\n", i * 32, (i + 1) * 32, (uintmax_t)le32dec(walker + 4 + i * 4));
	for (i = 1; i < 32; i++)
		printf("%4dms-%4dms:                 %ju\n", i, i + 1, (uintmax_t)le32dec(walker + 132 + i * 4));
	for (i = 1; i < 32; i++)
		printf("%4dms-%4dms:                 %ju\n", i * 32, (i + 1) * 32, (uintmax_t)le32dec(walker + 256 + i * 4));
}

static void
print_intel_read_lat_log(const struct nvme_controller_data *cdata __unused, void *buf, uint32_t size)
{

	printf("Intel Read Latency Log\n");
	printf("======================\n");
	print_intel_read_write_lat_log(cdata, buf, size);
}

static void
print_intel_write_lat_log(const struct nvme_controller_data *cdata __unused, void *buf, uint32_t size)
{

	printf("Intel Write Latency Log\n");
	printf("=======================\n");
	print_intel_read_write_lat_log(cdata, buf, size);
}

/*
 * Table 19. 5.4 SMART Attributes. Others also implement this and some extra data not documented.
 */
void
print_intel_add_smart(const struct nvme_controller_data *cdata __unused, void *buf, uint32_t size __unused)
{
	uint8_t *walker = buf;
	uint8_t *end = walker + 150;
	const char *name;
	uint64_t raw;
	uint8_t normalized;

	static struct kv_name kv[] =
	{
		{ 0xab, "Program Fail Count" },
		{ 0xac, "Erase Fail Count" },
		{ 0xad, "Wear Leveling Count" },
		{ 0xb8, "End to End Error Count" },
		{ 0xc7, "CRC Error Count" },
		{ 0xe2, "Timed: Media Wear" },
		{ 0xe3, "Timed: Host Read %" },
		{ 0xe4, "Timed: Elapsed Time" },
		{ 0xea, "Thermal Throttle Status" },
		{ 0xf0, "Retry Buffer Overflows" },
		{ 0xf3, "PLL Lock Loss Count" },
		{ 0xf4, "NAND Bytes Written" },
		{ 0xf5, "Host Bytes Written" },
	};

	printf("Additional SMART Data Log\n");
	printf("=========================\n");
	/*
	 * walker[0] = Key
	 * walker[1,2] = reserved
	 * walker[3] = Normalized Value
	 * walker[4] = reserved
	 * walker[5..10] = Little Endian Raw value
	 *	(or other represenations)
	 * walker[11] = reserved
	 */
	while (walker < end) {
		name = kv_lookup(kv, nitems(kv), *walker);
		normalized = walker[3];
		raw = le48dec(walker + 5);
		switch (*walker){
		case 0:
			break;
		case 0xad:
			printf("%-32s: %3d min: %u max: %u ave: %u\n", name, normalized,
			    le16dec(walker + 5), le16dec(walker + 7), le16dec(walker + 9));
			break;
		case 0xe2:
			printf("%-32s: %3d %.3f%%\n", name, normalized, raw / 1024.0);
			break;
		case 0xea:
			printf("%-32s: %3d %d%% %d times\n", name, normalized, walker[5], le32dec(walker+6));
			break;
		default:
			printf("%-32s: %3d %ju\n", name, normalized, (uintmax_t)raw);
			break;
		}
		walker += 12;
	}
}

NVME_LOGPAGE(intel_temp,
    INTEL_LOG_TEMP_STATS,		"intel", "Temperature Stats",
    print_intel_temp_stats,		sizeof(struct intel_log_temp_stats));
NVME_LOGPAGE(intel_rlat,
    INTEL_LOG_READ_LAT_LOG,		"intel", "Read Latencies",
    print_intel_read_lat_log,		DEFAULT_SIZE);
NVME_LOGPAGE(intel_wlat,
    INTEL_LOG_WRITE_LAT_LOG,		"intel", "Write Latencies",
    print_intel_write_lat_log,		DEFAULT_SIZE);
NVME_LOGPAGE(intel_smart,
    INTEL_LOG_ADD_SMART,		"intel", "Extra Health/SMART Data",
    print_intel_add_smart,		DEFAULT_SIZE);
