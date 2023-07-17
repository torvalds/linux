// SPDX-License-Identifier: GPL-2.0
#include <uapi/linux/bpf.h>
#include <uapi/linux/netdev.h>
#include <linux/if_link.h>
#include <signal.h>
#include <argp.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <pthread.h>

#include <network_helpers.h>

#include "xdp_features.skel.h"
#include "xdp_features.h"

#define RED(str)	"\033[0;31m" str "\033[0m"
#define GREEN(str)	"\033[0;32m" str "\033[0m"
#define YELLOW(str)	"\033[0;33m" str "\033[0m"

static struct env {
	bool verbosity;
	char ifname[IF_NAMESIZE];
	int ifindex;
	bool is_tester;
	struct {
		enum netdev_xdp_act drv_feature;
		enum xdp_action action;
	} feature;
	struct sockaddr_storage dut_ctrl_addr;
	struct sockaddr_storage dut_addr;
	struct sockaddr_storage tester_addr;
} env;

#define BUFSIZE		128

void test__fail(void) { /* for network_helpers.c */ }

static int libbpf_print_fn(enum libbpf_print_level level,
			   const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !env.verbosity)
		return 0;
	return vfprintf(stderr, format, args);
}

static volatile bool exiting;

static void sig_handler(int sig)
{
	exiting = true;
}

const char *argp_program_version = "xdp-features 0.0";
const char argp_program_doc[] =
"XDP features detection application.\n"
"\n"
"XDP features application checks the XDP advertised features match detected ones.\n"
"\n"
"USAGE: ./xdp-features [-vt] [-f <xdp-feature>] [-D <dut-data-ip>] [-T <tester-data-ip>] [-C <dut-ctrl-ip>] <iface-name>\n"
"\n"
"dut-data-ip, tester-data-ip, dut-ctrl-ip: IPv6 or IPv4-mapped-IPv6 addresses;\n"
"\n"
"XDP features\n:"
"- XDP_PASS\n"
"- XDP_DROP\n"
"- XDP_ABORTED\n"
"- XDP_REDIRECT\n"
"- XDP_NDO_XMIT\n"
"- XDP_TX\n";

static const struct argp_option opts[] = {
	{ "verbose", 'v', NULL, 0, "Verbose debug output" },
	{ "tester", 't', NULL, 0, "Tester mode" },
	{ "feature", 'f', "XDP-FEATURE", 0, "XDP feature to test" },
	{ "dut_data_ip", 'D', "DUT-DATA-IP", 0, "DUT IP data channel" },
	{ "dut_ctrl_ip", 'C', "DUT-CTRL-IP", 0, "DUT IP control channel" },
	{ "tester_data_ip", 'T', "TESTER-DATA-IP", 0, "Tester IP data channel" },
	{},
};

static int get_xdp_feature(const char *arg)
{
	if (!strcmp(arg, "XDP_PASS")) {
		env.feature.action = XDP_PASS;
		env.feature.drv_feature = NETDEV_XDP_ACT_BASIC;
	} else if (!strcmp(arg, "XDP_DROP")) {
		env.feature.drv_feature = NETDEV_XDP_ACT_BASIC;
		env.feature.action = XDP_DROP;
	} else if (!strcmp(arg, "XDP_ABORTED")) {
		env.feature.drv_feature = NETDEV_XDP_ACT_BASIC;
		env.feature.action = XDP_ABORTED;
	} else if (!strcmp(arg, "XDP_TX")) {
		env.feature.drv_feature = NETDEV_XDP_ACT_BASIC;
		env.feature.action = XDP_TX;
	} else if (!strcmp(arg, "XDP_REDIRECT")) {
		env.feature.drv_feature = NETDEV_XDP_ACT_REDIRECT;
		env.feature.action = XDP_REDIRECT;
	} else if (!strcmp(arg, "XDP_NDO_XMIT")) {
		env.feature.drv_feature = NETDEV_XDP_ACT_NDO_XMIT;
	} else {
		return -EINVAL;
	}

	return 0;
}

static char *get_xdp_feature_str(void)
{
	switch (env.feature.action) {
	case XDP_PASS:
		return YELLOW("XDP_PASS");
	case XDP_DROP:
		return YELLOW("XDP_DROP");
	case XDP_ABORTED:
		return YELLOW("XDP_ABORTED");
	case XDP_TX:
		return YELLOW("XDP_TX");
	case XDP_REDIRECT:
		return YELLOW("XDP_REDIRECT");
	default:
		break;
	}

	if (env.feature.drv_feature == NETDEV_XDP_ACT_NDO_XMIT)
		return YELLOW("XDP_NDO_XMIT");

	return "";
}

