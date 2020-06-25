// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/tls.h>
#include <linux/tcp.h>
#include <linux/socket.h>

#include <sys/types.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "../kselftest_harness.h"

#define TLS_PAYLOAD_MAX_LEN 16384
#define SOL_TLS 282

FIXTURE(tls_basic)
{
	int fd, cfd;
	bool notls;
};

FIXTURE_SETUP(tls_basic)
{
	struct sockaddr_in addr;
	socklen_t len;
	int sfd, ret;

	self->notls = false;
	len = sizeof(addr);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;

	self->fd = socket(AF_INET, SOCK_STREAM, 0);
	sfd = socket(AF_INET, SOCK_STREAM, 0);

	ret = bind(sfd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);
	ret = listen(sfd, 10);
	ASSERT_EQ(ret, 0);

	ret = getsockname(sfd, &addr, &len);
	ASSERT_EQ(ret, 0);

	ret = connect(self->fd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);

	self->cfd = accept(sfd, &addr, &len);
	ASSERT_GE(self->cfd, 0);

	close(sfd);

	ret = setsockopt(self->fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (ret != 0) {
		ASSERT_EQ(errno, ENOENT);
		self->notls = true;
		printf("Failure setting TCP_ULP, testing without tls\n");
		return;
	}

	ret = setsockopt(self->cfd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	ASSERT_EQ(ret, 0);
}

FIXTURE_TEARDOWN(tls_basic)
{
	close(self->fd);
	close(self->cfd);
}

/* Send some data through with ULP but no keys */
TEST_F(tls_basic, base_base)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10];

	ASSERT_EQ(strlen(test_str) + 1, send_len);

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
};

FIXTURE(tls)
{
	int fd, cfd;
	bool notls;
};

FIXTURE_VARIANT(tls)
{
	unsigned int tls_version;
};

FIXTURE_VARIANT_ADD(tls, 12)
{
	.tls_version = TLS_1_2_VERSION,
};

FIXTURE_VARIANT_ADD(tls, 13)
{
	.tls_version = TLS_1_3_VERSION,
};

FIXTURE_SETUP(tls)
{
	struct tls12_crypto_info_aes_gcm_128 tls12;
	struct sockaddr_in addr;
	socklen_t len;
	int sfd, ret;

	self->notls = false;
	len = sizeof(addr);

	memset(&tls12, 0, sizeof(tls12));
	tls12.info.version = variant->tls_version;
	tls12.info.cipher_type = TLS_CIPHER_AES_GCM_128;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;

	self->fd = socket(AF_INET, SOCK_STREAM, 0);
	sfd = socket(AF_INET, SOCK_STREAM, 0);

	ret = bind(sfd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);
	ret = listen(sfd, 10);
	ASSERT_EQ(ret, 0);

	ret = getsockname(sfd, &addr, &len);
	ASSERT_EQ(ret, 0);

	ret = connect(self->fd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);

	ret = setsockopt(self->fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (ret != 0) {
		self->notls = true;
		printf("Failure setting TCP_ULP, testing without tls\n");
	}

	if (!self->notls) {
		ret = setsockopt(self->fd, SOL_TLS, TLS_TX, &tls12,
				 sizeof(tls12));
		ASSERT_EQ(ret, 0);
	}

	self->cfd = accept(sfd, &addr, &len);
	ASSERT_GE(self->cfd, 0);

	if (!self->notls) {
		ret = setsockopt(self->cfd, IPPROTO_TCP, TCP_ULP, "tls",
				 sizeof("tls"));
		ASSERT_EQ(ret, 0);

		ret = setsockopt(self->cfd, SOL_TLS, TLS_RX, &tls12,
				 sizeof(tls12));
		ASSERT_EQ(ret, 0);
	}

	close(sfd);
}

FIXTURE_TEARDOWN(tls)
{
	close(self->fd);
	close(self->cfd);
}

TEST_F(tls, sendfile)
{
	int filefd = open("/proc/self/exe", O_RDONLY);
	struct stat st;

	EXPECT_GE(filefd, 0);
	fstat(filefd, &st);
	EXPECT_GE(sendfile(self->fd, filefd, 0, st.st_size), 0);
}

TEST_F(tls, send_then_sendfile)
{
	int filefd = open("/proc/self/exe", O_RDONLY);
	char const *test_str = "test_send";
	int to_send = strlen(test_str) + 1;
	char recv_buf[10];
	struct stat st;
	char *buf;

	EXPECT_GE(filefd, 0);
	fstat(filefd, &st);
	buf = (char *)malloc(st.st_size);

	EXPECT_EQ(send(self->fd, test_str, to_send, 0), to_send);
	EXPECT_EQ(recv(self->cfd, recv_buf, to_send, MSG_WAITALL), to_send);
	EXPECT_EQ(memcmp(test_str, recv_buf, to_send), 0);

	EXPECT_GE(sendfile(self->fd, filefd, 0, st.st_size), 0);
	EXPECT_EQ(recv(self->cfd, buf, st.st_size, MSG_WAITALL), st.st_size);
}

static void chunked_sendfile(struct __test_metadata *_metadata,
			     struct _test_data_tls *self,
			     uint16_t chunk_size,
			     uint16_t extra_payload_size)
{
	char buf[TLS_PAYLOAD_MAX_LEN];
	uint16_t test_payload_size;
	int size = 0;
	int ret;
	char filename[] = "/tmp/mytemp.XXXXXX";
	int fd = mkstemp(filename);
	off_t offset = 0;

	unlink(filename);
	ASSERT_GE(fd, 0);
	EXPECT_GE(chunk_size, 1);
	test_payload_size = chunk_size + extra_payload_size;
	ASSERT_GE(TLS_PAYLOAD_MAX_LEN, test_payload_size);
	memset(buf, 1, test_payload_size);
	size = write(fd, buf, test_payload_size);
	EXPECT_EQ(size, test_payload_size);
	fsync(fd);

	while (size > 0) {
		ret = sendfile(self->fd, fd, &offset, chunk_size);
		EXPECT_GE(ret, 0);
		size -= ret;
	}

	EXPECT_EQ(recv(self->cfd, buf, test_payload_size, MSG_WAITALL),
		  test_payload_size);

	close(fd);
}

TEST_F(tls, multi_chunk_sendfile)
{
	chunked_sendfile(_metadata, self, 4096, 4096);
	chunked_sendfile(_metadata, self, 4096, 0);
	chunked_sendfile(_metadata, self, 4096, 1);
	chunked_sendfile(_metadata, self, 4096, 2048);
	chunked_sendfile(_metadata, self, 8192, 2048);
	chunked_sendfile(_metadata, self, 4096, 8192);
	chunked_sendfile(_metadata, self, 8192, 4096);
	chunked_sendfile(_metadata, self, 12288, 1024);
	chunked_sendfile(_metadata, self, 12288, 2000);
	chunked_sendfile(_metadata, self, 15360, 100);
	chunked_sendfile(_metadata, self, 15360, 300);
	chunked_sendfile(_metadata, self, 1, 4096);
	chunked_sendfile(_metadata, self, 2048, 4096);
	chunked_sendfile(_metadata, self, 2048, 8192);
	chunked_sendfile(_metadata, self, 4096, 8192);
	chunked_sendfile(_metadata, self, 1024, 12288);
	chunked_sendfile(_metadata, self, 2000, 12288);
	chunked_sendfile(_metadata, self, 100, 15360);
	chunked_sendfile(_metadata, self, 300, 15360);
}

TEST_F(tls, recv_max)
{
	unsigned int send_len = TLS_PAYLOAD_MAX_LEN;
	char recv_mem[TLS_PAYLOAD_MAX_LEN];
	char buf[TLS_PAYLOAD_MAX_LEN];

	EXPECT_GE(send(self->fd, buf, send_len, 0), 0);
	EXPECT_NE(recv(self->cfd, recv_mem, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, recv_mem, send_len), 0);
}

TEST_F(tls, recv_small)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10];

	send_len = strlen(test_str) + 1;
	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
}

