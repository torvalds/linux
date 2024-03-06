/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stddef.h>

static const char *const initial_sid_to_string[] = {
	NULL, /* zero placeholder, not used */
	"kernel", /* kernel / SECINITSID_KERNEL */
	"security", /* security / SECINITSID_SECURITY */
	"unlabeled", /* unlabeled / SECINITSID_UNLABELED */
	NULL, /* fs */
	"file", /* file / SECINITSID_FILE */
	NULL, /* file_labels */
	"init", /* init / SECINITSID_INIT */
	"any_socket", /* any_socket / SECINITSID_ANY_SOCKET */
	"port", /* port / SECINITSID_PORT */
	"netif", /* netif / SECINITSID_NETIF */
	"netmsg", /* netmsg / SECINITSID_NETMSG */
	"node", /* node / SECINITSID_NODE */
	NULL, /* igmp_packet */
	NULL, /* icmp_socket */
	NULL, /* tcp_socket */
	NULL, /* sysctl_modprobe */
	NULL, /* sysctl */
	NULL, /* sysctl_fs */
	NULL, /* sysctl_kernel */
	NULL, /* sysctl_net */
	NULL, /* sysctl_net_unix */
	NULL, /* sysctl_vm */
	NULL, /* sysctl_dev */
	NULL, /* kmod */
	NULL, /* policy */
	NULL, /* scmp_packet */
	"devnull", /* devnull / SECINITSID_DEVNULL */
};
