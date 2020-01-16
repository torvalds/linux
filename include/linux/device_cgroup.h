/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>
#include <linux/bpf-cgroup.h>

#define DEVCG_ACC_MKNOD 1
#define DEVCG_ACC_READ  2
#define DEVCG_ACC_WRITE 4
#define DEVCG_ACC_MASK (DEVCG_ACC_MKNOD | DEVCG_ACC_READ | DEVCG_ACC_WRITE)

#define DEVCG_DEV_BLOCK 1
#define DEVCG_DEV_CHAR  2
#define DEVCG_DEV_ALL   4  /* this represents all devices */

#ifdef CONFIG_CGROUP_DEVICE
int devcgroup_check_permission(short type, u32 major, u32 miyesr,
			       short access);
#else
static inline int devcgroup_check_permission(short type, u32 major, u32 miyesr,
					     short access)
{ return 0; }
#endif

#if defined(CONFIG_CGROUP_DEVICE) || defined(CONFIG_CGROUP_BPF)
static inline int devcgroup_iyesde_permission(struct iyesde *iyesde, int mask)
{
	short type, access = 0;

	if (likely(!iyesde->i_rdev))
		return 0;

	if (S_ISBLK(iyesde->i_mode))
		type = DEVCG_DEV_BLOCK;
	else if (S_ISCHR(iyesde->i_mode))
		type = DEVCG_DEV_CHAR;
	else
		return 0;

	if (mask & MAY_WRITE)
		access |= DEVCG_ACC_WRITE;
	if (mask & MAY_READ)
		access |= DEVCG_ACC_READ;

	return devcgroup_check_permission(type, imajor(iyesde), imiyesr(iyesde),
					  access);
}

static inline int devcgroup_iyesde_mkyesd(int mode, dev_t dev)
{
	short type;

	if (!S_ISBLK(mode) && !S_ISCHR(mode))
		return 0;

	if (S_ISBLK(mode))
		type = DEVCG_DEV_BLOCK;
	else
		type = DEVCG_DEV_CHAR;

	return devcgroup_check_permission(type, MAJOR(dev), MINOR(dev),
					  DEVCG_ACC_MKNOD);
}

#else
static inline int devcgroup_iyesde_permission(struct iyesde *iyesde, int mask)
{ return 0; }
static inline int devcgroup_iyesde_mkyesd(int mode, dev_t dev)
{ return 0; }
#endif
