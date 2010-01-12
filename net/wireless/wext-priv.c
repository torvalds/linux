/*
 * This file implement the Wireless Extensions priv API.
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 * Copyright	2009 Johannes Berg <johannes@sipsolutions.net>
 *
 * (As all part of the Linux kernel, this file is GPL)
 */
#include <linux/wireless.h>
#include <linux/netdevice.h>
#include <net/iw_handler.h>
#include <net/wext.h>

int iw_handler_get_private(struct net_device *		dev,
			   struct iw_request_info *	info,
			   union iwreq_data *		wrqu,
			   char *			extra)
{
	/* Check if the driver has something to export */
	if ((dev->wireless_handlers->num_private_args == 0) ||
	   (dev->wireless_handlers->private_args == NULL))
		return -EOPNOTSUPP;

	/* Check if there is enough buffer up there */
	if (wrqu->data.length < dev->wireless_handlers->num_private_args) {
		/* User space can't know in advance how large the buffer
		 * needs to be. Give it a hint, so that we can support
		 * any size buffer we want somewhat efficiently... */
		wrqu->data.length = dev->wireless_handlers->num_private_args;
		return -E2BIG;
	}

	/* Set the number of available ioctls. */
	wrqu->data.length = dev->wireless_handlers->num_private_args;

	/* Copy structure to the user buffer. */
	memcpy(extra, dev->wireless_handlers->private_args,
	       sizeof(struct iw_priv_args) * wrqu->data.length);

	return 0;
}

/* Size (in bytes) of the various private data types */
static const char iw_priv_type_size[] = {
	0,				/* IW_PRIV_TYPE_NONE */
	1,				/* IW_PRIV_TYPE_BYTE */
	1,				/* IW_PRIV_TYPE_CHAR */
	0,				/* Not defined */
	sizeof(__u32),			/* IW_PRIV_TYPE_INT */
	sizeof(struct iw_freq),		/* IW_PRIV_TYPE_FLOAT */
	sizeof(struct sockaddr),	/* IW_PRIV_TYPE_ADDR */
	0,				/* Not defined */
};

static int get_priv_size(__u16 args)
{
	int	num = args & IW_PRIV_SIZE_MASK;
	int	type = (args & IW_PRIV_TYPE_MASK) >> 12;

	return num * iw_priv_type_size[type];
}

static int adjust_priv_size(__u16 args, struct iw_point *iwp)
{
	int	num = iwp->length;
	int	max = args & IW_PRIV_SIZE_MASK;
	int	type = (args & IW_PRIV_TYPE_MASK) >> 12;

	/* Make sure the driver doesn't goof up */
	if (max < num)
		num = max;

	return num * iw_priv_type_size[type];
}

/*
 * Wrapper to call a private Wireless Extension handler.
 * We do various checks and also take care of moving data between
 * user space and kernel space.
 * It's not as nice and slimline as the standard wrapper. The cause
 * is struct iw_priv_args, which was not really designed for the
 * job we are going here.
 *
 * IMPORTANT : This function prevent to set and get data on the same
 * IOCTL and enforce the SET/GET convention. Not doing it would be
 * far too hairy...
 * If you need to set and get data at the same time, please don't use
 * a iw_handler but process it in your ioctl handler (i.e. use the
 * old driver API).
 */
static int get_priv_descr_and_size(struct net_device *dev, unsigned int cmd,
				   const struct iw_priv_args **descrp)
{
	const struct iw_priv_args *descr;
	int i, extra_size;

	descr = NULL;
	for (i = 0; i < dev->wireless_handlers->num_private_args; i++) {
		if (cmd == dev->wireless_handlers->private_args[i].cmd) {
			descr = &dev->wireless_handlers->private_args[i];
			break;
		}
	}

	extra_size = 0;
	if (descr) {
		if (IW_IS_SET(cmd)) {
			int	offset = 0;	/* For sub-ioctls */
			/* Check for sub-ioctl handler */
			if (descr->name[0] == '\0')
				/* Reserve one int for sub-ioctl index */
				offset = sizeof(__u32);

			/* Size of set arguments */
			extra_size = get_priv_size(descr->set_args);

			/* Does it fits in iwr ? */
			if ((descr->set_args & IW_PRIV_SIZE_FIXED) &&
			   ((extra_size + offset) <= IFNAMSIZ))
				extra_size = 0;
		} else {
			/* Size of get arguments */
			extra_size = get_priv_size(descr->get_args);

			/* Does it fits in iwr ? */
			if ((descr->get_args & IW_PRIV_SIZE_FIXED) &&
			   (extra_size <= IFNAMSIZ))
				extra_size = 0;
		}
	}
	*descrp = descr;
	return extra_size;
}

