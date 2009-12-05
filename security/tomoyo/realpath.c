/*
 * security/tomoyo/realpath.c
 *
 * Get the canonicalized absolute pathnames. The basis for TOMOYO.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
 */

#include <linux/types.h>
#include <linux/mount.h>
#include <linux/mnt_namespace.h>
#include <linux/fs_struct.h>
#include <linux/hash.h>

#include "common.h"
#include "realpath.h"

/**
 * tomoyo_encode: Convert binary string to ascii string.
 *
 * @buffer:  Buffer for ASCII string.
 * @buflen:  Size of @buffer.
 * @str:     Binary string.
 *
 * Returns 0 on success, -ENOMEM otherwise.
 */
int tomoyo_encode(char *buffer, int buflen, const char *str)
{
	while (1) {
		const unsigned char c = *(unsigned char *) str++;

		if (tomoyo_is_valid(c)) {
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			if (c != '\\')
				continue;
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			continue;
		}
		if (!c) {
			if (--buflen <= 0)
				break;
			*buffer = '\0';
			return 0;
		}
		buflen -= 4;
		if (buflen <= 0)
			break;
		*buffer++ = '\\';
		*buffer++ = (c >> 6) + '0';
		*buffer++ = ((c >> 3) & 7) + '0';
		*buffer++ = (c & 7) + '0';
	}
	return -ENOMEM;
}

/**
 * tomoyo_realpath_from_path2 - Returns realpath(3) of the given dentry but ignores chroot'ed root.
 *
 * @path:        Pointer to "struct path".
 * @newname:     Pointer to buffer to return value in.
 * @newname_len: Size of @newname.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * If dentry is a directory, trailing '/' is appended.
 * Characters out of 0x20 < c < 0x7F range are converted to
 * \ooo style octal string.
 * Character \ is converted to \\ string.
 */
int tomoyo_realpath_from_path2(struct path *path, char *newname,
			       int newname_len)
{
	int error = -ENOMEM;
	struct dentry *dentry = path->dentry;
	char *sp;

	if (!dentry || !path->mnt || !newname || newname_len <= 2048)
		return -EINVAL;
	if (dentry->d_op && dentry->d_op->d_dname) {
		/* For "socket:[\$]" and "pipe:[\$]". */
		static const int offset = 1536;
		sp = dentry->d_op->d_dname(dentry, newname + offset,
					   newname_len - offset);
	} else {
		/* Taken from d_namespace_path(). */
		struct path root;
		struct path ns_root = { };
		struct path tmp;

		read_lock(&current->fs->lock);
		root = current->fs->root;
		path_get(&root);
		read_unlock(&current->fs->lock);
		spin_lock(&vfsmount_lock);
		if (root.mnt && root.mnt->mnt_ns)
			ns_root.mnt = mntget(root.mnt->mnt_ns->root);
		if (ns_root.mnt)
			ns_root.dentry = dget(ns_root.mnt->mnt_root);
		spin_unlock(&vfsmount_lock);
		spin_lock(&dcache_lock);
		tmp = ns_root;
		sp = __d_path(path, &tmp, newname, newname_len);
		spin_unlock(&dcache_lock);
		path_put(&root);
		path_put(&ns_root);
	}
	if (IS_ERR(sp))
		error = PTR_ERR(sp);
	else
		error = tomoyo_encode(newname, sp - newname, sp);
	/* Append trailing '/' if dentry is a directory. */
	if (!error && dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)
	    && *newname) {
		sp = newname + strlen(newname);
		if (*(sp - 1) != '/') {
			if (sp < newname + newname_len - 4) {
				*sp++ = '/';
				*sp = '\0';
			} else {
				error = -ENOMEM;
			}
		}
	}
	if (error)
		printk(KERN_WARNING "tomoyo_realpath: Pathname too long.\n");
	return error;
}

/**
 * tomoyo_realpath_from_path - Returns realpath(3) of the given pathname but ignores chroot'ed root.
 *
 * @path: Pointer to "struct path".
 *
 * Returns the realpath of the given @path on success, NULL otherwise.
 *
 * These functions use tomoyo_alloc(), so the caller must call tomoyo_free()
 * if these functions didn't return NULL.
 */
char *tomoyo_realpath_from_path(struct path *path)
{
	char *buf = tomoyo_alloc(sizeof(struct tomoyo_page_buffer));

	BUILD_BUG_ON(sizeof(struct tomoyo_page_buffer)
		     <= TOMOYO_MAX_PATHNAME_LEN - 1);
	if (!buf)
		return NULL;
	if (tomoyo_realpath_from_path2(path, buf,
				       TOMOYO_MAX_PATHNAME_LEN - 1) == 0)
		return buf;
	tomoyo_free(buf);
	return NULL;
}

/**
 * tomoyo_realpath - Get realpath of a pathname.
 *
 * @pathname: The pathname to solve.
 *
 * Returns the realpath of @pathname on success, NULL otherwise.
 */
char *tomoyo_realpath(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, LOOKUP_FOLLOW, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}

/**
 * tomoyo_realpath_nofollow - Get realpath of a pathname.
 *
 * @pathname: The pathname to solve.
 *
 * Returns the realpath of @pathname on success, NULL otherwise.
 */
