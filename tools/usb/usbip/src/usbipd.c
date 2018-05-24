/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_LIBWRAP
#include <tcpd.h>
#endif

#include <getopt.h>
#include <signal.h>
#include <poll.h>

#include "usbip_host_driver.h"
#include "usbip_host_common.h"
#include "usbip_device_driver.h"
#include "usbip_common.h"
#include "usbip_network.h"
#include "list.h"

#undef  PROGNAME
#define PROGNAME "usbipd"
#define MAXSOCKFD 20

#define MAIN_LOOP_TIMEOUT 10

#define DEFAULT_PID_FILE "/var/run/" PROGNAME ".pid"

static const char usbip_version_string[] = PACKAGE_STRING;

static const char usbipd_help_string[] =
	"usage: usbipd [options]\n"
	"\n"
	"	-4, --ipv4\n"
	"		Bind to IPv4. Default is both.\n"
	"\n"
	"	-6, --ipv6\n"
	"		Bind to IPv6. Default is both.\n"
	"\n"
	"	-e, --device\n"
	"		Run in device mode.\n"
	"		Rather than drive an attached device, create\n"
	"		a virtual UDC to bind gadgets to.\n"
	"\n"
	"	-D, --daemon\n"
	"		Run as a daemon process.\n"
	"\n"
	"	-d, --debug\n"
	"		Print debugging information.\n"
	"\n"
	"	-PFILE, --pid FILE\n"
	"		Write process id to FILE.\n"
	"		If no FILE specified, use " DEFAULT_PID_FILE "\n"
	"\n"
	"	-tPORT, --tcp-port PORT\n"
	"		Listen on TCP/IP port PORT.\n"
	"\n"
	"	-h, --help\n"
	"		Print this help.\n"
	"\n"
	"	-v, --version\n"
	"		Show version.\n";

static struct usbip_host_driver *driver;

static void usbipd_help(void)
{
	printf("%s\n", usbipd_help_string);
}

static int recv_request_import(int sockfd)
{
	struct op_import_request req;
	struct usbip_exported_device *edev;
	struct usbip_usb_device pdu_udev;
	struct list_head *i;
	int found = 0;
	int status = ST_OK;
	int rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(sockfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: import request");
		return -1;
	}
	PACK_OP_IMPORT_REQUEST(0, &req);

	list_for_each(i, &driver->edev_list) {
		edev = list_entry(i, struct usbip_exported_device, node);
		if (!strncmp(req.busid, edev->udev.busid, SYSFS_BUS_ID_SIZE)) {
			info("found requested device: %s", req.busid);
			found = 1;
			break;
		}
	}

	if (found) {
		/* should set TCP_NODELAY for usbip */
		usbip_net_set_nodelay(sockfd);

		/* export device needs a TCP/IP socket descriptor */
		status = usbip_export_device(edev, sockfd);
		if (status < 0)
			status = ST_NA;
	} else {
		info("requested device not found: %s", req.busid);
		status = ST_NODEV;
	}

	rc = usbip_net_send_op_common(sockfd, OP_REP_IMPORT, status);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_IMPORT);
		return -1;
	}

	if (status) {
		dbg("import request busid %s: failed", req.busid);
		return -1;
	}

	memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
	usbip_net_pack_usb_device(1, &pdu_udev);

	rc = usbip_net_send(sockfd, &pdu_udev, sizeof(pdu_udev));
	if (rc < 0) {
		dbg("usbip_net_send failed: devinfo");
		return -1;
	}

	dbg("import request busid %s: complete", req.busid);

	return 0;
}

