/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock audit helpers
 *
 * Copyright © 2024-2025 Microsoft Corporation
 */

#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/limits.h>
#include <linux/netlink.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "kselftest.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define REGEX_LANDLOCK_PREFIX "^audit([0-9.:]\\+): domain=\\([0-9a-f]\\+\\)"

struct audit_filter {
	__u32 record_type;
	size_t exe_len;
	char exe[PATH_MAX];
};

struct audit_message {
	struct nlmsghdr header;
	union {
		struct audit_status status;
		struct audit_features features;
		struct audit_rule_data rule;
		struct nlmsgerr err;
		char data[PATH_MAX + 200];
	};
};

static const struct timeval audit_tv_dom_drop = {
	/*
	 * Because domain deallocation is tied to asynchronous credential
	 * freeing, receiving such event may take some time.  In practice,
	 * on a small VM, it should not exceed 100k usec, but let's wait up
	 * to 1 second to be safe.
	 */
	.tv_sec = 1,
};

static const struct timeval audit_tv_default = {
	.tv_usec = 1,
};

static int audit_send(const int fd, const struct audit_message *const msg)
{
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};
	int ret;

	do {
		ret = sendto(fd, msg, msg->header.nlmsg_len, 0,
			     (struct sockaddr *)&addr, sizeof(addr));
	} while (ret < 0 && errno == EINTR);

	if (ret < 0)
		return -errno;

	if (ret != msg->header.nlmsg_len)
		return -E2BIG;

	return 0;
}

static int audit_recv(const int fd, struct audit_message *msg)
{
	struct sockaddr_nl addr;
	socklen_t addrlen = sizeof(addr);
	struct audit_message msg_tmp;
	int err;

	if (!msg)
		msg = &msg_tmp;

	do {
		err = recvfrom(fd, msg, sizeof(*msg), 0,
			       (struct sockaddr *)&addr, &addrlen);
	} while (err < 0 && errno == EINTR);

	if (err < 0)
		return -errno;

	if (addrlen != sizeof(addr) || addr.nl_pid != 0)
		return -EINVAL;

	/* Checks Netlink error or end of messages. */
	if (msg->header.nlmsg_type == NLMSG_ERROR)
		return msg->err.error;

	return 0;
}

static int audit_request(const int fd,
			 const struct audit_message *const request,
			 struct audit_message *reply)
{
	struct audit_message msg_tmp;
	bool first_reply = true;
	int err;

	err = audit_send(fd, request);
	if (err)
		return err;

	if (!reply)
		reply = &msg_tmp;

	do {
		if (first_reply)
			first_reply = false;
		else
			reply = &msg_tmp;

		err = audit_recv(fd, reply);
		if (err)
			return err;
	} while (reply->header.nlmsg_type != NLMSG_ERROR &&
		 reply->err.msg.nlmsg_type != request->header.nlmsg_type);

	return reply->err.error;
}

static int audit_filter_exe(const int audit_fd,
			    const struct audit_filter *const filter,
			    const __u16 type)
{
	struct audit_message msg = {
		.header = {
			.nlmsg_len = NLMSG_SPACE(sizeof(msg.rule)) +
				     NLMSG_ALIGN(filter->exe_len),
			.nlmsg_type = type,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
		},
		.rule = {
			.flags = AUDIT_FILTER_EXCLUDE,
			.action = AUDIT_NEVER,
			.field_count = 1,
			.fields[0] = filter->record_type,
			.fieldflags[0] = AUDIT_NOT_EQUAL,
			.values[0] = filter->exe_len,
			.buflen = filter->exe_len,
		}
	};

	if (filter->record_type != AUDIT_EXE)
		return -EINVAL;

	memcpy(msg.rule.buf, filter->exe, filter->exe_len);
	return audit_request(audit_fd, &msg, NULL);
}

static int audit_filter_drop(const int audit_fd, const __u16 type)
{
	struct audit_message msg = {
		.header = {
			.nlmsg_len = NLMSG_SPACE(sizeof(msg.rule)),
			.nlmsg_type = type,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
		},
		.rule = {
			.flags = AUDIT_FILTER_EXCLUDE,
			.action = AUDIT_NEVER,
			.field_count = 1,
			.fields[0] = AUDIT_MSGTYPE,
			.fieldflags[0] = AUDIT_NOT_EQUAL,
			.values[0] = AUDIT_LANDLOCK_DOMAIN,
		}
	};

	return audit_request(audit_fd, &msg, NULL);
}

static int audit_set_status(int fd, __u32 key, __u32 val)
{
	const struct audit_message msg = {
		.header = {
			.nlmsg_len = NLMSG_SPACE(sizeof(msg.status)),
			.nlmsg_type = AUDIT_SET,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
		},
		.status = {
			.mask = key,
			.enabled = key == AUDIT_STATUS_ENABLED ? val : 0,
			.pid = key == AUDIT_STATUS_PID ? val : 0,
		}
	};

	return audit_request(fd, &msg, NULL);
}

