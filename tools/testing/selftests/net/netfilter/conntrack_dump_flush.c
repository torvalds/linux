// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <time.h>
#include <libmnl/libmnl.h>
#include <netinet/ip.h>

#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include "../../kselftest_harness.h"

#define TEST_ZONE_ID 123
#define NF_CT_DEFAULT_ZONE_ID 0

static int reply_counter;

static int build_cta_tuple_v4(struct nlmsghdr *nlh, int type,
			      uint32_t src_ip, uint32_t dst_ip,
			      uint16_t src_port, uint16_t dst_port)
{
	struct nlattr *nest, *nest_ip, *nest_proto;

	nest = mnl_attr_nest_start(nlh, type);
	if (!nest)
		return -1;

	nest_ip = mnl_attr_nest_start(nlh, CTA_TUPLE_IP);
	if (!nest_ip)
		return -1;
	mnl_attr_put_u32(nlh, CTA_IP_V4_SRC, src_ip);
	mnl_attr_put_u32(nlh, CTA_IP_V4_DST, dst_ip);
	mnl_attr_nest_end(nlh, nest_ip);

	nest_proto = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
	if (!nest_proto)
		return -1;
	mnl_attr_put_u8(nlh, CTA_PROTO_NUM, 6);
	mnl_attr_put_u16(nlh, CTA_PROTO_SRC_PORT, htons(src_port));
	mnl_attr_put_u16(nlh, CTA_PROTO_DST_PORT, htons(dst_port));
	mnl_attr_nest_end(nlh, nest_proto);

	mnl_attr_nest_end(nlh, nest);

	return 0;
}

static int build_cta_tuple_v6(struct nlmsghdr *nlh, int type,
			      struct in6_addr src_ip, struct in6_addr dst_ip,
			      uint16_t src_port, uint16_t dst_port)
{
	struct nlattr *nest, *nest_ip, *nest_proto;

	nest = mnl_attr_nest_start(nlh, type);
	if (!nest)
		return -1;

	nest_ip = mnl_attr_nest_start(nlh, CTA_TUPLE_IP);
	if (!nest_ip)
		return -1;
	mnl_attr_put(nlh, CTA_IP_V6_SRC, sizeof(struct in6_addr), &src_ip);
	mnl_attr_put(nlh, CTA_IP_V6_DST, sizeof(struct in6_addr), &dst_ip);
	mnl_attr_nest_end(nlh, nest_ip);

	nest_proto = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
	if (!nest_proto)
		return -1;
	mnl_attr_put_u8(nlh, CTA_PROTO_NUM, 6);
	mnl_attr_put_u16(nlh, CTA_PROTO_SRC_PORT, htons(src_port));
	mnl_attr_put_u16(nlh, CTA_PROTO_DST_PORT, htons(dst_port));
	mnl_attr_nest_end(nlh, nest_proto);

	mnl_attr_nest_end(nlh, nest);

	return 0;
}

static int build_cta_proto(struct nlmsghdr *nlh)
{
	struct nlattr *nest, *nest_proto;

	nest = mnl_attr_nest_start(nlh, CTA_PROTOINFO);
	if (!nest)
		return -1;

	nest_proto = mnl_attr_nest_start(nlh, CTA_PROTOINFO_TCP);
	if (!nest_proto)
		return -1;
	mnl_attr_put_u8(nlh, CTA_PROTOINFO_TCP_STATE, TCP_CONNTRACK_ESTABLISHED);
	mnl_attr_put_u16(nlh, CTA_PROTOINFO_TCP_FLAGS_ORIGINAL, 0x0a0a);
	mnl_attr_put_u16(nlh, CTA_PROTOINFO_TCP_FLAGS_REPLY, 0x0a0a);
	mnl_attr_nest_end(nlh, nest_proto);

	mnl_attr_nest_end(nlh, nest);

	return 0;
}

