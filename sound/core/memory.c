/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 * 
 *  Memory allocation helpers.
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

#include <sound/driver.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/info.h>

/*
 *  memory allocation helpers and debug routines
 */

#ifdef CONFIG_SND_DEBUG_MEMORY

struct snd_alloc_track {
	unsigned long magic;
	void *caller;
	size_t size;
	struct list_head list;
	long data[0];
};

#define snd_alloc_track_entry(obj) (struct snd_alloc_track *)((char*)obj - (unsigned long)((struct snd_alloc_track *)0)->data)

static long snd_alloc_kmalloc;
static long snd_alloc_vmalloc;
static LIST_HEAD(snd_alloc_kmalloc_list);
static LIST_HEAD(snd_alloc_vmalloc_list);
static DEFINE_SPINLOCK(snd_alloc_kmalloc_lock);
static DEFINE_SPINLOCK(snd_alloc_vmalloc_lock);
#define KMALLOC_MAGIC 0x87654321
#define VMALLOC_MAGIC 0x87654320
static snd_info_entry_t *snd_memory_info_entry;

void __init snd_memory_init(void)
{
	snd_alloc_kmalloc = 0;
	snd_alloc_vmalloc = 0;
}

void snd_memory_done(void)
{
	struct list_head *head;
	struct snd_alloc_track *t;

	if (snd_alloc_kmalloc > 0)
		snd_printk(KERN_ERR "Not freed snd_alloc_kmalloc = %li\n", snd_alloc_kmalloc);
	if (snd_alloc_vmalloc > 0)
		snd_printk(KERN_ERR "Not freed snd_alloc_vmalloc = %li\n", snd_alloc_vmalloc);
	list_for_each_prev(head, &snd_alloc_kmalloc_list) {
		t = list_entry(head, struct snd_alloc_track, list);
		if (t->magic != KMALLOC_MAGIC) {
			snd_printk(KERN_ERR "Corrupted kmalloc\n");
			break;
		}
		snd_printk(KERN_ERR "kmalloc(%ld) from %p not freed\n", (long) t->size, t->caller);
	}
	list_for_each_prev(head, &snd_alloc_vmalloc_list) {
		t = list_entry(head, struct snd_alloc_track, list);
		if (t->magic != VMALLOC_MAGIC) {
			snd_printk(KERN_ERR "Corrupted vmalloc\n");
			break;
		}
		snd_printk(KERN_ERR "vmalloc(%ld) from %p not freed\n", (long) t->size, t->caller);
	}
}

static void *__snd_kmalloc(size_t size, gfp_t flags, void *caller)
{
	unsigned long cpu_flags;
	struct snd_alloc_track *t;
	void *ptr;
	
	ptr = snd_wrapper_kmalloc(size + sizeof(struct snd_alloc_track), flags);
	if (ptr != NULL) {
		t = (struct snd_alloc_track *)ptr;
		t->magic = KMALLOC_MAGIC;
		t->caller = caller;
		spin_lock_irqsave(&snd_alloc_kmalloc_lock, cpu_flags);
		list_add_tail(&t->list, &snd_alloc_kmalloc_list);
		spin_unlock_irqrestore(&snd_alloc_kmalloc_lock, cpu_flags);
		t->size = size;
		snd_alloc_kmalloc += size;
		ptr = t->data;
	}
	return ptr;
}

#define _snd_kmalloc(size, flags) __snd_kmalloc((size), (flags), __builtin_return_address(0));
void *snd_hidden_kmalloc(size_t size, gfp_t flags)
{
	return _snd_kmalloc(size, flags);
}