char *tomoyo_realpath_nofollow(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, 0, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}

/* Memory allocated for non-string data. */
static unsigned int tomoyo_allocated_memory_for_elements;
/* Quota for holding non-string data. */
static unsigned int tomoyo_quota_for_elements;

/**
 * tomoyo_alloc_element - Allocate permanent memory for structures.
 *
 * @size: Size in bytes.
 *
 * Returns pointer to allocated memory on success, NULL otherwise.
 *
 * Memory has to be zeroed.
 * The RAM is chunked, so NEVER try to kfree() the returned pointer.
 */
void *tomoyo_alloc_element(const unsigned int size)
{
	static char *buf;
	static DEFINE_MUTEX(lock);
	static unsigned int buf_used_len = PATH_MAX;
	char *ptr = NULL;
	/*Assumes sizeof(void *) >= sizeof(long) is true. */
	const unsigned int word_aligned_size
		= roundup(size, max(sizeof(void *), sizeof(long)));
	if (word_aligned_size > PATH_MAX)
		return NULL;
	mutex_lock(&lock);
	if (buf_used_len + word_aligned_size > PATH_MAX) {
		if (!tomoyo_quota_for_elements ||
		    tomoyo_allocated_memory_for_elements
		    + PATH_MAX <= tomoyo_quota_for_elements)
			ptr = kzalloc(PATH_MAX, GFP_KERNEL);
		if (!ptr) {
			printk(KERN_WARNING "ERROR: Out of memory "
			       "for tomoyo_alloc_element().\n");
			if (!tomoyo_policy_loaded)
				panic("MAC Initialization failed.\n");
		} else {
			buf = ptr;
			tomoyo_allocated_memory_for_elements += PATH_MAX;
			buf_used_len = word_aligned_size;
			ptr = buf;
		}
	} else if (word_aligned_size) {
		int i;
		ptr = buf + buf_used_len;
		buf_used_len += word_aligned_size;
		for (i = 0; i < word_aligned_size; i++) {
			if (!ptr[i])
				continue;
			printk(KERN_ERR "WARNING: Reserved memory was tainted! "
			       "The system might go wrong.\n");
			ptr[i] = '\0';
		}
	}
	mutex_unlock(&lock);
	return ptr;
}

/* Memory allocated for string data in bytes. */
static unsigned int tomoyo_allocated_memory_for_savename;
/* Quota for holding string data in bytes. */
static unsigned int tomoyo_quota_for_savename;

/*
 * TOMOYO uses this hash only when appending a string into the string
 * table. Frequency of appending strings is very low. So we don't need
 * large (e.g. 64k) hash size. 256 will be sufficient.
 */
#define TOMOYO_HASH_BITS  8
#define TOMOYO_MAX_HASH (1u<<TOMOYO_HASH_BITS)

/*
 * tomoyo_name_entry is a structure which is used for linking
 * "struct tomoyo_path_info" into tomoyo_name_list .
 *
 * Since tomoyo_name_list manages a list of strings which are shared by
 * multiple processes (whereas "struct tomoyo_path_info" inside
 * "struct tomoyo_path_info_with_data" is not shared), a reference counter will
 * be added to "struct tomoyo_name_entry" rather than "struct tomoyo_path_info"
 * when TOMOYO starts supporting garbage collector.
 */
struct tomoyo_name_entry {
	struct list_head list;
	struct tomoyo_path_info entry;
};

/* Structure for available memory region. */
struct tomoyo_free_memory_block_list {
	struct list_head list;
	char *ptr;             /* Pointer to a free area. */
	int len;               /* Length of the area.     */
};

/*
 * tomoyo_name_list is used for holding string data used by TOMOYO.
 * Since same string data is likely used for multiple times (e.g.
 * "/lib/libc-2.5.so"), TOMOYO shares string data in the form of
 * "const struct tomoyo_path_info *".
 */
static struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];

/**
 * tomoyo_save_name - Allocate permanent memory for string data.
 *
 * @name: The string to store into the permernent memory.
 *
 * Returns pointer to "struct tomoyo_path_info" on success, NULL otherwise.
 *
 * The RAM is shared, so NEVER try to modify or kfree() the returned name.
 */
