/*
 * kobject.c - library routines for handling generic kernel objects
 *
 * Copyright (c) 2002-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (c) 2006-2007 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2006-2007 Novell Inc.
 *
 * This file is released under the GPLv2.
 *
 *
 * Please see the file Documentation/kobject.txt for critical information
 * about using the kobject interface.
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/slab.h>

/**
 *	populate_dir - populate directory with attributes.
 *	@kobj:	object we're working on.
 *
 *	Most subsystems have a set of default attributes that 
 *	are associated with an object that registers with them.
 *	This is a helper called during object registration that 
 *	loops through the default attributes of the subsystem 
 *	and creates attributes files for them in sysfs.
 *
 */

static int populate_dir(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct attribute * attr;
	int error = 0;
	int i;
	
	if (t && t->default_attrs) {
		for (i = 0; (attr = t->default_attrs[i]) != NULL; i++) {
			if ((error = sysfs_create_file(kobj,attr)))
				break;
		}
	}
	return error;
}

static int create_dir(struct kobject * kobj)
{
	int error = 0;
	if (kobject_name(kobj)) {
		error = sysfs_create_dir(kobj);
		if (!error) {
			if ((error = populate_dir(kobj)))
				sysfs_remove_dir(kobj);
		}
	}
	return error;
}

static inline struct kobject * to_kobj(struct list_head * entry)
{
	return container_of(entry,struct kobject,entry);
}

static int get_kobj_path_length(struct kobject *kobj)
{
	int length = 1;
	struct kobject * parent = kobj;

	/* walk up the ancestors until we hit the one pointing to the 
	 * root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		if (kobject_name(parent) == NULL)
			return 0;
		length += strlen(kobject_name(parent)) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

static void fill_kobj_path(struct kobject *kobj, char *path, int length)
{
	struct kobject * parent;

	--length;
	for (parent = kobj; parent; parent = parent->parent) {
		int cur = strlen(kobject_name(parent));
		/* back up enough to print this name with '/' */
		length -= cur;
		strncpy (path + length, kobject_name(parent), cur);
		*(path + --length) = '/';
	}

	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);
}

/**
 * kobject_get_path - generate and return the path associated with a given kobj and kset pair.
 *
 * @kobj:	kobject in question, with which to build the path
 * @gfp_mask:	the allocation type used to allocate the path
 *
 * The result must be freed by the caller with kfree().
 */
char *kobject_get_path(struct kobject *kobj, gfp_t gfp_mask)
{
	char *path;
	int len;

	len = get_kobj_path_length(kobj);
	if (len == 0)
		return NULL;
	path = kzalloc(len, gfp_mask);
	if (!path)
		return NULL;
	fill_kobj_path(kobj, path, len);

	return path;
}
EXPORT_SYMBOL_GPL(kobject_get_path);

/**
 *	kobject_init - initialize object.
 *	@kobj:	object in question.
 */
void kobject_init(struct kobject * kobj)
{
	if (!kobj)
		return;
	kref_init(&kobj->kref);
	INIT_LIST_HEAD(&kobj->entry);
	kobj->kset = kset_get(kobj->kset);
}


/**
 *	unlink - remove kobject from kset list.
 *	@kobj:	kobject.
 *
 *	Remove the kobject from the kset list and decrement
 *	its parent's refcount.
 *	This is separated out, so we can use it in both 
 *	kobject_del() and kobject_add() on error.
 */

static void unlink(struct kobject * kobj)
{
	if (kobj->kset) {
		spin_lock(&kobj->kset->list_lock);
		list_del_init(&kobj->entry);
		spin_unlock(&kobj->kset->list_lock);
	}
	kobject_put(kobj);
}

/**
 *	kobject_add - add an object to the hierarchy.
 *	@kobj:	object.
 */

