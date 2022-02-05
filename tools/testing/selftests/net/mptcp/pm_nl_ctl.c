// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <net/if.h>

#include <linux/rtnetlink.h>
#include <linux/genetlink.h>

#include "linux/mptcp.h"

#ifndef MPTCP_PM_NAME
#define MPTCP_PM_NAME		"mptcp_pm"
#endif

static void syntax(char *argv[])
{
	fprintf(stderr, "%s add|get|set|del|flush|dump|accept [<args>]\n", argv[0]);
	fprintf(stderr, "\tadd [flags signal|subflow|backup|fullmesh] [id <nr>] [dev <name>] <ip>\n");
	fprintf(stderr, "\tdel <id> [<ip>]\n");
	fprintf(stderr, "\tget <id>\n");
	fprintf(stderr, "\tset [<ip>] [id <nr>] flags [no]backup|[no]fullmesh [port <nr>]\n");
	fprintf(stderr, "\tflush\n");
	fprintf(stderr, "\tdump\n");
	fprintf(stderr, "\tlimits [<rcv addr max> <subflow max>]\n");
	exit(0);
}

static int init_genl_req(char *data, int family, int cmd, int version)
{
	struct nlmsghdr *nh = (void *)data;
	struct genlmsghdr *gh;
	int off = 0;

	nh->nlmsg_type = family;
	nh->nlmsg_flags = NLM_F_REQUEST;
	nh->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	off += NLMSG_ALIGN(sizeof(*nh));

	gh = (void *)(data + off);
	gh->cmd = cmd;
	gh->version = version;
	off += NLMSG_ALIGN(sizeof(*gh));
	return off;
}

static void nl_error(struct nlmsghdr *nh)
{
	struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
	int len = nh->nlmsg_len - sizeof(*nh);
	uint32_t off;

	if (len < sizeof(struct nlmsgerr))
		error(1, 0, "netlink error message truncated %d min %ld", len,
		      sizeof(struct nlmsgerr));

	if (!err->error) {
		/* check messages from kernel */
		struct rtattr *attrs = (struct rtattr *)NLMSG_DATA(nh);

		while (RTA_OK(attrs, len)) {
			if (attrs->rta_type == NLMSGERR_ATTR_MSG)
				fprintf(stderr, "netlink ext ack msg: %s\n",
					(char *)RTA_DATA(attrs));
			if (attrs->rta_type == NLMSGERR_ATTR_OFFS) {
				memcpy(&off, RTA_DATA(attrs), 4);
				fprintf(stderr, "netlink err off %d\n",
					(int)off);
			}
			attrs = RTA_NEXT(attrs, len);
		}
	} else {
		fprintf(stderr, "netlink error %d", err->error);
	}
}

/* do a netlink command and, if max > 0, fetch the reply  */
static int do_nl_req(int fd, struct nlmsghdr *nh, int len, int max)
{
	struct sockaddr_nl nladdr = { .nl_family = AF_NETLINK };
	socklen_t addr_len;
	void *data = nh;
	int rem, ret;
	int err = 0;

	nh->nlmsg_len = len;
	ret = sendto(fd, data, len, 0, (void *)&nladdr, sizeof(nladdr));
	if (ret != len)
		error(1, errno, "send netlink: %uB != %uB\n", ret, len);
	if (max == 0)
		return 0;

	addr_len = sizeof(nladdr);
	rem = ret = recvfrom(fd, data, max, 0, (void *)&nladdr, &addr_len);
	if (ret < 0)
		error(1, errno, "recv netlink: %uB\n", ret);

	/* Beware: the NLMSG_NEXT macro updates the 'rem' argument */
	for (; NLMSG_OK(nh, rem); nh = NLMSG_NEXT(nh, rem)) {
		if (nh->nlmsg_type == NLMSG_ERROR) {
			nl_error(nh);
			err = 1;
		}
	}
	if (err)
		error(1, 0, "bailing out due to netlink error[s]");
	return ret;
}