TEST_F(tls, msg_more)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10 * 2];

	EXPECT_EQ(send(self->fd, test_str, send_len, MSG_MORE), send_len);
	EXPECT_EQ(recv(self->cfd, buf, send_len, MSG_DONTWAIT), -1);
	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_EQ(recv(self->cfd, buf, send_len * 2, MSG_WAITALL),
		  send_len * 2);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
}

TEST_F(tls, msg_more_unsent)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10];

	EXPECT_EQ(send(self->fd, test_str, send_len, MSG_MORE), send_len);
	EXPECT_EQ(recv(self->cfd, buf, send_len, MSG_DONTWAIT), -1);
}

TEST_F(tls, sendmsg_single)
{
	struct msghdr msg;

	char const *test_str = "test_sendmsg";
	size_t send_len = 13;
	struct iovec vec;
	char buf[13];

	vec.iov_base = (char *)test_str;
	vec.iov_len = send_len;
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	EXPECT_EQ(sendmsg(self->fd, &msg, 0), send_len);
	EXPECT_EQ(recv(self->cfd, buf, send_len, MSG_WAITALL), send_len);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
}

#define MAX_FRAGS	64
#define SEND_LEN	13
TEST_F(tls, sendmsg_fragmented)
{
	char const *test_str = "test_sendmsg";
	char buf[SEND_LEN * MAX_FRAGS];
	struct iovec vec[MAX_FRAGS];
	struct msghdr msg;
	int i, frags;

	for (frags = 1; frags <= MAX_FRAGS; frags++) {
		for (i = 0; i < frags; i++) {
			vec[i].iov_base = (char *)test_str;
			vec[i].iov_len = SEND_LEN;
		}

		memset(&msg, 0, sizeof(struct msghdr));
		msg.msg_iov = vec;
		msg.msg_iovlen = frags;

		EXPECT_EQ(sendmsg(self->fd, &msg, 0), SEND_LEN * frags);
		EXPECT_EQ(recv(self->cfd, buf, SEND_LEN * frags, MSG_WAITALL),
			  SEND_LEN * frags);

		for (i = 0; i < frags; i++)
			EXPECT_EQ(memcmp(buf + SEND_LEN * i,
					 test_str, SEND_LEN), 0);
	}
}
#undef MAX_FRAGS
#undef SEND_LEN