int kobject_add(struct kobject * kobj)
{
	int error = 0;
	struct kobject * parent;

	if (!(kobj = kobject_get(kobj)))
		return -ENOENT;
	if (!kobj->k_name)
		kobject_set_name(kobj, "NO_NAME");
	if (!*kobj->k_name) {
		pr_debug("kobject attempted to be registered with no name!\n");
		WARN_ON(1);
		kobject_put(kobj);
		return -EINVAL;
	}
	parent = kobject_get(kobj->parent);

	pr_debug("kobject %s: registering. parent: %s, set: %s\n",
		 kobject_name(kobj), parent ? kobject_name(parent) : "<NULL>", 
		 kobj->kset ? kobject_name(&kobj->kset->kobj) : "<NULL>" );

	if (kobj->kset) {
		spin_lock(&kobj->kset->list_lock);

		if (!parent)
			parent = kobject_get(&kobj->kset->kobj);

		list_add_tail(&kobj->entry,&kobj->kset->list);
		spin_unlock(&kobj->kset->list_lock);
		kobj->parent = parent;
	}

	error = create_dir(kobj);
	if (error) {
		/* unlink does the kobject_put() for us */
		unlink(kobj);
		kobject_put(parent);

		/* be noisy on error issues */
		if (error == -EEXIST)
			printk(KERN_ERR "kobject_add failed for %s with "
			       "-EEXIST, don't try to register things with "
			       "the same name in the same directory.\n",
			       kobject_name(kobj));
		else
			printk(KERN_ERR "kobject_add failed for %s (%d)\n",
			       kobject_name(kobj), error);
		dump_stack();
	}

	return error;
}

/**
 *	kobject_register - initialize and add an object.
 *	@kobj:	object in question.
 */

int kobject_register(struct kobject * kobj)
{
	int error = -EINVAL;
	if (kobj) {
		kobject_init(kobj);
		error = kobject_add(kobj);
		if (!error)
			kobject_uevent(kobj, KOBJ_ADD);
	}
	return error;
}


/**
 *	kobject_set_name - Set the name of an object
 *	@kobj:	object.
 *	@fmt:	format string used to build the name
 *
 *	If strlen(name) >= KOBJ_NAME_LEN, then use a dynamically allocated
 *	string that @kobj->k_name points to. Otherwise, use the static 
 *	@kobj->name array.
 */
int kobject_set_name(struct kobject * kobj, const char * fmt, ...)
{
	int error = 0;
	int limit;
	int need;
	va_list args;
	char *name;

	/* find out how big a buffer we need */
	name = kmalloc(1024, GFP_KERNEL);
	if (!name) {
		error = -ENOMEM;
		goto done;
	}
	va_start(args, fmt);
	need = vsnprintf(name, 1024, fmt, args);
	va_end(args);
	kfree(name);

	/* Allocate the new space and copy the string in */
	limit = need + 1;
	name = kmalloc(limit, GFP_KERNEL);
	if (!name) {
		error = -ENOMEM;
		goto done;
	}
	va_start(args, fmt);
	need = vsnprintf(name, limit, fmt, args);
	va_end(args);

	/* something wrong with the string we copied? */
	if (need >= limit) {
		kfree(name);
		error = -EFAULT;
		goto done;
	}

	/* Free the old name, if necessary. */
	kfree(kobj->k_name);

	/* Now, set the new name */
	kobj->k_name = name;
done:
	return error;
}
EXPORT_SYMBOL(kobject_set_name);

/**
 *	kobject_rename - change the name of an object
 *	@kobj:	object in question.
 *	@new_name: object's new name
 */

int kobject_rename(struct kobject * kobj, const char *new_name)
{
	int error = 0;
	const char *devpath = NULL;
	char *devpath_string = NULL;
	char *envp[2];

	kobj = kobject_get(kobj);
	if (!kobj)
		return -EINVAL;
	if (!kobj->parent)
		return -EINVAL;

	/* see if this name is already in use */
	if (kobj->kset) {
		struct kobject *temp_kobj;
		temp_kobj = kset_find_obj(kobj->kset, new_name);
		if (temp_kobj) {
			printk(KERN_WARNING "kobject '%s' cannot be renamed "
			       "to '%s' as '%s' is already in existence.\n",
			       kobject_name(kobj), new_name, new_name);
			kobject_put(temp_kobj);
			return -EINVAL;
		}
	}

	devpath = kobject_get_path(kobj, GFP_KERNEL);
	if (!devpath) {
		error = -ENOMEM;
		goto out;
	}
	devpath_string = kmalloc(strlen(devpath) + 15, GFP_KERNEL);
	if (!devpath_string) {
		error = -ENOMEM;
		goto out;
	}
	sprintf(devpath_string, "DEVPATH_OLD=%s", devpath);
	envp[0] = devpath_string;
	envp[1] = NULL;
	/* Note : if we want to send the new name alone, not the full path,
	 * we could probably use kobject_name(kobj); */

	error = sysfs_rename_dir(kobj, new_name);

	/* This function is mostly/only used for network interface.
	 * Some hotplug package track interfaces by their name and
	 * therefore want to know when the name is changed by the user. */
	if (!error)
		kobject_uevent_env(kobj, KOBJ_MOVE, envp);

out:
	kfree(devpath_string);
	kfree(devpath);
	kobject_put(kobj);

	return error;
}