static error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case 'v':
		env.verbosity = true;
		break;
	case 't':
		env.is_tester = true;
		break;
	case 'f':
		if (get_xdp_feature(arg) < 0) {
			fprintf(stderr, "Invalid xdp feature: %s\n", arg);
			argp_usage(state);
			return ARGP_ERR_UNKNOWN;
		}
		break;
	case 'D':
		if (make_sockaddr(AF_INET6, arg, DUT_ECHO_PORT,
				  &env.dut_addr, NULL)) {
			fprintf(stderr,
				"Invalid address assigned to the Device Under Test: %s\n",
				arg);
			return ARGP_ERR_UNKNOWN;
		}
		break;
	case 'C':
		if (make_sockaddr(AF_INET6, arg, DUT_CTRL_PORT,
				  &env.dut_ctrl_addr, NULL)) {
			fprintf(stderr,
				"Invalid address assigned to the Device Under Test: %s\n",
				arg);
			return ARGP_ERR_UNKNOWN;
		}
		break;
	case 'T':
		if (make_sockaddr(AF_INET6, arg, 0, &env.tester_addr, NULL)) {
			fprintf(stderr,
				"Invalid address assigned to the Tester device: %s\n",
				arg);
			return ARGP_ERR_UNKNOWN;
		}
		break;
	case ARGP_KEY_ARG:
		errno = 0;
		if (strlen(arg) >= IF_NAMESIZE) {
			fprintf(stderr, "Invalid device name: %s\n", arg);
			argp_usage(state);
			return ARGP_ERR_UNKNOWN;
		}

		env.ifindex = if_nametoindex(arg);
		if (!env.ifindex)
			env.ifindex = strtoul(arg, NULL, 0);
		if (!env.ifindex || !if_indextoname(env.ifindex, env.ifname)) {
			fprintf(stderr,
				"Bad interface index or name (%d): %s\n",
				errno, strerror(errno));
			argp_usage(state);
			return ARGP_ERR_UNKNOWN;
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static const struct argp argp = {
	.options = opts,
	.parser = parse_arg,
	.doc = argp_program_doc,
};

static void set_env_default(void)
{
	env.feature.drv_feature = NETDEV_XDP_ACT_NDO_XMIT;
	env.feature.action = -EINVAL;
	env.ifindex = -ENODEV;
	strcpy(env.ifname, "unknown");
	make_sockaddr(AF_INET6, "::ffff:127.0.0.1", DUT_CTRL_PORT,
		      &env.dut_ctrl_addr, NULL);
	make_sockaddr(AF_INET6, "::ffff:127.0.0.1", DUT_ECHO_PORT,
		      &env.dut_addr, NULL);
	make_sockaddr(AF_INET6, "::ffff:127.0.0.1", 0, &env.tester_addr, NULL);
}

static void *dut_echo_thread(void *arg)
{
	unsigned char buf[sizeof(struct tlv_hdr)];
	int sockfd = *(int *)arg;

	while (!exiting) {
		struct tlv_hdr *tlv = (struct tlv_hdr *)buf;
		struct sockaddr_storage addr;
		socklen_t addrlen;
		size_t n;

		n = recvfrom(sockfd, buf, sizeof(buf), MSG_WAITALL,
			     (struct sockaddr *)&addr, &addrlen);
		if (n != ntohs(tlv->len))
			continue;

		if (ntohs(tlv->type) != CMD_ECHO)
			continue;

		sendto(sockfd, buf, sizeof(buf), MSG_NOSIGNAL | MSG_CONFIRM,
		       (struct sockaddr *)&addr, addrlen);
	}

	pthread_exit((void *)0);
	close(sockfd);

	return NULL;
}

static int dut_run_echo_thread(pthread_t *t, int *sockfd)
{
	int err;

	sockfd = start_reuseport_server(AF_INET6, SOCK_DGRAM, NULL,
					DUT_ECHO_PORT, 0, 1);
	if (!sockfd) {
		fprintf(stderr,
			"Failed creating data UDP socket on device %s\n",
			env.ifname);
		return -errno;
	}

	/* start echo channel */
	err = pthread_create(t, NULL, dut_echo_thread, sockfd);
	if (err) {
		fprintf(stderr,
			"Failed creating data UDP thread on device %s: %s\n",
			env.ifname, strerror(-err));
		free_fds(sockfd, 1);
		return -EINVAL;
	}

	return 0;
}

static int dut_attach_xdp_prog(struct xdp_features *skel, int flags)
{
	enum xdp_action action = env.feature.action;
	struct bpf_program *prog;
	unsigned int key = 0;
	int err, fd = 0;

	if (env.feature.drv_feature == NETDEV_XDP_ACT_NDO_XMIT) {
		struct bpf_devmap_val entry = {
			.ifindex = env.ifindex,
		};

		err = bpf_map__update_elem(skel->maps.dev_map,
					   &key, sizeof(key),
					   &entry, sizeof(entry), 0);
		if (err < 0)
			return err;

		fd = bpf_program__fd(skel->progs.xdp_do_redirect_cpumap);
		action = XDP_REDIRECT;
	}

	switch (action) {
	case XDP_TX:
		prog = skel->progs.xdp_do_tx;
		break;
	case XDP_DROP:
		prog = skel->progs.xdp_do_drop;
		break;
	case XDP_ABORTED:
		prog = skel->progs.xdp_do_aborted;
		break;
	case XDP_PASS:
		prog = skel->progs.xdp_do_pass;
		break;
	case XDP_REDIRECT: {
		struct bpf_cpumap_val entry = {
			.qsize = 2048,
			.bpf_prog.fd = fd,
		};

		err = bpf_map__update_elem(skel->maps.cpu_map,
					   &key, sizeof(key),
					   &entry, sizeof(entry), 0);
		if (err < 0)
			return err;

		prog = skel->progs.xdp_do_redirect;
		break;
	}
	default:
		return -EINVAL;
	}

	err = bpf_xdp_attach(env.ifindex, bpf_program__fd(prog), flags, NULL);
	if (err)
		fprintf(stderr, "Failed attaching XDP program to device %s\n",
			env.ifname);
	return err;
}

static int recv_msg(int sockfd, void *buf, size_t bufsize, void *val,
		    size_t val_size)
{
	struct tlv_hdr *tlv = (struct tlv_hdr *)buf;
	size_t len;

	len = recv(sockfd, buf, bufsize, 0);
	if (len != ntohs(tlv->len) || len < sizeof(*tlv))
		return -EINVAL;

	if (val) {
		len -= sizeof(*tlv);
		if (len > val_size)
			return -ENOMEM;

		memcpy(val, tlv->data, len);
	}

	return 0;
}

static int dut_run(struct xdp_features *skel)
{
	int flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_DRV_MODE;
	int state, err, *sockfd, ctrl_sockfd, echo_sockfd;
	struct sockaddr_storage ctrl_addr;
	pthread_t dut_thread;
	socklen_t addrlen;

	sockfd = start_reuseport_server(AF_INET6, SOCK_STREAM, NULL,
					DUT_CTRL_PORT, 0, 1);
	if (!sockfd) {
		fprintf(stderr,
			"Failed creating control socket on device %s\n", env.ifname);
		return -errno;
	}

	ctrl_sockfd = accept(*sockfd, (struct sockaddr *)&ctrl_addr, &addrlen);
	if (ctrl_sockfd < 0) {
		fprintf(stderr,
			"Failed accepting connections on device %s control socket\n",
			env.ifname);
		free_fds(sockfd, 1);
		return -errno;
	}

	/* CTRL loop */
	while (!exiting) {
		unsigned char buf[BUFSIZE] = {};
		struct tlv_hdr *tlv = (struct tlv_hdr *)buf;

		err = recv_msg(ctrl_sockfd, buf, BUFSIZE, NULL, 0);
		if (err)
			continue;

		switch (ntohs(tlv->type)) {
		case CMD_START: {
			if (state == CMD_START)
				continue;

			state = CMD_START;
			/* Load the XDP program on the DUT */
			err = dut_attach_xdp_prog(skel, flags);
			if (err)
				goto out;

			err = dut_run_echo_thread(&dut_thread, &echo_sockfd);
			if (err < 0)
				goto out;

			tlv->type = htons(CMD_ACK);
			tlv->len = htons(sizeof(*tlv));
			err = send(ctrl_sockfd, buf, sizeof(*tlv), 0);
			if (err < 0)
				goto end_thread;
			break;
		}
		case CMD_STOP:
			if (state != CMD_START)
				break;

			state = CMD_STOP;

			exiting = true;
			bpf_xdp_detach(env.ifindex, flags, NULL);

			tlv->type = htons(CMD_ACK);
			tlv->len = htons(sizeof(*tlv));
			err = send(ctrl_sockfd, buf, sizeof(*tlv), 0);
			goto end_thread;
		case CMD_GET_XDP_CAP: {
			LIBBPF_OPTS(bpf_xdp_query_opts, opts);
			unsigned long long val;
			size_t n;

			err = bpf_xdp_query(env.ifindex, XDP_FLAGS_DRV_MODE,
					    &opts);
			if (err) {
				fprintf(stderr,
					"Failed querying XDP cap for device %s\n",
					env.ifname);
				goto end_thread;
			}

			tlv->type = htons(CMD_ACK);
			n = sizeof(*tlv) + sizeof(opts.feature_flags);
			tlv->len = htons(n);

			val = htobe64(opts.feature_flags);
			memcpy(tlv->data, &val, sizeof(val));

			err = send(ctrl_sockfd, buf, n, 0);
			if (err < 0)
				goto end_thread;
			break;
		}
		case CMD_GET_STATS: {
			unsigned int key = 0, val;
			size_t n;

			err = bpf_map__lookup_elem(skel->maps.dut_stats,
						   &key, sizeof(key),
						   &val, sizeof(val), 0);
			if (err) {
				fprintf(stderr,
					"bpf_map_lookup_elem failed (%d)\n", err);
				goto end_thread;
			}

			tlv->type = htons(CMD_ACK);
			n = sizeof(*tlv) + sizeof(val);
			tlv->len = htons(n);

			val = htonl(val);
			memcpy(tlv->data, &val, sizeof(val));

			err = send(ctrl_sockfd, buf, n, 0);
			if (err < 0)
				goto end_thread;
			break;
		}
		default:
			break;
		}
	}

end_thread:
	pthread_join(dut_thread, NULL);
out:
	bpf_xdp_detach(env.ifindex, flags, NULL);
	close(ctrl_sockfd);
	free_fds(sockfd, 1);

	return err;
}

static bool tester_collect_detected_cap(struct xdp_features *skel,
					unsigned int dut_stats)
{
	unsigned int err, key = 0, val;

	if (!dut_stats)
		return false;

	err = bpf_map__lookup_elem(skel->maps.stats, &key, sizeof(key),
				   &val, sizeof(val), 0);
	if (err) {
		fprintf(stderr, "bpf_map_lookup_elem failed (%d)\n", err);
		return false;
	}

	switch (env.feature.action) {
	case XDP_PASS:
	case XDP_TX:
	case XDP_REDIRECT:
		return val > 0;
	case XDP_DROP:
	case XDP_ABORTED:
		return val == 0;
	default:
		break;
	}

	if (env.feature.drv_feature == NETDEV_XDP_ACT_NDO_XMIT)
		return val > 0;

	return false;
}

static int send_and_recv_msg(int sockfd, enum test_commands cmd, void *val,
			     size_t val_size)
{
	unsigned char buf[BUFSIZE] = {};
	struct tlv_hdr *tlv = (struct tlv_hdr *)buf;
	int err;

	tlv->type = htons(cmd);
	tlv->len = htons(sizeof(*tlv));

	err = send(sockfd, buf, sizeof(*tlv), 0);
	if (err < 0)
		return err;

	err = recv_msg(sockfd, buf, BUFSIZE, val, val_size);
	if (err < 0)
		return err;

	return ntohs(tlv->type) == CMD_ACK ? 0 : -EINVAL;
}

static int send_echo_msg(void)
{
	unsigned char buf[sizeof(struct tlv_hdr)];
	struct tlv_hdr *tlv = (struct tlv_hdr *)buf;
	int sockfd, n;

	sockfd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		fprintf(stderr,
			"Failed creating data UDP socket on device %s\n",
			env.ifname);
		return -errno;
	}

	tlv->type = htons(CMD_ECHO);
	tlv->len = htons(sizeof(*tlv));

	n = sendto(sockfd, buf, sizeof(*tlv), MSG_NOSIGNAL | MSG_CONFIRM,
		   (struct sockaddr *)&env.dut_addr, sizeof(env.dut_addr));
	close(sockfd);

	return n == ntohs(tlv->len) ? 0 : -EINVAL;
}

