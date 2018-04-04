/*
 * Copyright (c) 2010 Werner Fink, Jiri Slaby
 *
 * Licensed under GPLv2
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tty_driver.h>

/*
 * This is handler for /proc/consoles
 */
static int show_console_dev(struct seq_file *m, void *v)
{
	static const struct {
		short flag;
		char name;
	} con_flags[] = {
		{ CON_ENABLED,		'E' },
		{ CON_CONSDEV,		'C' },
		{ CON_BOOT,		'B' },
		{ CON_PRINTBUFFER,	'p' },
		{ CON_BRL,		'b' },
		{ CON_ANYTIME,		'a' },
	};
	char flags[ARRAY_SIZE(con_flags) + 1];
	struct console *con = v;
	unsigned int a;
	dev_t dev = 0;

	if (con->device) {
		const struct tty_driver *driver;
		int index;
		driver = con->device(con, &index);
		if (driver) {
			dev = MKDEV(driver->major, driver->minor_start);
			dev += index;
		}
	}

	for (a = 0; a < ARRAY_SIZE(con_flags); a++)
		flags[a] = (con->flags & con_flags[a].flag) ?
			con_flags[a].name : ' ';
	flags[a] = 0;

	seq_setwidth(m, 21 - 1);
	seq_printf(m, "%s%d", con->name, con->index);
	seq_pad(m, ' ');
	seq_printf(m, "%c%c%c (%s)", con->read ? 'R' : '-',
			con->write ? 'W' : '-', con->unblank ? 'U' : '-',
			flags);
	if (dev)
		seq_printf(m, " %4d:%d", MAJOR(dev), MINOR(dev));

	seq_putc(m, '\n');
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	struct console *con;
	loff_t off = 0;

	console_lock();
	for_each_console(con)
		if (off++ == *pos)
			break;

	return con;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct console *con = v;
	++*pos;
	return con->next;
}

static void c_stop(struct seq_file *m, void *v)
{
	console_unlock();
}

static const struct seq_operations consoles_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_console_dev
};

static int consoles_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &consoles_op);
}

static const struct file_operations proc_consoles_operations = {
	.open		= consoles_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init proc_consoles_init(void)
{
	proc_create("consoles", 0, NULL, &proc_consoles_operations);
	return 0;
}
fs_initcall(proc_consoles_init);