TEST_F(tls, sendmsg_large)
{
	void *mem = malloc(16384);
	size_t send_len = 16384;
	size_t sends = 128;
	struct msghdr msg;
	size_t recvs = 0;
	size_t sent = 0;

	memset(&msg, 0, sizeof(struct msghdr));
	while (sent++ < sends) {
		struct iovec vec = { (void *)mem, send_len };

		msg.msg_iov = &vec;
		msg.msg_iovlen = 1;
		EXPECT_EQ(sendmsg(self->cfd, &msg, 0), send_len);
	}

	while (recvs++ < sends)
		EXPECT_NE(recv(self->fd, mem, send_len, 0), -1);

	free(mem);
}

TEST_F(tls, sendmsg_multiple)
{
	char const *test_str = "test_sendmsg_multiple";
	struct iovec vec[5];
	char *test_strs[5];
	struct msghdr msg;
	int total_len = 0;
	int len_cmp = 0;
	int iov_len = 5;
	char *buf;
	int i;

	memset(&msg, 0, sizeof(struct msghdr));
	for (i = 0; i < iov_len; i++) {
		test_strs[i] = (char *)malloc(strlen(test_str) + 1);
		snprintf(test_strs[i], strlen(test_str) + 1, "%s", test_str);
		vec[i].iov_base = (void *)test_strs[i];
		vec[i].iov_len = strlen(test_strs[i]) + 1;
		total_len += vec[i].iov_len;
	}
	msg.msg_iov = vec;
	msg.msg_iovlen = iov_len;

	EXPECT_EQ(sendmsg(self->cfd, &msg, 0), total_len);
	buf = malloc(total_len);
	EXPECT_NE(recv(self->fd, buf, total_len, 0), -1);
	for (i = 0; i < iov_len; i++) {
		EXPECT_EQ(memcmp(test_strs[i], buf + len_cmp,
				 strlen(test_strs[i])),
			  0);
		len_cmp += strlen(buf + len_cmp) + 1;
	}
	for (i = 0; i < iov_len; i++)
		free(test_strs[i]);
	free(buf);
}

TEST_F(tls, sendmsg_multiple_stress)
{
	char const *test_str = "abcdefghijklmno";
	struct iovec vec[1024];
	char *test_strs[1024];
	int iov_len = 1024;
	int total_len = 0;
	char buf[1 << 14];
	struct msghdr msg;
	int len_cmp = 0;
	int i;

	memset(&msg, 0, sizeof(struct msghdr));
	for (i = 0; i < iov_len; i++) {
		test_strs[i] = (char *)malloc(strlen(test_str) + 1);
		snprintf(test_strs[i], strlen(test_str) + 1, "%s", test_str);
		vec[i].iov_base = (void *)test_strs[i];
		vec[i].iov_len = strlen(test_strs[i]) + 1;
		total_len += vec[i].iov_len;
	}
	msg.msg_iov = vec;
	msg.msg_iovlen = iov_len;

	EXPECT_EQ(sendmsg(self->fd, &msg, 0), total_len);
	EXPECT_NE(recv(self->cfd, buf, total_len, 0), -1);

	for (i = 0; i < iov_len; i++)
		len_cmp += strlen(buf + len_cmp) + 1;

	for (i = 0; i < iov_len; i++)
		free(test_strs[i]);
}

TEST_F(tls, splice_from_pipe)
{
	int send_len = TLS_PAYLOAD_MAX_LEN;
	char mem_send[TLS_PAYLOAD_MAX_LEN];
	char mem_recv[TLS_PAYLOAD_MAX_LEN];
	int p[2];

	ASSERT_GE(pipe(p), 0);
	EXPECT_GE(write(p[1], mem_send, send_len), 0);
	EXPECT_GE(splice(p[0], NULL, self->fd, NULL, send_len, 0), 0);
	EXPECT_EQ(recv(self->cfd, mem_recv, send_len, MSG_WAITALL), send_len);
	EXPECT_EQ(memcmp(mem_send, mem_recv, send_len), 0);
}

TEST_F(tls, splice_from_pipe2)
{
	int send_len = 16000;
	char mem_send[16000];
	char mem_recv[16000];
	int p2[2];
	int p[2];

	ASSERT_GE(pipe(p), 0);
	ASSERT_GE(pipe(p2), 0);
	EXPECT_GE(write(p[1], mem_send, 8000), 0);
	EXPECT_GE(splice(p[0], NULL, self->fd, NULL, 8000, 0), 0);
	EXPECT_GE(write(p2[1], mem_send + 8000, 8000), 0);
	EXPECT_GE(splice(p2[0], NULL, self->fd, NULL, 8000, 0), 0);
	EXPECT_EQ(recv(self->cfd, mem_recv, send_len, MSG_WAITALL), send_len);
	EXPECT_EQ(memcmp(mem_send, mem_recv, send_len), 0);
}

TEST_F(tls, send_and_splice)
{
	int send_len = TLS_PAYLOAD_MAX_LEN;
	char mem_send[TLS_PAYLOAD_MAX_LEN];
	char mem_recv[TLS_PAYLOAD_MAX_LEN];
	char const *test_str = "test_read";
	int send_len2 = 10;
	char buf[10];
	int p[2];

	ASSERT_GE(pipe(p), 0);
	EXPECT_EQ(send(self->fd, test_str, send_len2, 0), send_len2);
	EXPECT_EQ(recv(self->cfd, buf, send_len2, MSG_WAITALL), send_len2);
	EXPECT_EQ(memcmp(test_str, buf, send_len2), 0);

	EXPECT_GE(write(p[1], mem_send, send_len), send_len);
	EXPECT_GE(splice(p[0], NULL, self->fd, NULL, send_len, 0), send_len);

	EXPECT_EQ(recv(self->cfd, mem_recv, send_len, MSG_WAITALL), send_len);
	EXPECT_EQ(memcmp(mem_send, mem_recv, send_len), 0);
}