static int tester_run(struct xdp_features *skel)
{
	int flags = XDP_FLAGS_UPDATE_IF_NOEXIST | XDP_FLAGS_DRV_MODE;
	unsigned long long advertised_feature;
	struct bpf_program *prog;
	unsigned int stats;
	int i, err, sockfd;
	bool detected_cap;

	sockfd = socket(AF_INET6, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fprintf(stderr,
			"Failed creating tester service control socket\n");
		return -errno;
	}

	if (settimeo(sockfd, 1000) < 0)
		return -EINVAL;

	err = connect(sockfd, (struct sockaddr *)&env.dut_ctrl_addr,
		      sizeof(env.dut_ctrl_addr));
	if (err) {
		fprintf(stderr,
			"Failed connecting to the Device Under Test control socket\n");
		return -errno;
	}

	err = send_and_recv_msg(sockfd, CMD_GET_XDP_CAP, &advertised_feature,
				sizeof(advertised_feature));
	if (err < 0) {
		close(sockfd);
		return err;
	}

	advertised_feature = be64toh(advertised_feature);

	if (env.feature.drv_feature == NETDEV_XDP_ACT_NDO_XMIT ||
	    env.feature.action == XDP_TX)
		prog = skel->progs.xdp_tester_check_tx;
	else
		prog = skel->progs.xdp_tester_check_rx;

	err = bpf_xdp_attach(env.ifindex, bpf_program__fd(prog), flags, NULL);
	if (err) {
		fprintf(stderr, "Failed attaching XDP program to device %s\n",
			env.ifname);
		goto out;
	}

	err = send_and_recv_msg(sockfd, CMD_START, NULL, 0);
	if (err)
		goto out;

	for (i = 0; i < 10 && !exiting; i++) {
		err = send_echo_msg();
		if (err < 0)
			goto out;

		sleep(1);
	}

	err = send_and_recv_msg(sockfd, CMD_GET_STATS, &stats, sizeof(stats));
	if (err)
		goto out;

	/* stop the test */
	err = send_and_recv_msg(sockfd, CMD_STOP, NULL, 0);
	/* send a new echo message to wake echo thread of the dut */
	send_echo_msg();

	detected_cap = tester_collect_detected_cap(skel, ntohl(stats));

	fprintf(stdout, "Feature %s: [%s][%s]\n", get_xdp_feature_str(),
		detected_cap ? GREEN("DETECTED") : RED("NOT DETECTED"),
		env.feature.drv_feature & advertised_feature ? GREEN("ADVERTISED")
							     : RED("NOT ADVERTISED"));
out:
	bpf_xdp_detach(env.ifindex, flags, NULL);
	close(sockfd);
	return err < 0 ? err : 0;
}

