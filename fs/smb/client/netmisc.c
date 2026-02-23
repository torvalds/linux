// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Error mapping routines from Samba libsmb/errormap.c
 *   Copyright (C) Andrew Tridgell 2001
 */

#include <linux/net.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <asm/div64.h>
#include <asm/byteorder.h>
#include <linux/inet.h>
#include "cifsfs.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb1proto.h"
#include "smberr.h"
#include "cifs_debug.h"
#include "nterr.h"

/*
 * Convert a string containing text IPv4 or IPv6 address to binary form.
 *
 * Returns 0 on failure.
 */
static int
cifs_inet_pton(const int address_family, const char *cp, int len, void *dst)
{
	int ret = 0;

	/* calculate length by finding first slash or NULL */
	if (address_family == AF_INET)
		ret = in4_pton(cp, len, dst, '\\', NULL);
	else if (address_family == AF_INET6)
		ret = in6_pton(cp, len, dst , '\\', NULL);

	cifs_dbg(NOISY, "address conversion returned %d for %*.*s\n",
		 ret, len, len, cp);
	if (ret > 0)
		ret = 1;
	return ret;
}

/*
 * Try to convert a string to an IPv4 address and then attempt to convert
 * it to an IPv6 address if that fails. Set the family field if either
 * succeeds. If it's an IPv6 address and it has a '%' sign in it, try to
 * treat the part following it as a numeric sin6_scope_id.
 *
 * Returns 0 on failure.
 */
int
cifs_convert_address(struct sockaddr *dst, const char *src, int len)
{
	int rc, alen, slen;
	const char *pct;
	char scope_id[13];
	struct sockaddr_in *s4 = (struct sockaddr_in *) dst;
	struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) dst;

	/* IPv4 address */
	if (cifs_inet_pton(AF_INET, src, len, &s4->sin_addr.s_addr)) {
		s4->sin_family = AF_INET;
		return 1;
	}

	/* attempt to exclude the scope ID from the address part */
	pct = memchr(src, '%', len);
	alen = pct ? pct - src : len;

	rc = cifs_inet_pton(AF_INET6, src, alen, &s6->sin6_addr.s6_addr);
	if (!rc)
		return rc;

	s6->sin6_family = AF_INET6;
	if (pct) {
		/* grab the scope ID */
		slen = len - (alen + 1);
		if (slen <= 0 || slen > 12)
			return 0;
		memcpy(scope_id, pct + 1, slen);
		scope_id[slen] = '\0';

		rc = kstrtouint(scope_id, 0, &s6->sin6_scope_id);
		rc = (rc == 0) ? 1 : 0;
	}

	return rc;
}

void
cifs_set_port(struct sockaddr *addr, const unsigned short int port)
{
	switch (addr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)addr)->sin_port = htons(port);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
		break;
	}
}

/* The following are taken from fs/ntfs/util.c */

#define NTFS_TIME_OFFSET ((u64)(369*365 + 89) * 24 * 3600 * 10000000)

/*
 * Convert the NT UTC (based 1601-01-01, in hundred nanosecond units)
 * into Unix UTC (based 1970-01-01, in seconds).
 */
struct timespec64
cifs_NTtimeToUnix(__le64 ntutc)
{
	struct timespec64 ts;
	/* BB what about the timezone? BB */

	/* Subtract the NTFS time offset, then convert to 1s intervals. */
	s64 t = le64_to_cpu(ntutc) - NTFS_TIME_OFFSET;
	u64 abs_t;

	/*
	 * Unfortunately can not use normal 64 bit division on 32 bit arch, but
	 * the alternative, do_div, does not work with negative numbers so have
	 * to special case them
	 */
	if (t < 0) {
		abs_t = -t;
		ts.tv_nsec = (time64_t)(do_div(abs_t, 10000000) * 100);
		ts.tv_nsec = -ts.tv_nsec;
		ts.tv_sec = -abs_t;
	} else {
		abs_t = t;
		ts.tv_nsec = (time64_t)do_div(abs_t, 10000000) * 100;
		ts.tv_sec = abs_t;
	}

	return ts;
}

/* Convert the Unix UTC into NT UTC. */
u64
cifs_UnixTimeToNT(struct timespec64 t)
{
	/* Convert to 100ns intervals and then add the NTFS time offset. */
	return (u64) t.tv_sec * 10000000 + t.tv_nsec/100 + NTFS_TIME_OFFSET;
}

static const int total_days_of_prev_months[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

struct timespec64 cnvrtDosUnixTm(__le16 le_date, __le16 le_time, int offset)
{
	struct timespec64 ts;
	time64_t sec, days;
	int min, day, month, year;
	u16 date = le16_to_cpu(le_date);
	u16 time = le16_to_cpu(le_time);
	SMB_TIME *st = (SMB_TIME *)&time;
	SMB_DATE *sd = (SMB_DATE *)&date;

	cifs_dbg(FYI, "date %d time %d\n", date, time);

	sec = 2 * st->TwoSeconds;
	min = st->Minutes;
	if ((sec > 59) || (min > 59))
		cifs_dbg(VFS, "Invalid time min %d sec %lld\n", min, sec);
	sec += (min * 60);
	sec += 60 * 60 * st->Hours;
	if (st->Hours > 24)
		cifs_dbg(VFS, "Invalid hours %d\n", st->Hours);
	day = sd->Day;
	month = sd->Month;
	if (day < 1 || day > 31 || month < 1 || month > 12) {
		cifs_dbg(VFS, "Invalid date, month %d day: %d\n", month, day);
		day = clamp(day, 1, 31);
		month = clamp(month, 1, 12);
	}
	month -= 1;
	days = day + total_days_of_prev_months[month];
	days += 3652; /* account for difference in days between 1980 and 1970 */
	year = sd->Year;
	days += year * 365;
	days += (year/4); /* leap year */
	/* generalized leap year calculation is more complex, ie no leap year
	for years/100 except for years/400, but since the maximum number for DOS
	 year is 2**7, the last year is 1980+127, which means we need only
	 consider 2 special case years, ie the years 2000 and 2100, and only
	 adjust for the lack of leap year for the year 2100, as 2000 was a
	 leap year (divisible by 400) */
	if (year >= 120)  /* the year 2100 */
		days = days - 1;  /* do not count leap year for the year 2100 */

	/* adjust for leap year where we are still before leap day */
	if (year != 120)
		days -= ((year & 0x03) == 0) && (month < 2 ? 1 : 0);
	sec += 24 * 60 * 60 * days;

	ts.tv_sec = sec + offset;

	/* cifs_dbg(FYI, "sec after cnvrt dos to unix time %d\n",sec); */

	ts.tv_nsec = 0;
	return ts;
}