TEST_F(tls, splice_to_pipe)
{
	int send_len = TLS_PAYLOAD_MAX_LEN;
	char mem_send[TLS_PAYLOAD_MAX_LEN];
	char mem_recv[TLS_PAYLOAD_MAX_LEN];
	int p[2];

	ASSERT_GE(pipe(p), 0);
	EXPECT_GE(send(self->fd, mem_send, send_len, 0), 0);
	EXPECT_GE(splice(self->cfd, NULL, p[1], NULL, send_len, 0), 0);
	EXPECT_GE(read(p[0], mem_recv, send_len), 0);
	EXPECT_EQ(memcmp(mem_send, mem_recv, send_len), 0);
}

TEST_F(tls, recvmsg_single)
{
	char const *test_str = "test_recvmsg_single";
	int send_len = strlen(test_str) + 1;
	char buf[20];
	struct msghdr hdr;
	struct iovec vec;

	memset(&hdr, 0, sizeof(hdr));
	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	vec.iov_base = (char *)buf;
	vec.iov_len = send_len;
	hdr.msg_iovlen = 1;
	hdr.msg_iov = &vec;
	EXPECT_NE(recvmsg(self->cfd, &hdr, 0), -1);
	EXPECT_EQ(memcmp(test_str, buf, send_len), 0);
}

TEST_F(tls, recvmsg_single_max)
{
	int send_len = TLS_PAYLOAD_MAX_LEN;
	char send_mem[TLS_PAYLOAD_MAX_LEN];
	char recv_mem[TLS_PAYLOAD_MAX_LEN];
	struct iovec vec;
	struct msghdr hdr;

	EXPECT_EQ(send(self->fd, send_mem, send_len, 0), send_len);
	vec.iov_base = (char *)recv_mem;
	vec.iov_len = TLS_PAYLOAD_MAX_LEN;

	hdr.msg_iovlen = 1;
	hdr.msg_iov = &vec;
	EXPECT_NE(recvmsg(self->cfd, &hdr, 0), -1);
	EXPECT_EQ(memcmp(send_mem, recv_mem, send_len), 0);
}

TEST_F(tls, recvmsg_multiple)
{
	unsigned int msg_iovlen = 1024;
	unsigned int len_compared = 0;
	struct iovec vec[1024];
	char *iov_base[1024];
	unsigned int iov_len = 16;
	int send_len = 1 << 14;
	char buf[1 << 14];
	struct msghdr hdr;
	int i;

	EXPECT_EQ(send(self->fd, buf, send_len, 0), send_len);
	for (i = 0; i < msg_iovlen; i++) {
		iov_base[i] = (char *)malloc(iov_len);
		vec[i].iov_base = iov_base[i];
		vec[i].iov_len = iov_len;
	}

	hdr.msg_iovlen = msg_iovlen;
	hdr.msg_iov = vec;
	EXPECT_NE(recvmsg(self->cfd, &hdr, 0), -1);
	for (i = 0; i < msg_iovlen; i++)
		len_compared += iov_len;

	for (i = 0; i < msg_iovlen; i++)
		free(iov_base[i]);
}

TEST_F(tls, single_send_multiple_recv)
{
	unsigned int total_len = TLS_PAYLOAD_MAX_LEN * 2;
	unsigned int send_len = TLS_PAYLOAD_MAX_LEN;
	char send_mem[TLS_PAYLOAD_MAX_LEN * 2];
	char recv_mem[TLS_PAYLOAD_MAX_LEN * 2];

	EXPECT_GE(send(self->fd, send_mem, total_len, 0), 0);
	memset(recv_mem, 0, total_len);

	EXPECT_NE(recv(self->cfd, recv_mem, send_len, 0), -1);
	EXPECT_NE(recv(self->cfd, recv_mem + send_len, send_len, 0), -1);
	EXPECT_EQ(memcmp(send_mem, recv_mem, total_len), 0);
}

TEST_F(tls, multiple_send_single_recv)
{
	unsigned int total_len = 2 * 10;
	unsigned int send_len = 10;
	char recv_mem[2 * 10];
	char send_mem[10];

	EXPECT_GE(send(self->fd, send_mem, send_len, 0), 0);
	EXPECT_GE(send(self->fd, send_mem, send_len, 0), 0);
	memset(recv_mem, 0, total_len);
	EXPECT_EQ(recv(self->cfd, recv_mem, total_len, MSG_WAITALL), total_len);

	EXPECT_EQ(memcmp(send_mem, recv_mem, send_len), 0);
	EXPECT_EQ(memcmp(send_mem, recv_mem + send_len, send_len), 0);
}

