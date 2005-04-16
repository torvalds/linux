/* -*- linux-c -*-
 * sysctl_net_x25.c: sysctl interface to net X.25 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/x25 directory entry (empty =) ). [MS]
 */

#include <linux/sysctl.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <net/x25.h>

static int min_timer[] = {   1 * HZ };
static int max_timer[] = { 300 * HZ };

static struct ctl_table_header *x25_table_header;

static struct ctl_table x25_table[] = {
        {
		.ctl_name =	NET_X25_RESTART_REQUEST_TIMEOUT,
		.procname =	"restart_request_timeout",
		.data =		&sysctl_x25_restart_request_timeout,
		.maxlen =	sizeof(int),
		.mode =		0644,
		.proc_handler =	&proc_dointvec_minmax,
		.strategy =	&sysctl_intvec,
		.extra1 =	&min_timer,
		.extra2 =	&max_timer,
	},
        {
		.ctl_name =	NET_X25_CALL_REQUEST_TIMEOUT,
		.procname =	"call_request_timeout",
		.data =		&sysctl_x25_call_request_timeout,
		.maxlen =	sizeof(int),
		.mode =		0644,
		.proc_handler =	&proc_dointvec_minmax,
		.strategy =	&sysctl_intvec,
		.extra1 =	&min_timer,
		.extra2 =	&max_timer,
	},
        {
		.ctl_name =	NET_X25_RESET_REQUEST_TIMEOUT,
		.procname =	"reset_request_timeout",
		.data =		&sysctl_x25_reset_request_timeout,
		.maxlen =	sizeof(int),
		.mode =		0644,
		.proc_handler =	&proc_dointvec_minmax,
		.strategy =	&sysctl_intvec,
		.extra1 =	&min_timer,
		.extra2 =	&max_timer,
	},
        {
		.ctl_name =	NET_X25_CLEAR_REQUEST_TIMEOUT,
		.procname =	"clear_request_timeout",
		.data =		&sysctl_x25_clear_request_timeout,
		.maxlen =	sizeof(int),
		.mode =		0644,
		.proc_handler =	&proc_dointvec_minmax,
		.strategy =	&sysctl_intvec,
		.extra1 =	&min_timer,
		.extra2 =	&max_timer,
	},
        {
		.ctl_name =	NET_X25_ACK_HOLD_BACK_TIMEOUT,
		.procname =	"acknowledgement_hold_back_timeout",
		.data =		&sysctl_x25_ack_holdback_timeout,
		.maxlen =	sizeof(int),
		.mode =		0644,
		.proc_handler =	&proc_dointvec_minmax,
		.strategy =	&sysctl_intvec,
		.extra1 =	&min_timer,
		.extra2 =	&max_timer,
	},
	{ 0, },
};

static struct ctl_table x25_dir_table[] = {
	{
		.ctl_name =	NET_X25,
		.procname =	"x25",
		.mode =		0555,
		.child =	x25_table,
	},
	{ 0, },
};

static struct ctl_table x25_root_table[] = {
	{
		.ctl_name =	CTL_NET,
		.procname =	"net",
		.mode =		0555,
		.child =	x25_dir_table,
	},
	{ 0, },
};

void __init x25_register_sysctl(void)
{
	x25_table_header = register_sysctl_table(x25_root_table, 1);
}

void x25_unregister_sysctl(void)
{
	unregister_sysctl_table(x25_table_header);
}