int main(int argc, char **argv)
{
	struct xdp_features *skel;
	int err;

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
	libbpf_set_print(libbpf_print_fn);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	set_env_default();

	/* Parse command line arguments */
	err = argp_parse(&argp, argc, argv, 0, NULL, NULL);
	if (err)
		return err;

	if (env.ifindex < 0) {
		fprintf(stderr, "Invalid device name %s\n", env.ifname);
		return -ENODEV;
	}

	/* Load and verify BPF application */
	skel = xdp_features__open();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return -EINVAL;
	}

	skel->rodata->tester_addr =
		((struct sockaddr_in6 *)&env.tester_addr)->sin6_addr;
	skel->rodata->dut_addr =
		((struct sockaddr_in6 *)&env.dut_addr)->sin6_addr;

	/* Load & verify BPF programs */
	err = xdp_features__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}

	err = xdp_features__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF skeleton\n");
		goto cleanup;
	}

	if (env.is_tester) {
		/* Tester */
		fprintf(stdout, "Starting tester service on device %s\n",
			env.ifname);
		err = tester_run(skel);
	} else {
		/* DUT */
		fprintf(stdout, "Starting test on device %s\n", env.ifname);
		err = dut_run(skel);
	}

cleanup:
	xdp_features__destroy(skel);

	return err < 0 ? -err : 0;
}