TEST_F(tls, single_send_multiple_recv_non_align)
{
	const unsigned int total_len = 15;
	const unsigned int recv_len = 10;
	char recv_mem[recv_len * 2];
	char send_mem[total_len];

	EXPECT_GE(send(self->fd, send_mem, total_len, 0), 0);
	memset(recv_mem, 0, total_len);

	EXPECT_EQ(recv(self->cfd, recv_mem, recv_len, 0), recv_len);
	EXPECT_EQ(recv(self->cfd, recv_mem + recv_len, recv_len, 0), 5);
	EXPECT_EQ(memcmp(send_mem, recv_mem, total_len), 0);
}

TEST_F(tls, recv_partial)
{
	char const *test_str = "test_read_partial";
	char const *test_str_first = "test_read";
	char const *test_str_second = "_partial";
	int send_len = strlen(test_str) + 1;
	char recv_mem[18];

	memset(recv_mem, 0, sizeof(recv_mem));
	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, recv_mem, strlen(test_str_first),
		       MSG_WAITALL), -1);
	EXPECT_EQ(memcmp(test_str_first, recv_mem, strlen(test_str_first)), 0);
	memset(recv_mem, 0, sizeof(recv_mem));
	EXPECT_NE(recv(self->cfd, recv_mem, strlen(test_str_second),
		       MSG_WAITALL), -1);
	EXPECT_EQ(memcmp(test_str_second, recv_mem, strlen(test_str_second)),
		  0);
}

TEST_F(tls, recv_nonblock)
{
	char buf[4096];
	bool err;

	EXPECT_EQ(recv(self->cfd, buf, sizeof(buf), MSG_DONTWAIT), -1);
	err = (errno == EAGAIN || errno == EWOULDBLOCK);
	EXPECT_EQ(err, true);
}

TEST_F(tls, recv_peek)
{
	char const *test_str = "test_read_peek";
	int send_len = strlen(test_str) + 1;
	char buf[15];

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, buf, send_len, MSG_PEEK), -1);
	EXPECT_EQ(memcmp(test_str, buf, send_len), 0);
	memset(buf, 0, sizeof(buf));
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(test_str, buf, send_len), 0);
}

TEST_F(tls, recv_peek_multiple)
{
	char const *test_str = "test_read_peek";
	int send_len = strlen(test_str) + 1;
	unsigned int num_peeks = 100;
	char buf[15];
	int i;

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	for (i = 0; i < num_peeks; i++) {
		EXPECT_NE(recv(self->cfd, buf, send_len, MSG_PEEK), -1);
		EXPECT_EQ(memcmp(test_str, buf, send_len), 0);
		memset(buf, 0, sizeof(buf));
	}
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(test_str, buf, send_len), 0);
}

TEST_F(tls, recv_peek_multiple_records)
{
	char const *test_str = "test_read_peek_mult_recs";
	char const *test_str_first = "test_read_peek";
	char const *test_str_second = "_mult_recs";
	int len;
	char buf[64];

	len = strlen(test_str_first);
	EXPECT_EQ(send(self->fd, test_str_first, len, 0), len);

	len = strlen(test_str_second) + 1;
	EXPECT_EQ(send(self->fd, test_str_second, len, 0), len);

	len = strlen(test_str_first);
	memset(buf, 0, len);
	EXPECT_EQ(recv(self->cfd, buf, len, MSG_PEEK | MSG_WAITALL), len);

	/* MSG_PEEK can only peek into the current record. */
	len = strlen(test_str_first);
	EXPECT_EQ(memcmp(test_str_first, buf, len), 0);

	len = strlen(test_str) + 1;
	memset(buf, 0, len);
	EXPECT_EQ(recv(self->cfd, buf, len, MSG_WAITALL), len);

	/* Non-MSG_PEEK will advance strparser (and therefore record)
	 * however.
	 */
	len = strlen(test_str) + 1;
	EXPECT_EQ(memcmp(test_str, buf, len), 0);

	/* MSG_MORE will hold current record open, so later MSG_PEEK
	 * will see everything.
	 */
	len = strlen(test_str_first);
	EXPECT_EQ(send(self->fd, test_str_first, len, MSG_MORE), len);

	len = strlen(test_str_second) + 1;
	EXPECT_EQ(send(self->fd, test_str_second, len, 0), len);

	len = strlen(test_str) + 1;
	memset(buf, 0, len);
	EXPECT_EQ(recv(self->cfd, buf, len, MSG_PEEK | MSG_WAITALL), len);

	len = strlen(test_str) + 1;
	EXPECT_EQ(memcmp(test_str, buf, len), 0);
}

TEST_F(tls, recv_peek_large_buf_mult_recs)
{
	char const *test_str = "test_read_peek_mult_recs";
	char const *test_str_first = "test_read_peek";
	char const *test_str_second = "_mult_recs";
	int len;
	char buf[64];

	len = strlen(test_str_first);
	EXPECT_EQ(send(self->fd, test_str_first, len, 0), len);

	len = strlen(test_str_second) + 1;
	EXPECT_EQ(send(self->fd, test_str_second, len, 0), len);

	len = strlen(test_str) + 1;
	memset(buf, 0, len);
	EXPECT_NE((len = recv(self->cfd, buf, len,
			      MSG_PEEK | MSG_WAITALL)), -1);
	len = strlen(test_str) + 1;
	EXPECT_EQ(memcmp(test_str, buf, len), 0);
}