void *snd_hidden_kzalloc(size_t size, gfp_t flags)
{
	void *ret = _snd_kmalloc(size, flags);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
EXPORT_SYMBOL(snd_hidden_kzalloc);

void *snd_hidden_kcalloc(size_t n, size_t size, gfp_t flags)
{
	void *ret = NULL;
	if (n != 0 && size > INT_MAX / n)
		return ret;
	return snd_hidden_kzalloc(n * size, flags);
}

void snd_hidden_kfree(const void *obj)
{
	unsigned long flags;
	struct snd_alloc_track *t;
	if (obj == NULL)
		return;
	t = snd_alloc_track_entry(obj);
	if (t->magic != KMALLOC_MAGIC) {
		snd_printk(KERN_WARNING "bad kfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	spin_lock_irqsave(&snd_alloc_kmalloc_lock, flags);
	list_del(&t->list);
	spin_unlock_irqrestore(&snd_alloc_kmalloc_lock, flags);
	t->magic = 0;
	snd_alloc_kmalloc -= t->size;
	obj = t;
	snd_wrapper_kfree(obj);
}

void *snd_hidden_vmalloc(unsigned long size)
{
	void *ptr;
	ptr = snd_wrapper_vmalloc(size + sizeof(struct snd_alloc_track));
	if (ptr) {
		struct snd_alloc_track *t = (struct snd_alloc_track *)ptr;
		t->magic = VMALLOC_MAGIC;
		t->caller = __builtin_return_address(0);
		spin_lock(&snd_alloc_vmalloc_lock);
		list_add_tail(&t->list, &snd_alloc_vmalloc_list);
		spin_unlock(&snd_alloc_vmalloc_lock);
		t->size = size;
		snd_alloc_vmalloc += size;
		ptr = t->data;
	}
	return ptr;
}

void snd_hidden_vfree(void *obj)
{
	struct snd_alloc_track *t;
	if (obj == NULL)
		return;
	t = snd_alloc_track_entry(obj);
	if (t->magic != VMALLOC_MAGIC) {
		snd_printk(KERN_ERR "bad vfree (called from %p)\n", __builtin_return_address(0));
		return;
	}
	spin_lock(&snd_alloc_vmalloc_lock);
	list_del(&t->list);
	spin_unlock(&snd_alloc_vmalloc_lock);
	t->magic = 0;
	snd_alloc_vmalloc -= t->size;
	obj = t;
	snd_wrapper_vfree(obj);
}

char *snd_hidden_kstrdup(const char *s, gfp_t flags)
{
	int len;
	char *buf;

	if (!s) return NULL;

	len = strlen(s) + 1;
	buf = _snd_kmalloc(len, flags);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}

static void snd_memory_info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	snd_iprintf(buffer, "kmalloc: %li bytes\n", snd_alloc_kmalloc);
	snd_iprintf(buffer, "vmalloc: %li bytes\n", snd_alloc_vmalloc);
}

int __init snd_memory_info_init(void)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "meminfo", NULL);
	if (entry) {
		entry->c.text.read_size = 256;
		entry->c.text.read = snd_memory_info_read;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	snd_memory_info_entry = entry;
	return 0;
}

int __exit snd_memory_info_done(void)
{
	if (snd_memory_info_entry)
		snd_info_unregister(snd_memory_info_entry);
	return 0;
}

#endif /* CONFIG_SND_DEBUG_MEMORY */

/**
 * copy_to_user_fromio - copy data from mmio-space to user-space
 * @dst: the destination pointer on user-space
 * @src: the source pointer on mmio
 * @count: the data size to copy in bytes
 *
 * Copies the data from mmio-space to user-space.
 *
 * Returns zero if successful, or non-zero on failure.
 */
int copy_to_user_fromio(void __user *dst, const volatile void __iomem *src, size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_to_user(dst, (const void __force*)src, count) ? -EFAULT : 0;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		memcpy_fromio(buf, (void __iomem *)src, c);
		if (copy_to_user(dst, buf, c))
			return -EFAULT;
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}

/**
 * copy_from_user_toio - copy data from user-space to mmio-space
 * @dst: the destination pointer on mmio-space
 * @src: the source pointer on user-space
 * @count: the data size to copy in bytes
 *
 * Copies the data from user-space to mmio-space.
 *
 * Returns zero if successful, or non-zero on failure.
 */
int copy_from_user_toio(volatile void __iomem *dst, const void __user *src, size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_from_user((void __force *)dst, src, count) ? -EFAULT : 0;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (copy_from_user(buf, src, c))
			return -EFAULT;
		memcpy_toio(dst, buf, c);
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}