static int genl_parse_getfamily(struct nlmsghdr *nlh)
{
	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	int len = nlh->nlmsg_len;
	struct rtattr *attrs;

	if (nlh->nlmsg_type != GENL_ID_CTRL)
		error(1, errno, "Not a controller message, len=%d type=0x%x\n",
		      nlh->nlmsg_len, nlh->nlmsg_type);

	len -= NLMSG_LENGTH(GENL_HDRLEN);

	if (len < 0)
		error(1, errno, "wrong controller message len %d\n", len);

	if (ghdr->cmd != CTRL_CMD_NEWFAMILY)
		error(1, errno, "Unknown controller command %d\n", ghdr->cmd);

	attrs = (struct rtattr *) ((char *) ghdr + GENL_HDRLEN);
	while (RTA_OK(attrs, len)) {
		if (attrs->rta_type == CTRL_ATTR_FAMILY_ID)
			return *(__u16 *)RTA_DATA(attrs);
		attrs = RTA_NEXT(attrs, len);
	}

	error(1, errno, "can't find CTRL_ATTR_FAMILY_ID attr");
	return -1;
}

static int resolve_mptcp_pm_netlink(int fd)
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct nlmsghdr *nh;
	struct rtattr *rta;
	int namelen;
	int off = 0;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0);

	rta = (void *)(data + off);
	namelen = strlen(MPTCP_PM_NAME) + 1;
	rta->rta_type = CTRL_ATTR_FAMILY_NAME;
	rta->rta_len = RTA_LENGTH(namelen);
	memcpy(RTA_DATA(rta), MPTCP_PM_NAME, namelen);
	off += NLMSG_ALIGN(rta->rta_len);

	do_nl_req(fd, nh, off, sizeof(data));
	return genl_parse_getfamily((void *)data);
}

int add_addr(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct rtattr *rta, *nest;
	struct nlmsghdr *nh;
	u_int32_t flags = 0;
	u_int16_t family;
	int nest_start;
	u_int8_t id;
	int off = 0;
	int arg;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_ADD_ADDR,
			    MPTCP_PM_VER);

	if (argc < 3)
		syntax(argv);

	nest_start = off;
	nest = (void *)(data + off);
	nest->rta_type = NLA_F_NESTED | MPTCP_PM_ATTR_ADDR;
	nest->rta_len = RTA_LENGTH(0);
	off += NLMSG_ALIGN(nest->rta_len);

	/* addr data */
	rta = (void *)(data + off);
	if (inet_pton(AF_INET, argv[2], RTA_DATA(rta))) {
		family = AF_INET;
		rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR4;
		rta->rta_len = RTA_LENGTH(4);
	} else if (inet_pton(AF_INET6, argv[2], RTA_DATA(rta))) {
		family = AF_INET6;
		rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR6;
		rta->rta_len = RTA_LENGTH(16);
	} else
		error(1, errno, "can't parse ip %s", argv[2]);
	off += NLMSG_ALIGN(rta->rta_len);

	/* family */
	rta = (void *)(data + off);
	rta->rta_type = MPTCP_PM_ADDR_ATTR_FAMILY;
	rta->rta_len = RTA_LENGTH(2);
	memcpy(RTA_DATA(rta), &family, 2);
	off += NLMSG_ALIGN(rta->rta_len);

	for (arg = 3; arg < argc; arg++) {
		if (!strcmp(argv[arg], "flags")) {
			char *tok, *str;

			/* flags */
			if (++arg >= argc)
				error(1, 0, " missing flags value");

			/* do not support flag list yet */
			for (str = argv[arg]; (tok = strtok(str, ","));
			     str = NULL) {
				if (!strcmp(tok, "subflow"))
					flags |= MPTCP_PM_ADDR_FLAG_SUBFLOW;
				else if (!strcmp(tok, "signal"))
					flags |= MPTCP_PM_ADDR_FLAG_SIGNAL;
				else if (!strcmp(tok, "backup"))
					flags |= MPTCP_PM_ADDR_FLAG_BACKUP;
				else if (!strcmp(tok, "fullmesh"))
					flags |= MPTCP_PM_ADDR_FLAG_FULLMESH;
				else
					error(1, errno,
					      "unknown flag %s", argv[arg]);
			}

			if (flags & MPTCP_PM_ADDR_FLAG_SIGNAL &&
			    flags & MPTCP_PM_ADDR_FLAG_FULLMESH) {
				error(1, errno, "error flag fullmesh");
			}

			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_FLAGS;
			rta->rta_len = RTA_LENGTH(4);
			memcpy(RTA_DATA(rta), &flags, 4);
			off += NLMSG_ALIGN(rta->rta_len);
		} else if (!strcmp(argv[arg], "id")) {
			if (++arg >= argc)
				error(1, 0, " missing id value");

			id = atoi(argv[arg]);
			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_ID;
			rta->rta_len = RTA_LENGTH(1);
			memcpy(RTA_DATA(rta), &id, 1);
			off += NLMSG_ALIGN(rta->rta_len);
		} else if (!strcmp(argv[arg], "dev")) {
			int32_t ifindex;

			if (++arg >= argc)
				error(1, 0, " missing dev name");

			ifindex = if_nametoindex(argv[arg]);
			if (!ifindex)
				error(1, errno, "unknown device %s", argv[arg]);

			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_IF_IDX;
			rta->rta_len = RTA_LENGTH(4);
			memcpy(RTA_DATA(rta), &ifindex, 4);
			off += NLMSG_ALIGN(rta->rta_len);
		} else if (!strcmp(argv[arg], "port")) {
			u_int16_t port;

			if (++arg >= argc)
				error(1, 0, " missing port value");
			if (!(flags & MPTCP_PM_ADDR_FLAG_SIGNAL))
				error(1, 0, " flags must be signal when using port");

			port = atoi(argv[arg]);
			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_PORT;
			rta->rta_len = RTA_LENGTH(2);
			memcpy(RTA_DATA(rta), &port, 2);
			off += NLMSG_ALIGN(rta->rta_len);
		} else
			error(1, 0, "unknown keyword %s", argv[arg]);
	}
	nest->rta_len = off - nest_start;

	do_nl_req(fd, nh, off, 0);
	return 0;
}