/* Returns a pointer to the last filled character of @dst, which is `\0`.  */
static __maybe_unused char *regex_escape(const char *const src, char *dst,
					 size_t dst_size)
{
	char *d = dst;

	for (const char *s = src; *s; s++) {
		switch (*s) {
		case '$':
		case '*':
		case '.':
		case '[':
		case '\\':
		case ']':
		case '^':
			if (d >= dst + dst_size - 2)
				return (char *)-ENOMEM;

			*d++ = '\\';
			*d++ = *s;
			break;
		default:
			if (d >= dst + dst_size - 1)
				return (char *)-ENOMEM;

			*d++ = *s;
		}
	}
	if (d >= dst + dst_size - 1)
		return (char *)-ENOMEM;

	*d = '\0';
	return d;
}

/*
 * @domain_id: The domain ID extracted from the audit message (if the first part
 * of @pattern is REGEX_LANDLOCK_PREFIX).  It is set to 0 if the domain ID is
 * not found.
 */
static int audit_match_record(int audit_fd, const __u16 type,
			      const char *const pattern, __u64 *domain_id)
{
	struct audit_message msg, last_mismatch = {};
	int ret, err = 0;
	int num_type_match = 0;
	regmatch_t matches[2];
	regex_t regex;

	ret = regcomp(&regex, pattern, 0);
	if (ret)
		return -EINVAL;

	/*
	 * Reads records until one matches both the expected type and the
	 * pattern.  Type-matching records with non-matching content are
	 * silently consumed, which handles stale domain deallocation records
	 * from a previous test emitted asynchronously by kworker threads.
	 */
	while (true) {
		memset(&msg, 0, sizeof(msg));
		err = audit_recv(audit_fd, &msg);
		if (err) {
			if (num_type_match) {
				printf("DATA: %s\n", last_mismatch.data);
				printf("ERROR: %d record(s) matched type %u"
				       " but not pattern: %s\n",
				       num_type_match, type, pattern);
			}
			goto out;
		}

		if (type && msg.header.nlmsg_type != type)
			continue;

		ret = regexec(&regex, msg.data, ARRAY_SIZE(matches), matches,
			      0);
		if (!ret)
			break;

		num_type_match++;
		last_mismatch = msg;
	}

	if (domain_id) {
		*domain_id = 0;
		if (matches[1].rm_so != -1) {
			int match_len = matches[1].rm_eo - matches[1].rm_so;
			/* The maximal characters of a 2^64 hexadecimal number is 17. */
			char dom_id[18];

			if (match_len > 0 && match_len < sizeof(dom_id)) {
				memcpy(dom_id, msg.data + matches[1].rm_so,
				       match_len);
				dom_id[match_len] = '\0';
				if (domain_id)
					*domain_id = strtoull(dom_id, NULL, 16);
			}
		}
	}

out:
	regfree(&regex);
	return err;
}

static int __maybe_unused matches_log_domain_allocated(int audit_fd, pid_t pid,
						       __u64 *domain_id)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" status=allocated mode=enforcing pid=%d uid=[0-9]\\+"
		" exe=\"[^\"]\\+\" comm=\".*_test\"$";
	char log_match[sizeof(log_template) + 10];
	int log_match_len;

	log_match_len =
		snprintf(log_match, sizeof(log_match), log_template, pid);
	if (log_match_len >= sizeof(log_match))
		return -E2BIG;

	return audit_match_record(audit_fd, AUDIT_LANDLOCK_DOMAIN, log_match,
				  domain_id);
}

/*
 * Matches a domain deallocation record.  When expected_domain_id is non-zero,
 * the pattern includes the specific domain ID so that stale deallocation
 * records from a previous test (with a different domain ID) are skipped by
 * audit_match_record(), and the socket timeout is temporarily increased to
 * audit_tv_dom_drop to wait for the asynchronous kworker deallocation.
 */
static int __maybe_unused
matches_log_domain_deallocated(int audit_fd, unsigned int num_denials,
			       __u64 expected_domain_id, __u64 *domain_id)
{
	static const char log_template[] = REGEX_LANDLOCK_PREFIX
		" status=deallocated denials=%u$";
	static const char log_template_with_id[] =
		"^audit([0-9.:]\\+): domain=\\(%llx\\)"
		" status=deallocated denials=%u$";
	char log_match[sizeof(log_template_with_id) + 32];
	int log_match_len, err;

	if (expected_domain_id)
		log_match_len = snprintf(log_match, sizeof(log_match),
					 log_template_with_id,
					 (unsigned long long)expected_domain_id,
					 num_denials);
	else
		log_match_len = snprintf(log_match, sizeof(log_match),
					 log_template, num_denials);

	if (log_match_len >= sizeof(log_match))
		return -E2BIG;

	if (expected_domain_id)
		setsockopt(audit_fd, SOL_SOCKET, SO_RCVTIMEO,
			   &audit_tv_dom_drop, sizeof(audit_tv_dom_drop));

	err = audit_match_record(audit_fd, AUDIT_LANDLOCK_DOMAIN, log_match,
				 domain_id);

	if (expected_domain_id)
		setsockopt(audit_fd, SOL_SOCKET, SO_RCVTIMEO, &audit_tv_default,
			   sizeof(audit_tv_default));

	return err;
}

