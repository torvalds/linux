/*-
 * Copyright (c) 2005 Robert N. M. Watson
 * Copyright (c) 2015 Mark Johnston
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

/*
 * UNIX domain sockets allow file descriptors to be passed via "ancillary
 * data", or control messages.  This regression test is intended to exercise
 * this facility, both performing some basic tests that it operates, and also
 * causing some kernel edge cases to execute, such as garbage collection when
 * there are cyclic file descriptor references.  Right now we test only with
 * stream sockets, but ideally we'd also test with datagram sockets.
 */

static void
domainsocketpair(int *fdp)
{

	ATF_REQUIRE_MSG(socketpair(PF_UNIX, SOCK_STREAM, 0, fdp) != -1,
	    "socketpair(PF_UNIX, SOCK_STREAM) failed: %s", strerror(errno));
}

static void
closesocketpair(int *fdp)
{

	close(fdp[0]);
	close(fdp[1]);
}

static void
devnull(int *fdp)
{
	int fd;

	fd = open("/dev/null", O_RDONLY);
	ATF_REQUIRE_MSG(fd != -1, "open failed: %s", strerror(errno));
	*fdp = fd;
}

static void
tempfile(int *fdp)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, PATH_MAX, "%s/unix_passfd.XXXXXXXXXXXXXXX",
	    getenv("TMPDIR") == NULL ? "/tmp" : getenv("TMPDIR"));
	fd = mkstemp(path);
	ATF_REQUIRE_MSG(fd != -1, "mkstemp(%s) failed", path);
	(void)unlink(path);
	*fdp = fd;
}

static void
dofstat(int fd, struct stat *sb)
{

	ATF_REQUIRE_MSG(fstat(fd, sb) == 0,
	    "fstat failed: %s", strerror(errno));
}

static int
getnfds(void)
{
	size_t len;
	int mib[4], n, rc;

	len = sizeof(n);
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_NFDS;
	mib[3] = 0;

	rc = sysctl(mib, 4, &n, &len, NULL, 0);
	ATF_REQUIRE_MSG(rc != -1, "sysctl(KERN_PROC_NFDS) failed");
	return (n);
}

static void
putfds(char *buf, int fd, int nfds)
{
	struct cmsghdr *cm;
	int *fdp, i;

	cm = (struct cmsghdr *)buf;
	cm->cmsg_len = CMSG_LEN(nfds * sizeof(int));
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	for (fdp = (int *)CMSG_DATA(cm), i = 0; i < nfds; i++)
		*fdp++ = fd;
}

static void
samefile(struct stat *sb1, struct stat *sb2)
{

	ATF_REQUIRE_MSG(sb1->st_dev == sb2->st_dev, "different device");
	ATF_REQUIRE_MSG(sb1->st_ino == sb2->st_ino, "different inode");
}

static size_t
sendfd_payload(int sockfd, int send_fd, void *payload, size_t paylen)
{
	struct iovec iovec;
	char message[CMSG_SPACE(sizeof(int))];
	struct msghdr msghdr;
	ssize_t len;

	bzero(&msghdr, sizeof(msghdr));
	bzero(&message, sizeof(message));

	msghdr.msg_control = message;
	msghdr.msg_controllen = sizeof(message);

	iovec.iov_base = payload;
	iovec.iov_len = paylen;

	msghdr.msg_iov = &iovec;
	msghdr.msg_iovlen = 1;

	putfds(message, send_fd, 1);
	len = sendmsg(sockfd, &msghdr, 0);
	ATF_REQUIRE_MSG(len != -1, "sendmsg failed: %s", strerror(errno));
	return ((size_t)len);
}

static void
sendfd(int sockfd, int send_fd)
{
	size_t len;
	char ch;

	ch = 0;
	len = sendfd_payload(sockfd, send_fd, &ch, sizeof(ch));
	ATF_REQUIRE_MSG(len == sizeof(ch),
	    "sendmsg: %zu bytes sent; expected %zu; %s", len, sizeof(ch),
	    strerror(errno));
}

static bool
localcreds(int sockfd)
{
	socklen_t sz;
	int rc, val;

	sz = sizeof(val);
	rc = getsockopt(sockfd, 0, LOCAL_CREDS, &val, &sz);
	ATF_REQUIRE_MSG(rc != -1, "getsockopt(LOCAL_CREDS) failed: %s",
	    strerror(errno));
	return (val != 0);
}

