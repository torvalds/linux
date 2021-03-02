#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

int kdb_write_address;
EXPORT_SYMBOL(kdb_write_address);

noinline void dummy_func18(void)
{
	panic("Hello KDB has paniced!");
}
noinline void dummy_func17(void)
{
	dummy_func18();
}
noinline void dummy_func16(void)
{
	dummy_func17();
}
noinline void dummy_func15(void)
{
	dummy_func16();
}
noinline void dummy_func14(void)
{
	dummy_func15();
}
noinline void dummy_func13(void)
{
	dummy_func14();
}
noinline void dummy_func12(void)
{
	dummy_func13();
}
noinline void dummy_func11(void)
{
	dummy_func12();
}
noinline void dummy_func10(void)
{
	dummy_func11();
}
noinline void dummy_func9(void)
{
	dummy_func10();
}
noinline void dummy_func8(void)
{
	dummy_func9();
}
noinline void dummy_func7(void)
{
	dummy_func8();
}
noinline void dummy_func6(void)
{
	dummy_func7();
}
noinline void dummy_func5(void)
{
	dummy_func6();
}
noinline void dummy_func4(void)
{
	dummy_func5();
}
noinline void dummy_func3(void)
{
	dummy_func4();
}
noinline void dummy_func2(void)
{
	dummy_func3();
}
noinline void dummy_func1(void)
{
	dummy_func2();
}

static int hello_proc_show(struct seq_file *m, void *v) {
	seq_printf(m, "Hello proc!\n");
	return 0;
}

static int hello_proc_open(struct inode *inode, struct  file *file) {
	return single_open(file, hello_proc_show, NULL);
}

static int edit_write(struct file *file, const char *buffer,
		size_t count, loff_t *data)
{
	kdb_write_address += 1;
	return count;
}

static int bug_write(struct file *file, const char *buffer,
		size_t count, loff_t *data)
{
	dummy_func1();
	return count;
}

static const struct proc_ops edit_proc_ops = {
	.proc_open	= hello_proc_open,
	.proc_read	= seq_read,
	.proc_write	= edit_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static const struct proc_ops bug_proc_ops = {
	.proc_open	= hello_proc_open,
	.proc_read	= seq_read,
	.proc_write	= bug_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init hello_proc_init(void) {
	struct proc_dir_entry *file;
	file = proc_create("hello_kdb_bug", 0, NULL, &bug_proc_ops);
	if (file == NULL) {
		return -ENOMEM;
	}

	file = proc_create("hello_kdb_break", 0, NULL, &edit_proc_ops);
	if (file == NULL) {
		remove_proc_entry("hello_kdb_bug", NULL);
		return -ENOMEM;
	}
	return 0;
}

static void __exit hello_proc_exit(void) {
	remove_proc_entry("hello_kdb_bug", NULL);
	remove_proc_entry("hello_kdb_break", NULL);
}

MODULE_LICENSE("GPL");
module_init(hello_proc_init);
module_exit(hello_proc_exit);