static int send_reply_devlist(int connfd)
{
	struct usbip_exported_device *edev;
	struct usbip_usb_device pdu_udev;
	struct usbip_usb_interface pdu_uinf;
	struct op_devlist_reply reply;
	struct list_head *j;
	int rc, i;

	/*
	 * Exclude devices that are already exported to a client from
	 * the exportable device list to avoid:
	 *	- import requests for devices that are exported only to
	 *	  fail the request.
	 *	- revealing devices that are imported by a client to
	 *	  another client.
	 */

	reply.ndev = 0;
	/* number of exported devices */
	list_for_each(j, &driver->edev_list) {
		edev = list_entry(j, struct usbip_exported_device, node);
		if (edev->status != SDEV_ST_USED)
			reply.ndev += 1;
	}
	info("exportable devices: %d", reply.ndev);

	rc = usbip_net_send_op_common(connfd, OP_REP_DEVLIST, ST_OK);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_DEVLIST);
		return -1;
	}
	PACK_OP_DEVLIST_REPLY(1, &reply);

	rc = usbip_net_send(connfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("usbip_net_send failed: %#0x", OP_REP_DEVLIST);
		return -1;
	}

	list_for_each(j, &driver->edev_list) {
		edev = list_entry(j, struct usbip_exported_device, node);
		if (edev->status == SDEV_ST_USED)
			continue;

		dump_usb_device(&edev->udev);
		memcpy(&pdu_udev, &edev->udev, sizeof(pdu_udev));
		usbip_net_pack_usb_device(1, &pdu_udev);

		rc = usbip_net_send(connfd, &pdu_udev, sizeof(pdu_udev));
		if (rc < 0) {
			dbg("usbip_net_send failed: pdu_udev");
			return -1;
		}

		for (i = 0; i < edev->udev.bNumInterfaces; i++) {
			dump_usb_interface(&edev->uinf[i]);
			memcpy(&pdu_uinf, &edev->uinf[i], sizeof(pdu_uinf));
			usbip_net_pack_usb_interface(1, &pdu_uinf);

			rc = usbip_net_send(connfd, &pdu_uinf,
					sizeof(pdu_uinf));
			if (rc < 0) {
				err("usbip_net_send failed: pdu_uinf");
				return -1;
			}
		}
	}

	return 0;
}

static int recv_request_devlist(int connfd)
{
	struct op_devlist_request req;
	int rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(connfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: devlist request");
		return -1;
	}

	rc = send_reply_devlist(connfd);
	if (rc < 0) {
		dbg("send_reply_devlist failed");
		return -1;
	}

	return 0;
}

static int recv_pdu(int connfd)
{
	uint16_t code = OP_UNSPEC;
	int ret;
	int status;

	ret = usbip_net_recv_op_common(connfd, &code, &status);
	if (ret < 0) {
		dbg("could not receive opcode: %#0x", code);
		return -1;
	}

	ret = usbip_refresh_device_list(driver);
	if (ret < 0) {
		dbg("could not refresh device list: %d", ret);
		return -1;
	}

	info("received request: %#0x(%d)", code, connfd);
	switch (code) {
	case OP_REQ_DEVLIST:
		ret = recv_request_devlist(connfd);
		break;
	case OP_REQ_IMPORT:
		ret = recv_request_import(connfd);
		break;
	case OP_REQ_DEVINFO:
	case OP_REQ_CRYPKEY:
	default:
		err("received an unknown opcode: %#0x", code);
		ret = -1;
	}

	if (ret == 0)
		info("request %#0x(%d): complete", code, connfd);
	else
		info("request %#0x(%d): failed", code, connfd);

	return ret;
}

#ifdef HAVE_LIBWRAP
static int tcpd_auth(int connfd)
{
	struct request_info request;
	int rc;

	request_init(&request, RQ_DAEMON, PROGNAME, RQ_FILE, connfd, 0);
	fromhost(&request);
	rc = hosts_access(&request);
	if (rc == 0)
		return -1;

	return 0;
}
#endif

