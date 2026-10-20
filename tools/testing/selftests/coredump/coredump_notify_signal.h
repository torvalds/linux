/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __COREDUMP_NOTIFY_SIGNAL_H
#define __COREDUMP_NOTIFY_SIGNAL_H

#include <stdbool.h>
#include <sys/types.h>

/*
 * Define a bunch of constants we need. We create a situation where the
 * NT_FILE note blows past 200K. That's way beyond the default 64K
 * pipe ring and past the ~36K an af_unix skb holds. So we force a write
 * to come back short.
 */
#define NOTIFY_SIGNAL_MAP_COUNT		4000
#define NOTIFY_SIGNAL_ANON_BYTES	(4UL << 20)
#define NOTIFY_SIGNAL_STALL_US		200000
#define NOTIFY_SIGNAL_MAPFILE		"/tmp/coredump.notify_signal.mapfile"
#define NOTIFY_SIGNAL_TRIGGER		"/tmp/coredump.notify_signal.trigger"
#define NOTIFY_SIGNAL_CORE_FILE		"/tmp/coredump.notify_signal.core"
#define NOTIFY_SIGNAL_CORE_TMPFILE	"/tmp/coredump.notify_signal.core.tmp"
#define NOTIFY_SIGNAL_SOCKET		"/tmp/coredump.notify_signal.socket"

void crashing_child_notify_signal(void);
bool coredump_io_uring_available(void);
ssize_t recv_coredump_notify_signal(int fd, int fd_out, bool arm);
long long coredump_expected_size(const char *path);

#endif /* __COREDUMP_NOTIFY_SIGNAL_H */
