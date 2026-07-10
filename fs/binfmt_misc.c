// SPDX-License-Identifier: GPL-2.0-only
/*
 * binfmt_misc.c
 *
 * Copyright (C) 1997 Richard Günther
 *
 * binfmt_misc detects binaries via a magic or filename extension and invokes
 * a specified wrapper. See Documentation/admin-guide/binfmt-misc.rst for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/magic.h>
#include <linux/binfmts.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string_helpers.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/rculist.h>
#include <linux/seq_file.h>
#include <linux/fs_context.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "internal.h"

/* Entry status and match type bit numbers. */
enum binfmt_misc_entry_bits {
	MISC_FMT_ENABLED_BIT	= 0,
	MISC_FMT_MAGIC_BIT	= 1,
};

/* Entry behavior flags, fixed at registration time. */
enum binfmt_misc_entry_flags {
	MISC_FMT_PRESERVE_ARGV0	= (1U << 31),
	MISC_FMT_OPEN_BINARY	= (1U << 30),
	MISC_FMT_CREDENTIALS	= (1U << 29),
	MISC_FMT_OPEN_FILE	= (1U << 28),
};

struct binfmt_misc_entry {
	struct hlist_node node;
	unsigned long flags;		/* type, status, etc. */
	int offset;			/* offset of magic */
	int size;			/* size of magic/mask */
	char *magic;			/* magic or filename extension */
	char *mask;			/* mask, NULL for exact match */
	const char *interpreter;	/* filename of interpreter */
	char *name;
	struct dentry *dentry;
	struct file *interp_file;
	refcount_t users;		/* sync removal with load_misc_binary() */
	struct rcu_head rcu;
};

static struct file_system_type bm_fs_type;

/*
 * Max length of the register string.  Determined by:
 *  - 7 delimiters
 *  - name:   ~50 bytes
 *  - type:   1 byte
 *  - offset: 3 bytes (has to be smaller than BINPRM_BUF_SIZE)
 *  - magic:  128 bytes (512 in escaped form)
 *  - mask:   128 bytes (512 in escaped form)
 *  - interp: ~50 bytes
 *  - flags:  5 bytes
 * Round that up a bit, and then back off to hold the internal data
 * (like struct binfmt_misc_entry).
 */
#define MAX_REGISTER_LENGTH 1920

/* Check if @e's magic matches @bprm's buffer, applying the mask if set. */
static bool entry_matches_magic(const struct binfmt_misc_entry *e,
				const struct linux_binprm *bprm)
{
	const char *s = bprm->buf + e->offset;
	int i;

	if (!e->mask)
		return !memcmp(s, e->magic, e->size);

	for (i = 0; i < e->size; i++)
		if ((s[i] ^ e->magic[i]) & e->mask[i])
			return false;
	return true;
}

/* Check if @e's registered extension matches @ext, NULL if there is none. */
static bool entry_matches_extension(const struct binfmt_misc_entry *e,
				    const char *ext)
{
	return ext && !strcmp(e->magic, ext);
}

/**
 * search_binfmt_handler - search for a binary handler for @bprm
 * @misc: handle to binfmt_misc instance
 * @bprm: binary for which we are looking for a handler
 *
 * Search for a binary type handler for @bprm in the list of registered binary
 * type handlers.
 *
 * The caller must hold the RCU read lock.
 *
 * Return: binary type list entry on success, NULL on failure
 */
static struct binfmt_misc_entry *
search_binfmt_handler(struct binfmt_misc *misc, struct linux_binprm *bprm)
{
	char *dot = strrchr(bprm->interp, '.');
	const char *ext = dot ? dot + 1 : NULL;
	struct binfmt_misc_entry *e;

	/* Walk all the registered handlers. */
	hlist_for_each_entry_rcu(e, &misc->entries, node) {
		/* Make sure this one is currently enabled. */
		if (!test_bit(MISC_FMT_ENABLED_BIT, &e->flags))
			continue;

		if (test_bit(MISC_FMT_MAGIC_BIT, &e->flags)) {
			if (entry_matches_magic(e, bprm))
				return e;
		} else {
			if (entry_matches_extension(e, ext))
				return e;
		}
	}

	return NULL;
}

