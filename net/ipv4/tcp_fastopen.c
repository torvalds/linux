#include <linux/init.h>
#include <linux/kernel.h>

int sysctl_tcp_fastopen;

static int __init tcp_fastopen_init(void)
{
	return 0;
}

late_initcall(tcp_fastopen_init);