/**
 *	kobject_move - move object to another parent
 *	@kobj:	object in question.
 *	@new_parent: object's new parent (can be NULL)
 */

int kobject_move(struct kobject *kobj, struct kobject *new_parent)
{
	int error;
	struct kobject *old_parent;
	const char *devpath = NULL;
	char *devpath_string = NULL;
	char *envp[2];

	kobj = kobject_get(kobj);
	if (!kobj)
		return -EINVAL;
	new_parent = kobject_get(new_parent);
	if (!new_parent) {
		if (kobj->kset)
			new_parent = kobject_get(&kobj->kset->kobj);
	}
	/* old object path */
	devpath = kobject_get_path(kobj, GFP_KERNEL);
	if (!devpath) {
		error = -ENOMEM;
		goto out;
	}
	devpath_string = kmalloc(strlen(devpath) + 15, GFP_KERNEL);
	if (!devpath_string) {
		error = -ENOMEM;
		goto out;
	}
	sprintf(devpath_string, "DEVPATH_OLD=%s", devpath);
	envp[0] = devpath_string;
	envp[1] = NULL;
	error = sysfs_move_dir(kobj, new_parent);
	if (error)
		goto out;
	old_parent = kobj->parent;
	kobj->parent = new_parent;
	new_parent = NULL;
	kobject_put(old_parent);
	kobject_uevent_env(kobj, KOBJ_MOVE, envp);
out:
	kobject_put(new_parent);
	kobject_put(kobj);
	kfree(devpath_string);
	kfree(devpath);
	return error;
}

/**
 *	kobject_del - unlink kobject from hierarchy.
 * 	@kobj:	object.
 */

void kobject_del(struct kobject * kobj)
{
	if (!kobj)
		return;
	sysfs_remove_dir(kobj);
	unlink(kobj);
}

/**
 *	kobject_unregister - remove object from hierarchy and decrement refcount.
 *	@kobj:	object going away.
 */