static void
recvfd_payload(int sockfd, int *recv_fd, void *buf, size_t buflen,
    size_t cmsgsz)
{
	struct cmsghdr *cmsghdr;
	struct msghdr msghdr;
	struct iovec iovec;
	char *message;
	ssize_t len;
	bool foundcreds;

	bzero(&msghdr, sizeof(msghdr));
	message = malloc(cmsgsz);
	ATF_REQUIRE(message != NULL);

	msghdr.msg_control = message;
	msghdr.msg_controllen = cmsgsz;

	iovec.iov_base = buf;
	iovec.iov_len = buflen;

	msghdr.msg_iov = &iovec;
	msghdr.msg_iovlen = 1;

	len = recvmsg(sockfd, &msghdr, 0);
	ATF_REQUIRE_MSG(len != -1, "recvmsg failed: %s", strerror(errno));
	ATF_REQUIRE_MSG((size_t)len == buflen,
	    "recvmsg: %zd bytes received; expected %zd", len, buflen);

	cmsghdr = CMSG_FIRSTHDR(&msghdr);
	ATF_REQUIRE_MSG(cmsghdr != NULL,
	    "recvmsg: did not receive control message");
	foundcreds = false;
	*recv_fd = -1;
	for (; cmsghdr != NULL; cmsghdr = CMSG_NXTHDR(&msghdr, cmsghdr)) {
		if (cmsghdr->cmsg_level == SOL_SOCKET &&
		    cmsghdr->cmsg_type == SCM_RIGHTS &&
		    cmsghdr->cmsg_len == CMSG_LEN(sizeof(int))) {
			memcpy(recv_fd, CMSG_DATA(cmsghdr), sizeof(int));
			ATF_REQUIRE(*recv_fd != -1);
		} else if (cmsghdr->cmsg_level == SOL_SOCKET &&
		    cmsghdr->cmsg_type == SCM_CREDS)
			foundcreds = true;
	}
	ATF_REQUIRE_MSG(*recv_fd != -1,
	    "recvmsg: did not receive single-fd message");
	ATF_REQUIRE_MSG(!localcreds(sockfd) || foundcreds,
	    "recvmsg: expected credentials were not received");
}

static void
recvfd(int sockfd, int *recv_fd)
{
	char ch = 0;

	recvfd_payload(sockfd, recv_fd, &ch, sizeof(ch),
	    CMSG_SPACE(sizeof(int)));
}

/*
 * Put a temporary file into a UNIX domain socket, then take it out and make
 * sure it's the same file.  First time around, don't close the reference
 * after sending.
 */
ATF_TC_WITHOUT_HEAD(simple_send_fd);
ATF_TC_BODY(simple_send_fd, tc)
{
	struct stat getfd_stat, putfd_stat;
	int fd[2], getfd, putfd;

	domainsocketpair(fd);
	tempfile(&putfd);
	dofstat(putfd, &putfd_stat);
	sendfd(fd[0], putfd);
	recvfd(fd[1], &getfd);
	dofstat(getfd, &getfd_stat);
	samefile(&putfd_stat, &getfd_stat);
	close(putfd);
	close(getfd);
	closesocketpair(fd);
}

/*
 * Same as simple_send_fd, only close the file reference after sending, so that
 * the only reference is the descriptor in the UNIX domain socket buffer.
 */
ATF_TC_WITHOUT_HEAD(send_and_close);
ATF_TC_BODY(send_and_close, tc)
{
	struct stat getfd_stat, putfd_stat;
	int fd[2], getfd, putfd;

	domainsocketpair(fd);
	tempfile(&putfd);
	dofstat(putfd, &putfd_stat);
	sendfd(fd[0], putfd);
	close(putfd);
	recvfd(fd[1], &getfd);
	dofstat(getfd, &getfd_stat);
	samefile(&putfd_stat, &getfd_stat);
	close(getfd);
	closesocketpair(fd);
}

/*
 * Put a temporary file into a UNIX domain socket, then close both endpoints
 * causing garbage collection to kick off.
 */
ATF_TC_WITHOUT_HEAD(send_and_cancel);
ATF_TC_BODY(send_and_cancel, tc)
{
	int fd[2], putfd;

	domainsocketpair(fd);
	tempfile(&putfd);
	sendfd(fd[0], putfd);
	close(putfd);
	closesocketpair(fd);
}