int del_addr(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct rtattr *rta, *nest;
	struct nlmsghdr *nh;
	u_int16_t family;
	int nest_start;
	u_int8_t id;
	int off = 0;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_DEL_ADDR,
			    MPTCP_PM_VER);

	/* the only argument is the address id (nonzero) */
	if (argc != 3 && argc != 4)
		syntax(argv);

	id = atoi(argv[2]);
	/* zero id with the IP address */
	if (!id && argc != 4)
		syntax(argv);

	nest_start = off;
	nest = (void *)(data + off);
	nest->rta_type = NLA_F_NESTED | MPTCP_PM_ATTR_ADDR;
	nest->rta_len =  RTA_LENGTH(0);
	off += NLMSG_ALIGN(nest->rta_len);

	/* build a dummy addr with only the ID set */
	rta = (void *)(data + off);
	rta->rta_type = MPTCP_PM_ADDR_ATTR_ID;
	rta->rta_len = RTA_LENGTH(1);
	memcpy(RTA_DATA(rta), &id, 1);
	off += NLMSG_ALIGN(rta->rta_len);

	if (!id) {
		/* addr data */
		rta = (void *)(data + off);
		if (inet_pton(AF_INET, argv[3], RTA_DATA(rta))) {
			family = AF_INET;
			rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR4;
			rta->rta_len = RTA_LENGTH(4);
		} else if (inet_pton(AF_INET6, argv[3], RTA_DATA(rta))) {
			family = AF_INET6;
			rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR6;
			rta->rta_len = RTA_LENGTH(16);
		} else {
			error(1, errno, "can't parse ip %s", argv[3]);
		}
		off += NLMSG_ALIGN(rta->rta_len);

		/* family */
		rta = (void *)(data + off);
		rta->rta_type = MPTCP_PM_ADDR_ATTR_FAMILY;
		rta->rta_len = RTA_LENGTH(2);
		memcpy(RTA_DATA(rta), &family, 2);
		off += NLMSG_ALIGN(rta->rta_len);
	}
	nest->rta_len = off - nest_start;

	do_nl_req(fd, nh, off, 0);
	return 0;
}