struct audit_records {
	size_t access;
	size_t domain;
};

/*
 * WARNING: Do not assert records.domain == 0 without a preceding
 * audit_match_record() call.  Domain deallocation records are emitted
 * asynchronously from kworker threads and can arrive after the drain in
 * audit_init(), corrupting the domain count.  A preceding audit_match_record()
 * call consumes stale records while scanning, making the assertion safe in
 * practice because stale deallocation records arrive before the expected access
 * records.
 */
static int audit_count_records(int audit_fd, struct audit_records *records)
{
	struct audit_message msg;
	int err;

	records->access = 0;
	records->domain = 0;

	do {
		memset(&msg, 0, sizeof(msg));
		err = audit_recv(audit_fd, &msg);
		if (err) {
			if (err == -EAGAIN)
				return 0;
			else
				return err;
		}

		switch (msg.header.nlmsg_type) {
		case AUDIT_LANDLOCK_ACCESS:
			records->access++;
			break;
		case AUDIT_LANDLOCK_DOMAIN:
			records->domain++;
			break;
		}
	} while (true);

	return 0;
}

static int audit_init(void)
{
	int fd, err;

	fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_AUDIT);
	if (fd < 0)
		return -errno;

	err = audit_set_status(fd, AUDIT_STATUS_ENABLED, 1);
	if (err)
		goto err_close;

	err = audit_set_status(fd, AUDIT_STATUS_PID, getpid());
	if (err)
		goto err_close;

	/* Sets a timeout for negative tests. */
	err = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &audit_tv_default,
			 sizeof(audit_tv_default));
	if (err) {
		err = -errno;
		goto err_close;
	}

	/*
	 * Drains stale audit records that accumulated in the kernel backlog
	 * while no audit daemon socket was open.  This happens when non-audit
	 * Landlock tests generate records while audit_enabled is non-zero (e.g.
	 * from boot configuration), or when domain deallocation records arrive
	 * asynchronously after a previous test's socket was closed.
	 */
	while (audit_recv(fd, NULL) == 0)
		;

	return fd;

err_close:
	close(fd);
	return err;
}

static int audit_init_filter_exe(struct audit_filter *filter, const char *path)
{
	char *absolute_path = NULL;

	/* It is assume that there is not already filtering rules. */
	filter->record_type = AUDIT_EXE;
	if (!path) {
		int ret = readlink("/proc/self/exe", filter->exe,
				   sizeof(filter->exe) - 1);
		if (ret < 0)
			return -errno;

		filter->exe_len = ret;
		return 0;
	}

	absolute_path = realpath(path, NULL);
	if (!absolute_path)
		return -errno;

	/* No need for the terminating NULL byte. */
	filter->exe_len = strlen(absolute_path);
	if (filter->exe_len > sizeof(filter->exe))
		return -E2BIG;

	memcpy(filter->exe, absolute_path, filter->exe_len);
	free(absolute_path);
	return 0;
}

static int audit_cleanup(int audit_fd, struct audit_filter *filter)
{
	struct audit_filter new_filter;

	if (audit_fd < 0 || !filter) {
		int err;

		/*
		 * Simulates audit_init_with_exe_filter() when called from
		 * FIXTURE_TEARDOWN_PARENT().
		 */
		audit_fd = audit_init();
		if (audit_fd < 0)
			return audit_fd;

		filter = &new_filter;
		err = audit_init_filter_exe(filter, NULL);
		if (err) {
			close(audit_fd);
			return err;
		}
	}

	/* Filters might not be in place. */
	audit_filter_exe(audit_fd, filter, AUDIT_DEL_RULE);
	audit_filter_drop(audit_fd, AUDIT_DEL_RULE);

	/*
	 * Because audit_cleanup() might not be called by the test auditd
	 * process, it might not be possible to explicitly set it.  Anyway,
	 * AUDIT_STATUS_ENABLED will implicitly be set to 0 when the auditd
	 * process will exit.
	 */
	return close(audit_fd);
}

static int audit_init_with_exe_filter(struct audit_filter *filter)
{
	int fd, err;

	fd = audit_init();
	if (fd < 0)
		return fd;

	err = audit_init_filter_exe(filter, NULL);
	if (err)
		goto err_close;

	err = audit_filter_exe(fd, filter, AUDIT_ADD_RULE);
	if (err)
		goto err_close;

	return fd;

err_close:
	close(fd);
	return err;
}