/**
 * get_binfmt_handler - try to find a binary type handler
 * @misc: handle to binfmt_misc instance
 * @bprm: binary for which we are looking for a handler
 *
 * Try to find a binfmt handler for the binary type. If one is found take a
 * reference to protect against removal via bm_{entry,status}_write(). The
 * refcount of an entry can only drop to zero once it has been unlinked and
 * a restarted search cannot find an unlinked entry again so the retry loop
 * is bounded.
 *
 * Return: binary type list entry on success, NULL on failure
 */
static struct binfmt_misc_entry *get_binfmt_handler(struct binfmt_misc *misc,
						    struct linux_binprm *bprm)
{
	struct binfmt_misc_entry *e;

	guard(rcu)();
	do {
		e = search_binfmt_handler(misc, bprm);
	} while (e && !refcount_inc_not_zero(&e->users));
	return e;
}

/**
 * put_binfmt_handler - put binary handler entry
 * @e: entry to put
 *
 * Free entry syncing with load_misc_binary() and defer final free to
 * load_misc_binary() in case it is using the binary type handler we were
 * requested to remove.
 */
static void put_binfmt_handler(struct binfmt_misc_entry *e)
{
	if (refcount_dec_and_test(&e->users)) {
		if (e->flags & MISC_FMT_OPEN_FILE) {
			exe_file_allow_write_access(e->interp_file);
			filp_close(e->interp_file, NULL);
		}
		/* Lockless walkers may still dereference this entry. */
		kfree_rcu(e, rcu);
	}
}

DEFINE_FREE(put_binfmt_handler, struct binfmt_misc_entry *, if (_T) put_binfmt_handler(_T))

/**
 * current_binfmt_misc - get the binfmt_misc instance of the caller's user namespace
 *
 * If a user namespace doesn't have its own binfmt_misc mount it uses the
 * handlers of its closest ancestor with one. This mimics the behavior of
 * pre-namespaced binfmt_misc where all registered handlers were available
 * to all users and user namespaces on the system. The init user namespace
 * instance is statically set up so the fallback is never reached in
 * practice.
 *
 * Return: the binfmt_misc instance of the caller's user namespace
 */
static struct binfmt_misc *current_binfmt_misc(void)
{
	const struct user_namespace *user_ns;
	struct binfmt_misc *misc;

	for (user_ns = current_user_ns(); user_ns; user_ns = user_ns->parent) {
		/* Pairs with smp_store_release() in bm_fill_super(). */
		misc = smp_load_acquire(&user_ns->binfmt_misc);
		if (misc)
			return misc;
	}

	return &init_binfmt_misc;
}

/*
 * the loader itself
 */
static int load_misc_binary(struct linux_binprm *bprm)
{
	struct binfmt_misc_entry *fmt __free(put_binfmt_handler) = NULL;
	struct file *interp_file;
	struct binfmt_misc *misc;
	int retval;

	misc = current_binfmt_misc();
	if (!READ_ONCE(misc->enabled))
		return -ENOEXEC;

	fmt = get_binfmt_handler(misc, bprm);
	if (!fmt)
		return -ENOEXEC;

	/* Need to be able to load the file after exec */
	if (bprm->interp_flags & BINPRM_FLAGS_PATH_INACCESSIBLE)
		return -ENOENT;

	if (fmt->flags & MISC_FMT_PRESERVE_ARGV0) {
		bprm->interp_flags |= BINPRM_FLAGS_PRESERVE_ARGV0;
	} else {
		retval = remove_arg_zero(bprm);
		if (retval)
			return retval;
	}

	/* make argv[1] be the path to the binary */
	retval = copy_string_kernel(bprm->interp, bprm);
	if (retval < 0)
		return retval;
	bprm->argc++;

	/* add the interp as argv[0] */
	retval = copy_string_kernel(fmt->interpreter, bprm);
	if (retval < 0)
		return retval;
	bprm->argc++;

	/* Update interp in case binfmt_script needs it. */
	retval = bprm_change_interp(fmt->interpreter, bprm);
	if (retval < 0)
		return retval;

	if (fmt->flags & MISC_FMT_OPEN_FILE) {
		interp_file = file_clone_open(fmt->interp_file);
		if (!IS_ERR(interp_file)) {
			int err = exe_file_deny_write_access(interp_file);

			if (err) {
				fput(interp_file);
				interp_file = ERR_PTR(err);
			}
		}
	} else {
		interp_file = open_exec(fmt->interpreter);
	}
	if (IS_ERR(interp_file))
		return PTR_ERR(interp_file);

	bprm->interpreter = interp_file;
	if (fmt->flags & MISC_FMT_OPEN_BINARY)
		bprm->have_execfd = 1;
	if (fmt->flags & MISC_FMT_CREDENTIALS)
		bprm->execfd_creds = 1;
	return 0;
}