static void print_addr(struct rtattr *attrs, int len)
{
	uint16_t family = 0;
	uint16_t port = 0;
	char str[1024];
	uint32_t flags;
	uint8_t id;

	while (RTA_OK(attrs, len)) {
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_FAMILY)
			memcpy(&family, RTA_DATA(attrs), 2);
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_PORT)
			memcpy(&port, RTA_DATA(attrs), 2);
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_ADDR4) {
			if (family != AF_INET)
				error(1, errno, "wrong IP (v4) for family %d",
				      family);
			inet_ntop(AF_INET, RTA_DATA(attrs), str, sizeof(str));
			printf("%s", str);
			if (port)
				printf(" %d", port);
		}
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_ADDR6) {
			if (family != AF_INET6)
				error(1, errno, "wrong IP (v6) for family %d",
				      family);
			inet_ntop(AF_INET6, RTA_DATA(attrs), str, sizeof(str));
			printf("%s", str);
			if (port)
				printf(" %d", port);
		}
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_ID) {
			memcpy(&id, RTA_DATA(attrs), 1);
			printf("id %d ", id);
		}
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_FLAGS) {
			memcpy(&flags, RTA_DATA(attrs), 4);

			printf("flags ");
			if (flags & MPTCP_PM_ADDR_FLAG_SIGNAL) {
				printf("signal");
				flags &= ~MPTCP_PM_ADDR_FLAG_SIGNAL;
				if (flags)
					printf(",");
			}

			if (flags & MPTCP_PM_ADDR_FLAG_SUBFLOW) {
				printf("subflow");
				flags &= ~MPTCP_PM_ADDR_FLAG_SUBFLOW;
				if (flags)
					printf(",");
			}

			if (flags & MPTCP_PM_ADDR_FLAG_BACKUP) {
				printf("backup");
				flags &= ~MPTCP_PM_ADDR_FLAG_BACKUP;
				if (flags)
					printf(",");
			}

			if (flags & MPTCP_PM_ADDR_FLAG_FULLMESH) {
				printf("fullmesh");
				flags &= ~MPTCP_PM_ADDR_FLAG_FULLMESH;
				if (flags)
					printf(",");
			}

			/* bump unknown flags, if any */
			if (flags)
				printf("0x%x", flags);
			printf(" ");
		}
		if (attrs->rta_type == MPTCP_PM_ADDR_ATTR_IF_IDX) {
			char name[IF_NAMESIZE], *ret;
			int32_t ifindex;

			memcpy(&ifindex, RTA_DATA(attrs), 4);
			ret = if_indextoname(ifindex, name);
			if (ret)
				printf("dev %s ", ret);
			else
				printf("dev unknown/%d", ifindex);
		}

		attrs = RTA_NEXT(attrs, len);
	}
	printf("\n");
}

static void print_addrs(struct nlmsghdr *nh, int pm_family, int total_len)
{
	struct rtattr *attrs;

	for (; NLMSG_OK(nh, total_len); nh = NLMSG_NEXT(nh, total_len)) {
		int len = nh->nlmsg_len;

		if (nh->nlmsg_type == NLMSG_DONE)
			break;
		if (nh->nlmsg_type == NLMSG_ERROR)
			nl_error(nh);
		if (nh->nlmsg_type != pm_family)
			continue;

		len -= NLMSG_LENGTH(GENL_HDRLEN);
		attrs = (struct rtattr *) ((char *) NLMSG_DATA(nh) +
					   GENL_HDRLEN);
		while (RTA_OK(attrs, len)) {
			if (attrs->rta_type ==
			    (MPTCP_PM_ATTR_ADDR | NLA_F_NESTED))
				print_addr((void *)RTA_DATA(attrs),
					   attrs->rta_len);
			attrs = RTA_NEXT(attrs, len);
		}
	}
}

int get_addr(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct rtattr *rta, *nest;
	struct nlmsghdr *nh;
	int nest_start;
	u_int8_t id;
	int off = 0;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_GET_ADDR,
			    MPTCP_PM_VER);

	/* the only argument is the address id */
	if (argc != 3)
		syntax(argv);

	id = atoi(argv[2]);

	nest_start = off;
	nest = (void *)(data + off);
	nest->rta_type = NLA_F_NESTED | MPTCP_PM_ATTR_ADDR;
	nest->rta_len =  RTA_LENGTH(0);
	off += NLMSG_ALIGN(nest->rta_len);

	/* build a dummy addr with only the ID set */
	rta = (void *)(data + off);
	rta->rta_type = MPTCP_PM_ADDR_ATTR_ID;
	rta->rta_len = RTA_LENGTH(1);
	memcpy(RTA_DATA(rta), &id, 1);
	off += NLMSG_ALIGN(rta->rta_len);
	nest->rta_len = off - nest_start;

	print_addrs(nh, pm_family, do_nl_req(fd, nh, off, sizeof(data)));
	return 0;
}