void kobject_unregister(struct kobject * kobj)
{
	if (!kobj)
		return;
	pr_debug("kobject %s: unregistering\n",kobject_name(kobj));
	kobject_uevent(kobj, KOBJ_REMOVE);
	kobject_del(kobj);
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

struct kobject * kobject_get(struct kobject * kobj)
{
	if (kobj)
		kref_get(&kobj->kref);
	return kobj;
}

/**
 *	kobject_cleanup - free kobject resources. 
 *	@kobj:	object.
 */

void kobject_cleanup(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct kset * s = kobj->kset;
	struct kobject * parent = kobj->parent;
	const char *name = kobj->k_name;

	pr_debug("kobject %s: cleaning up\n",kobject_name(kobj));
	if (t && t->release) {
		t->release(kobj);
		/* If we have a release function, we can guess that this was
		 * not a statically allocated kobject, so we should be safe to
		 * free the name */
		kfree(name);
	}
	if (s)
		kset_put(s);
	kobject_put(parent);
}

static void kobject_release(struct kref *kref)
{
	kobject_cleanup(container_of(kref, struct kobject, kref));
}

/**
 *	kobject_put - decrement refcount for object.
 *	@kobj:	object.
 *
 *	Decrement the refcount, and if 0, call kobject_cleanup().
 */
void kobject_put(struct kobject * kobj)
{
	if (kobj)
		kref_put(&kobj->kref, kobject_release);
}


static void dir_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type dir_ktype = {
	.release	= dir_release,
	.sysfs_ops	= NULL,
	.default_attrs	= NULL,
};

/**
 *	kobject_kset_add_dir - add sub directory of object.
 *	@kset:		kset the directory is belongs to.
 *	@parent:	object in which a directory is created.
 *	@name:	directory name.
 *
 *	Add a plain directory object as child of given object.
 */
struct kobject *kobject_kset_add_dir(struct kset *kset,
				     struct kobject *parent, const char *name)
{
	struct kobject *k;
	int ret;

	if (!parent)
		return NULL;

	k = kzalloc(sizeof(*k), GFP_KERNEL);
	if (!k)
		return NULL;

	k->kset = kset;
	k->parent = parent;
	k->ktype = &dir_ktype;
	kobject_set_name(k, name);
	ret = kobject_register(k);
	if (ret < 0) {
		printk(KERN_WARNING "%s: kobject_register error: %d\n",
			__func__, ret);
		kobject_del(k);
		return NULL;
	}

	return k;
}

/**
 *	kobject_add_dir - add sub directory of object.
 *	@parent:	object in which a directory is created.
 *	@name:	directory name.
 *
 *	Add a plain directory object as child of given object.
 */
struct kobject *kobject_add_dir(struct kobject *parent, const char *name)
{
	return kobject_kset_add_dir(NULL, parent, name);
}

/**
 *	kset_init - initialize a kset for use
 *	@k:	kset 
 */

void kset_init(struct kset * k)
{
	kobject_init(&k->kobj);
	INIT_LIST_HEAD(&k->list);
	spin_lock_init(&k->list_lock);
}


/**
 *	kset_add - add a kset object to the hierarchy.
 *	@k:	kset.
 */

int kset_add(struct kset * k)
{
	return kobject_add(&k->kobj);
}


/**
 *	kset_register - initialize and add a kset.
 *	@k:	kset.
 */

int kset_register(struct kset * k)
{
	int err;

	if (!k)
		return -EINVAL;

	kset_init(k);
	err = kset_add(k);
	if (err)
		return err;
	kobject_uevent(&k->kobj, KOBJ_ADD);
	return 0;
}


/**
 *	kset_unregister - remove a kset.
 *	@k:	kset.
 */

void kset_unregister(struct kset * k)
{
	if (!k)
		return;
	kobject_unregister(&k->kobj);
}


/**
 *	kset_find_obj - search for object in kset.
 *	@kset:	kset we're looking in.
 *	@name:	object's name.
 *
 *	Lock kset via @kset->subsys, and iterate over @kset->list,
 *	looking for a matching kobject. If matching object is found
 *	take a reference and return the object.
 */

struct kobject * kset_find_obj(struct kset * kset, const char * name)
{
	struct list_head * entry;
	struct kobject * ret = NULL;

	spin_lock(&kset->list_lock);
	list_for_each(entry,&kset->list) {
		struct kobject * k = to_kobj(entry);
		if (kobject_name(k) && !strcmp(kobject_name(k),name)) {
			ret = kobject_get(k);
			break;
		}
	}
	spin_unlock(&kset->list_lock);
	return ret;
}

int subsystem_register(struct kset *s)
{
	return kset_register(s);
}

void subsystem_unregister(struct kset *s)
{
	kset_unregister(s);
}

/**
 *	subsystem_create_file - export sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	subsystem attribute descriptor.
 */

int subsys_create_file(struct kset *s, struct subsys_attribute *a)
{
	int error = 0;

	if (!s || !a)
		return -EINVAL;

	if (kset_get(s)) {
		error = sysfs_create_file(&s->kobj, &a->attr);
		kset_put(s);
	}
	return error;
}

EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);
EXPORT_SYMBOL(kobject_add);
EXPORT_SYMBOL(kobject_del);

EXPORT_SYMBOL(kset_register);
EXPORT_SYMBOL(kset_unregister);

EXPORT_SYMBOL(subsystem_register);
EXPORT_SYMBOL(subsystem_unregister);
EXPORT_SYMBOL(subsys_create_file);
