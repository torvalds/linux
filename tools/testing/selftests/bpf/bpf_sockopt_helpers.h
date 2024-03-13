/* SPDX-License-Identifier: GPL-2.0 */

#include <sys/socket.h>
#include <bpf/bpf_helpers.h>

int get_set_sk_priority(void *ctx)
{
	int prio;

	/* Verify that context allows calling bpf_getsockopt and
	 * bpf_setsockopt by reading and writing back socket
	 * priority.
	 */

	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 0;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 0;

	return 1;
}
