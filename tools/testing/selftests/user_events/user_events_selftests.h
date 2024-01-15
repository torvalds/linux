/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _USER_EVENTS_SELFTESTS_H
#define _USER_EVENTS_SELFTESTS_H

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <unistd.h>
#include <errno.h>

#include "../kselftest.h"

static inline void tracefs_unmount(void)
{
	umount("/sys/kernel/tracing");
}

static inline bool tracefs_enabled(char **message, bool *fail, bool *umount)
{
	struct stat buf;
	int ret;

	*message = "";
	*fail = false;
	*umount = false;

	/* Ensure tracefs is installed */
	ret = stat("/sys/kernel/tracing", &buf);

	if (ret == -1) {
		*message = "Tracefs is not installed";
		return false;
	}

	/* Ensure mounted tracefs */
	ret = stat("/sys/kernel/tracing/README", &buf);

	if (ret == -1 && errno == ENOENT) {
		if (mount(NULL, "/sys/kernel/tracing", "tracefs", 0, NULL) != 0) {
			*message = "Cannot mount tracefs";
			*fail = true;
			return false;
		}

		*umount = true;

		ret = stat("/sys/kernel/tracing/README", &buf);
	}

	if (ret == -1) {
		*message = "Cannot access tracefs";
		*fail = true;
		return false;
	}

	return true;
}

static inline bool user_events_enabled(char **message, bool *fail, bool *umount)
{
	struct stat buf;
	int ret;

	*message = "";
	*fail = false;
	*umount = false;

	if (getuid() != 0) {
		*message = "Must be run as root";
		*fail = true;
		return false;
	}

	if (!tracefs_enabled(message, fail, umount))
		return false;

	/* Ensure user_events is installed */
	ret = stat("/sys/kernel/tracing/user_events_data", &buf);

	if (ret == -1) {
		switch (errno) {
		case ENOENT:
			*message = "user_events is not installed";
			return false;

		default:
			*message = "Cannot access user_events_data";
			*fail = true;
			return false;
		}
	}

	return true;
}

#define USER_EVENT_FIXTURE_SETUP(statement, umount) do { \
	char *message; \
	bool fail; \
	if (!user_events_enabled(&message, &fail, &(umount))) { \
		if (fail) { \
			TH_LOG("Setup failed due to: %s", message); \
			ASSERT_FALSE(fail); \
		} \
		SKIP(statement, "Skipping due to: %s", message); \
	} \
} while (0)

#define USER_EVENT_FIXTURE_TEARDOWN(umount) do { \
	if ((umount))  \
		tracefs_unmount(); \
} while (0)

#endif /* _USER_EVENTS_SELFTESTS_H */