const struct tomoyo_path_info *tomoyo_save_name(const char *name)
{
	static LIST_HEAD(fmb_list);
	static DEFINE_MUTEX(lock);
	struct tomoyo_name_entry *ptr;
	unsigned int hash;
	/* fmb contains available size in bytes.
	   fmb is removed from the fmb_list when fmb->len becomes 0. */
	struct tomoyo_free_memory_block_list *fmb;
	int len;
	char *cp;
	struct list_head *head;

	if (!name)
		return NULL;
	len = strlen(name) + 1;
	if (len > TOMOYO_MAX_PATHNAME_LEN) {
		printk(KERN_WARNING "ERROR: Name too long "
		       "for tomoyo_save_name().\n");
		return NULL;
	}
	hash = full_name_hash((const unsigned char *) name, len - 1);
	head = &tomoyo_name_list[hash_long(hash, TOMOYO_HASH_BITS)];

	mutex_lock(&lock);
	list_for_each_entry(ptr, head, list) {
		if (hash == ptr->entry.hash && !strcmp(name, ptr->entry.name))
			goto out;
	}
	list_for_each_entry(fmb, &fmb_list, list) {
		if (len <= fmb->len)
			goto ready;
	}
	if (!tomoyo_quota_for_savename ||
	    tomoyo_allocated_memory_for_savename + PATH_MAX
	    <= tomoyo_quota_for_savename)
		cp = kzalloc(PATH_MAX, GFP_KERNEL);
	else
		cp = NULL;
	fmb = kzalloc(sizeof(*fmb), GFP_KERNEL);
	if (!cp || !fmb) {
		kfree(cp);
		kfree(fmb);
		printk(KERN_WARNING "ERROR: Out of memory "
		       "for tomoyo_save_name().\n");
		if (!tomoyo_policy_loaded)
			panic("MAC Initialization failed.\n");
		ptr = NULL;
		goto out;
	}
	tomoyo_allocated_memory_for_savename += PATH_MAX;
	list_add(&fmb->list, &fmb_list);
	fmb->ptr = cp;
	fmb->len = PATH_MAX;
 ready:
	ptr = tomoyo_alloc_element(sizeof(*ptr));
	if (!ptr)
		goto out;
	ptr->entry.name = fmb->ptr;
	memmove(fmb->ptr, name, len);
	tomoyo_fill_path_info(&ptr->entry);
	fmb->ptr += len;
	fmb->len -= len;
	list_add_tail(&ptr->list, head);
	if (fmb->len == 0) {
		list_del(&fmb->list);
		kfree(fmb);
	}
 out:
	mutex_unlock(&lock);
	return ptr ? &ptr->entry : NULL;
}

/**
 * tomoyo_realpath_init - Initialize realpath related code.
 */
void __init tomoyo_realpath_init(void)
{
	int i;

	BUILD_BUG_ON(TOMOYO_MAX_PATHNAME_LEN > PATH_MAX);
	for (i = 0; i < TOMOYO_MAX_HASH; i++)
		INIT_LIST_HEAD(&tomoyo_name_list[i]);
	INIT_LIST_HEAD(&tomoyo_kernel_domain.acl_info_list);
	tomoyo_kernel_domain.domainname = tomoyo_save_name(TOMOYO_ROOT_NAME);
	list_add_tail(&tomoyo_kernel_domain.list, &tomoyo_domain_list);
	down_read(&tomoyo_domain_list_lock);
	if (tomoyo_find_domain(TOMOYO_ROOT_NAME) != &tomoyo_kernel_domain)
		panic("Can't register tomoyo_kernel_domain");
	up_read(&tomoyo_domain_list_lock);
}

/* Memory allocated for temporary purpose. */
static atomic_t tomoyo_dynamic_memory_size;

/**
 * tomoyo_alloc - Allocate memory for temporary purpose.
 *
 * @size: Size in bytes.
 *
 * Returns pointer to allocated memory on success, NULL otherwise.
 */
void *tomoyo_alloc(const size_t size)
{
	void *p = kzalloc(size, GFP_KERNEL);
	if (p)
		atomic_add(ksize(p), &tomoyo_dynamic_memory_size);
	return p;
}

/**
 * tomoyo_free - Release memory allocated by tomoyo_alloc().
 *
 * @p: Pointer returned by tomoyo_alloc(). May be NULL.
 *
 * Returns nothing.
 */
void tomoyo_free(const void *p)
{
	if (p) {
		atomic_sub(ksize(p), &tomoyo_dynamic_memory_size);
		kfree(p);
	}
}

/**
 * tomoyo_read_memory_counter - Check for memory usage in bytes.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns memory usage.
 */
int tomoyo_read_memory_counter(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		const unsigned int shared
			= tomoyo_allocated_memory_for_savename;
		const unsigned int private
			= tomoyo_allocated_memory_for_elements;
		const unsigned int dynamic
			= atomic_read(&tomoyo_dynamic_memory_size);
		char buffer[64];

		memset(buffer, 0, sizeof(buffer));
		if (tomoyo_quota_for_savename)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)",
				 tomoyo_quota_for_savename);
		else
			buffer[0] = '\0';
		tomoyo_io_printf(head, "Shared:  %10u%s\n", shared, buffer);
		if (tomoyo_quota_for_elements)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)",
				 tomoyo_quota_for_elements);
		else
			buffer[0] = '\0';
		tomoyo_io_printf(head, "Private: %10u%s\n", private, buffer);
		tomoyo_io_printf(head, "Dynamic: %10u\n", dynamic);
		tomoyo_io_printf(head, "Total:   %10u\n",
				 shared + private + dynamic);
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_write_memory_quota - Set memory quota.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
int tomoyo_write_memory_quota(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int size;

	if (sscanf(data, "Shared: %u", &size) == 1)
		tomoyo_quota_for_savename = size;
	else if (sscanf(data, "Private: %u", &size) == 1)
		tomoyo_quota_for_elements = size;
	return 0;
}