TEST_F(tls, recv_lowat)
{
	char send_mem[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	char recv_mem[20];
	int lowat = 8;

	EXPECT_EQ(send(self->fd, send_mem, 10, 0), 10);
	EXPECT_EQ(send(self->fd, send_mem, 5, 0), 5);

	memset(recv_mem, 0, 20);
	EXPECT_EQ(setsockopt(self->cfd, SOL_SOCKET, SO_RCVLOWAT,
			     &lowat, sizeof(lowat)), 0);
	EXPECT_EQ(recv(self->cfd, recv_mem, 1, MSG_WAITALL), 1);
	EXPECT_EQ(recv(self->cfd, recv_mem + 1, 6, MSG_WAITALL), 6);
	EXPECT_EQ(recv(self->cfd, recv_mem + 7, 10, 0), 8);

	EXPECT_EQ(memcmp(send_mem, recv_mem, 10), 0);
	EXPECT_EQ(memcmp(send_mem, recv_mem + 10, 5), 0);
}

TEST_F(tls, bidir)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10];
	int ret;

	if (!self->notls) {
		struct tls12_crypto_info_aes_gcm_128 tls12;

		memset(&tls12, 0, sizeof(tls12));
		tls12.info.version = variant->tls_version;
		tls12.info.cipher_type = TLS_CIPHER_AES_GCM_128;

		ret = setsockopt(self->fd, SOL_TLS, TLS_RX, &tls12,
				 sizeof(tls12));
		ASSERT_EQ(ret, 0);

		ret = setsockopt(self->cfd, SOL_TLS, TLS_TX, &tls12,
				 sizeof(tls12));
		ASSERT_EQ(ret, 0);
	}

	ASSERT_EQ(strlen(test_str) + 1, send_len);

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);

	memset(buf, 0, sizeof(buf));

	EXPECT_EQ(send(self->cfd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->fd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
};

TEST_F(tls, pollin)
{
	char const *test_str = "test_poll";
	struct pollfd fd = { 0, 0, 0 };
	char buf[10];
	int send_len = 10;

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	fd.fd = self->cfd;
	fd.events = POLLIN;

	EXPECT_EQ(poll(&fd, 1, 20), 1);
	EXPECT_EQ(fd.revents & POLLIN, 1);
	EXPECT_EQ(recv(self->cfd, buf, send_len, MSG_WAITALL), send_len);
	/* Test timing out */
	EXPECT_EQ(poll(&fd, 1, 20), 0);
}

TEST_F(tls, poll_wait)
{
	char const *test_str = "test_poll_wait";
	int send_len = strlen(test_str) + 1;
	struct pollfd fd = { 0, 0, 0 };
	char recv_mem[15];

	fd.fd = self->cfd;
	fd.events = POLLIN;
	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	/* Set timeout to inf. secs */
	EXPECT_EQ(poll(&fd, 1, -1), 1);
	EXPECT_EQ(fd.revents & POLLIN, 1);
	EXPECT_EQ(recv(self->cfd, recv_mem, send_len, MSG_WAITALL), send_len);
}

TEST_F(tls, poll_wait_split)
{
	struct pollfd fd = { 0, 0, 0 };
	char send_mem[20] = {};
	char recv_mem[15];

	fd.fd = self->cfd;
	fd.events = POLLIN;
	/* Send 20 bytes */
	EXPECT_EQ(send(self->fd, send_mem, sizeof(send_mem), 0),
		  sizeof(send_mem));
	/* Poll with inf. timeout */
	EXPECT_EQ(poll(&fd, 1, -1), 1);
	EXPECT_EQ(fd.revents & POLLIN, 1);
	EXPECT_EQ(recv(self->cfd, recv_mem, sizeof(recv_mem), MSG_WAITALL),
		  sizeof(recv_mem));

	/* Now the remaining 5 bytes of record data are in TLS ULP */
	fd.fd = self->cfd;
	fd.events = POLLIN;
	EXPECT_EQ(poll(&fd, 1, -1), 1);
	EXPECT_EQ(fd.revents & POLLIN, 1);
	EXPECT_EQ(recv(self->cfd, recv_mem, sizeof(recv_mem), 0),
		  sizeof(send_mem) - sizeof(recv_mem));
}

TEST_F(tls, blocking)
{
	size_t data = 100000;
	int res = fork();

	EXPECT_NE(res, -1);

	if (res) {
		/* parent */
		size_t left = data;
		char buf[16384];
		int status;
		int pid2;

		while (left) {
			int res = send(self->fd, buf,
				       left > 16384 ? 16384 : left, 0);

			EXPECT_GE(res, 0);
			left -= res;
		}

		pid2 = wait(&status);
		EXPECT_EQ(status, 0);
		EXPECT_EQ(res, pid2);
	} else {
		/* child */
		size_t left = data;
		char buf[16384];

		while (left) {
			int res = recv(self->cfd, buf,
				       left > 16384 ? 16384 : left, 0);

			EXPECT_GE(res, 0);
			left -= res;
		}
	}
}

TEST_F(tls, nonblocking)
{
	size_t data = 100000;
	int sendbuf = 100;
	int flags;
	int res;

	flags = fcntl(self->fd, F_GETFL, 0);
	fcntl(self->fd, F_SETFL, flags | O_NONBLOCK);
	fcntl(self->cfd, F_SETFL, flags | O_NONBLOCK);

	/* Ensure nonblocking behavior by imposing a small send
	 * buffer.
	 */
	EXPECT_EQ(setsockopt(self->fd, SOL_SOCKET, SO_SNDBUF,
			     &sendbuf, sizeof(sendbuf)), 0);

	res = fork();
	EXPECT_NE(res, -1);

	if (res) {
		/* parent */
		bool eagain = false;
		size_t left = data;
		char buf[16384];
		int status;
		int pid2;

		while (left) {
			int res = send(self->fd, buf,
				       left > 16384 ? 16384 : left, 0);

			if (res == -1 && errno == EAGAIN) {
				eagain = true;
				usleep(10000);
				continue;
			}
			EXPECT_GE(res, 0);
			left -= res;
		}

		EXPECT_TRUE(eagain);
		pid2 = wait(&status);

		EXPECT_EQ(status, 0);
		EXPECT_EQ(res, pid2);
	} else {
		/* child */
		bool eagain = false;
		size_t left = data;
		char buf[16384];

		while (left) {
			int res = recv(self->cfd, buf,
				       left > 16384 ? 16384 : left, 0);

			if (res == -1 && errno == EAGAIN) {
				eagain = true;
				usleep(10000);
				continue;
			}
			EXPECT_GE(res, 0);
			left -= res;
		}
		EXPECT_TRUE(eagain);
	}
}

static void
test_mutliproc(struct __test_metadata *_metadata, struct _test_data_tls *self,
	       bool sendpg, unsigned int n_readers, unsigned int n_writers)
{
	const unsigned int n_children = n_readers + n_writers;
	const size_t data = 6 * 1000 * 1000;
	const size_t file_sz = data / 100;
	size_t read_bias, write_bias;
	int i, fd, child_id;
	char buf[file_sz];
	pid_t pid;

	/* Only allow multiples for simplicity */
	ASSERT_EQ(!(n_readers % n_writers) || !(n_writers % n_readers), true);
	read_bias = n_writers / n_readers ?: 1;
	write_bias = n_readers / n_writers ?: 1;

	/* prep a file to send */
	fd = open("/tmp/", O_TMPFILE | O_RDWR, 0600);
	ASSERT_GE(fd, 0);

	memset(buf, 0xac, file_sz);
	ASSERT_EQ(write(fd, buf, file_sz), file_sz);

	/* spawn children */
	for (child_id = 0; child_id < n_children; child_id++) {
		pid = fork();
		ASSERT_NE(pid, -1);
		if (!pid)
			break;
	}

	/* parent waits for all children */
	if (pid) {
		for (i = 0; i < n_children; i++) {
			int status;

			wait(&status);
			EXPECT_EQ(status, 0);
		}

		return;
	}

	/* Split threads for reading and writing */
	if (child_id < n_readers) {
		size_t left = data * read_bias;
		char rb[8001];

		while (left) {
			int res;

			res = recv(self->cfd, rb,
				   left > sizeof(rb) ? sizeof(rb) : left, 0);

			EXPECT_GE(res, 0);
			left -= res;
		}
	} else {
		size_t left = data * write_bias;

		while (left) {
			int res;

			ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
			if (sendpg)
				res = sendfile(self->fd, fd, NULL,
					       left > file_sz ? file_sz : left);
			else
				res = send(self->fd, buf,
					   left > file_sz ? file_sz : left, 0);

			EXPECT_GE(res, 0);
			left -= res;
		}
	}
}

TEST_F(tls, mutliproc_even)
{
	test_mutliproc(_metadata, self, false, 6, 6);
}

TEST_F(tls, mutliproc_readers)
{
	test_mutliproc(_metadata, self, false, 4, 12);
}

TEST_F(tls, mutliproc_writers)
{
	test_mutliproc(_metadata, self, false, 10, 2);
}

TEST_F(tls, mutliproc_sendpage_even)
{
	test_mutliproc(_metadata, self, true, 6, 6);
}

TEST_F(tls, mutliproc_sendpage_readers)
{
	test_mutliproc(_metadata, self, true, 4, 12);
}

TEST_F(tls, mutliproc_sendpage_writers)
{
	test_mutliproc(_metadata, self, true, 10, 2);
}

TEST_F(tls, control_msg)
{
	if (self->notls)
		return;

	char cbuf[CMSG_SPACE(sizeof(char))];
	char const *test_str = "test_read";
	int cmsg_len = sizeof(char);
	char record_type = 100;
	struct cmsghdr *cmsg;
	struct msghdr msg;
	int send_len = 10;
	struct iovec vec;
	char buf[10];

	vec.iov_base = (char *)test_str;
	vec.iov_len = 10;
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof(cbuf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_TLS;
	/* test sending non-record types. */
	cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
	cmsg->cmsg_len = CMSG_LEN(cmsg_len);
	*CMSG_DATA(cmsg) = record_type;
	msg.msg_controllen = cmsg->cmsg_len;

	EXPECT_EQ(sendmsg(self->fd, &msg, 0), send_len);
	/* Should fail because we didn't provide a control message */
	EXPECT_EQ(recv(self->cfd, buf, send_len, 0), -1);

	vec.iov_base = buf;
	EXPECT_EQ(recvmsg(self->cfd, &msg, MSG_WAITALL | MSG_PEEK), send_len);

	cmsg = CMSG_FIRSTHDR(&msg);
	EXPECT_NE(cmsg, NULL);
	EXPECT_EQ(cmsg->cmsg_level, SOL_TLS);
	EXPECT_EQ(cmsg->cmsg_type, TLS_GET_RECORD_TYPE);
	record_type = *((unsigned char *)CMSG_DATA(cmsg));
	EXPECT_EQ(record_type, 100);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);

	/* Recv the message again without MSG_PEEK */
	record_type = 0;
	memset(buf, 0, sizeof(buf));

	EXPECT_EQ(recvmsg(self->cfd, &msg, MSG_WAITALL), send_len);
	cmsg = CMSG_FIRSTHDR(&msg);
	EXPECT_NE(cmsg, NULL);
	EXPECT_EQ(cmsg->cmsg_level, SOL_TLS);
	EXPECT_EQ(cmsg->cmsg_type, TLS_GET_RECORD_TYPE);
	record_type = *((unsigned char *)CMSG_DATA(cmsg));
	EXPECT_EQ(record_type, 100);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);
}

