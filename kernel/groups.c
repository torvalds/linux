/*
 * Supplementary group IDs
 */
#include <linux/cred.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/user_namespace.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

struct group_info *groups_alloc(int gidsetsize)
{
	struct group_info *gi;
	unsigned int len;

	len = sizeof(struct group_info) + sizeof(kgid_t) * gidsetsize;
	gi = kmalloc(len, GFP_KERNEL_ACCOUNT|__GFP_NOWARN|__GFP_NORETRY);
	if (!gi)
		gi = __vmalloc(len, GFP_KERNEL_ACCOUNT|__GFP_HIGHMEM, PAGE_KERNEL);
	if (!gi)
		return NULL;

	atomic_set(&gi->usage, 1);
	gi->ngroups = gidsetsize;
	return gi;
}

EXPORT_SYMBOL(groups_alloc);

void groups_free(struct group_info *group_info)
{
	kvfree(group_info);
}

EXPORT_SYMBOL(groups_free);

/* export the group_info to a user-space array */
static int groups_to_user(gid_t __user *grouplist,
			  const struct group_info *group_info)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	unsigned int count = group_info->ngroups;

	for (i = 0; i < count; i++) {
		gid_t gid;
		gid = from_kgid_munged(user_ns, group_info->gid[i]);
		if (put_user(gid, grouplist+i))
			return -EFAULT;
	}
	return 0;
}

/* fill a group_info from a user-space array - it must be allocated already */
static int groups_from_user(struct group_info *group_info,
    gid_t __user *grouplist)
{
	struct user_namespace *user_ns = current_user_ns();
	int i;
	unsigned int count = group_info->ngroups;

	for (i = 0; i < count; i++) {
		gid_t gid;
		kgid_t kgid;
		if (get_user(gid, grouplist+i))
			return -EFAULT;

		kgid = make_kgid(user_ns, gid);
		if (!gid_valid(kgid))
			return -EINVAL;

		group_info->gid[i] = kgid;
	}
	return 0;
}

/* a simple Shell sort */
static void groups_sort(struct group_info *group_info)
{
	int base, max, stride;
	int gidsetsize = group_info->ngroups;

	for (stride = 1; stride < gidsetsize; stride = 3 * stride + 1)
		; /* nothing */
	stride /= 3;

	while (stride) {
		max = gidsetsize - stride;
		for (base = 0; base < max; base++) {
			int left = base;
			int right = left + stride;
			kgid_t tmp = group_info->gid[right];

			while (left >= 0 && gid_gt(group_info->gid[left], tmp)) {
				group_info->gid[right] = group_info->gid[left];
				right = left;
				left -= stride;
			}
			group_info->gid[right] = tmp;
		}
		stride /= 3;
	}
}

/* a simple bsearch */
int groups_search(const struct group_info *group_info, kgid_t grp)
{
	unsigned int left, right;

	if (!group_info)
		return 0;

	left = 0;
	right = group_info->ngroups;
	while (left < right) {
		unsigned int mid = (left+right)/2;
		if (gid_gt(grp, group_info->gid[mid]))
			left = mid + 1;
		else if (gid_lt(grp, group_info->gid[mid]))
			right = mid;
		else
			return 1;
	}
	return 0;
}

/**
 * set_groups - Change a group subscription in a set of credentials
 * @new: The newly prepared set of credentials to alter
 * @group_info: The group list to install
 */
void set_groups(struct cred *new, struct group_info *group_info)
{
	put_group_info(new->group_info);
	groups_sort(group_info);
	get_group_info(group_info);
	new->group_info = group_info;
}

EXPORT_SYMBOL(set_groups);

/**
 * set_current_groups - Change current's group subscription
 * @group_info: The group list to impose
 *
 * Validate a group subscription and, if valid, impose it upon current's task
 * security record.
 */
int set_current_groups(struct group_info *group_info)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	set_groups(new, group_info);
	return commit_creds(new);
}

EXPORT_SYMBOL(set_current_groups);

SYSCALL_DEFINE2(getgroups, int, gidsetsize, gid_t __user *, grouplist)
{
	const struct cred *cred = current_cred();
	int i;

	if (gidsetsize < 0)
		return -EINVAL;

	/* no need to grab task_lock here; it cannot change */
	i = cred->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups_to_user(grouplist, cred->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	return i;
}

bool may_setgroups(void)
{
	struct user_namespace *user_ns = current_user_ns();

	return ns_capable(user_ns, CAP_SETGID) &&
		userns_may_setgroups(user_ns);
}

/*
 *	SMP: Our groups are copy-on-write. We can set them safely
 *	without another task interfering.
 */

SYSCALL_DEFINE2(setgroups, int, gidsetsize, gid_t __user *, grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!may_setgroups())
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

/*
 * Check whether we're fsgid/egid or in the supplemental group..
 */
int in_group_p(kgid_t grp)
{
	const struct cred *cred = current_cred();
	int retval = 1;

	if (!gid_eq(grp, cred->fsgid))
		retval = groups_search(cred->group_info, grp);
	return retval;
}

EXPORT_SYMBOL(in_group_p);

int in_egroup_p(kgid_t grp)
{
	const struct cred *cred = current_cred();
	int retval = 1;

	if (!gid_eq(grp, cred->egid))
		retval = groups_search(cred->group_info, grp);
	return retval;
}

EXPORT_SYMBOL(in_egroup_p);
