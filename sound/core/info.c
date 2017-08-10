/*
 *  Information interface for ALSA driver
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/init.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <linux/utsname.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <stdarg.h>

int snd_info_check_reserved_words(const char *str)
{
	static char *reserved[] =
	{
		"version",
		"meminfo",
		"memdebug",
		"detect",
		"devices",
		"oss",
		"cards",
		"timers",
		"synth",
		"pcm",
		"seq",
		NULL
	};
	char **xstr = reserved;

	while (*xstr) {
		if (!strcmp(*xstr, str))
			return 0;
		xstr++;
	}
	if (!strncmp(str, "card", 4))
		return 0;
	return 1;
}

static DEFINE_MUTEX(info_mutex);

struct snd_info_private_data {
	struct snd_info_buffer *rbuffer;
	struct snd_info_buffer *wbuffer;
	struct snd_info_entry *entry;
	void *file_private_data;
};

static int snd_info_version_init(void);
static void snd_info_disconnect(struct snd_info_entry *entry);

/*

 */

static struct snd_info_entry *snd_proc_root;
struct snd_info_entry *snd_seq_root;
EXPORT_SYMBOL(snd_seq_root);

#ifdef CONFIG_SND_OSSEMUL
struct snd_info_entry *snd_oss_root;
#endif

static int alloc_info_private(struct snd_info_entry *entry,
			      struct snd_info_private_data **ret)
{
	struct snd_info_private_data *data;

	if (!entry || !entry->p)
		return -ENODEV;
	if (!try_module_get(entry->module))
		return -EFAULT;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		module_put(entry->module);
		return -ENOMEM;
	}
	data->entry = entry;
	*ret = data;
	return 0;
}

static bool valid_pos(loff_t pos, size_t count)
{
	if (pos < 0 || (long) pos != pos || (ssize_t) count < 0)
		return false;
	if ((unsigned long) pos + (unsigned long) count < (unsigned long) pos)
		return false;
	return true;
}

/*
 * file ops for binary proc files
 */
static loff_t snd_info_entry_llseek(struct file *file, loff_t offset, int orig)
{
	struct snd_info_private_data *data;
	struct snd_info_entry *entry;
	loff_t ret = -EINVAL, size;

	data = file->private_data;
	entry = data->entry;
	mutex_lock(&entry->access);
	if (entry->c.ops->llseek) {
		offset = entry->c.ops->llseek(entry,
					      data->file_private_data,
					      file, offset, orig);
		goto out;
	}

	size = entry->size;
	switch (orig) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += file->f_pos;
		break;
	case SEEK_END:
		if (!size)
			goto out;
		offset += size;
		break;
	default:
		goto out;
	}
	if (offset < 0)
		goto out;
	if (size && offset > size)
		offset = size;
	file->f_pos = offset;
	ret = offset;
 out:
	mutex_unlock(&entry->access);
	return ret;
}

static ssize_t snd_info_entry_read(struct file *file, char __user *buffer,
				   size_t count, loff_t * offset)
{
	struct snd_info_private_data *data = file->private_data;
	struct snd_info_entry *entry = data->entry;
	size_t size;
	loff_t pos;

	pos = *offset;
	if (!valid_pos(pos, count))
		return -EIO;
	if (pos >= entry->size)
		return 0;
	size = entry->size - pos;
	size = min(count, size);
	size = entry->c.ops->read(entry, data->file_private_data,
				  file, buffer, size, pos);
	if ((ssize_t) size > 0)
		*offset = pos + size;
	return size;
}

static ssize_t snd_info_entry_write(struct file *file, const char __user *buffer,
				    size_t count, loff_t * offset)
{
	struct snd_info_private_data *data = file->private_data;
	struct snd_info_entry *entry = data->entry;
	ssize_t size = 0;
	loff_t pos;

	pos = *offset;
	if (!valid_pos(pos, count))
		return -EIO;
	if (count > 0) {
		size_t maxsize = entry->size - pos;
		count = min(count, maxsize);
		size = entry->c.ops->write(entry, data->file_private_data,
					   file, buffer, count, pos);
	}
	if (size > 0)
		*offset = pos + size;
	return size;
}

