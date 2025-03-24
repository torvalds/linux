/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSG_ZEROCOPY_COMMON_H
#define MSG_ZEROCOPY_COMMON_H

#include <stdbool.h>

#ifndef SOL_VSOCK
#define SOL_VSOCK	287
#endif

#ifndef VSOCK_RECVERR
#define VSOCK_RECVERR	1
#endif

void vsock_recv_completion(int fd, const bool *zerocopied);

#endif /* MSG_ZEROCOPY_COMMON_H */