static int do_accept(int listenfd)
{
	int connfd;
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int rc;

	memset(&ss, 0, sizeof(ss));

	connfd = accept(listenfd, (struct sockaddr *)&ss, &len);
	if (connfd < 0) {
		err("failed to accept connection");
		return -1;
	}

	rc = getnameinfo((struct sockaddr *)&ss, len, host, sizeof(host),
			 port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
	if (rc)
		err("getnameinfo: %s", gai_strerror(rc));

#ifdef HAVE_LIBWRAP
	rc = tcpd_auth(connfd);
	if (rc < 0) {
		info("denied access from %s", host);
		close(connfd);
		return -1;
	}
#endif
	info("connection from %s:%s", host, port);

	return connfd;
}

int process_request(int listenfd)
{
	pid_t childpid;
	int connfd;

	connfd = do_accept(listenfd);
	if (connfd < 0)
		return -1;
	childpid = fork();
	if (childpid == 0) {
		close(listenfd);
		recv_pdu(connfd);
		exit(0);
	}
	close(connfd);
	return 0;
}

static void addrinfo_to_text(struct addrinfo *ai, char buf[],
			     const size_t buf_size)
{
	char hbuf[NI_MAXHOST];
	char sbuf[NI_MAXSERV];
	int rc;

	buf[0] = '\0';

	rc = getnameinfo(ai->ai_addr, ai->ai_addrlen, hbuf, sizeof(hbuf),
			 sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	if (rc)
		err("getnameinfo: %s", gai_strerror(rc));

	snprintf(buf, buf_size, "%s:%s", hbuf, sbuf);
}

static int listen_all_addrinfo(struct addrinfo *ai_head, int sockfdlist[],
			     int maxsockfd)
{
	struct addrinfo *ai;
	int ret, nsockfd = 0;
	const size_t ai_buf_size = NI_MAXHOST + NI_MAXSERV + 2;
	char ai_buf[ai_buf_size];

	for (ai = ai_head; ai && nsockfd < maxsockfd; ai = ai->ai_next) {
		int sock;

		addrinfo_to_text(ai, ai_buf, ai_buf_size);
		dbg("opening %s", ai_buf);
		sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0) {
			err("socket: %s: %d (%s)",
			    ai_buf, errno, strerror(errno));
			continue;
		}

		usbip_net_set_reuseaddr(sock);
		usbip_net_set_nodelay(sock);
		/* We use seperate sockets for IPv4 and IPv6
		 * (see do_standalone_mode()) */
		usbip_net_set_v6only(sock);

		ret = bind(sock, ai->ai_addr, ai->ai_addrlen);
		if (ret < 0) {
			err("bind: %s: %d (%s)",
			    ai_buf, errno, strerror(errno));
			close(sock);
			continue;
		}

		ret = listen(sock, SOMAXCONN);
		if (ret < 0) {
			err("listen: %s: %d (%s)",
			    ai_buf, errno, strerror(errno));
			close(sock);
			continue;
		}

		info("listening on %s", ai_buf);
		sockfdlist[nsockfd++] = sock;
	}

	return nsockfd;
}

static struct addrinfo *do_getaddrinfo(char *host, int ai_family)
{
	struct addrinfo hints, *ai_head;
	int rc;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = ai_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;

	rc = getaddrinfo(host, usbip_port_string, &hints, &ai_head);
	if (rc) {
		err("failed to get a network address %s: %s", usbip_port_string,
		    gai_strerror(rc));
		return NULL;
	}

	return ai_head;
}

static void signal_handler(int i)
{
	dbg("received '%s' signal", strsignal(i));
}

static void set_signal(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	act.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &act, NULL);
}

static const char *pid_file;

static void write_pid_file(void)
{
	if (pid_file) {
		dbg("creating pid file %s", pid_file);
		FILE *fp;

		fp = fopen(pid_file, "w");
		if (!fp) {
			err("pid_file: %s: %d (%s)",
			    pid_file, errno, strerror(errno));
			return;
		}
		fprintf(fp, "%d\n", getpid());
		fclose(fp);
	}
}

static void remove_pid_file(void)
{
	if (pid_file) {
		dbg("removing pid file %s", pid_file);
		unlink(pid_file);
	}
}