static unsigned int snd_info_entry_poll(struct file *file, poll_table *wait)
{
	struct snd_info_private_data *data = file->private_data;
	struct snd_info_entry *entry = data->entry;
	unsigned int mask = 0;

	if (entry->c.ops->poll)
		return entry->c.ops->poll(entry,
					  data->file_private_data,
					  file, wait);
	if (entry->c.ops->read)
		mask |= POLLIN | POLLRDNORM;
	if (entry->c.ops->write)
		mask |= POLLOUT | POLLWRNORM;
	return mask;
}

static long snd_info_entry_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct snd_info_private_data *data = file->private_data;
	struct snd_info_entry *entry = data->entry;

	if (!entry->c.ops->ioctl)
		return -ENOTTY;
	return entry->c.ops->ioctl(entry, data->file_private_data,
				   file, cmd, arg);
}

static int snd_info_entry_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	struct snd_info_private_data *data;
	struct snd_info_entry *entry;

	data = file->private_data;
	if (data == NULL)
		return 0;
	entry = data->entry;
	if (!entry->c.ops->mmap)
		return -ENXIO;
	return entry->c.ops->mmap(entry, data->file_private_data,
				  inode, file, vma);
}

static int snd_info_entry_open(struct inode *inode, struct file *file)
{
	struct snd_info_entry *entry = PDE_DATA(inode);
	struct snd_info_private_data *data;
	int mode, err;

	mutex_lock(&info_mutex);
	err = alloc_info_private(entry, &data);
	if (err < 0)
		goto unlock;

	mode = file->f_flags & O_ACCMODE;
	if (((mode == O_RDONLY || mode == O_RDWR) && !entry->c.ops->read) ||
	    ((mode == O_WRONLY || mode == O_RDWR) && !entry->c.ops->write)) {
		err = -ENODEV;
		goto error;
	}

	if (entry->c.ops->open) {
		err = entry->c.ops->open(entry, mode, &data->file_private_data);
		if (err < 0)
			goto error;
	}

	file->private_data = data;
	mutex_unlock(&info_mutex);
	return 0;

 error:
	kfree(data);
	module_put(entry->module);
 unlock:
	mutex_unlock(&info_mutex);
	return err;
}

static int snd_info_entry_release(struct inode *inode, struct file *file)
{
	struct snd_info_private_data *data = file->private_data;
	struct snd_info_entry *entry = data->entry;

	if (entry->c.ops->release)
		entry->c.ops->release(entry, file->f_flags & O_ACCMODE,
				      data->file_private_data);
	module_put(entry->module);
	kfree(data);
	return 0;
}

static const struct file_operations snd_info_entry_operations =
{
	.owner =		THIS_MODULE,
	.llseek =		snd_info_entry_llseek,
	.read =			snd_info_entry_read,
	.write =		snd_info_entry_write,
	.poll =			snd_info_entry_poll,
	.unlocked_ioctl =	snd_info_entry_ioctl,
	.mmap =			snd_info_entry_mmap,
	.open =			snd_info_entry_open,
	.release =		snd_info_entry_release,
};

/*
 * file ops for text proc files
 */
static ssize_t snd_info_text_entry_write(struct file *file,
					 const char __user *buffer,
					 size_t count, loff_t *offset)
{
	struct seq_file *m = file->private_data;
	struct snd_info_private_data *data = m->private;
	struct snd_info_entry *entry = data->entry;
	struct snd_info_buffer *buf;
	loff_t pos;
	size_t next;
	int err = 0;

	if (!entry->c.text.write)
		return -EIO;
	pos = *offset;
	if (!valid_pos(pos, count))
		return -EIO;
	next = pos + count;
	/* don't handle too large text inputs */
	if (next > 16 * 1024)
		return -EIO;
	mutex_lock(&entry->access);
	buf = data->wbuffer;
	if (!buf) {
		data->wbuffer = buf = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf) {
			err = -ENOMEM;
			goto error;
		}
	}
	if (next > buf->len) {
		char *nbuf = kvzalloc(PAGE_ALIGN(next), GFP_KERNEL);
		if (!nbuf) {
			err = -ENOMEM;
			goto error;
		}
		kvfree(buf->buffer);
		buf->buffer = nbuf;
		buf->len = PAGE_ALIGN(next);
	}
	if (copy_from_user(buf->buffer + pos, buffer, count)) {
		err = -EFAULT;
		goto error;
	}
	buf->size = next;
 error:
	mutex_unlock(&entry->access);
	if (err < 0)
		return err;
	*offset = next;
	return count;
}

