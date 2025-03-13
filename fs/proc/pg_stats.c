#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>

static int pg_stats_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    
    rcu_read_lock();
    for_each_process(task) {
        seq_printf(m, "%d: [%llu,%llu,%llu], [%llu,%llu,%llu], [%llu,%llu,%llu], [%llu,%llu,%llu]\n",
            task->pid,
            task->pgd_alloc_count, task->pgd_free_count, task->pgd_set_count,
            task->pud_alloc_count, task->pud_free_count, task->pud_set_count,
            task->pmd_alloc_count, task->pmd_free_count, task->pmd_set_count,
            task->pte_alloc_count, task->pte_free_count, task->pte_set_count);
    }
    rcu_read_unlock();

    return 0;
}

static int pg_stats_open(struct inode *inode, struct file *file) {
    return single_open(file, pg_stats_show, NULL);
}

static const struct file_operations pg_stats_fops = {
    .owner = THIS_MODULE,
    .open = pg_stats_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init pg_stats_init(void) {
    proc_create("pg_stats", 0, NULL, &pg_stats_fops);
    return 0;
}

static void __exit pg_stats_exit(void) {
    remove_proc_entry("pg_stats", NULL);
}

module_init(pg_stats_init);
module_exit(pg_stats_exit);