static int ioctl_private_iw_point(struct iw_point *iwp, unsigned int cmd,
				  const struct iw_priv_args *descr,
				  iw_handler handler, struct net_device *dev,
				  struct iw_request_info *info, int extra_size)
{
	char *extra;
	int err;

	/* Check what user space is giving us */
	if (IW_IS_SET(cmd)) {
		if (!iwp->pointer && iwp->length != 0)
			return -EFAULT;

		if (iwp->length > (descr->set_args & IW_PRIV_SIZE_MASK))
			return -E2BIG;
	} else if (!iwp->pointer)
		return -EFAULT;

	extra = kmalloc(extra_size, GFP_KERNEL);
	if (!extra)
		return -ENOMEM;

	/* If it is a SET, get all the extra data in here */
	if (IW_IS_SET(cmd) && (iwp->length != 0)) {
		if (copy_from_user(extra, iwp->pointer, extra_size)) {
			err = -EFAULT;
			goto out;
		}
	}

	/* Call the handler */
	err = handler(dev, info, (union iwreq_data *) iwp, extra);

	/* If we have something to return to the user */
	if (!err && IW_IS_GET(cmd)) {
		/* Adjust for the actual length if it's variable,
		 * avoid leaking kernel bits outside.
		 */
		if (!(descr->get_args & IW_PRIV_SIZE_FIXED))
			extra_size = adjust_priv_size(descr->get_args, iwp);

		if (copy_to_user(iwp->pointer, extra, extra_size))
			err =  -EFAULT;
	}

out:
	kfree(extra);
	return err;
}

int ioctl_private_call(struct net_device *dev, struct iwreq *iwr,
		       unsigned int cmd, struct iw_request_info *info,
		       iw_handler handler)
{
	int extra_size = 0, ret = -EINVAL;
	const struct iw_priv_args *descr;

	extra_size = get_priv_descr_and_size(dev, cmd, &descr);

	/* Check if we have a pointer to user space data or not. */
	if (extra_size == 0) {
		/* No extra arguments. Trivial to handle */
		ret = handler(dev, info, &(iwr->u), (char *) &(iwr->u));
	} else {
		ret = ioctl_private_iw_point(&iwr->u.data, cmd, descr,
					     handler, dev, info, extra_size);
	}

	/* Call commit handler if needed and defined */
	if (ret == -EIWCOMMIT)
		ret = call_commit_handler(dev);

	return ret;
}

#ifdef CONFIG_COMPAT
int compat_private_call(struct net_device *dev, struct iwreq *iwr,
			unsigned int cmd, struct iw_request_info *info,
			iw_handler handler)
{
	const struct iw_priv_args *descr;
	int ret, extra_size;

	extra_size = get_priv_descr_and_size(dev, cmd, &descr);

	/* Check if we have a pointer to user space data or not. */
	if (extra_size == 0) {
		/* No extra arguments. Trivial to handle */
		ret = handler(dev, info, &(iwr->u), (char *) &(iwr->u));
	} else {
		struct compat_iw_point *iwp_compat;
		struct iw_point iwp;

		iwp_compat = (struct compat_iw_point *) &iwr->u.data;
		iwp.pointer = compat_ptr(iwp_compat->pointer);
		iwp.length = iwp_compat->length;
		iwp.flags = iwp_compat->flags;

		ret = ioctl_private_iw_point(&iwp, cmd, descr,
					     handler, dev, info, extra_size);

		iwp_compat->pointer = ptr_to_compat(iwp.pointer);
		iwp_compat->length = iwp.length;
		iwp_compat->flags = iwp.flags;
	}

	/* Call commit handler if needed and defined */
	if (ret == -EIWCOMMIT)
		ret = call_commit_handler(dev);

	return ret;
}
#endif