/* Command parsers */

/*
 * parses and copies one argument enclosed in del from *sp to *dp,
 * recognising the \x special.
 * returns pointer to the copied argument or NULL in case of an
 * error (and sets err) or null argument length.
 */
static char *scanarg(char *s, char del)
{
	char c;

	while ((c = *s++) != del) {
		if (c == '\\' && *s == 'x') {
			s++;
			if (!isxdigit(*s++))
				return NULL;
			if (!isxdigit(*s++))
				return NULL;
		}
	}
	s[-1] ='\0';
	return s;
}

static char *check_special_flags(char *sfs, struct binfmt_misc_entry *e)
{
	char *p = sfs;
	int cont = 1;

	/* special flags */
	while (cont) {
		switch (*p) {
		case 'P':
			pr_debug("register: flag: P (preserve argv0)\n");
			p++;
			e->flags |= MISC_FMT_PRESERVE_ARGV0;
			break;
		case 'O':
			pr_debug("register: flag: O (open binary)\n");
			p++;
			e->flags |= MISC_FMT_OPEN_BINARY;
			break;
		case 'C':
			pr_debug("register: flag: C (preserve creds)\n");
			p++;
			/* this flags also implies the
			   open-binary flag */
			e->flags |= (MISC_FMT_CREDENTIALS |
					MISC_FMT_OPEN_BINARY);
			break;
		case 'F':
			pr_debug("register: flag: F: open interpreter file now\n");
			p++;
			e->flags |= MISC_FMT_OPEN_FILE;
			break;
		default:
			cont = 0;
		}
	}

	return p;
}

/*
 * This registers a new binary format, it recognises the syntax
 * ':name:type:offset:magic:mask:interpreter:flags'
 * where the ':' is the IFS, that can be chosen with the first char
 */