static int snd_info_seq_show(struct seq_file *seq, void *p)
{
	struct snd_info_private_data *data = seq->private;
	struct snd_info_entry *entry = data->entry;

	if (!entry->c.text.read) {
		return -EIO;
	} else {
		data->rbuffer->buffer = (char *)seq; /* XXX hack! */
		entry->c.text.read(entry, data->rbuffer);
	}
	return 0;
}

static int snd_info_text_entry_open(struct inode *inode, struct file *file)
{
	struct snd_info_entry *entry = PDE_DATA(inode);
	struct snd_info_private_data *data;
	int err;

	mutex_lock(&info_mutex);
	err = alloc_info_private(entry, &data);
	if (err < 0)
		goto unlock;

	data->rbuffer = kzalloc(sizeof(*data->rbuffer), GFP_KERNEL);
	if (!data->rbuffer) {
		err = -ENOMEM;
		goto error;
	}
	if (entry->size)
		err = single_open_size(file, snd_info_seq_show, data,
				       entry->size);
	else
		err = single_open(file, snd_info_seq_show, data);
	if (err < 0)
		goto error;
	mutex_unlock(&info_mutex);
	return 0;

 error:
	kfree(data->rbuffer);
	kfree(data);
	module_put(entry->module);
 unlock:
	mutex_unlock(&info_mutex);
	return err;
}

static int snd_info_text_entry_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct snd_info_private_data *data = m->private;
	struct snd_info_entry *entry = data->entry;

	if (data->wbuffer && entry->c.text.write)
		entry->c.text.write(entry, data->wbuffer);

	single_release(inode, file);
	kfree(data->rbuffer);
	if (data->wbuffer) {
		kvfree(data->wbuffer->buffer);
		kfree(data->wbuffer);
	}

	module_put(entry->module);
	kfree(data);
	return 0;
}

static const struct file_operations snd_info_text_entry_ops =
{
	.owner =		THIS_MODULE,
	.open =			snd_info_text_entry_open,
	.release =		snd_info_text_entry_release,
	.write =		snd_info_text_entry_write,
	.llseek =		seq_lseek,
	.read =			seq_read,
};

static struct snd_info_entry *create_subdir(struct module *mod,
					    const char *name)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(mod, name, NULL);
	if (!entry)
		return NULL;
	entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}

static struct snd_info_entry *
snd_info_create_entry(const char *name, struct snd_info_entry *parent);

int __init snd_info_init(void)
{
	snd_proc_root = snd_info_create_entry("asound", NULL);
	if (!snd_proc_root)
		return -ENOMEM;
	snd_proc_root->mode = S_IFDIR | S_IRUGO | S_IXUGO;
	snd_proc_root->p = proc_mkdir("asound", NULL);
	if (!snd_proc_root->p)
		goto error;
#ifdef CONFIG_SND_OSSEMUL
	snd_oss_root = create_subdir(THIS_MODULE, "oss");
	if (!snd_oss_root)
		goto error;
#endif
#if IS_ENABLED(CONFIG_SND_SEQUENCER)
	snd_seq_root = create_subdir(THIS_MODULE, "seq");
	if (!snd_seq_root)
		goto error;
#endif
	if (snd_info_version_init() < 0 ||
	    snd_minor_info_init() < 0 ||
	    snd_minor_info_oss_init() < 0 ||
	    snd_card_info_init() < 0 ||
	    snd_info_minor_register() < 0)
		goto error;
	return 0;

 error:
	snd_info_free_entry(snd_proc_root);
	return -ENOMEM;
}

int __exit snd_info_done(void)
{
	snd_info_free_entry(snd_proc_root);
	return 0;
}

/*
 * create a card proc file
 * called from init.c
 */
int snd_info_card_create(struct snd_card *card)
{
	char str[8];
	struct snd_info_entry *entry;

	if (snd_BUG_ON(!card))
		return -ENXIO;

	sprintf(str, "card%i", card->number);
	entry = create_subdir(card->module, str);
	if (!entry)
		return -ENOMEM;
	card->proc_root = entry;
	return 0;
}

