/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_TIME_WRAPPERS_H
#define __KSMBD_TIME_WRAPPERS_H

/*
 * A bunch of ugly hacks to workaoround all the API differences
 * between different kernel versions.
 */

#define NTFS_TIME_OFFSET	((u64)(369*365 + 89) * 24 * 3600 * 10000000)

/* Convert the Unix UTC into NT UTC. */
static inline u64 ksmbd_UnixTimeToNT(struct timespec64 t)
{
	/* Convert to 100ns intervals and then add the NTFS time offset. */
	return (u64) t.tv_sec * 10000000 + t.tv_nsec / 100 + NTFS_TIME_OFFSET;
}

struct timespec64 ksmbd_NTtimeToUnix(__le64 ntutc);

#define KSMBD_TIME_TO_TM	time64_to_tm

static inline long long ksmbd_systime(void)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	return ksmbd_UnixTimeToNT(ts);
}
#endif /* __KSMBD_TIME_WRAPPERS_H */