static struct binfmt_misc_entry *create_entry(const char __user *buffer,
					      size_t count)
{
	struct binfmt_misc_entry *e;
	int memsize, err;
	char *buf, *p;
	char del;

	pr_debug("register: received %zu bytes\n", count);

	/* some sanity checks */
	err = -EINVAL;
	if ((count < 11) || (count > MAX_REGISTER_LENGTH))
		goto out;

	err = -ENOMEM;
	memsize = sizeof(*e) + count + 8;
	e = kmalloc(memsize, GFP_KERNEL_ACCOUNT);
	if (!e)
		goto out;

	p = buf = (char *)e + sizeof(*e);

	memset(e, 0, sizeof(*e));
	if (copy_from_user(buf, buffer, count))
		goto efault;

	del = *p++;	/* delimeter */

	pr_debug("register: delim: %#x {%c}\n", del, del);

	/* A flag-char delimiter runs the flag scan off the buffer. */
	if (del == 'P' || del == 'O' || del == 'C' || del == 'F')
		goto einval;

	/* Pad the buffer with the delim to simplify parsing below. */
	memset(buf + count, del, 8);

	/* Parse the 'name' field. */
	e->name = p;
	p = strchr(p, del);
	if (!p)
		goto einval;
	*p++ = '\0';
	if (!e->name[0] ||
	    !strcmp(e->name, ".") ||
	    !strcmp(e->name, "..") ||
	    strchr(e->name, '/'))
		goto einval;

	pr_debug("register: name: {%s}\n", e->name);

	/* Parse the 'type' field. */
	switch (*p++) {
	case 'E':
		pr_debug("register: type: E (extension)\n");
		e->flags = BIT(MISC_FMT_ENABLED_BIT);
		break;
	case 'M':
		pr_debug("register: type: M (magic)\n");
		e->flags = BIT(MISC_FMT_ENABLED_BIT) | BIT(MISC_FMT_MAGIC_BIT);
		break;
	default:
		goto einval;
	}
	if (*p++ != del)
		goto einval;

	if (test_bit(MISC_FMT_MAGIC_BIT, &e->flags)) {
		/* Handle the 'M' (magic) format. */
		char *s;

		/* Parse the 'offset' field. */
		s = strchr(p, del);
		if (!s)
			goto einval;
		*s = '\0';
		if (p != s) {
			int r = kstrtoint(p, 10, &e->offset);
			if (r != 0 || e->offset < 0)
				goto einval;
		}
		p = s;
		if (*p++)
			goto einval;
		pr_debug("register: offset: %#x\n", e->offset);

		/* Parse the 'magic' field. */
		e->magic = p;
		p = scanarg(p, del);
		if (!p)
			goto einval;
		if (!e->magic[0])
			goto einval;
		print_hex_dump_debug(
			KBUILD_MODNAME ": register: magic[raw]: ",
			DUMP_PREFIX_NONE, 16, 1, e->magic, p - e->magic, true);

		/* Parse the 'mask' field. */
		e->mask = p;
		p = scanarg(p, del);
		if (!p)
			goto einval;
		if (!e->mask[0]) {
			e->mask = NULL;
			pr_debug("register:  mask[raw]: none\n");
		} else {
			print_hex_dump_debug(
				KBUILD_MODNAME ": register:  mask[raw]: ",
				DUMP_PREFIX_NONE, 16, 1, e->mask, p - e->mask,
				true);
		}

		/*
		 * Decode the magic & mask fields.
		 * Note: while we might have accepted embedded NUL bytes from
		 * above, the unescape helpers here will stop at the first one
		 * it encounters.
		 */
		e->size = string_unescape_inplace(e->magic, UNESCAPE_HEX);
		if (e->mask &&
		    string_unescape_inplace(e->mask, UNESCAPE_HEX) != e->size)
			goto einval;
		if (e->size > BINPRM_BUF_SIZE ||
		    BINPRM_BUF_SIZE - e->size < e->offset)
			goto einval;
		pr_debug("register: magic/mask length: %i\n", e->size);
		print_hex_dump_debug(
			KBUILD_MODNAME ": register: magic[decoded]: ",
			DUMP_PREFIX_NONE, 16, 1, e->magic, e->size, true);
		if (e->mask)
			print_hex_dump_debug(
				KBUILD_MODNAME ": register:  mask[decoded]: ",
				DUMP_PREFIX_NONE, 16, 1, e->mask, e->size, true);
	} else {
		/* Handle the 'E' (extension) format. */

		/* Skip the 'offset' field. */
		p = strchr(p, del);
		if (!p)
			goto einval;
		*p++ = '\0';

		/* Parse the 'magic' field. */
		e->magic = p;
		p = strchr(p, del);
		if (!p)
			goto einval;
		*p++ = '\0';
		if (!e->magic[0] || strchr(e->magic, '/'))
			goto einval;
		pr_debug("register: extension: {%s}\n", e->magic);

		/* Skip the 'mask' field. */
		p = strchr(p, del);
		if (!p)
			goto einval;
		*p++ = '\0';
	}

	/* Parse the 'interpreter' field. */
	e->interpreter = p;
	p = strchr(p, del);
	if (!p)
		goto einval;
	*p++ = '\0';
	if (!e->interpreter[0])
		goto einval;
	pr_debug("register: interpreter: {%s}\n", e->interpreter);

	/* Parse the 'flags' field. */
	p = check_special_flags(p, e);
	if (*p == '\n')
		p++;
	if (p != buf + count)
		goto einval;

	return e;

out:
	return ERR_PTR(err);

efault:
	kfree(e);
	return ERR_PTR(-EFAULT);
einval:
	kfree(e);
	return ERR_PTR(-EINVAL);
}