static int conntrack_data_insert(struct mnl_socket *sock, struct nlmsghdr *nlh,
				 uint16_t zone)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *rplnlh;
	unsigned int portid;
	int ret;

	portid = mnl_socket_get_portid(sock);

	ret = build_cta_proto(nlh);
	if (ret < 0) {
		perror("build_cta_proto");
		return -1;
	}
	mnl_attr_put_u32(nlh, CTA_TIMEOUT, htonl(20000));
	mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));

	if (mnl_socket_sendto(sock, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_sendto");
		return -1;
	}

	ret = mnl_socket_recvfrom(sock, buf, MNL_SOCKET_BUFFER_SIZE);
	if (ret < 0) {
		perror("mnl_socket_recvfrom");
		return ret;
	}

	ret = mnl_cb_run(buf, ret, nlh->nlmsg_seq, portid, NULL, NULL);
	if (ret < 0) {
		if (errno == EEXIST) {
			/* The entries are probably still there from a previous
			 * run. So we are good
			 */
			return 0;
		}
		perror("mnl_cb_run");
		return ret;
	}

	return 0;
}

static int conntrack_data_generate_v4(struct mnl_socket *sock, uint32_t src_ip,
				      uint32_t dst_ip, uint16_t zone)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfh;
	int ret;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE |
			   NLM_F_ACK | NLM_F_EXCL;
	nlh->nlmsg_seq = time(NULL);

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = AF_INET;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	ret = build_cta_tuple_v4(nlh, CTA_TUPLE_ORIG, src_ip, dst_ip, 12345, 443);
	if (ret < 0) {
		perror("build_cta_tuple_v4");
		return ret;
	}
	ret = build_cta_tuple_v4(nlh, CTA_TUPLE_REPLY, dst_ip, src_ip, 443, 12345);
	if (ret < 0) {
		perror("build_cta_tuple_v4");
		return ret;
	}
	return conntrack_data_insert(sock, nlh, zone);
}

static int conntrack_data_generate_v6(struct mnl_socket *sock,
				      struct in6_addr src_ip,
				      struct in6_addr dst_ip,
				      uint16_t zone)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfh;
	int ret;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE |
			   NLM_F_ACK | NLM_F_EXCL;
	nlh->nlmsg_seq = time(NULL);

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = AF_INET6;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	ret = build_cta_tuple_v6(nlh, CTA_TUPLE_ORIG, src_ip, dst_ip,
				 12345, 443);
	if (ret < 0) {
		perror("build_cta_tuple_v6");
		return ret;
	}
	ret = build_cta_tuple_v6(nlh, CTA_TUPLE_REPLY, dst_ip, src_ip,
				 12345, 443);
	if (ret < 0) {
		perror("build_cta_tuple_v6");
		return ret;
	}
	return conntrack_data_insert(sock, nlh, zone);
}

static int count_entries(const struct nlmsghdr *nlh, void *data)
{
	reply_counter++;
	return MNL_CB_OK;
}

static int conntracK_count_zone(struct mnl_socket *sock, uint16_t zone)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh, *rplnlh;
	struct nfgenmsg *nfh;
	struct nlattr *nest;
	unsigned int portid;
	int ret;

	portid = mnl_socket_get_portid(sock);

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = time(NULL);

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = AF_UNSPEC;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));

	ret = mnl_socket_sendto(sock, nlh, nlh->nlmsg_len);
	if (ret < 0) {
		perror("mnl_socket_sendto");
		return ret;
	}

	reply_counter = 0;
	ret = mnl_socket_recvfrom(sock, buf, MNL_SOCKET_BUFFER_SIZE);
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, nlh->nlmsg_seq, portid,
				 count_entries, NULL);
		if (ret <= MNL_CB_STOP)
			break;

		ret = mnl_socket_recvfrom(sock, buf, MNL_SOCKET_BUFFER_SIZE);
	}
	if (ret < 0) {
		perror("mnl_socket_recvfrom");
		return ret;
	}

	return reply_counter;
}

static int conntrack_flush_zone(struct mnl_socket *sock, uint16_t zone)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh, *rplnlh;
	struct nfgenmsg *nfh;
	struct nlattr *nest;
	unsigned int portid;
	int ret;

	portid = mnl_socket_get_portid(sock);

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_DELETE;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_seq = time(NULL);

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = AF_UNSPEC;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));

	ret = mnl_socket_sendto(sock, nlh, nlh->nlmsg_len);
	if (ret < 0) {
		perror("mnl_socket_sendto");
		return ret;
	}

	ret = mnl_socket_recvfrom(sock, buf, MNL_SOCKET_BUFFER_SIZE);
	if (ret < 0) {
		perror("mnl_socket_recvfrom");
		return ret;
	}

	ret = mnl_cb_run(buf, ret, nlh->nlmsg_seq, portid, NULL, NULL);
	if (ret < 0) {
		perror("mnl_cb_run");
		return ret;
	}

	return 0;
}

