/*
 * SO2 - Networking Lab (#10)
 *
 * Bonus: simple kernel UDP socket
 *
 * Code skeleton.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>

MODULE_DESCRIPTION("Simple kernel UDP socket");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define LOG_LEVEL		KERN_ALERT
#define MY_UDP_LOCAL_PORT	60000
#define MY_UDP_REMOTE_PORT	60001
#define MY_TEST_MESSAGE		"kernelsocket\n"

#define ON			1
#define OFF			0
#define DEBUG			ON

#if DEBUG == ON
#define LOG(s)					\
	do {					\
		printk(KERN_DEBUG s "\n");	\
	} while (0)
#else
#define LOG(s)					\
	do {} while (0)
#endif

#define print_sock_address(addr)		\
	do {					\
		printk(LOG_LEVEL "connection established to "	\
				NIPQUAD_FMT ":%d\n", 		\
				NIPQUAD(addr.sin_addr.s_addr),	\
				ntohs(addr.sin_port));		\
	} while (0)

static struct socket *sock;	/* UDP server */

/* send datagram */
static int my_udp_msgsend(struct socket *s)
{
	/* address to send to */
	struct sockaddr_in raddr = {
		.sin_family	= AF_INET,
		.sin_port	= htons(MY_UDP_REMOTE_PORT),
		.sin_addr	= { htonl(INADDR_LOOPBACK) }
	};
	int raddrlen = sizeof(raddr);
	/* message */
	struct msghdr msg;
	struct iovec iov;
	char *buffer = MY_TEST_MESSAGE;
	int len = strlen(buffer) + 1;

	/* TODO 1/7: build message */
	iov.iov_base = buffer;
	iov.iov_len = len;
	msg.msg_flags = 0;
	msg.msg_name = &raddr;
	msg.msg_namelen = raddrlen;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	/* TODO 1/1: send the message down the socket and return the
	 * error code.
	 */
	return kernel_sendmsg(s, &msg, (struct kvec *) &iov, 1, len);

	return 0;
}

int __init my_udp_sock_init(void)
{
	int err;
	/* address to bind on */
	struct sockaddr_in addr = {
		.sin_family	= AF_INET,
		.sin_port	= htons(MY_UDP_LOCAL_PORT),
		.sin_addr	= { htonl(INADDR_LOOPBACK) }
	};
	int addrlen = sizeof(addr);

	/* TODO 1/5: create UDP socket */
	err = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (err < 0) {
		printk(LOG_LEVEL "can't create socket\n");
		goto out;
	}

	/* TODO 1/5: bind socket to loopback on port MY_UDP_LOCAL_PORT */
	err = sock->ops->bind(sock, (struct sockaddr *) &addr, addrlen);
	if (err < 0) {
		printk(LOG_LEVEL "can't bind socket\n");
		goto out_release;
	}

	/* send message */
	err = my_udp_msgsend(sock);
	if (err < 0) {
		printk(LOG_LEVEL "can't send message\n");
		goto out_release;
	}

	return 0;

out_release:
	sock_release(sock);
out:
	return err;
}

void __exit my_udp_sock_exit(void)
{
	sock_release(sock);
}

module_init(my_udp_sock_init);
module_exit(my_udp_sock_exit);