/*
 * Send two files.  Then receive them.  Make sure they are returned in the
 * right order, and both get there.
 */
ATF_TC_WITHOUT_HEAD(two_files);
ATF_TC_BODY(two_files, tc)
{
	struct stat getfd_1_stat, getfd_2_stat, putfd_1_stat, putfd_2_stat;
	int fd[2], getfd_1, getfd_2, putfd_1, putfd_2;

	domainsocketpair(fd);
	tempfile(&putfd_1);
	tempfile(&putfd_2);
	dofstat(putfd_1, &putfd_1_stat);
	dofstat(putfd_2, &putfd_2_stat);
	sendfd(fd[0], putfd_1);
	sendfd(fd[0], putfd_2);
	close(putfd_1);
	close(putfd_2);
	recvfd(fd[1], &getfd_1);
	recvfd(fd[1], &getfd_2);
	dofstat(getfd_1, &getfd_1_stat);
	dofstat(getfd_2, &getfd_2_stat);
	samefile(&putfd_1_stat, &getfd_1_stat);
	samefile(&putfd_2_stat, &getfd_2_stat);
	close(getfd_1);
	close(getfd_2);
	closesocketpair(fd);
}

/*
 * Big bundling test.  Send an endpoint of the UNIX domain socket over itself,
 * closing the door behind it.
 */
ATF_TC_WITHOUT_HEAD(bundle);
ATF_TC_BODY(bundle, tc)
{
	int fd[2], getfd;

	domainsocketpair(fd);

	sendfd(fd[0], fd[0]);
	close(fd[0]);
	recvfd(fd[1], &getfd);
	close(getfd);
	close(fd[1]);
}

/*
 * Big bundling test part two: Send an endpoint of the UNIX domain socket over
 * itself, close the door behind it, and never remove it from the other end.
 */
ATF_TC_WITHOUT_HEAD(bundle_cancel);
ATF_TC_BODY(bundle_cancel, tc)
{
	int fd[2];

	domainsocketpair(fd);
	sendfd(fd[0], fd[0]);
	sendfd(fd[1], fd[0]);
	closesocketpair(fd);
}

/*
 * Test for PR 151758: Send an character device over the UNIX domain socket
 * and then close both sockets to orphan the device.
 */
ATF_TC_WITHOUT_HEAD(devfs_orphan);
ATF_TC_BODY(devfs_orphan, tc)
{
	int fd[2], putfd;

	domainsocketpair(fd);
	devnull(&putfd);
	sendfd(fd[0], putfd);
	close(putfd);
	closesocketpair(fd);
}

#define	LOCAL_SENDSPACE_SYSCTL	"net.local.stream.sendspace"

/*
 * Test for PR 181741. Receiver sets LOCAL_CREDS, and kernel prepends a
 * control message to the data. Sender sends large payload using a non-blocking
 * socket. Payload + SCM_RIGHTS + LOCAL_CREDS hit socket buffer limit, and
 * receiver receives truncated data.
 */
ATF_TC_WITHOUT_HEAD(rights_creds_payload);
ATF_TC_BODY(rights_creds_payload, tc)
{
	const int on = 1;
	u_long sendspace;
	size_t len;
	void *buf;
	int fd[2], getfd, putfd, rc;

	len = sizeof(sendspace);
	rc = sysctlbyname(LOCAL_SENDSPACE_SYSCTL, &sendspace,
	    &len, NULL, 0);
	ATF_REQUIRE_MSG(rc != -1,
	    "sysctl %s failed: %s", LOCAL_SENDSPACE_SYSCTL, strerror(errno));

	buf = calloc(1, sendspace);
	ATF_REQUIRE(buf != NULL);

	domainsocketpair(fd);
	tempfile(&putfd);

	rc = fcntl(fd[0], F_SETFL, O_NONBLOCK);
	ATF_REQUIRE_MSG(rc != -1, "fcntl(O_NONBLOCK) failed: %s",
	    strerror(errno));
	rc = setsockopt(fd[1], 0, LOCAL_CREDS, &on, sizeof(on));
	ATF_REQUIRE_MSG(rc != -1, "setsockopt(LOCAL_CREDS) failed: %s",
	    strerror(errno));

	len = sendfd_payload(fd[0], putfd, buf, sendspace);
	ATF_REQUIRE_MSG(len < sendspace, "sendmsg: %zu bytes sent", len);
	recvfd_payload(fd[1], &getfd, buf, len,
	    CMSG_SPACE(SOCKCREDSIZE(CMGROUP_MAX)) + CMSG_SPACE(sizeof(int)));

	close(putfd);
	close(getfd);
	closesocketpair(fd);
}

