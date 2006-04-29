/*
 * kobject.c - library routines for handling generic kernel objects
 *
 * Copyright (c) 2002-2003 Patrick Mochel <mochel@osdl.org>
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
 * kobject_get_path - generate and return the path associated with a given kobj
 * and kset pair.  The result must be freed by the caller with kfree().
 *
 * @kobj:	kobject in question, with which to build the path
 * @gfp_mask:	the allocation type used to allocate the path
 */
char *kobject_get_path(struct kobject *kobj, gfp_t gfp_mask)
{
	char *path;
	int len;

	len = get_kobj_path_length(kobj);
	if (len == 0)
		return NULL;
	path = kmalloc(len, gfp_mask);
	if (!path)
		return NULL;
	memset(path, 0x00, len);
	fill_kobj_path(kobj, path, len);

	return path;
}

/**
 *	kobject_init - initialize object.
 *	@kobj:	object in question.
 */
void kobject_init(struct kobject * kobj)
{
	kref_init(&kobj->kref);
	INIT_LIST_HEAD(&kobj->entry);
	init_waitqueue_head(&kobj->poll);
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
		kobj->k_name = kobj->name;
	if (!kobj->k_name) {
		pr_debug("kobject attempted to be registered with no name!\n");
		WARN_ON(1);
		return -EINVAL;
	}
	parent = kobject_get(kobj->parent);

	pr_debug("kobject %s: registering. parent: %s, set: %s\n",
		 kobject_name(kobj), parent ? kobject_name(parent) : "<NULL>", 
		 kobj->kset ? kobj->kset->kobj.name : "<NULL>" );

	if (kobj->kset) {
		spin_lock(&kobj->kset->list_lock);

		if (!parent)
			parent = kobject_get(&kobj->kset->kobj);

		list_add_tail(&kobj->entry,&kobj->kset->list);
		spin_unlock(&kobj->kset->list_lock);
	}
	kobj->parent = parent;

	error = create_dir(kobj);
	if (error) {
		/* unlink does the kobject_put() for us */
		unlink(kobj);
		if (parent)
			kobject_put(parent);

		/* be noisy on error issues */
		if (error == -EEXIST)
			printk("kobject_add failed for %s with -EEXIST, "
			       "don't try to register things with the "
			       "same name in the same directory.\n",
			       kobject_name(kobj));
		else
			printk("kobject_add failed for %s (%d)\n",
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
	int limit = KOBJ_NAME_LEN;
	int need;
	va_list args;
	char * name;

	/* 
	 * First, try the static array 
	 */
	va_start(args,fmt);
	need = vsnprintf(kobj->name,limit,fmt,args);
	va_end(args);
	if (need < limit) 
		name = kobj->name;
	else {
		/* 
		 * Need more space? Allocate it and try again 
		 */
		limit = need + 1;
		name = kmalloc(limit,GFP_KERNEL);
		if (!name) {
			error = -ENOMEM;
			goto Done;
		}
		va_start(args,fmt);
		need = vsnprintf(name,limit,fmt,args);
		va_end(args);

		/* Still? Give up. */
		if (need >= limit) {
			kfree(name);
			error = -EFAULT;
			goto Done;
		}
	}

	/* Free the old name, if necessary. */
	if (kobj->k_name && kobj->k_name != kobj->name)
		kfree(kobj->k_name);

	/* Now, set the new name */
	kobj->k_name = name;
 Done:
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

	kobj = kobject_get(kobj);
	if (!kobj)
		return -EINVAL;
	error = sysfs_rename_dir(kobj, new_name);
	kobject_put(kobj);

	return error;
}

/**
 *	kobject_del - unlink kobject from hierarchy.
 * 	@kobj:	object.
 */

void kobject_del(struct kobject * kobj)
{
	sysfs_remove_dir(kobj);
	unlink(kobj);
}

/**
 *	kobject_unregister - remove object from hierarchy and decrement refcount.
 *	@kobj:	object going away.
 */

void kobject_unregister(struct kobject * kobj)
{
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

	pr_debug("kobject %s: cleaning up\n",kobject_name(kobj));
	if (kobj->k_name != kobj->name)
		kfree(kobj->k_name);
	kobj->k_name = NULL;
	if (t && t->release)
		t->release(kobj);
	if (s)
		kset_put(s);
	if (parent) 
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
 *	kobject_add_dir - add sub directory of object.
 *	@parent:	object in which a directory is created.
 *	@name:	directory name.
 *
 *	Add a plain directory object as child of given object.
 */
struct kobject *kobject_add_dir(struct kobject *parent, const char *name)
{
	struct kobject *k;

	if (!parent)
		return NULL;

	k = kzalloc(sizeof(*k), GFP_KERNEL);
	if (!k)
		return NULL;

	k->parent = parent;
	k->ktype = &dir_ktype;
	kobject_set_name(k, name);
	kobject_register(k);

	return k;
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
 *
 *	Simply, this adds the kset's embedded kobject to the 
 *	hierarchy. 
 *	We also try to make sure that the kset's embedded kobject
 *	has a parent before it is added. We only care if the embedded
 *	kobject is not part of a kset itself, since kobject_add()
 *	assigns a parent in that case. 
 *	If that is the case, and the kset has a controlling subsystem,
 *	then we set the kset's parent to be said subsystem. 
 */

int kset_add(struct kset * k)
{
	if (!k->kobj.parent && !k->kobj.kset && k->subsys)
		k->kobj.parent = &k->subsys->kset.kobj;

	return kobject_add(&k->kobj);
}


/**
 *	kset_register - initialize and add a kset.
 *	@k:	kset.
 */

int kset_register(struct kset * k)
{
	kset_init(k);
	return kset_add(k);
}


/**
 *	kset_unregister - remove a kset.
 *	@k:	kset.
 */

void kset_unregister(struct kset * k)
{
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


void subsystem_init(struct subsystem * s)
{
	init_rwsem(&s->rwsem);
	kset_init(&s->kset);
}

/**
 *	subsystem_register - register a subsystem.
 *	@s:	the subsystem we're registering.
 *
 *	Once we register the subsystem, we want to make sure that 
 *	the kset points back to this subsystem for correct usage of 
 *	the rwsem. 
 */

int subsystem_register(struct subsystem * s)
{
	int error;

	subsystem_init(s);
	pr_debug("subsystem %s: registering\n",s->kset.kobj.name);

	if (!(error = kset_add(&s->kset))) {
		if (!s->kset.subsys)
			s->kset.subsys = s;
	}
	return error;
}

void subsystem_unregister(struct subsystem * s)
{
	pr_debug("subsystem %s: unregistering\n",s->kset.kobj.name);
	kset_unregister(&s->kset);
}


/**
 *	subsystem_create_file - export sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	subsystem attribute descriptor.
 */

int subsys_create_file(struct subsystem * s, struct subsys_attribute * a)
{
	int error = 0;
	if (subsys_get(s)) {
		error = sysfs_create_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
	return error;
}


/**
 *	subsystem_remove_file - remove sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	attribute desciptor.
 */
#if 0
void subsys_remove_file(struct subsystem * s, struct subsys_attribute * a)
{
	if (subsys_get(s)) {
		sysfs_remove_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
}
#endif  /*  0  */

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