/* register all pending info entries */
static int snd_info_register_recursive(struct snd_info_entry *entry)
{
	struct snd_info_entry *p;
	int err;

	if (!entry->p) {
		err = snd_info_register(entry);
		if (err < 0)
			return err;
	}

	list_for_each_entry(p, &entry->children, list) {
		err = snd_info_register_recursive(p);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * register the card proc file
 * called from init.c
 * can be called multiple times for reinitialization
 */
int snd_info_card_register(struct snd_card *card)
{
	struct proc_dir_entry *p;
	int err;

	if (snd_BUG_ON(!card))
		return -ENXIO;

	err = snd_info_register_recursive(card->proc_root);
	if (err < 0)
		return err;

	if (!strcmp(card->id, card->proc_root->name))
		return 0;

	if (card->proc_root_link)
		return 0;
	p = proc_symlink(card->id, snd_proc_root->p, card->proc_root->name);
	if (!p)
		return -ENOMEM;
	card->proc_root_link = p;
	return 0;
}

/*
 * called on card->id change
 */
void snd_info_card_id_change(struct snd_card *card)
{
	mutex_lock(&info_mutex);
	if (card->proc_root_link) {
		proc_remove(card->proc_root_link);
		card->proc_root_link = NULL;
	}
	if (strcmp(card->id, card->proc_root->name))
		card->proc_root_link = proc_symlink(card->id,
						    snd_proc_root->p,
						    card->proc_root->name);
	mutex_unlock(&info_mutex);
}

/*
 * de-register the card proc file
 * called from init.c
 */
void snd_info_card_disconnect(struct snd_card *card)
{
	if (!card)
		return;
	mutex_lock(&info_mutex);
	proc_remove(card->proc_root_link);
	card->proc_root_link = NULL;
	if (card->proc_root)
		snd_info_disconnect(card->proc_root);
	mutex_unlock(&info_mutex);
}

/*
 * release the card proc file resources
 * called from init.c
 */
int snd_info_card_free(struct snd_card *card)
{
	if (!card)
		return 0;
	snd_info_free_entry(card->proc_root);
	card->proc_root = NULL;
	return 0;
}


/**
 * snd_info_get_line - read one line from the procfs buffer
 * @buffer: the procfs buffer
 * @line: the buffer to store
 * @len: the max. buffer size
 *
 * Reads one line from the buffer and stores the string.
 *
 * Return: Zero if successful, or 1 if error or EOF.
 */
int snd_info_get_line(struct snd_info_buffer *buffer, char *line, int len)
{
	int c = -1;

	if (snd_BUG_ON(!buffer || !buffer->buffer))
		return 1;
	if (len <= 0 || buffer->stop || buffer->error)
		return 1;
	while (!buffer->stop) {
		c = buffer->buffer[buffer->curr++];
		if (buffer->curr >= buffer->size)
			buffer->stop = 1;
		if (c == '\n')
			break;
		if (len > 1) {
			len--;
			*line++ = c;
		}
	}
	*line = '\0';
	return 0;
}
EXPORT_SYMBOL(snd_info_get_line);

/**
 * snd_info_get_str - parse a string token
 * @dest: the buffer to store the string token
 * @src: the original string
 * @len: the max. length of token - 1
 *
 * Parses the original string and copy a token to the given
 * string buffer.
 *
 * Return: The updated pointer of the original string so that
 * it can be used for the next call.
 */
const char *snd_info_get_str(char *dest, const char *src, int len)
{
	int c;

	while (*src == ' ' || *src == '\t')
		src++;
	if (*src == '"' || *src == '\'') {
		c = *src++;
		while (--len > 0 && *src && *src != c) {
			*dest++ = *src++;
		}
		if (*src == c)
			src++;
	} else {
		while (--len > 0 && *src && *src != ' ' && *src != '\t') {
			*dest++ = *src++;
		}
	}
	*dest = 0;
	while (*src == ' ' || *src == '\t')
		src++;
	return src;
}
EXPORT_SYMBOL(snd_info_get_str);

/*
 * snd_info_create_entry - create an info entry
 * @name: the proc file name
 * @parent: the parent directory
 *
 * Creates an info entry with the given file name and initializes as
 * the default state.
 *
 * Usually called from other functions such as
 * snd_info_create_card_entry().
 *
 * Return: The pointer of the new instance, or %NULL on failure.
 */
static struct snd_info_entry *
snd_info_create_entry(const char *name, struct snd_info_entry *parent)
{
	struct snd_info_entry *entry;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL)
		return NULL;
	entry->name = kstrdup(name, GFP_KERNEL);
	if (entry->name == NULL) {
		kfree(entry);
		return NULL;
	}
	entry->mode = S_IFREG | S_IRUGO;
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	mutex_init(&entry->access);
	INIT_LIST_HEAD(&entry->children);
	INIT_LIST_HEAD(&entry->list);
	entry->parent = parent;
	if (parent)
		list_add_tail(&entry->list, &parent->children);
	return entry;
}

/**
 * snd_info_create_module_entry - create an info entry for the given module
 * @module: the module pointer
 * @name: the file name
 * @parent: the parent directory
 *
 * Creates a new info entry and assigns it to the given module.
 *
 * Return: The pointer of the new instance, or %NULL on failure.
 */
struct snd_info_entry *snd_info_create_module_entry(struct module * module,
					       const char *name,
					       struct snd_info_entry *parent)
{
	struct snd_info_entry *entry = snd_info_create_entry(name, parent);
	if (entry)
		entry->module = module;
	return entry;
}
EXPORT_SYMBOL(snd_info_create_module_entry);

/**
 * snd_info_create_card_entry - create an info entry for the given card
 * @card: the card instance
 * @name: the file name
 * @parent: the parent directory
 *
 * Creates a new info entry and assigns it to the given card.
 *
 * Return: The pointer of the new instance, or %NULL on failure.
 */
struct snd_info_entry *snd_info_create_card_entry(struct snd_card *card,
					     const char *name,
					     struct snd_info_entry * parent)
{
	struct snd_info_entry *entry = snd_info_create_entry(name, parent);
	if (entry) {
		entry->module = card->module;
		entry->card = card;
	}
	return entry;
}
EXPORT_SYMBOL(snd_info_create_card_entry);

static void snd_info_disconnect(struct snd_info_entry *entry)
{
	struct snd_info_entry *p;

	if (!entry->p)
		return;
	list_for_each_entry(p, &entry->children, list)
		snd_info_disconnect(p);
	proc_remove(entry->p);
	entry->p = NULL;
}

/**
 * snd_info_free_entry - release the info entry
 * @entry: the info entry
 *
 * Releases the info entry.
 */
void snd_info_free_entry(struct snd_info_entry * entry)
{
	struct snd_info_entry *p, *n;

	if (!entry)
		return;
	if (entry->p) {
		mutex_lock(&info_mutex);
		snd_info_disconnect(entry);
		mutex_unlock(&info_mutex);
	}

	/* free all children at first */
	list_for_each_entry_safe(p, n, &entry->children, list)
		snd_info_free_entry(p);

	list_del(&entry->list);
	kfree(entry->name);
	if (entry->private_free)
		entry->private_free(entry);
	kfree(entry);
}
EXPORT_SYMBOL(snd_info_free_entry);

/**
 * snd_info_register - register the info entry
 * @entry: the info entry
 *
 * Registers the proc info entry.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_info_register(struct snd_info_entry * entry)
{
	struct proc_dir_entry *root, *p = NULL;

	if (snd_BUG_ON(!entry))
		return -ENXIO;
	root = entry->parent == NULL ? snd_proc_root->p : entry->parent->p;
	mutex_lock(&info_mutex);
	if (S_ISDIR(entry->mode)) {
		p = proc_mkdir_mode(entry->name, entry->mode, root);
		if (!p) {
			mutex_unlock(&info_mutex);
			return -ENOMEM;
		}
	} else {
		const struct file_operations *ops;
		if (entry->content == SNDRV_INFO_CONTENT_DATA)
			ops = &snd_info_entry_operations;
		else
			ops = &snd_info_text_entry_ops;
		p = proc_create_data(entry->name, entry->mode, root,
				     ops, entry);
		if (!p) {
			mutex_unlock(&info_mutex);
			return -ENOMEM;
		}
		proc_set_size(p, entry->size);
	}
	entry->p = p;
	mutex_unlock(&info_mutex);
	return 0;
}
EXPORT_SYMBOL(snd_info_register);

/*

 */

static void snd_info_version_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer,
		    "Advanced Linux Sound Architecture Driver Version k%s.\n",
		    init_utsname()->release);
}

static int __init snd_info_version_init(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "version", NULL);
	if (entry == NULL)
		return -ENOMEM;
	entry->c.text.read = snd_info_version_read;
	return snd_info_register(entry); /* freed in error path */
}
