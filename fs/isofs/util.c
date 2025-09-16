// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/isofs/util.c
 */

#include <linux/time.h>
#include "isofs.h"

/* 
 * We have to convert from a MM/DD/YY format to the Unix ctime format.
 * We have to take into account leap years and all of that good stuff.
 * Unfortunately, the kernel does not have the information on hand to
 * take into account daylight savings time, but it shouldn't matter.
 * The time stored should be localtime (with or without DST in effect),
 * and the timezone offset should hold the offset required to get back
 * to GMT.  Thus  we should always be correct.
 */

struct timespec64 iso_date(u8 *p, int flags)
{
	int year, month, day, hour, minute, second, tz;
	struct timespec64 ts;

	if (flags & ISO_DATE_LONG_FORM) {
		year = (p[0] - '0') * 1000 +
		       (p[1] - '0') * 100 +
		       (p[2] - '0') * 10 +
		       (p[3] - '0') - 1900;
		month = ((p[4] - '0') * 10 + (p[5] - '0'));
		day = ((p[6] - '0') * 10 + (p[7] - '0'));
		hour = ((p[8] - '0') * 10 + (p[9] - '0'));
		minute = ((p[10] - '0') * 10 + (p[11] - '0'));
		second = ((p[12] - '0') * 10 + (p[13] - '0'));
		ts.tv_nsec = ((p[14] - '0') * 10 + (p[15] - '0')) * 10000000;
		tz = p[16];
	} else {
		year = p[0];
		month = p[1];
		day = p[2];
		hour = p[3];
		minute = p[4];
		second = p[5];
		ts.tv_nsec = 0;
		/* High sierra has no time zone */
		tz = flags & ISO_DATE_HIGH_SIERRA ? 0 : p[6];
	}

	if (year < 0) {
		ts.tv_sec = 0;
	} else {
		ts.tv_sec = mktime64(year+1900, month, day, hour, minute, second);

		/* sign extend */
		if (tz & 0x80)
			tz |= (-1 << 8);

		/* 
		 * The timezone offset is unreliable on some disks,
		 * so we make a sanity check.  In no case is it ever
		 * more than 13 hours from GMT, which is 52*15min.
		 * The time is always stored in localtime with the
		 * timezone offset being what get added to GMT to
		 * get to localtime.  Thus we need to subtract the offset
		 * to get to true GMT, which is what we store the time
		 * as internally.  On the local system, the user may set
		 * their timezone any way they wish, of course, so GMT
		 * gets converted back to localtime on the receiving
		 * system.
		 *
		 * NOTE: mkisofs in versions prior to mkisofs-1.10 had
		 * the sign wrong on the timezone offset.  This has now
		 * been corrected there too, but if you are getting screwy
		 * results this may be the explanation.  If enough people
		 * complain, a user configuration option could be added
		 * to add the timezone offset in with the wrong sign
		 * for 'compatibility' with older discs, but I cannot see how
		 * it will matter that much.
		 *
		 * Thanks to kuhlmav@elec.canterbury.ac.nz (Volker Kuhlmann)
		 * for pointing out the sign error.
		 */
		if (-52 <= tz && tz <= 52)
			ts.tv_sec -= tz * 15 * 60;
	}
	return ts;
}