/* Commands accepted by the /status and /<entry> files. */
enum bm_command {
	BM_CMD_IGNORE,	/* empty write */
	BM_CMD_DISABLE,	/* "0" */
	BM_CMD_ENABLE,	/* "1" */
	BM_CMD_REMOVE,	/* "-1" */
};

/*
 * Parse what userspace wrote to /status or an entry file: '1' enables,
 * '0' disables and '-1' removes the entry or all entries.
 */
static int parse_command(const char __user *buffer, size_t count)
{
	char s[4];

	if (count > 3)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	if (!count)
		return BM_CMD_IGNORE;
	if (s[count - 1] == '\n')
		count--;
	if (count == 1 && s[0] == '0')
		return BM_CMD_DISABLE;
	if (count == 1 && s[0] == '1')
		return BM_CMD_ENABLE;
	if (count == 2 && s[0] == '-' && s[1] == '1')
		return BM_CMD_REMOVE;
	return -EINVAL;
}

/* generic stuff */

static void bm_seq_hex(struct seq_file *m, const u8 *data, int size)
{
	for (int i = 0; i < size; i++)
		seq_printf(m, "%02x", data[i]);
}

static int bm_entry_show(struct seq_file *m, void *unused)
{
	struct binfmt_misc_entry *e = m->private;

	if (test_bit(MISC_FMT_ENABLED_BIT, &e->flags))
		seq_puts(m, "enabled\n");
	else
		seq_puts(m, "disabled\n");

	seq_printf(m, "interpreter %s\n", e->interpreter);

	/* print the special flags */
	seq_puts(m, "flags: ");
	if (e->flags & MISC_FMT_PRESERVE_ARGV0)
		seq_putc(m, 'P');
	if (e->flags & MISC_FMT_OPEN_BINARY)
		seq_putc(m, 'O');
	if (e->flags & MISC_FMT_CREDENTIALS)
		seq_putc(m, 'C');
	if (e->flags & MISC_FMT_OPEN_FILE)
		seq_putc(m, 'F');
	seq_putc(m, '\n');

	if (!test_bit(MISC_FMT_MAGIC_BIT, &e->flags)) {
		seq_printf(m, "extension .%s\n", e->magic);
	} else {
		seq_printf(m, "offset %i\nmagic ", e->offset);
		bm_seq_hex(m, e->magic, e->size);
		if (e->mask) {
			seq_puts(m, "\nmask ");
			bm_seq_hex(m, e->mask, e->size);
		}
		seq_putc(m, '\n');
	}
	return 0;
}

static struct inode *bm_get_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mode = mode;
		simple_inode_init_ts(inode);
	}
	return inode;
}

/**
 * i_binfmt_misc - retrieve struct binfmt_misc from a binfmt_misc inode
 * @inode: inode of the relevant binfmt_misc instance
 *
 * This helper retrieves struct binfmt_misc from a binfmt_misc inode. This can
 * be done without any memory barriers because we are guaranteed that
 * user_ns->binfmt_misc is fully initialized. It was fully initialized when the
 * binfmt_misc mount was first created.
 *
 * Return: struct binfmt_misc of the relevant binfmt_misc instance
 */
static struct binfmt_misc *i_binfmt_misc(struct inode *inode)
{
	return inode->i_sb->s_user_ns->binfmt_misc;
}

