/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet sysctl support functions
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *
 *
 * Changes:
 * Steve Whitehouse - C99 changes and default device handling
 * Steve Whitehouse - Memory buffer settings, like the tcp ones
 *
 */
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>

#include <asm/uaccess.h>

#include <net/dn.h>
#include <net/dn_dev.h>
#include <net/dn_route.h>


int decnet_debug_level;
int decnet_time_wait = 30;
int decnet_dn_count = 1;
int decnet_di_count = 3;
int decnet_dr_count = 3;
int decnet_log_martians = 1;
int decnet_no_fc_max_cwnd = NSP_MIN_WINDOW;

/* Reasonable defaults, I hope, based on tcp's defaults */
int sysctl_decnet_mem[3] = { 768 << 3, 1024 << 3, 1536 << 3 };
int sysctl_decnet_wmem[3] = { 4 * 1024, 16 * 1024, 128 * 1024 };
int sysctl_decnet_rmem[3] = { 4 * 1024, 87380, 87380 * 2 };

#ifdef CONFIG_SYSCTL
extern int decnet_dst_gc_interval;
static int min_decnet_time_wait[] = { 5 };
static int max_decnet_time_wait[] = { 600 };
static int min_state_count[] = { 1 };
static int max_state_count[] = { NSP_MAXRXTSHIFT };
static int min_decnet_dst_gc_interval[] = { 1 };
static int max_decnet_dst_gc_interval[] = { 60 };
static int min_decnet_no_fc_max_cwnd[] = { NSP_MIN_WINDOW };
static int max_decnet_no_fc_max_cwnd[] = { NSP_MAX_WINDOW };
static char node_name[7] = "???";

static struct ctl_table_header *dn_table_header = NULL;

/*
 * ctype.h :-)
 */
#define ISNUM(x) (((x) >= '0') && ((x) <= '9'))
#define ISLOWER(x) (((x) >= 'a') && ((x) <= 'z'))
#define ISUPPER(x) (((x) >= 'A') && ((x) <= 'Z'))
#define ISALPHA(x) (ISLOWER(x) || ISUPPER(x))
#define INVALID_END_CHAR(x) (ISNUM(x) || ISALPHA(x))

static void strip_it(char *str)
{
	for(;;) {
		switch(*str) {
			case ' ':
			case '\n':
			case '\r':
			case ':':
				*str = 0;
			case 0:
				return;
		}
		str++;
	}
}

/*
 * Simple routine to parse an ascii DECnet address
 * into a network order address.
 */
static int parse_addr(__le16 *addr, char *str)
{
	__u16 area, node;

	while(*str && !ISNUM(*str)) str++;

	if (*str == 0)
		return -1;

	area = (*str++ - '0');
	if (ISNUM(*str)) {
		area *= 10;
		area += (*str++ - '0');
	}

	if (*str++ != '.')
		return -1;

	if (!ISNUM(*str))
		return -1;

	node = *str++ - '0';
	if (ISNUM(*str)) {
		node *= 10;
		node += (*str++ - '0');
	}
	if (ISNUM(*str)) {
		node *= 10;
		node += (*str++ - '0');
	}
	if (ISNUM(*str)) {
		node *= 10;
		node += (*str++ - '0');
	}

	if ((node > 1023) || (area > 63))
		return -1;

	if (INVALID_END_CHAR(*str))
		return -1;

	*addr = cpu_to_le16((area << 10) | node);

	return 0;
}


static int dn_node_address_strategy(ctl_table *table,
				void __user *oldval, size_t __user *oldlenp,
				void __user *newval, size_t newlen)
{
	size_t len;
	__le16 addr;

	if (oldval && oldlenp) {
		if (get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			if (len != sizeof(unsigned short))
				return -EINVAL;
			if (put_user(decnet_address, (__le16 __user *)oldval))
				return -EFAULT;
		}
	}
	if (newval && newlen) {
		if (newlen != sizeof(unsigned short))
			return -EINVAL;
		if (get_user(addr, (__le16 __user *)newval))
			return -EFAULT;

		dn_dev_devices_off();

		decnet_address = addr;

		dn_dev_devices_on();
	}
	return 0;
}

static int dn_node_address_handler(ctl_table *table, int write,
				struct file *filp,
				void __user *buffer,
				size_t *lenp, loff_t *ppos)
{
	char addr[DN_ASCBUF_LEN];
	size_t len;
	__le16 dnaddr;

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		int len = (*lenp < DN_ASCBUF_LEN) ? *lenp : (DN_ASCBUF_LEN-1);

		if (copy_from_user(addr, buffer, len))
			return -EFAULT;

		addr[len] = 0;
		strip_it(addr);

		if (parse_addr(&dnaddr, addr))
			return -EINVAL;

		dn_dev_devices_off();

		decnet_address = dnaddr;

		dn_dev_devices_on();

		*ppos += len;

		return 0;
	}

	dn_addr2asc(le16_to_cpu(decnet_address), addr);
	len = strlen(addr);
	addr[len++] = '\n';

	if (len > *lenp) len = *lenp;

	if (copy_to_user(buffer, addr, len))
		return -EFAULT;

	*lenp = len;
	*ppos += len;

	return 0;
}


