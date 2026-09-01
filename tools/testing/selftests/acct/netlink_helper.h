/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared generic netlink helpers for the acct selftests.
 */
#ifndef ACSELFTESTS_ACCT_NETLINK_HELPER_H
#define ACSELFTESTS_ACCT_NETLINK_HELPER_H

#include <stdbool.h>
#include <linux/netlink.h>

#ifndef NLA_ALIGNTO
#define NLA_ALIGNTO 4
#define NLA_ALIGN(len) (((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN ((int)NLA_ALIGN(sizeof(struct nlattr)))
#endif

/* Fail an individual test case instead of hanging the whole binary. */
#define ACCT_RCV_TIMEOUT_SEC 2

static inline void *nla_data(const struct nlattr *na)
{
	return (void *)((char *)na + NLA_HDRLEN);
}

static inline bool nla_ok(const struct nlattr *na, int remaining)
{
	return remaining >= (int)sizeof(*na) &&
	       na->nla_len >= sizeof(*na) &&
	       na->nla_len <= remaining;
}

static inline struct nlattr *nla_next(const struct nlattr *na, int *remaining)
{
	int aligned_len = NLA_ALIGN(na->nla_len);

	*remaining -= aligned_len;
	return (struct nlattr *)((char *)na + aligned_len);
}

int netlink_open(void);
int send_request(int fd, void *buf, size_t len);
int get_family_id(int fd, const char *name);

#endif /* ACSELFTESTS_ACCT_NETLINK_HELPER_H */