int dump_addrs(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	pid_t pid = getpid();
	struct nlmsghdr *nh;
	int off = 0;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_GET_ADDR,
			    MPTCP_PM_VER);
	nh->nlmsg_flags |= NLM_F_DUMP;
	nh->nlmsg_seq = 1;
	nh->nlmsg_pid = pid;
	nh->nlmsg_len = off;

	print_addrs(nh, pm_family, do_nl_req(fd, nh, off, sizeof(data)));
	return 0;
}

int flush_addrs(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct nlmsghdr *nh;
	int off = 0;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_FLUSH_ADDRS,
			    MPTCP_PM_VER);

	do_nl_req(fd, nh, off, 0);
	return 0;
}

static void print_limits(struct nlmsghdr *nh, int pm_family, int total_len)
{
	struct rtattr *attrs;
	uint32_t max;

	for (; NLMSG_OK(nh, total_len); nh = NLMSG_NEXT(nh, total_len)) {
		int len = nh->nlmsg_len;

		if (nh->nlmsg_type == NLMSG_DONE)
			break;
		if (nh->nlmsg_type == NLMSG_ERROR)
			nl_error(nh);
		if (nh->nlmsg_type != pm_family)
			continue;

		len -= NLMSG_LENGTH(GENL_HDRLEN);
		attrs = (struct rtattr *) ((char *) NLMSG_DATA(nh) +
					   GENL_HDRLEN);
		while (RTA_OK(attrs, len)) {
			int type = attrs->rta_type;

			if (type != MPTCP_PM_ATTR_RCV_ADD_ADDRS &&
			    type != MPTCP_PM_ATTR_SUBFLOWS)
				goto next;

			memcpy(&max, RTA_DATA(attrs), 4);
			printf("%s %u\n", type == MPTCP_PM_ATTR_SUBFLOWS ?
					  "subflows" : "accept", max);

next:
			attrs = RTA_NEXT(attrs, len);
		}
	}
}

int get_set_limits(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	uint32_t rcv_addr = 0, subflows = 0;
	int cmd, len = sizeof(data);
	struct nlmsghdr *nh;
	int off = 0;

	/* limit */
	if (argc == 4) {
		rcv_addr = atoi(argv[2]);
		subflows = atoi(argv[3]);
		cmd = MPTCP_PM_CMD_SET_LIMITS;
	} else {
		cmd = MPTCP_PM_CMD_GET_LIMITS;
	}

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, cmd, MPTCP_PM_VER);

	/* limit */
	if (cmd == MPTCP_PM_CMD_SET_LIMITS) {
		struct rtattr *rta = (void *)(data + off);

		rta->rta_type = MPTCP_PM_ATTR_RCV_ADD_ADDRS;
		rta->rta_len = RTA_LENGTH(4);
		memcpy(RTA_DATA(rta), &rcv_addr, 4);
		off += NLMSG_ALIGN(rta->rta_len);

		rta = (void *)(data + off);
		rta->rta_type = MPTCP_PM_ATTR_SUBFLOWS;
		rta->rta_len = RTA_LENGTH(4);
		memcpy(RTA_DATA(rta), &subflows, 4);
		off += NLMSG_ALIGN(rta->rta_len);

		/* do not expect a reply */
		len = 0;
	}

	len = do_nl_req(fd, nh, off, len);
	if (cmd == MPTCP_PM_CMD_GET_LIMITS)
		print_limits(nh, pm_family, len);
	return 0;
}