static void
send_cmsg(int sockfd, void *cmsg, size_t cmsgsz)
{
	struct iovec iov;
	struct msghdr msghdr;
	ssize_t len;
	char ch;

	ch = 0;
	bzero(&msghdr, sizeof(msghdr));

	iov.iov_base = &ch;
	iov.iov_len = sizeof(ch);
	msghdr.msg_control = cmsg;
	msghdr.msg_controllen = cmsgsz;
	msghdr.msg_iov = &iov;
	msghdr.msg_iovlen = 1;

	len = sendmsg(sockfd, &msghdr, 0);
	ATF_REQUIRE_MSG(len != -1,
	    "sendmsg failed: %s", strerror(errno));
	ATF_REQUIRE_MSG(len == sizeof(ch),
	    "sendmsg: %zd bytes sent; expected %zu", len, sizeof(ch));
}

static void
recv_cmsg(int sockfd, char *cmsg, size_t cmsgsz, int flags)
{
	struct iovec iov;
	struct msghdr msghdr;
	ssize_t len;
	char ch;

	ch = 0;
	bzero(&msghdr, sizeof(msghdr));

	iov.iov_base = &ch;
	iov.iov_len = sizeof(ch);
	msghdr.msg_control = cmsg;
	msghdr.msg_controllen = cmsgsz;
	msghdr.msg_iov = &iov;
	msghdr.msg_iovlen = 1;

	len = recvmsg(sockfd, &msghdr, 0);
	ATF_REQUIRE_MSG(len != -1,
	    "recvmsg failed: %s", strerror(errno));
	ATF_REQUIRE_MSG(len == sizeof(ch),
	    "recvmsg: %zd bytes received; expected %zu", len, sizeof(ch));
	ATF_REQUIRE_MSG((msghdr.msg_flags & flags) == flags,
	    "recvmsg: got flags %#x; expected %#x", msghdr.msg_flags, flags);
}

/*
 * Test for PR 131876.  Receiver uses a control message buffer that is too
 * small for the incoming SCM_RIGHTS message, so the message is truncated.
 * The kernel must not leak the copied right into the receiver's namespace.
 */
