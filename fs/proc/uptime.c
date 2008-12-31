#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <asm/cputime.h>

static int proc_calc_metrics(char *page, char **start, off_t off,
				 int count, int *eof, int len)
{
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int uptime_read_proc(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	struct timespec uptime;
	struct timespec idle;
	int len;
	cputime_t idletime = cputime_add(init_task.utime, init_task.stime);

	do_posix_clock_monotonic_gettime(&uptime);
	monotonic_to_bootbased(&uptime);
	cputime_to_timespec(idletime, &idle);
	len = sprintf(page, "%lu.%02lu %lu.%02lu\n",
			(unsigned long) uptime.tv_sec,
			(uptime.tv_nsec / (NSEC_PER_SEC / 100)),
			(unsigned long) idle.tv_sec,
			(idle.tv_nsec / (NSEC_PER_SEC / 100)));
	return proc_calc_metrics(page, start, off, count, eof, len);
}

static int __init proc_uptime_init(void)
{
	create_proc_read_entry("uptime", 0, NULL, uptime_read_proc, NULL);
	return 0;
}
module_init(proc_uptime_init);