int set_flags(int fd, int pm_family, int argc, char *argv[])
{
	char data[NLMSG_ALIGN(sizeof(struct nlmsghdr)) +
		  NLMSG_ALIGN(sizeof(struct genlmsghdr)) +
		  1024];
	struct rtattr *rta, *nest;
	struct nlmsghdr *nh;
	u_int32_t flags = 0;
	u_int16_t family;
	int nest_start;
	int use_id = 0;
	u_int8_t id;
	int off = 0;
	int arg = 2;

	memset(data, 0, sizeof(data));
	nh = (void *)data;
	off = init_genl_req(data, pm_family, MPTCP_PM_CMD_SET_FLAGS,
			    MPTCP_PM_VER);

	if (argc < 3)
		syntax(argv);

	nest_start = off;
	nest = (void *)(data + off);
	nest->rta_type = NLA_F_NESTED | MPTCP_PM_ATTR_ADDR;
	nest->rta_len = RTA_LENGTH(0);
	off += NLMSG_ALIGN(nest->rta_len);

	if (!strcmp(argv[arg], "id")) {
		if (++arg >= argc)
			error(1, 0, " missing id value");

		use_id = 1;
		id = atoi(argv[arg]);
		rta = (void *)(data + off);
		rta->rta_type = MPTCP_PM_ADDR_ATTR_ID;
		rta->rta_len = RTA_LENGTH(1);
		memcpy(RTA_DATA(rta), &id, 1);
		off += NLMSG_ALIGN(rta->rta_len);
	} else {
		/* addr data */
		rta = (void *)(data + off);
		if (inet_pton(AF_INET, argv[arg], RTA_DATA(rta))) {
			family = AF_INET;
			rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR4;
			rta->rta_len = RTA_LENGTH(4);
		} else if (inet_pton(AF_INET6, argv[arg], RTA_DATA(rta))) {
			family = AF_INET6;
			rta->rta_type = MPTCP_PM_ADDR_ATTR_ADDR6;
			rta->rta_len = RTA_LENGTH(16);
		} else {
			error(1, errno, "can't parse ip %s", argv[arg]);
		}
		off += NLMSG_ALIGN(rta->rta_len);

		/* family */
		rta = (void *)(data + off);
		rta->rta_type = MPTCP_PM_ADDR_ATTR_FAMILY;
		rta->rta_len = RTA_LENGTH(2);
		memcpy(RTA_DATA(rta), &family, 2);
		off += NLMSG_ALIGN(rta->rta_len);
	}

	if (++arg >= argc)
		error(1, 0, " missing flags keyword");

	for (; arg < argc; arg++) {
		if (!strcmp(argv[arg], "flags")) {
			char *tok, *str;

			/* flags */
			if (++arg >= argc)
				error(1, 0, " missing flags value");

			for (str = argv[arg]; (tok = strtok(str, ","));
			     str = NULL) {
				if (!strcmp(tok, "backup"))
					flags |= MPTCP_PM_ADDR_FLAG_BACKUP;
				else if (!strcmp(tok, "fullmesh"))
					flags |= MPTCP_PM_ADDR_FLAG_FULLMESH;
				else if (strcmp(tok, "nobackup") &&
					 strcmp(tok, "nofullmesh"))
					error(1, errno,
					      "unknown flag %s", argv[arg]);
			}

			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_FLAGS;
			rta->rta_len = RTA_LENGTH(4);
			memcpy(RTA_DATA(rta), &flags, 4);
			off += NLMSG_ALIGN(rta->rta_len);
		} else if (!strcmp(argv[arg], "port")) {
			u_int16_t port;

			if (use_id)
				error(1, 0, " port can't be used with id");

			if (++arg >= argc)
				error(1, 0, " missing port value");

			port = atoi(argv[arg]);
			rta = (void *)(data + off);
			rta->rta_type = MPTCP_PM_ADDR_ATTR_PORT;
			rta->rta_len = RTA_LENGTH(2);
			memcpy(RTA_DATA(rta), &port, 2);
			off += NLMSG_ALIGN(rta->rta_len);
		} else {
			error(1, 0, "unknown keyword %s", argv[arg]);
		}
	}
	nest->rta_len = off - nest_start;

	do_nl_req(fd, nh, off, 0);
	return 0;
}

int main(int argc, char *argv[])
{
	int fd, pm_family;

	if (argc < 2)
		syntax(argv);

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd == -1)
		error(1, errno, "socket netlink");

	pm_family = resolve_mptcp_pm_netlink(fd);

	if (!strcmp(argv[1], "add"))
		return add_addr(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "del"))
		return del_addr(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "flush"))
		return flush_addrs(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "get"))
		return get_addr(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "dump"))
		return dump_addrs(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "limits"))
		return get_set_limits(fd, pm_family, argc, argv);
	else if (!strcmp(argv[1], "set"))
		return set_flags(fd, pm_family, argc, argv);

	fprintf(stderr, "unknown sub-command: %s", argv[1]);
	syntax(argv);
	return 0;
}
