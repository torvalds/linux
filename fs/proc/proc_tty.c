/*
 * proc_tty.c -- handles /proc/tty
 *
 * Copyright 1997, Theodore Ts'o
 */

#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/fdtable.h>
#include <linux/bitops.h>

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
	struct tty_driver *p = list_entry(v, struct tty_driver, tty_drivers);
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
	mutex_lock(&tty_mutex);
	return seq_list_start(&tty_drivers, *pos);
}

static void *t_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &tty_drivers, pos);
}

static void t_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&tty_mutex);
}

static const struct seq_operations tty_drivers_op = {
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
 * The device ID of file descriptor 0 of the current reading
 * task if a character device...
 */
static dev_t current_dev;

/*
 * This is the handler for /proc/tty/consoles
 */
static int show_console_dev(struct seq_file *m, void *v)
{
	const struct tty_driver *driver;
	struct console *con;
	int index, len;
	char flags[10];
	dev_t dev;

	if (v == SEQ_START_TOKEN)
		return 0;
	con = (struct console *)v;
	if (!con)
		return 0;
	driver = con->device(con, &index);
	if (!driver)
		return 0;
	dev = MKDEV(driver->major, driver->minor_start) + index;

	index = 0;
	if (con->flags & CON_ENABLED)
		flags[index++] = 'E';
	if (con->flags & CON_CONSDEV)
		flags[index++] = 'C';
	if (con->flags & CON_BOOT)
		flags[index++] = 'B';
	if (con->flags & CON_PRINTBUFFER)
		flags[index++] = 'p';
	if (con->flags & CON_BRL)
		flags[index++] = 'b';
	if (con->flags & CON_ANYTIME)
		flags[index++] = 'a';
	if (current_dev == dev)
		flags[index++] = '*';
	flags[index] = 0;

	seq_printf(m, "%s%d%n", con->name, con->index, &len);
	len = 21 - len;
	if (len < 1)
		len = 1;
	seq_printf(m, "%*c", len, ' ');
	seq_printf(m, "%c%c%c (%s)%n", con->read ? 'R' : '-',
			con->write ? 'W' : '-', con->unblank ? 'U' : '-',
			flags, &len);
	len = 13 - len;
	if (len < 1)
		len = 1;
	seq_printf(m, "%*c%4d:%d\n", len, ' ', MAJOR(dev), MINOR(dev));

	return 0;
}

/* iterator for consoles */
static void *c_start(struct seq_file *m, loff_t *pos)
{
	struct console *con;
	loff_t off = 0;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	acquire_console_sem();
	for (con = console_drivers; con; con = con->next) {
		if (!con->device)
			continue;
		if (++off == *pos)
			break;
	}
	release_console_sem();

	return con;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct console *con;

	acquire_console_sem();
	if (v == SEQ_START_TOKEN)
		con = console_drivers;
	else
		con = ((struct console *)v)->next;
	for (; con; con = con->next) {
		if (!con->device)
			continue;
		++*pos;
		break;
	}
	release_console_sem();

	return con;
}

static void c_stop(struct seq_file *m, void *v)
{
}

static const struct seq_operations tty_consoles_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_console_dev
};

/*
 * Used for open /proc/tty/consoles. Before this detect
 * the device ID of file descriptor 0 of the current
 * reading task if a character device...
 */
static int tty_consoles_open(struct inode *inode, struct file *file)
{
	struct files_struct *curfiles;

	current_dev = 0;
	curfiles = get_files_struct(current);
	if (curfiles) {
		const struct file *curfp;
		spin_lock(&curfiles->file_lock);
		curfp = fcheck_files(curfiles, 0);
		if (curfp && curfp->private_data) {
			const struct inode *inode;
			dget(curfp->f_dentry);
			inode = curfp->f_dentry->d_inode;
			if (S_ISCHR(inode->i_mode)) {
				struct tty_struct *tty;
				tty = (struct tty_struct *)curfp->private_data;
				if (tty && tty->magic == TTY_MAGIC) {
					tty = tty_pair_get_tty(tty);
					current_dev = tty_devnum(tty);
				}
			}
			dput(curfp->f_dentry);
		}
		spin_unlock(&curfiles->file_lock);
		put_files_struct(curfiles);
	}
	return seq_open(file, &tty_consoles_op);
}

static const struct file_operations proc_tty_consoles_operations = {
	.open		= tty_consoles_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*
 * This function is called by tty_register_driver() to handle
 * registering the driver's /proc handler into /proc/tty/driver/<foo>
 */
void proc_tty_register_driver(struct tty_driver *driver)
{
	struct proc_dir_entry *ent;
		
	if (!driver->driver_name || driver->proc_entry ||
	    !driver->ops->proc_fops)
		return;

	ent = proc_create_data(driver->driver_name, 0, proc_tty_driver,
			       driver->ops->proc_fops, driver);
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
	if (!proc_mkdir("tty", NULL))
		return;
	proc_tty_ldisc = proc_mkdir("tty/ldisc", NULL);
	/*
	 * /proc/tty/driver/serial reveals the exact character counts for
	 * serial links which is just too easy to abuse for inferring
	 * password lengths and inter-keystroke timings during password
	 * entry.
	 */
	proc_tty_driver = proc_mkdir_mode("tty/driver", S_IRUSR|S_IXUSR, NULL);
	proc_create("tty/ldiscs", 0, NULL, &tty_ldiscs_proc_fops);
	proc_create("tty/drivers", 0, NULL, &proc_tty_drivers_operations);
	proc_create("tty/consoles", 0, NULL, &proc_tty_consoles_operations);
}
