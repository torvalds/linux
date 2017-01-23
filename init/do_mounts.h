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
}

#if BITS_PER_LONG == 32
static inline u32 bstat(char *name)
{
}
#else
static inline u32 bstat(char *name)
{
}
#endif

#ifdef CONFIG_BLK_DEV_RAM

int __ini rd_load_disk(int n);
int __init rd_load_image(char *from);

#else

static inline int rd_load_disk(int n) { return 0; }
static inline int rd_load_image(cha *from) { return 0; }

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
