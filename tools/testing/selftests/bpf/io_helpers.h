// SPDX-License-Identifier: GPL-2.0
#include <unistd.h>

/* As a regular read(2), but allows to specify a timeout in micro-seconds.
 * Returns -EAGAIN on timeout.
 */
int read_with_timeout(int fd, char *buf, size_t count, long usec);