/**
 * bm_evict_inode - cleanup data associated with @inode
 * @inode: inode to which the data is attached
 *
 * Cleanup the binary type handler data associated with @inode if a binary type
 * entry is removed or the filesystem is unmounted and the super block is
 * shutdown.
 *
 * If the ->evict call was not caused by a super block shutdown but by a write
 * to remove the entry or all entries via bm_{entry,status}_write() the entry
 * will have already been removed from the list. We keep the hlist_unhashed()
 * check to make that explicit.
*/
static void bm_evict_inode(struct inode *inode)
{
	struct binfmt_misc_entry *e = inode->i_private;

	clear_inode(inode);

	if (e) {
		struct binfmt_misc *misc;

		misc = i_binfmt_misc(inode);
		spin_lock(&misc->entries_lock);
		if (!hlist_unhashed(&e->node))
			hlist_del_init_rcu(&e->node);
		spin_unlock(&misc->entries_lock);
		put_binfmt_handler(e);
	}
}

/**
 * remove_binfmt_handler - remove a binary type handler
 * @misc: handle to binfmt_misc instance
 * @e: binary type handler to remove
 *
 * Remove a binary type handler from the list of binary type handlers and
 * remove its associated dentry.
 *
 * Adding and removing entries via bm_{entry,register,status}_write()
 * happens under the exclusively held inode lock of the root dentry keeping
 * the list stable for writers. load_misc_binary() walks it concurrently
 * under RCU. The entries_lock is only held around the actual unlink to
 * serialize against bm_evict_inode() which unlinks entries during umount
 * without holding the root inode lock.
 *
 * In the future, we might want to think about adding a proper ->unlink()
 * method to binfmt_misc instead of forcing callers to use writes to files
 * in order to delete binary type handlers. But it has worked for so long
 * that it's not a pressing issue.
 */
static void remove_binfmt_handler(struct binfmt_misc *misc,
				  struct binfmt_misc_entry *e)
{
	spin_lock(&misc->entries_lock);
	hlist_del_init_rcu(&e->node);
	spin_unlock(&misc->entries_lock);
	locked_recursive_removal(e->dentry, NULL);
}

/* Remove @e unless a concurrent write already unlinked it. */
static void bm_remove_entry(struct binfmt_misc_entry *e, struct super_block *sb)
{
	struct inode *root = d_inode(sb->s_root);

	inode_lock_nested(root, I_MUTEX_PARENT);
	if (!hlist_unhashed(&e->node))
		remove_binfmt_handler(i_binfmt_misc(root), e);
	inode_unlock(root);
}

/* Remove all entries of the binfmt_misc instance @misc belonging to @sb. */
static void bm_remove_all_entries(struct binfmt_misc *misc,
				  struct super_block *sb)
{
	struct inode *root = d_inode(sb->s_root);
	struct binfmt_misc_entry *e;
	struct hlist_node *next;

	inode_lock_nested(root, I_MUTEX_PARENT);
	hlist_for_each_entry_safe(e, next, &misc->entries, node)
		remove_binfmt_handler(misc, e);
	inode_unlock(root);
}

/* /<entry> */

static int bm_entry_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, bm_entry_show, inode->i_private);
	if (ret)
		return ret;

	/* seq_open() clears FMODE_PWRITE, bm_entry_write() takes any offset */
	if (file->f_mode & FMODE_WRITE)
		file->f_mode |= FMODE_PWRITE;
	return 0;
}

static ssize_t bm_entry_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct binfmt_misc_entry *e = inode->i_private;
	int res = parse_command(buffer, count);

	switch (res) {
	case BM_CMD_DISABLE:
		clear_bit(MISC_FMT_ENABLED_BIT, &e->flags);
		break;
	case BM_CMD_ENABLE:
		set_bit(MISC_FMT_ENABLED_BIT, &e->flags);
		break;
	case BM_CMD_REMOVE:
		bm_remove_entry(e, inode->i_sb);
		break;
	default:
		return res;
	}

	return count;
}