TEST_F(tls, shutdown)
{
	char const *test_str = "test_read";
	int send_len = 10;
	char buf[10];

	ASSERT_EQ(strlen(test_str) + 1, send_len);

	EXPECT_EQ(send(self->fd, test_str, send_len, 0), send_len);
	EXPECT_NE(recv(self->cfd, buf, send_len, 0), -1);
	EXPECT_EQ(memcmp(buf, test_str, send_len), 0);

	shutdown(self->fd, SHUT_RDWR);
	shutdown(self->cfd, SHUT_RDWR);
}

TEST_F(tls, shutdown_unsent)
{
	char const *test_str = "test_read";
	int send_len = 10;

	EXPECT_EQ(send(self->fd, test_str, send_len, MSG_MORE), send_len);

	shutdown(self->fd, SHUT_RDWR);
	shutdown(self->cfd, SHUT_RDWR);
}

TEST_F(tls, shutdown_reuse)
{
	struct sockaddr_in addr;
	int ret;

	shutdown(self->fd, SHUT_RDWR);
	shutdown(self->cfd, SHUT_RDWR);
	close(self->cfd);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;

	ret = bind(self->fd, &addr, sizeof(addr));
	EXPECT_EQ(ret, 0);
	ret = listen(self->fd, 10);
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EINVAL);

	ret = connect(self->fd, &addr, sizeof(addr));
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EISCONN);
}

