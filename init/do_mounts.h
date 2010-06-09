#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/major.h>
#include <linux/root_dev.h>

void  change_floppy(char *fmt, ...);
void  mount_block_root(char *name, int flags);
void  mount_root(void);
extern int root_mountflags;

static inline int create_dev(char *name, dev_t dev)
{
	sys_unlink(name);
	return sys_mknod(name, S_IFBLK|0600, new_encode_dev(dev));
}

#if BITS_PER_LONG == 32
static inline u32 bstat(char *name)
{
	struct stat64 stat;
	if (sys_stat64(name, &stat) != 0)
		return 0;
	if (!S_ISBLK(stat.st_mode))
		return 0;
	if (stat.st_rdev != (u32)stat.st_rdev)
		return 0;
	return stat.st_rdev;
}
#else
static inline u32 bstat(char *name)
{
	struct stat stat;
	if (sys_newstat(name, &stat) != 0)
		return 0;
	if (!S_ISBLK(stat.st_mode))
		return 0;
	return stat.st_rdev;
}
#endif

#ifdef CONFIG_BLK_DEV_RAM

int __init rd_load_disk(int n);
int __init rd_load_image(char *from);

#else

static inline int rd_load_disk(int n) { return 0; }
static inline int rd_load_image(char *from) { return 0; }

#endif

#ifdef CONFIG_BLK_DEV_INITRD

bool __init initrd_load(void);

#else

static inline bool initrd_load(void) { return false; }

#endif

#ifdef CONFIG_BLK_DEV_MD

void md_run_setup(void);

#else

static inline void md_run_setup(void) {}

#endif

#ifdef CONFIG_BLK_DEV_DM

void dm_run_setup(void);

#else

static inline void dm_run_setup(void) {}

#endif
