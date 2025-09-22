/*	$OpenBSD: timetest.c,v 1.5 2025/08/17 08:43:03 phessler Exp $ */

/*
 * Copyright (c) 2022 Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>
#include <sys/stdint.h>
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct timetest {
	char *descr;
	char *timezone;
	time_t time;
	struct tm local_tm;
	struct tm gmt_tm;
};

static int tm_match(struct tm * tm1, struct tm *tm2) {
	if (tm2->tm_year != tm1->tm_year ||
	    tm2->tm_mon != tm1->tm_mon ||
	    tm2->tm_mday != tm1->tm_mday ||
	    tm2->tm_hour != tm1->tm_hour ||
	    tm2->tm_min != tm1->tm_min ||
	    tm2->tm_sec != tm1->tm_sec ||
	    tm2->tm_wday != tm1->tm_wday ||
	    tm2->tm_yday != tm1->tm_yday ||
	    tm2->tm_yday != tm1->tm_yday ||
	    tm2->tm_isdst != tm1->tm_isdst ||
	    tm2->tm_gmtoff != tm1->tm_gmtoff ||
	    strcmp(tm2->tm_zone, tm1->tm_zone) != 0)
		return 0;
	return 1;
}

struct timetest timetests[] = {
	{
		.descr="moon",
		.timezone="posix/America/Edmonton",
		.time=-16751025,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=20,
			.tm_hour=19,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=5,
			.tm_yday=170,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=21,
			.tm_hour=2,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=6,
			.tm_yday=171,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="moon",
		.timezone="right/GMT",
		.time=-16751025,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=21,
			.tm_hour=2,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=6,
			.tm_yday=171,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=21,
			.tm_hour=2,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=6,
			.tm_yday=171,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="moon",
		.timezone="right/America/Edmonton",
		.time=-16751025,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=20,
			.tm_hour=19,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=5,
			.tm_yday=170,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=5,
			.tm_mday=21,
			.tm_hour=2,
			.tm_min=56,
			.tm_sec=15,
			.tm_wday=6,
			.tm_yday=171,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch",
		.timezone="posix/America/Edmonton",
		.time=0,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=70,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=4,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch",
		.timezone="right/GMT",
		.time=0,
		.local_tm=		{
			.tm_year=70,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=4,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=70,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=4,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch",
		.timezone="right/America/Edmonton",
		.time=0,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=70,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=4,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch - 1",
		.timezone="posix/America/Edmonton",
		.time=-1,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch - 1",
		.timezone="right/GMT",
		.time=-1,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="epoch - 1",
		.timezone="right/America/Edmonton",
		.time=-1,
		.local_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=69,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=3,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy min",
		.timezone="posix/America/Edmonton",
		.time=-2147483648,
		.local_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=13,
			.tm_min=12,
			.tm_sec=0,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=-27232,
			.tm_zone="LMT"
		},
		.gmt_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=52,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy min",
		.timezone="right/GMT",
		.time=-2147483648,
		.local_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=52,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=52,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy min - 1",
		.timezone="posix/America/Edmonton",
		.time=-2147483649,
		.local_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=13,
			.tm_min=11,
			.tm_sec=59,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=-27232,
			.tm_zone="LMT"
		},
		.gmt_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=51,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy min - 1",
		.timezone="right/GMT",
		.time=-2147483649,
		.local_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=51,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=1,
			.tm_mon=11,
			.tm_mday=13,
			.tm_hour=20,
			.tm_min=45,
			.tm_sec=51,
			.tm_wday=5,
			.tm_yday=346,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy max",
		.timezone="posix/America/Edmonton",
		.time=2147483647,
		.local_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=18,
			.tm_hour=20,
			.tm_min=14,
			.tm_sec=7,
			.tm_wday=1,
			.tm_yday=17,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=14,
			.tm_sec=7,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy max",
		.timezone="right/GMT",
		.time=2147483647,
		.local_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=13,
			.tm_sec=40,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=14,
			.tm_sec=7,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy max + 1",
		.timezone="posix/America/Edmonton",
		.time=2147483648,
		.local_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=18,
			.tm_hour=20,
			.tm_min=14,
			.tm_sec=8,
			.tm_wday=1,
			.tm_yday=17,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=14,
			.tm_sec=8,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="legacy max + 1",
		.timezone="right/GMT",
		.time=2147483648,
		.local_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=13,
			.tm_sec=41,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=138,
			.tm_mon=0,
			.tm_mday=19,
			.tm_hour=3,
			.tm_min=14,
			.tm_sec=8,
			.tm_wday=2,
			.tm_yday=18,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="min",
		.timezone="posix/America/Edmonton",
		.time=INT64_MIN,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="min",
		.timezone="right/GMT",
		.time=INT64_MIN,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="max",
		.timezone="right/America/Edmonton",
		.time=9223372036854775807,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="max",
		.timezone="posix/America/Edmonton",
		.time=9223372036854775807,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="max",
		.timezone="right/GMT",
		.time=9223372036854775807,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="min",
		.timezone="right/America/Edmonton",
		.time=INT64_MIN,
		.local_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
		.gmt_tm=		{
			.tm_year=0,
			.tm_mon=0,
			.tm_mday=0,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="FAILURE"
		},
	},
	{
		.descr="maxint struct tm",
		.timezone="right/America/Edmonton",
		.time=67767976204675199,
		.local_tm=		{
                        .tm_year=2147481747,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=32,
			.tm_wday=4,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
                        .tm_year=2147481747,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=4,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="minint struct tm",
		.timezone="right/America/Edmonton",
		.time=-67768038398073601,
		.local_tm=		{
                        .tm_year=-2147483578,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=26,
			.tm_sec=7,
			.tm_wday=2,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=-27232,
			.tm_zone="LMT"
		},
		.gmt_tm=		{
                        .tm_year=-2147483578,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=2,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="maxint struct tm",
		.timezone="right/GMT",
		.time=67767976204675199,
		.local_tm=		{
                        .tm_year=2147481747,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=32,
			.tm_wday=4,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
                        .tm_year=2147481747,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=4,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="minint struct tm",
		.timezone="right/GMT",
		.time=-67768038398073601,
		.local_tm=		{
                        .tm_year=-2147483578,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=2,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
                        .tm_year=-2147483578,
			.tm_mon=0,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=2,
			.tm_yday=30,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="0000",
		.timezone="posix/America/Edmonton",
		.time=-62167219200,
		.local_tm=		{
			.tm_year=-1901,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=26,
			.tm_sec=8,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-27232,
			.tm_zone="LMT"
		},
		.gmt_tm=		{
			.tm_year=-1900,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=6,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="0000",
		.timezone="right/GMT",
		.time=-62167219200,
		.local_tm=		{
			.tm_year=-1900,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=6,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=-1900,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=6,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="0000",
		.timezone="right/America/Edmonton",
		.time=-62167219200,
		.local_tm=		{
			.tm_year=-1901,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=26,
			.tm_sec=8,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-27232,
			.tm_zone="LMT"
		},
		.gmt_tm=		{
			.tm_year=-1900,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=6,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="9999",
		.timezone="posix/America/Edmonton",
		.time=253402300799,
		.local_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="9999",
		.timezone="right/GMT",
		.time=253402300799,
		.local_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=32,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="9999",
		.timezone="right/America/Edmonton",
		.time=253402300799,
		.local_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=32,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=8099,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=5,
			.tm_yday=364,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second - 1",
		.timezone="posix/America/Edmonton",
		.time=1483228825,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second",
		.timezone="posix/America/Edmonton",
		.time=1483228826,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second + 1",
		.timezone="posix/America/Edmonton",
		.time=1483228827,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second - 1",
		.timezone="right/GMT",
		.time=1483228825,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second",
		.timezone="right/GMT",
		.time=1483228826,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=23,
			.tm_min=59,
			.tm_sec=60,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second + 1",
		.timezone="right/GMT",
		.time=1483228827,
		.local_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="GMT"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second - 1",
		.timezone="right/America/Edmonton",
		.time=1483228825,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second",
		.timezone="right/America/Edmonton",
		.time=1483228826,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=16,
			.tm_min=59,
			.tm_sec=60,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="leap second + 1",
		.timezone="right/America/Edmonton",
		.time=1483228827,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=11,
			.tm_mday=31,
			.tm_hour=17,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=6,
			.tm_yday=365,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=117,
			.tm_mon=0,
			.tm_mday=1,
			.tm_hour=0,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=0,
			.tm_yday=0,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight - 1",
		.timezone="posix/America/Edmonton",
		.time=1457859599,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=1,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=8,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight",
		.timezone="posix/America/Edmonton",
		.time=1457859600,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=3,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=9,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight + 1",
		.timezone="posix/America/Edmonton",
		.time=1457859601,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=3,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=9,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight - 1",
		.timezone="right/America/Edmonton",
		.time=1457859625,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=1,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=9,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight",
		.timezone="right/America/Edmonton",
		.time=1457859626,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=3,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=9,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition standard to daylight + 1",
		.timezone="right/America/Edmonton",
		.time=1457859627,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=3,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=2,
			.tm_mday=13,
			.tm_hour=9,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=0,
			.tm_yday=72,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard - 1",
		.timezone="posix/America/Edmonton",
		.time=1478419199,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=7,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard",
		.timezone="posix/America/Edmonton",
		.time=1478419200,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=8,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard + 1",
		.timezone="posix/America/Edmonton",
		.time=1478419201,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=8,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard - 1",
		.timezone="right/America/Edmonton",
		.time=1478419225,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=59,
			.tm_sec=59,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=1,
			.tm_gmtoff=-21600,
			.tm_zone="MDT"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=8,
			.tm_min=0,
			.tm_sec=25,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard",
		.timezone="right/America/Edmonton",
		.time=1478419226,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=0,
			.tm_sec=0,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=8,
			.tm_min=0,
			.tm_sec=26,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
	{
		.descr="transition daylight to standard + 1",
		.timezone="right/America/Edmonton",
		.time=1478419227,
		.local_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=1,
			.tm_min=0,
			.tm_sec=1,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=-25200,
			.tm_zone="MST"
		},
		.gmt_tm=		{
			.tm_year=116,
			.tm_mon=10,
			.tm_mday=6,
			.tm_hour=8,
			.tm_min=0,
			.tm_sec=27,
			.tm_wday=0,
			.tm_yday=310,
			.tm_isdst=0,
			.tm_gmtoff=0,
			.tm_zone="UTC"
		},
	},
        {
		.descr = NULL,
	},
};

void printtm(FILE *f, struct tm *tm)
{
	fprintf(f, "\t\t{\n\t\t\t.tm_year=%d,\n\t\t\t.tm_mon=%d,\n\t\t\t"
	    ".tm_mday=%d,\n\t\t\t.tm_hour=%d,\n\t\t\t.tm_min=%d,\n\t\t\t"
	    ".tm_sec=%d,\n\t\t\t.tm_wday=%d,\n\t\t\t.tm_yday=%d,\n\t\t\t"
	    ".tm_isdst=%d,\n\t\t\t.tm_gmtoff=%ld,\n\t\t\t.tm_zone=\"%s\""
	    "\n\t\t},\n",
	    tm->tm_year,
	    tm->tm_mon,
	    tm->tm_mday,
	    tm->tm_hour,
	    tm->tm_min,
	    tm->tm_sec,
	    tm->tm_wday,
	    tm->tm_yday,
	    tm->tm_isdst,
	    tm->tm_gmtoff,
	    tm->tm_zone);
}

int dotimetest(struct timetest *test, int print)
{
	int failures = 0;
	struct tm local = {}, gmt = {};
	time_t converted;
	if (gmtime_r(&test->time, &gmt) == NULL) {
		memset(&gmt, 0, sizeof(gmt));
		gmt.tm_zone="FAILURE";
	} else {
		converted = timegm(&gmt);
		if (converted != test->time) {
			fprintf(stderr, "FAIL: test \"%s\", tz \"%s\" timegm "
			    "does not match expected value\n", test->descr,
			    test->timezone);
			fprintf(stderr, "expected: %lld\n", test->time);
			fprintf(stderr, "actual: %lld\n", converted);
			failures++;
		}
	}
	if (!tm_match(&test->gmt_tm, &gmt)) {
		fprintf(stderr, "FAIL: test \"%s\", tz \"%s\" gmtime_r does not"
		    " match expected value\n", test->descr, test->timezone);
		fprintf(stderr, "expected: ");
		printtm(stderr, &test->gmt_tm);
		fprintf(stderr, "actual: ");
		printtm(stderr, &gmt);
		failures++;
	}
	setenv("TZ", test->timezone, 1);
	if (localtime_r(&test->time, &local) == NULL) {
		memset(&local, 0, sizeof(local));
		local.tm_zone="FAILURE";
	} else {
		converted = mktime(&local);
		if (converted != test->time) {
			fprintf(stderr, "FAIL: test \"%s\", tz \"%s\" mktime "
			    "does not match expected value\n", test->descr,
			    test->timezone);
			fprintf(stderr, "expected: %lld\n", test->time);
			fprintf(stderr, "actual: %lld\n", converted);
			failures++;
		}
	}
	if (!tm_match(&test->local_tm, &local)) {
		fprintf(stderr, "FAIL: test \"%s\", tz \"%s\" localtime_r does "
		    "not match expected value\n", test->descr, test->timezone);
		fprintf(stderr, "expected: ");
		printtm(stderr, &test->local_tm);
		fprintf(stderr, "actual: ");
		printtm(stderr, &local);
		failures++;
	}
	if (print) {
		printf("\t{\n\t\t.descr=\"%s\",\n\t\t.timezone=\"%s\",\n\t\t"
		    ".time=%lld,\n", test->descr, test->timezone, test->time);
		printf("\t\t.local_tm=");
		printtm(stdout, &local);
		printf("\t\t.gmt_tm=");
		printtm(stdout, &gmt);
		printf("\t},\n");
	}
	return failures;
}


void printtmdescr(FILE *f, struct tm *tm, char * descr)
{
	fprintf(f, "%s: ", descr);
	printtm(f, tm);
}

int main() {
	int failures = 0;
	int verbose = 0;
	struct stat sb;
	size_t i;

	if (stat("/usr/share/zoneinfo/posix", &sb) == -1 ||
	    stat("/usr/share/zoneinfo/right", &sb) == -1) {
		fprintf(stderr, "POSIX time zones missing, run the following command:\n\n"
		    "\tmake -C ../../../../../share/zoneinfo other_two\n\n"
		    "SKIPPED\n");
		exit(0);
	}

	for (i = 0; timetests[i].descr != NULL; i++) {
		failures += dotimetest(&timetests[i], verbose);
	}
	if (failures)
		fprintf(stderr, "FAIL:  %d time test failures\n", failures);
	else
		printf("SUCCESS: no time test failures\n");
	exit(failures);
}

