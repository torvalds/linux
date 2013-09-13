#ifndef __GADGET_CONFIGFS__
#define __GADGET_CONFIGFS__

#include <linux/configfs.h>

int check_user_usb_string(const char *name,
		struct usb_gadget_strings *stringtab_dev);

#define GS_STRINGS_W(__struct, __name)	\
	static ssize_t __struct##_##__name##_store(struct __struct *gs, \
		const char *page, size_t len)		\
{							\
	int ret;					\
							\
	ret = usb_string_copy(page, &gs->__name);	\
	if (ret)					\
		return ret;				\
	return len;					\
}

#define GS_STRINGS_R(__struct, __name)	\
	static ssize_t __struct##_##__name##_show(struct __struct *gs, \
			char *page)	\
{	\
	return sprintf(page, "%s\n", gs->__name ?: "");	\
}

#define GS_STRING_ITEM_ATTR(struct_name, name)	\
	static struct struct_name##_attribute struct_name##_##name = \
		__CONFIGFS_ATTR(name,  S_IRUGO | S_IWUSR,		\
				struct_name##_##name##_show,		\
				struct_name##_##name##_store)

#define GS_STRINGS_RW(struct_name, _name)	\
	GS_STRINGS_R(struct_name, _name)	\
	GS_STRINGS_W(struct_name, _name)	\
	GS_STRING_ITEM_ATTR(struct_name, _name)

#define USB_CONFIG_STRING_RW_OPS(struct_in)				\
	CONFIGFS_ATTR_OPS(struct_in);					\
									\
static struct configfs_item_operations struct_in##_langid_item_ops = {	\
	.release                = struct_in##_attr_release,		\
	.show_attribute         = struct_in##_attr_show,		\
	.store_attribute        = struct_in##_attr_store,		\
};									\
									\
static struct config_item_type struct_in##_langid_type = {		\
	.ct_item_ops	= &struct_in##_langid_item_ops,			\
	.ct_attrs	= struct_in##_langid_attrs,			\
	.ct_owner	= THIS_MODULE,					\
}

#define USB_CONFIG_STRINGS_LANG(struct_in, struct_member)	\
	static struct config_group *struct_in##_strings_make(		\
			struct config_group *group,			\
			const char *name)				\
	{								\
	struct struct_member *gi;					\
	struct struct_in *gs;						\
	struct struct_in *new;						\
	int langs = 0;							\
	int ret;							\
									\
	new = kzalloc(sizeof(*new), GFP_KERNEL);			\
	if (!new)							\
		return ERR_PTR(-ENOMEM);				\
									\
	ret = check_user_usb_string(name, &new->stringtab_dev);		\
	if (ret)							\
		goto err;						\
	config_group_init_type_name(&new->group, name,			\
			&struct_in##_langid_type);			\
									\
	gi = container_of(group, struct struct_member, strings_group);	\
	ret = -EEXIST;							\
	list_for_each_entry(gs, &gi->string_list, list) {		\
		if (gs->stringtab_dev.language == new->stringtab_dev.language) \
			goto err;					\
		langs++;						\
	}								\
	ret = -EOVERFLOW;						\
	if (langs >= MAX_USB_STRING_LANGS)				\
		goto err;						\
									\
	list_add_tail(&new->list, &gi->string_list);			\
	return &new->group;						\
err:									\
	kfree(new);							\
	return ERR_PTR(ret);						\
}									\
									\
static void struct_in##_strings_drop(					\
		struct config_group *group,				\
		struct config_item *item)				\
{									\
	config_item_put(item);						\
}									\
									\
static struct configfs_group_operations struct_in##_strings_ops = {	\
	.make_group     = &struct_in##_strings_make,			\
	.drop_item      = &struct_in##_strings_drop,			\
};									\
									\
static struct config_item_type struct_in##_strings_type = {		\
	.ct_group_ops   = &struct_in##_strings_ops,			\
	.ct_owner       = THIS_MODULE,					\
}

#endif
