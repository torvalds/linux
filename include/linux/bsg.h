#ifndef BSG_H
#define BSG_H

#if defined(CONFIG_BLK_DEV_BSG)
struct bsg_class_device {
	struct class_device *class_dev;
	struct device *dev;
	int minor;
	struct gendisk *disk;
	struct list_head list;
};

extern int bsg_register_disk(struct gendisk *);
extern void bsg_unregister_disk(struct gendisk *);
#else
struct bsg_class_device { };
#define bsg_register_disk(disk)		(0)
#define bsg_unregister_disk(disk)	do { } while (0)
#endif

#endif