static const struct file_operations bm_entry_operations = {
	.open		= bm_entry_open,
	.read		= seq_read,
	.write		= bm_entry_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* /register */

/* add to filesystem */
static int add_entry(struct binfmt_misc_entry *e, struct super_block *sb)
{
	struct dentry *dentry = simple_start_creating(sb->s_root, e->name);
	struct inode *inode;
	struct binfmt_misc *misc;

	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	inode = bm_get_inode(sb, S_IFREG | 0644);
	if (unlikely(!inode)) {
		simple_done_creating(dentry);
		return -ENOMEM;
	}

	refcount_set(&e->users, 1);
	e->dentry = dentry;
	inode->i_private = e;
	inode->i_fop = &bm_entry_operations;

	d_make_persistent(dentry, inode);
	misc = i_binfmt_misc(inode);
	spin_lock(&misc->entries_lock);
	hlist_add_head_rcu(&e->node, &misc->entries);
	spin_unlock(&misc->entries_lock);
	simple_done_creating(dentry);
	return 0;
}

static ssize_t bm_register_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *ppos)
{
	struct binfmt_misc_entry *e;
	struct super_block *sb = file_inode(file)->i_sb;
	int err = 0;
	struct file *f = NULL;

	e = create_entry(buffer, count);

	if (IS_ERR(e))
		return PTR_ERR(e);

	if (e->flags & MISC_FMT_OPEN_FILE) {
		/*
		 * Now that we support unprivileged binfmt_misc mounts make
		 * sure we use the credentials that the register @file was
		 * opened with to also open the interpreter. Before that this
		 * didn't matter much as only a privileged process could open
		 * the register file.
		 */
		scoped_with_creds(file->f_cred)
			f = open_exec(e->interpreter);
		if (IS_ERR(f)) {
			pr_notice("register: failed to install interpreter file %s\n",
				 e->interpreter);
			kfree(e);
			return PTR_ERR(f);
		}
		e->interp_file = f;
	}

	err = add_entry(e, sb);
	if (err) {
		if (f) {
			exe_file_allow_write_access(f);
			filp_close(f, NULL);
		}
		kfree(e);
		return err;
	}
	return count;
}

static const struct file_operations bm_register_operations = {
	.write		= bm_register_write,
	.llseek		= noop_llseek,
};

/* /status */

static ssize_t
bm_status_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	struct binfmt_misc *misc;
	char *s;

	misc = i_binfmt_misc(file_inode(file));
	s = READ_ONCE(misc->enabled) ? "enabled\n" : "disabled\n";
	return simple_read_from_buffer(buf, nbytes, ppos, s, strlen(s));
}

static ssize_t bm_status_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *ppos)
{
	struct binfmt_misc *misc;
	int res = parse_command(buffer, count);

	misc = i_binfmt_misc(file_inode(file));
	switch (res) {
	case BM_CMD_DISABLE:
		WRITE_ONCE(misc->enabled, false);
		break;
	case BM_CMD_ENABLE:
		WRITE_ONCE(misc->enabled, true);
		break;
	case BM_CMD_REMOVE:
		bm_remove_all_entries(misc, file_inode(file)->i_sb);
		break;
	default:
		return res;
	}

	return count;
}

static const struct file_operations bm_status_operations = {
	.read		= bm_status_read,
	.write		= bm_status_write,
	.llseek		= default_llseek,
};

/* Superblock handling */

static const struct super_operations s_ops = {
	.statfs		= simple_statfs,
	.evict_inode	= bm_evict_inode,
};