TEST(non_established) {
	struct tls12_crypto_info_aes_gcm_256 tls12;
	struct sockaddr_in addr;
	int sfd, ret, fd;
	socklen_t len;

	len = sizeof(addr);

	memset(&tls12, 0, sizeof(tls12));
	tls12.info.version = TLS_1_2_VERSION;
	tls12.info.cipher_type = TLS_CIPHER_AES_GCM_256;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	sfd = socket(AF_INET, SOCK_STREAM, 0);

	ret = bind(sfd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);
	ret = listen(sfd, 10);
	ASSERT_EQ(ret, 0);

	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	EXPECT_EQ(ret, -1);
	/* TLS ULP not supported */
	if (errno == ENOENT)
		return;
	EXPECT_EQ(errno, ENOTCONN);

	ret = setsockopt(sfd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, ENOTCONN);

	ret = getsockname(sfd, &addr, &len);
	ASSERT_EQ(ret, 0);

	ret = connect(fd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);

	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	ASSERT_EQ(ret, 0);

	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	EXPECT_EQ(ret, -1);
	EXPECT_EQ(errno, EEXIST);

	close(fd);
	close(sfd);
}

TEST(keysizes) {
	struct tls12_crypto_info_aes_gcm_256 tls12;
	struct sockaddr_in addr;
	int sfd, ret, fd, cfd;
	socklen_t len;
	bool notls;

	notls = false;
	len = sizeof(addr);

	memset(&tls12, 0, sizeof(tls12));
	tls12.info.version = TLS_1_2_VERSION;
	tls12.info.cipher_type = TLS_CIPHER_AES_GCM_256;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	sfd = socket(AF_INET, SOCK_STREAM, 0);

	ret = bind(sfd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);
	ret = listen(sfd, 10);
	ASSERT_EQ(ret, 0);

	ret = getsockname(sfd, &addr, &len);
	ASSERT_EQ(ret, 0);

	ret = connect(fd, &addr, sizeof(addr));
	ASSERT_EQ(ret, 0);

	ret = setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls"));
	if (ret != 0) {
		notls = true;
		printf("Failure setting TCP_ULP, testing without tls\n");
	}

	if (!notls) {
		ret = setsockopt(fd, SOL_TLS, TLS_TX, &tls12,
				 sizeof(tls12));
		EXPECT_EQ(ret, 0);
	}

	cfd = accept(sfd, &addr, &len);
	ASSERT_GE(cfd, 0);

	if (!notls) {
		ret = setsockopt(cfd, IPPROTO_TCP, TCP_ULP, "tls",
				 sizeof("tls"));
		EXPECT_EQ(ret, 0);

		ret = setsockopt(cfd, SOL_TLS, TLS_RX, &tls12,
				 sizeof(tls12));
		EXPECT_EQ(ret, 0);
	}

	close(sfd);
	close(fd);
	close(cfd);
}

TEST_HARNESS_MAIN