ATF_TC_WITHOUT_HEAD(truncated_rights);
ATF_TC_BODY(truncated_rights, tc)
{
	char *message;
	int fd[2], nfds, putfd, rc;

	domainsocketpair(fd);
	devnull(&putfd);
	nfds = getnfds();

	/*
	 * Case 1: Send a single descriptor and truncate the message.
	 */
	message = malloc(CMSG_SPACE(sizeof(int)));
	ATF_REQUIRE(message != NULL);
	putfds(message, putfd, 1);
	send_cmsg(fd[0], message, CMSG_LEN(sizeof(int)));
	recv_cmsg(fd[1], message, CMSG_LEN(0), MSG_CTRUNC);
	ATF_REQUIRE(getnfds() == nfds);
	free(message);

	/*
	 * Case 2a: Send two descriptors in separate messages, and truncate at
	 *          the boundary between the two messages.  We should still
	 *          receive the first message.
	 */
	message = malloc(CMSG_SPACE(sizeof(int)) * 2);
	ATF_REQUIRE(message != NULL);
	putfds(message, putfd, 1);
	putfds(message + CMSG_SPACE(sizeof(int)), putfd, 1);
	send_cmsg(fd[0], message, CMSG_SPACE(sizeof(int)) * 2);
	recv_cmsg(fd[1], message, CMSG_SPACE(sizeof(int)), MSG_CTRUNC);
	rc = close(*(int *)CMSG_DATA(message));
	ATF_REQUIRE_MSG(rc == 0, "close failed: %s", strerror(errno));
	ATF_REQUIRE(getnfds() == nfds);
	free(message);

	/*
	 * Case 2b: Send two descriptors in separate messages, and truncate
	 *          before the end of the first message.
	 */
	message = malloc(CMSG_SPACE(sizeof(int)) * 2);
	ATF_REQUIRE(message != NULL);
	putfds(message, putfd, 1);
	putfds(message + CMSG_SPACE(sizeof(int)), putfd, 1);
	send_cmsg(fd[0], message, CMSG_SPACE(sizeof(int)) * 2);
	recv_cmsg(fd[1], message, CMSG_SPACE(0), MSG_CTRUNC);
	ATF_REQUIRE(getnfds() == nfds);
	free(message);

	/*
	 * Case 2c: Send two descriptors in separate messages, and truncate
	 *          after the end of the first message.  We should still
	 *          receive the first message.
	 */
	message = malloc(CMSG_SPACE(sizeof(int)) * 2);
	ATF_REQUIRE(message != NULL);
	putfds(message, putfd, 1);
	putfds(message + CMSG_SPACE(sizeof(int)), putfd, 1);
	send_cmsg(fd[0], message, CMSG_SPACE(sizeof(int)) * 2);
	recv_cmsg(fd[1], message, CMSG_SPACE(sizeof(int)) + CMSG_SPACE(0),
	    MSG_CTRUNC);
	rc = close(*(int *)CMSG_DATA(message));
	ATF_REQUIRE_MSG(rc == 0, "close failed: %s", strerror(errno));
	ATF_REQUIRE(getnfds() == nfds);
	free(message);

	/*
	 * Case 3: Send three descriptors in the same message, and leave space
	 *         only for the first when receiving the message.
	 */
	message = malloc(CMSG_SPACE(sizeof(int) * 3));
	ATF_REQUIRE(message != NULL);
	putfds(message, putfd, 3);
	send_cmsg(fd[0], message, CMSG_SPACE(sizeof(int) * 3));
	recv_cmsg(fd[1], message, CMSG_SPACE(sizeof(int)), MSG_CTRUNC);
	ATF_REQUIRE(getnfds() == nfds);
	free(message);

	close(putfd);
	closesocketpair(fd);
}

/*
 * Ensure that an attempt to copy a SCM_RIGHTS message to the recipient
 * fails.  In this case the kernel must dispose of the externalized rights
 * rather than leaking them into the recipient's file descriptor table.
 */
ATF_TC_WITHOUT_HEAD(copyout_rights_error);
ATF_TC_BODY(copyout_rights_error, tc)
{
	struct iovec iovec;
	struct msghdr msghdr;
	char buf[16];
	ssize_t len;
	int fd[2], error, nfds, putfd;

	memset(buf, 0, sizeof(buf));
	domainsocketpair(fd);
	devnull(&putfd);
	nfds = getnfds();

	sendfd_payload(fd[0], putfd, buf, sizeof(buf));

	bzero(&msghdr, sizeof(msghdr));

	iovec.iov_base = buf;
	iovec.iov_len = sizeof(buf);
	msghdr.msg_control = (char *)-1; /* trigger EFAULT */
	msghdr.msg_controllen = CMSG_SPACE(sizeof(int));
	msghdr.msg_iov = &iovec;
	msghdr.msg_iovlen = 1;

	len = recvmsg(fd[1], &msghdr, 0);
	error = errno;
	ATF_REQUIRE_MSG(len == -1, "recvmsg succeeded: %zd", len);
	ATF_REQUIRE_MSG(errno == EFAULT, "expected EFAULT, got %d (%s)",
	    error, strerror(errno));

	/* Verify that no FDs were leaked. */
	ATF_REQUIRE(getnfds() == nfds);

	close(putfd);
	closesocketpair(fd);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, simple_send_fd);
	ATF_TP_ADD_TC(tp, send_and_close);
	ATF_TP_ADD_TC(tp, send_and_cancel);
	ATF_TP_ADD_TC(tp, two_files);
	ATF_TP_ADD_TC(tp, bundle);
	ATF_TP_ADD_TC(tp, bundle_cancel);
	ATF_TP_ADD_TC(tp, devfs_orphan);
	ATF_TP_ADD_TC(tp, rights_creds_payload);
	ATF_TP_ADD_TC(tp, truncated_rights);
	ATF_TP_ADD_TC(tp, copyout_rights_error);

	return (atf_no_error());
}
