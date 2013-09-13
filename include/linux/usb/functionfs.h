#ifndef __LINUX_FUNCTIONFS_H__
#define __LINUX_FUNCTIONFS_H__ 1

#include <uapi/linux/usb/functionfs.h>


struct ffs_data;
struct usb_composite_dev;
struct usb_configuration;


static int  functionfs_init(void) __attribute__((warn_unused_result));
static void functionfs_cleanup(void);

static int functionfs_bind(struct ffs_data *ffs, struct usb_composite_dev *cdev)
	__attribute__((warn_unused_result, nonnull));
static void functionfs_unbind(struct ffs_data *ffs)
	__attribute__((nonnull));

static int functionfs_bind_config(struct usb_composite_dev *cdev,
				  struct usb_configuration *c,
				  struct ffs_data *ffs)
	__attribute__((warn_unused_result, nonnull));


static int functionfs_ready_callback(struct ffs_data *ffs)
	__attribute__((warn_unused_result, nonnull));
static void functionfs_closed_callback(struct ffs_data *ffs)
	__attribute__((nonnull));
static void *functionfs_acquire_dev_callback(const char *dev_name)
	__attribute__((warn_unused_result, nonnull));
static void functionfs_release_dev_callback(struct ffs_data *ffs_data)
	__attribute__((nonnull));


#endif