static int do_standalone_mode(int daemonize, int ipv4, int ipv6)
{
	struct addrinfo *ai_head;
	int sockfdlist[MAXSOCKFD];
	int nsockfd, family;
	int i, terminate;
	struct pollfd *fds;
	struct timespec timeout;
	sigset_t sigmask;

	if (usbip_driver_open(driver))
		return -1;

	if (daemonize) {
		if (daemon(0, 0) < 0) {
			err("daemonizing failed: %s", strerror(errno));
			usbip_driver_close(driver);
			return -1;
		}
		umask(0);
		usbip_use_syslog = 1;
	}
	set_signal();
	write_pid_file();

	info("starting " PROGNAME " (%s)", usbip_version_string);

	/*
	 * To suppress warnings on systems with bindv6only disabled
	 * (default), we use seperate sockets for IPv6 and IPv4 and set
	 * IPV6_V6ONLY on the IPv6 sockets.
	 */
	if (ipv4 && ipv6)
		family = AF_UNSPEC;
	else if (ipv4)
		family = AF_INET;
	else
		family = AF_INET6;

	ai_head = do_getaddrinfo(NULL, family);
	if (!ai_head) {
		usbip_driver_close(driver);
		return -1;
	}
	nsockfd = listen_all_addrinfo(ai_head, sockfdlist,
		sizeof(sockfdlist) / sizeof(*sockfdlist));
	freeaddrinfo(ai_head);
	if (nsockfd <= 0) {
		err("failed to open a listening socket");
		usbip_driver_close(driver);
		return -1;
	}

	dbg("listening on %d address%s", nsockfd, (nsockfd == 1) ? "" : "es");

	fds = calloc(nsockfd, sizeof(struct pollfd));
	for (i = 0; i < nsockfd; i++) {
		fds[i].fd = sockfdlist[i];
		fds[i].events = POLLIN;
	}
	timeout.tv_sec = MAIN_LOOP_TIMEOUT;
	timeout.tv_nsec = 0;

	sigfillset(&sigmask);
	sigdelset(&sigmask, SIGTERM);
	sigdelset(&sigmask, SIGINT);

	terminate = 0;
	while (!terminate) {
		int r;

		r = ppoll(fds, nsockfd, &timeout, &sigmask);
		if (r < 0) {
			dbg("%s", strerror(errno));
			terminate = 1;
		} else if (r) {
			for (i = 0; i < nsockfd; i++) {
				if (fds[i].revents & POLLIN) {
					dbg("read event on fd[%d]=%d",
					    i, sockfdlist[i]);
					process_request(sockfdlist[i]);
				}
			}
		} else {
			dbg("heartbeat timeout on ppoll()");
		}
	}

	info("shutting down " PROGNAME);
	free(fds);
	usbip_driver_close(driver);

	return 0;
}

int main(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "ipv4",     no_argument,       NULL, '4' },
		{ "ipv6",     no_argument,       NULL, '6' },
		{ "daemon",   no_argument,       NULL, 'D' },
		{ "daemon",   no_argument,       NULL, 'D' },
		{ "debug",    no_argument,       NULL, 'd' },
		{ "device",   no_argument,       NULL, 'e' },
		{ "pid",      optional_argument, NULL, 'P' },
		{ "tcp-port", required_argument, NULL, 't' },
		{ "help",     no_argument,       NULL, 'h' },
		{ "version",  no_argument,       NULL, 'v' },
		{ NULL,	      0,                 NULL,  0  }
	};

	enum {
		cmd_standalone_mode = 1,
		cmd_help,
		cmd_version
	} cmd;

	int daemonize = 0;
	int ipv4 = 0, ipv6 = 0;
	int opt, rc = -1;

	pid_file = NULL;

	usbip_use_stderr = 1;
	usbip_use_syslog = 0;

	if (geteuid() != 0)
		err("not running as root?");

	cmd = cmd_standalone_mode;
	driver = &host_driver;
	for (;;) {
		opt = getopt_long(argc, argv, "46DdeP::t:hv", longopts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case '4':
			ipv4 = 1;
			break;
		case '6':
			ipv6 = 1;
			break;
		case 'D':
			daemonize = 1;
			break;
		case 'd':
			usbip_use_debug = 1;
			break;
		case 'h':
			cmd = cmd_help;
			break;
		case 'P':
			pid_file = optarg ? optarg : DEFAULT_PID_FILE;
			break;
		case 't':
			usbip_setup_port_number(optarg);
			break;
		case 'v':
			cmd = cmd_version;
			break;
		case 'e':
			driver = &device_driver;
			break;
		case '?':
			usbipd_help();
		default:
			goto err_out;
		}
	}

	if (!ipv4 && !ipv6)
		ipv4 = ipv6 = 1;

	switch (cmd) {
	case cmd_standalone_mode:
		rc = do_standalone_mode(daemonize, ipv4, ipv6);
		remove_pid_file();
		break;
	case cmd_version:
		printf(PROGNAME " (%s)\n", usbip_version_string);
		rc = 0;
		break;
	case cmd_help:
		usbipd_help();
		rc = 0;
		break;
	default:
		usbipd_help();
		goto err_out;
	}

err_out:
	return (rc > -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
