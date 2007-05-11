/*
 * proc_tty.c -- handles /proc/tty
 *
 * Copyright 1997, Theodore Ts'o
 */

#include <asm/uaccess.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>

static int tty_ldiscs_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data);

/*
 * The /proc/tty directory inodes...
 */
static struct proc_dir_entry *proc_tty_ldisc, *proc_tty_driver;

/*
 * This is the handler for /proc/tty/drivers
 */
static void show_tty_range(struct seq_file *m, struct tty_driver *p,
	dev_t from, int num)
{
	seq_printf(m, "%-20s ", p->driver_name ? p->driver_name : "unknown");
	seq_printf(m, "/dev/%-8s ", p->name);
	if (p->num > 1) {
		seq_printf(m, "%3d %d-%d ", MAJOR(from), MINOR(from),
			MINOR(from) + num - 1);
	} else {
		seq_printf(m, "%3d %7d ", MAJOR(from), MINOR(from));
	}
	switch (p->type) {
	case TTY_DRIVER_TYPE_SYSTEM:
		seq_printf(m, "system");
		if (p->subtype == SYSTEM_TYPE_TTY)
			seq_printf(m, ":/dev/tty");
		else if (p->subtype == SYSTEM_TYPE_SYSCONS)
			seq_printf(m, ":console");
		else if (p->subtype == SYSTEM_TYPE_CONSOLE)
			seq_printf(m, ":vtmaster");
		break;
	case TTY_DRIVER_TYPE_CONSOLE:
		seq_printf(m, "console");
		break;
	case TTY_DRIVER_TYPE_SERIAL:
		seq_printf(m, "serial");
		break;
	case TTY_DRIVER_TYPE_PTY:
		if (p->subtype == PTY_TYPE_MASTER)
			seq_printf(m, "pty:master");
		else if (p->subtype == PTY_TYPE_SLAVE)
			seq_printf(m, "pty:slave");
		else
			seq_printf(m, "pty");
		break;
	default:
		seq_printf(m, "type:%d.%d", p->type, p->subtype);
	}
	seq_putc(m, '\n');
}

static int show_tty_driver(struct seq_file *m, void *v)
{
	struct tty_driver *p = v;
	dev_t from = MKDEV(p->major, p->minor_start);
	dev_t to = from + p->num;

	if (&p->tty_drivers == tty_drivers.next) {
		/* pseudo-drivers first */
		seq_printf(m, "%-20s /dev/%-8s ", "/dev/tty", "tty");
		seq_printf(m, "%3d %7d ", TTYAUX_MAJOR, 0);
		seq_printf(m, "system:/dev/tty\n");
		seq_printf(m, "%-20s /dev/%-8s ", "/dev/console", "console");
		seq_printf(m, "%3d %7d ", TTYAUX_MAJOR, 1);
		seq_printf(m, "system:console\n");
#ifdef CONFIG_UNIX98_PTYS
		seq_printf(m, "%-20s /dev/%-8s ", "/dev/ptmx", "ptmx");
		seq_printf(m, "%3d %7d ", TTYAUX_MAJOR, 2);
		seq_printf(m, "system\n");
#endif
#ifdef CONFIG_VT
		seq_printf(m, "%-20s /dev/%-8s ", "/dev/vc/0", "vc/0");
		seq_printf(m, "%3d %7d ", TTY_MAJOR, 0);
		seq_printf(m, "system:vtmaster\n");
#endif
	}

	while (MAJOR(from) < MAJOR(to)) {
		dev_t next = MKDEV(MAJOR(from)+1, 0);
		show_tty_range(m, p, from, next - from);
		from = next;
	}
	if (from != to)
		show_tty_range(m, p, from, to - from);
	return 0;
}

/* iterator */
static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *p;
	loff_t l = *pos;

	mutex_lock(&tty_mutex);
	list_for_each(p, &tty_drivers)
		if (!l--)
			return list_entry(p, struct tty_driver, tty_drivers);
	return NULL;
}

static void *t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct list_head *p = ((struct tty_driver *)v)->tty_drivers.next;
	(*pos)++;
	return p==&tty_drivers ? NULL :
			list_entry(p, struct tty_driver, tty_drivers);
}

static void t_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&tty_mutex);
}

static struct seq_operations tty_drivers_op = {
	.start	= t_start,
	.next	= t_next,
	.stop	= t_stop,
	.show	= show_tty_driver
};

static int tty_drivers_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &tty_drivers_op);
}

static const struct file_operations proc_tty_drivers_operations = {
	.open		= tty_drivers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * This is the handler for /proc/tty/ldiscs
 */
static int tty_ldiscs_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int	i;
	int	len = 0;
	off_t	begin = 0;
	struct tty_ldisc *ld;
	
	for (i=0; i < NR_LDISCS; i++) {
		ld = tty_ldisc_get(i);
		if (ld == NULL)
			continue;
		len += sprintf(page+len, "%-10s %2d\n",
			       ld->name ? ld->name : "???", i);
		tty_ldisc_put(i);
		if (len+begin > off+count)
			break;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	if (i >= NR_LDISCS)
		*eof = 1;
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/*
 * This function is called by tty_register_driver() to handle
 * registering the driver's /proc handler into /proc/tty/driver/<foo>
 */
void proc_tty_register_driver(struct tty_driver *driver)
{
	struct proc_dir_entry *ent;
		
	if ((!driver->read_proc && !driver->write_proc) ||
	    !driver->driver_name ||
	    driver->proc_entry)
		return;

	ent = create_proc_entry(driver->driver_name, 0, proc_tty_driver);
	if (!ent)
		return;
	ent->read_proc = driver->read_proc;
	ent->write_proc = driver->write_proc;
	ent->owner = driver->owner;
	ent->data = driver;

	driver->proc_entry = ent;
}

/*
 * This function is called by tty_unregister_driver()
 */
void proc_tty_unregister_driver(struct tty_driver *driver)
{
	struct proc_dir_entry *ent;

	ent = driver->proc_entry;
	if (!ent)
		return;
		
	remove_proc_entry(driver->driver_name, proc_tty_driver);
	
	driver->proc_entry = NULL;
}

/*
 * Called by proc_root_init() to initialize the /proc/tty subtree
 */
void __init proc_tty_init(void)
{
	struct proc_dir_entry *entry;
	if (!proc_mkdir("tty", NULL))
		return;
	proc_tty_ldisc = proc_mkdir("tty/ldisc", NULL);
	/*
	 * /proc/tty/driver/serial reveals the exact character counts for
	 * serial links which is just too easy to abuse for inferring
	 * password lengths and inter-keystroke timings during password
	 * entry.
	 */
	proc_tty_driver = proc_mkdir_mode("tty/driver", S_IRUSR | S_IXUSR, NULL);

	create_proc_read_entry("tty/ldiscs", 0, NULL, tty_ldiscs_read_proc, NULL);
	entry = create_proc_entry("tty/drivers", 0, NULL);
	if (entry)
		entry->proc_fops = &proc_tty_drivers_operations;
}
