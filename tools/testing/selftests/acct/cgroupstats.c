// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/cgroupstats.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "netlink_helper.h"
#include "kselftest.h"

static int send_cgroupstats_cmd(int fd, int family_id, uint32_t cgroup_fd,
				int flags)
{
	struct {
		struct nlmsghdr nlh;
		struct genlmsghdr genl;
		char buf[256];
	} req = { 0 };
	struct nlattr *na;

	req.nlh.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.nlh.nlmsg_type = family_id;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | flags;
	req.nlh.nlmsg_seq = 2;
	req.nlh.nlmsg_pid = getpid();

	req.genl.cmd = CGROUPSTATS_CMD_GET;
	req.genl.version = 1;

	na = (struct nlattr *)((char *)&req + NLMSG_ALIGN(req.nlh.nlmsg_len));
	na->nla_type = CGROUPSTATS_CMD_ATTR_FD;
	na->nla_len = NLA_HDRLEN + sizeof(cgroup_fd);
	memcpy(nla_data(na), &cgroup_fd, sizeof(cgroup_fd));
	req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + NLA_ALIGN(na->nla_len);

	return send_request(fd, &req, req.nlh.nlmsg_len);
}

/*
 * Receive and decode a cgroupstats response.
 *
 * Returns:
 *   0             — success, stats filled from CGROUPSTATS_CMD_NEW reply
 *   <0            — NLMSG_ERROR errno (e.g. -EBADF, -EINVAL)
 */
static int recv_cgroupstats_response(int fd, struct cgroupstats *stats)
{
	char resp[8192];
	struct nlmsghdr *nlh;
	struct genlmsghdr *genl;
	struct nlattr *na;
	int len;
	int rem;

	memset(stats, 0, sizeof(*stats));

	len = recv(fd, resp, sizeof(resp), 0);
	if (len < 0)
		return -errno;

	for (nlh = (struct nlmsghdr *)resp; NLMSG_OK(nlh, len);
	     nlh = NLMSG_NEXT(nlh, len)) {
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = NLMSG_DATA(nlh);

			return err->error;
		}

		genl = (struct genlmsghdr *)NLMSG_DATA(nlh);
		if (genl->cmd != CGROUPSTATS_CMD_NEW)
			continue;

		rem = nlh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
		na = (struct nlattr *)((char *)genl + GENL_HDRLEN);
		while (nla_ok(na, rem)) {
			if (na->nla_type == CGROUPSTATS_TYPE_CGROUP_STATS) {
				memcpy(stats, nla_data(na), sizeof(*stats));
				return 0;
			}
			na = nla_next(na, &rem);
		}
	}

	return -EIO;
}

/* mkdtemp() modifies the template in place, so this cannot be const. */
static char cg_mountpoint[32];
static bool cg_mounted;

static int setup_cgroup_v1(void)
{
	strcpy(cg_mountpoint, "/tmp/cgstats_test_XXXXXX");

	if (!mkdtemp(cg_mountpoint))
		return -errno;

	if (mount("cgstats_test", cg_mountpoint, "cgroup", 0,
		  "none,name=cgstats_test") < 0) {
		int ret = -errno;

		rmdir(cg_mountpoint);
		return ret;
	}

	cg_mounted = true;
	return 0;
}

static void cleanup_cgroup_v1(void)
{
	if (!cg_mounted)
		return;
	umount2(cg_mountpoint, MNT_DETACH);
	rmdir(cg_mountpoint);
	cg_mounted = false;
}

int main(void)
{
	struct cgroupstats stats;
	uint64_t total_tasks;
	int family_id;
	int nl_fd;
	int cg_fd;
	int ret;

	ksft_print_header();

	nl_fd = netlink_open();
	if (nl_fd < 0)
		ksft_exit_skip("failed to open generic netlink socket: %s\n",
			       strerror(-nl_fd));

	family_id = get_family_id(nl_fd, TASKSTATS_GENL_NAME);
	if (family_id < 0)
		ksft_exit_skip("taskstats generic netlink family unavailable: %s\n",
			       strerror(-family_id));

	ksft_set_plan(3);

	/*
	 * Test 1: mount a private cgroup v1 hierarchy, query it, and
	 * verify the response contains sane task counts. If the test
	 * environment cannot create a private cgroup v1 mount, skip this
	 * case and continue with the unprivileged regression checks below.
	 */
	ret = setup_cgroup_v1();
	if (ret) {
		ksft_test_result_skip("cgroupstats query: cannot mount cgroup v1: %s\n",
				      strerror(-ret));
	} else {
		cg_fd = open(cg_mountpoint, O_RDONLY | O_DIRECTORY);
		if (cg_fd < 0) {
			ksft_test_result_fail("cgroupstats query: open mountpoint: %s\n",
					      strerror(errno));
		} else {
			ret = send_cgroupstats_cmd(nl_fd, family_id,
						   (uint32_t)cg_fd, 0);
			if (ret) {
				ksft_test_result_fail("cgroupstats query: send: %s\n",
						      strerror(-ret));
			} else {
				ret = recv_cgroupstats_response(nl_fd, &stats);
				if (ret < 0) {
					ksft_test_result_fail("cgroupstats query: %s\n",
							      strerror(-ret));
				} else {
					total_tasks = (uint64_t)stats.nr_sleeping +
						      (uint64_t)stats.nr_running +
						      (uint64_t)stats.nr_stopped +
						      (uint64_t)stats.nr_uninterruptible +
						      (uint64_t)stats.nr_io_wait;

					ksft_print_msg("cgroupstats query: total_tasks=%llu\n",
						       (unsigned long long)total_tasks);

					ksft_test_result(total_tasks > 0,
							 "cgroupstats query returns valid stats\n");
				}
			}
			close(cg_fd);
		}
	}
	cleanup_cgroup_v1();

	/*
	 * Test 2: invalid fd without NLM_F_ACK.  The kernel should
	 * return -EBADF via NLMSG_ERROR regardless of whether the
	 * client requested an explicit ACK.
	 */
	ret = send_cgroupstats_cmd(nl_fd, family_id, 0xFFFFFFFF, 0);
	if (ret)
		ksft_exit_fail_msg("send test 2 failed: %s\n", strerror(-ret));

	ret = recv_cgroupstats_response(nl_fd, &stats);
	ksft_print_msg("bad fd (no ACK): response=%d (%s)\n",
		       ret, ret < 0 ? strerror(-ret) : "unexpected success");
	ksft_test_result(ret == -EBADF,
			 "cgroupstats rejects bad fd without NLM_F_ACK\n");

	/*
	 * Test 3: invalid fd with NLM_F_ACK.  Same expectation as
	 * test 2, but exercised through a different netlink flag
	 * path in the kernel's ack/error handling.
	 */
	ret = send_cgroupstats_cmd(nl_fd, family_id, 0xFFFFFFFF, NLM_F_ACK);
	if (ret)
		ksft_exit_fail_msg("send test 3 failed: %s\n", strerror(-ret));

	ret = recv_cgroupstats_response(nl_fd, &stats);
	ksft_print_msg("bad fd (with ACK): response=%d (%s)\n",
		       ret, ret < 0 ? strerror(-ret) : "unexpected success");
	ksft_test_result(ret == -EBADF,
			 "cgroupstats rejects bad fd with NLM_F_ACK\n");

	close(nl_fd);
	ksft_finished();
	return ksft_get_fail_cnt() ? KSFT_FAIL : KSFT_PASS;
}