static int bm_fill_super(struct super_block *sb, struct fs_context *fc)
{
	int err;
	struct user_namespace *user_ns = sb->s_user_ns;
	struct binfmt_misc *misc;
	static const struct tree_descr bm_files[] = {
		[2] = {"status", &bm_status_operations, S_IWUSR|S_IRUGO},
		[3] = {"register", &bm_register_operations, S_IWUSR},
		/* last one */ {""}
	};

	if (WARN_ON(user_ns != current_user_ns()))
		return -EINVAL;

	/* Never exec off this instance and never let anything stack on it. */
	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV;
	sb->s_stack_depth = FILESYSTEM_MAX_STACK_DEPTH;

	/*
	 * Lazily allocate a new binfmt_misc instance for this namespace, i.e.
	 * do it here during the first mount of binfmt_misc. We don't need to
	 * waste memory for every user namespace allocation. It's likely much
	 * more common to not mount a separate binfmt_misc instance than it is
	 * to mount one.
	 *
	 * While multiple superblocks can exist they are keyed by userns in
	 * s_fs_info for binfmt_misc. Hence, the vfs guarantees that
	 * bm_fill_super() is called exactly once whenever a binfmt_misc
	 * superblock for a userns is created. This in turn lets us conclude
	 * that when a binfmt_misc superblock is created for the first time for
	 * a userns there's no one racing us. Therefore we don't need any
	 * barriers when we dereference binfmt_misc.
	 */
	misc = user_ns->binfmt_misc;
	if (!misc) {
		/*
		 * If it turns out that most user namespaces actually want to
		 * register their own binary type handler and therefore all
		 * create their own separate binfmt_misc mounts we should
		 * consider turning this into a kmem cache.
		 */
		misc = kzalloc_obj(struct binfmt_misc);
		if (!misc)
			return -ENOMEM;

		INIT_HLIST_HEAD(&misc->entries);
		spin_lock_init(&misc->entries_lock);

		/* Pairs with smp_load_acquire() in current_binfmt_misc(). */
		smp_store_release(&user_ns->binfmt_misc, misc);
	}

	/*
	 * When the binfmt_misc superblock for this userns is shutdown
	 * ->enabled might have been set to false and we don't reinitialize
	 * ->enabled again during shutdown as someone might already be mounting
	 * binfmt_misc again. It also would be pointless since by then we know
	 * that the binary type list for this binfmt_misc mount is empty making
	 * load_misc_binary() return -ENOEXEC independent of whether ->enabled
	 * is true. Instead, if someone mounts binfmt_misc for the first time or
	 * again we simply reset ->enabled to true.
	 */
	WRITE_ONCE(misc->enabled, true);

	err = simple_fill_super(sb, BINFMTFS_MAGIC, bm_files);
	if (!err)
		sb->s_op = &s_ops;
	return err;
}

static void bm_free(struct fs_context *fc)
{
	if (fc->s_fs_info)
		put_user_ns(fc->s_fs_info);
}

static int bm_get_tree(struct fs_context *fc)
{
	return get_tree_keyed(fc, bm_fill_super, get_user_ns(fc->user_ns));
}

static const struct fs_context_operations bm_context_ops = {
	.free		= bm_free,
	.get_tree	= bm_get_tree,
};

static void bm_kill_sb(struct super_block *sb)
{
	struct user_namespace *user_ns = sb->s_fs_info;

	kill_anon_super(sb);
	put_user_ns(user_ns);
}

static int bm_init_fs_context(struct fs_context *fc)
{
	fc->ops = &bm_context_ops;
	return 0;
}

static struct linux_binfmt misc_format = {
	.module = THIS_MODULE,
	.load_binary = load_misc_binary,
};

static struct file_system_type bm_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "binfmt_misc",
	.init_fs_context = bm_init_fs_context,
	.fs_flags	= FS_USERNS_MOUNT,
	.kill_sb	= bm_kill_sb,
};
MODULE_ALIAS_FS("binfmt_misc");

static int __init init_misc_binfmt(void)
{
	int err = register_filesystem(&bm_fs_type);
	if (!err)
		insert_binfmt(&misc_format);
	return err;
}

static void __exit exit_misc_binfmt(void)
{
	unregister_binfmt(&misc_format);
	unregister_filesystem(&bm_fs_type);
}

core_initcall(init_misc_binfmt);
module_exit(exit_misc_binfmt);
MODULE_DESCRIPTION("Kernel support for miscellaneous binaries");
MODULE_LICENSE("GPL");
