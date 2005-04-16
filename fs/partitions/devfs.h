
#ifdef CONFIG_DEVFS_FS
void devfs_add_disk(struct gendisk *dev);
void devfs_add_partitioned(struct gendisk *dev);
void devfs_remove_disk(struct gendisk *dev);
#else
# define devfs_add_disk(disk)			do { } while (0)
# define devfs_add_partitioned(disk)		do { } while (0)
# define devfs_remove_disk(disk)		do { } while (0)
#endif
