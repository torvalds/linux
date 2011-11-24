/* GPL */
static void *single_start(struct seq_file *p, loff_t *pos)
{
   return (void*)(*pos == 0);
}

static void *single_next(struct seq_file *p, void *v, loff_t *pos)
{
   ++*pos;
   return NULL;
}

static void single_stop(struct seq_file *p, void *v)
{
}

int single_open(struct file *file, int (*show)(struct seq_file *, void *),
                void *data)
{
    struct seq_operations *op = kmalloc(sizeof(*op), GFP_KERNEL);
    int res = -ENOMEM;

    if (op) {
	op->start = single_start;
	op->next = single_next;
	op->stop = single_stop;
	op->show = show;
	res = seq_open(file, op);
	if (!res)
	    ((struct seq_file *)file->private_data)->private = data;
	else
	    kfree(op);
    }
    return res;
}

int single_release(struct inode *inode, struct file *file)
{
    struct seq_operations *op = (struct seq_operations *) ((struct seq_file *)file->private_data)->op;
    int res = seq_release(inode, file);
    kfree(op);
    return res;
}
