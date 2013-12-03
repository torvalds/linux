#ifndef __LINUX_FUNCTIONFS_H__
#define __LINUX_FUNCTIONFS_H__ 1

#include <uapi/linux/usb/functionfs.h>

#ifdef USB_FFS_INCLUDED

struct ffs_data;
struct usb_composite_dev;
struct usb_configuration;

static int functionfs_bind(struct ffs_data *ffs, struct usb_composite_dev *cdev)
	__attribute__((warn_unused_result, nonnull));
static void functionfs_unbind(struct ffs_data *ffs)
	__attribute__((nonnull));

static int functionfs_bind_config(struct usb_composite_dev *cdev,
				  struct usb_configuration *c,
				  struct ffs_data *ffs)
	__attribute__((warn_unused_result, nonnull));


#endif
#endif