FIXTURE(conntrack_dump_flush)
{
	struct mnl_socket *sock;
};

FIXTURE_SETUP(conntrack_dump_flush)
{
	struct in6_addr src, dst;
	int ret;

	self->sock = mnl_socket_open(NETLINK_NETFILTER);
	if (!self->sock) {
		perror("mnl_socket_open");
		SKIP(return, "cannot open netlink_netfilter socket");
	}

	ret = mnl_socket_bind(self->sock, 0, MNL_SOCKET_AUTOPID);
	EXPECT_EQ(ret, 0);

	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID);
	if (ret < 0 && errno == EPERM)
		SKIP(return, "Needs to be run as root");
	else if (ret < 0 && errno == EOPNOTSUPP)
		SKIP(return, "Kernel does not seem to support conntrack zones");

	ret = conntrack_data_generate_v4(self->sock, 0xf0f0f0f0, 0xf1f1f1f1,
					 TEST_ZONE_ID);
	EXPECT_EQ(ret, 0);
	ret = conntrack_data_generate_v4(self->sock, 0xf2f2f2f2, 0xf3f3f3f3,
					 TEST_ZONE_ID + 1);
	EXPECT_EQ(ret, 0);
	ret = conntrack_data_generate_v4(self->sock, 0xf4f4f4f4, 0xf5f5f5f5,
					 TEST_ZONE_ID + 2);
	EXPECT_EQ(ret, 0);
	ret = conntrack_data_generate_v4(self->sock, 0xf6f6f6f6, 0xf7f7f7f7,
					 NF_CT_DEFAULT_ZONE_ID);
	EXPECT_EQ(ret, 0);

	src = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x01000000
		}
	}};
	dst = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x02000000
		}
	}};
	ret = conntrack_data_generate_v6(self->sock, src, dst,
					 TEST_ZONE_ID);
	EXPECT_EQ(ret, 0);
	src = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x03000000
		}
	}};
	dst = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x04000000
		}
	}};
	ret = conntrack_data_generate_v6(self->sock, src, dst,
					 TEST_ZONE_ID + 1);
	EXPECT_EQ(ret, 0);
	src = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x05000000
		}
	}};
	dst = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x06000000
		}
	}};
	ret = conntrack_data_generate_v6(self->sock, src, dst,
					 TEST_ZONE_ID + 2);
	EXPECT_EQ(ret, 0);

	src = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x07000000
		}
	}};
	dst = (struct in6_addr) {{
		.__u6_addr32 = {
			0xb80d0120,
			0x00000000,
			0x00000000,
			0x08000000
		}
	}};
	ret = conntrack_data_generate_v6(self->sock, src, dst,
					 NF_CT_DEFAULT_ZONE_ID);
	EXPECT_EQ(ret, 0);

	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID);
	EXPECT_GE(ret, 2);
	if (ret > 2)
		SKIP(return, "kernel does not support filtering by zone");
}

FIXTURE_TEARDOWN(conntrack_dump_flush)
{
}

TEST_F(conntrack_dump_flush, test_dump_by_zone)
{
	int ret;

	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID);
	EXPECT_EQ(ret, 2);
}

TEST_F(conntrack_dump_flush, test_flush_by_zone)
{
	int ret;

	ret = conntrack_flush_zone(self->sock, TEST_ZONE_ID);
	EXPECT_EQ(ret, 0);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID);
	EXPECT_EQ(ret, 0);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID + 1);
	EXPECT_EQ(ret, 2);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID + 2);
	EXPECT_EQ(ret, 2);
	ret = conntracK_count_zone(self->sock, NF_CT_DEFAULT_ZONE_ID);
	EXPECT_EQ(ret, 2);
}

TEST_F(conntrack_dump_flush, test_flush_by_zone_default)
{
	int ret;

	ret = conntrack_flush_zone(self->sock, NF_CT_DEFAULT_ZONE_ID);
	EXPECT_EQ(ret, 0);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID);
	EXPECT_EQ(ret, 2);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID + 1);
	EXPECT_EQ(ret, 2);
	ret = conntracK_count_zone(self->sock, TEST_ZONE_ID + 2);
	EXPECT_EQ(ret, 2);
	ret = conntracK_count_zone(self->sock, NF_CT_DEFAULT_ZONE_ID);
	EXPECT_EQ(ret, 0);
}

TEST_HARNESS_MAIN