static int dn_def_dev_strategy(ctl_table *table,
				void __user *oldval, size_t __user *oldlenp,
				void __user *newval, size_t newlen)
{
	size_t len;
	struct net_device *dev;
	char devname[17];
	size_t namel;
	int rv = 0;

	devname[0] = 0;

	if (oldval && oldlenp) {
		if (get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			dev = dn_dev_get_default();
			if (dev) {
				strcpy(devname, dev->name);
				dev_put(dev);
			}

			namel = strlen(devname) + 1;
			if (len > namel) len = namel;

			if (copy_to_user(oldval, devname, len))
				return -EFAULT;

			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}

	if (newval && newlen) {
		if (newlen > 16)
			return -E2BIG;

		if (copy_from_user(devname, newval, newlen))
			return -EFAULT;

		devname[newlen] = 0;

		dev = dev_get_by_name(&init_net, devname);
		if (dev == NULL)
			return -ENODEV;

		rv = -ENODEV;
		if (dev->dn_ptr != NULL) {
			rv = dn_dev_set_default(dev, 1);
			if (rv)
				dev_put(dev);
		}
	}

	return rv;
}


static int dn_def_dev_handler(ctl_table *table, int write,
				struct file * filp,
				void __user *buffer,
				size_t *lenp, loff_t *ppos)
{
	size_t len;
	struct net_device *dev;
	char devname[17];

	if (!*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		if (*lenp > 16)
			return -E2BIG;

		if (copy_from_user(devname, buffer, *lenp))
			return -EFAULT;

		devname[*lenp] = 0;
		strip_it(devname);

		dev = dev_get_by_name(&init_net, devname);
		if (dev == NULL)
			return -ENODEV;

		if (dev->dn_ptr == NULL) {
			dev_put(dev);
			return -ENODEV;
		}

		if (dn_dev_set_default(dev, 1)) {
			dev_put(dev);
			return -ENODEV;
		}
		*ppos += *lenp;

		return 0;
	}

	dev = dn_dev_get_default();
	if (dev == NULL) {
		*lenp = 0;
		return 0;
	}

	strcpy(devname, dev->name);
	dev_put(dev);
	len = strlen(devname);
	devname[len++] = '\n';

	if (len > *lenp) len = *lenp;

	if (copy_to_user(buffer, devname, len))
		return -EFAULT;

	*lenp = len;
	*ppos += len;

	return 0;
}

static ctl_table dn_table[] = {
	{
		.ctl_name = NET_DECNET_NODE_ADDRESS,
		.procname = "node_address",
		.maxlen = 7,
		.mode = 0644,
		.proc_handler = dn_node_address_handler,
		.strategy = dn_node_address_strategy,
	},
	{
		.ctl_name = NET_DECNET_NODE_NAME,
		.procname = "node_name",
		.data = node_name,
		.maxlen = 7,
		.mode = 0644,
		.proc_handler = proc_dostring,
		.strategy = sysctl_string,
	},
	{
		.ctl_name = NET_DECNET_DEFAULT_DEVICE,
		.procname = "default_device",
		.maxlen = 16,
		.mode = 0644,
		.proc_handler = dn_def_dev_handler,
		.strategy = dn_def_dev_strategy,
	},
	{
		.ctl_name = NET_DECNET_TIME_WAIT,
		.procname = "time_wait",
		.data = &decnet_time_wait,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_decnet_time_wait,
		.extra2 = &max_decnet_time_wait
	},
	{
		.ctl_name = NET_DECNET_DN_COUNT,
		.procname = "dn_count",
		.data = &decnet_dn_count,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_state_count,
		.extra2 = &max_state_count
	},
	{
		.ctl_name = NET_DECNET_DI_COUNT,
		.procname = "di_count",
		.data = &decnet_di_count,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_state_count,
		.extra2 = &max_state_count
	},
	{
		.ctl_name = NET_DECNET_DR_COUNT,
		.procname = "dr_count",
		.data = &decnet_dr_count,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_state_count,
		.extra2 = &max_state_count
	},
	{
		.ctl_name = NET_DECNET_DST_GC_INTERVAL,
		.procname = "dst_gc_interval",
		.data = &decnet_dst_gc_interval,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_decnet_dst_gc_interval,
		.extra2 = &max_decnet_dst_gc_interval
	},
	{
		.ctl_name = NET_DECNET_NO_FC_MAX_CWND,
		.procname = "no_fc_max_cwnd",
		.data = &decnet_no_fc_max_cwnd,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.strategy = sysctl_intvec,
		.extra1 = &min_decnet_no_fc_max_cwnd,
		.extra2 = &max_decnet_no_fc_max_cwnd
	},
       {
		.ctl_name = NET_DECNET_MEM,
		.procname = "decnet_mem",
		.data = &sysctl_decnet_mem,
		.maxlen = sizeof(sysctl_decnet_mem),
		.mode = 0644,
		.proc_handler = proc_dointvec,
		.strategy = sysctl_intvec,
	},
	{
		.ctl_name = NET_DECNET_RMEM,
		.procname = "decnet_rmem",
		.data = &sysctl_decnet_rmem,
		.maxlen = sizeof(sysctl_decnet_rmem),
		.mode = 0644,
		.proc_handler = proc_dointvec,
		.strategy = sysctl_intvec,
	},
	{
		.ctl_name = NET_DECNET_WMEM,
		.procname = "decnet_wmem",
		.data = &sysctl_decnet_wmem,
		.maxlen = sizeof(sysctl_decnet_wmem),
		.mode = 0644,
		.proc_handler = proc_dointvec,
		.strategy = sysctl_intvec,
	},
	{
		.ctl_name = NET_DECNET_DEBUG_LEVEL,
		.procname = "debug",
		.data = &decnet_debug_level,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec,
		.strategy = sysctl_intvec,
	},
	{0}
};

static struct ctl_path dn_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "decnet", .ctl_name = NET_DECNET, },
	{ }
};

void dn_register_sysctl(void)
{
	dn_table_header = register_sysctl_paths(dn_path, dn_table);
}

void dn_unregister_sysctl(void)
{
	unregister_sysctl_table(dn_table_header);
}

#else  /* CONFIG_SYSCTL */
void dn_unregister_sysctl(void)
{
}
void dn_register_sysctl(void)
{
}

#endif
