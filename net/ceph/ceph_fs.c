/*
 * Some non-inline ceph helpers
 */
#include <linux/module.h>
#include <linux/ceph/types.h>

/*
 * return true if @layout appears to be valid
 */
int ceph_file_layout_is_valid(const struct ceph_file_layout *layout)
{
	__u32 su = le32_to_cpu(layout->fl_stripe_unit);
	__u32 sc = le32_to_cpu(layout->fl_stripe_count);
	__u32 os = le32_to_cpu(layout->fl_object_size);

	/* stripe unit, object size must be non-zero, 64k increment */
	if (!su || (su & (CEPH_MIN_STRIPE_UNIT-1)))
		return 0;
	if (!os || (os & (CEPH_MIN_STRIPE_UNIT-1)))
		return 0;
	/* object size must be a multiple of stripe unit */
	if (os < su || os % su)
		return 0;
	/* stripe count must be non-zero */
	if (!sc)
		return 0;
	return 1;
}


int ceph_flags_to_mode(int flags)
{
	int mode;

#ifdef O_DIRECTORY  /* fixme */
	if ((flags & O_DIRECTORY) == O_DIRECTORY)
		return CEPH_FILE_MODE_PIN;
#endif
	if ((flags & O_APPEND) == O_APPEND)
		flags |= O_WRONLY;

	if ((flags & O_ACCMODE) == O_RDWR)
		mode = CEPH_FILE_MODE_RDWR;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		mode = CEPH_FILE_MODE_WR;
	else
		mode = CEPH_FILE_MODE_RD;

#ifdef O_LAZY
	if (flags & O_LAZY)
		mode |= CEPH_FILE_MODE_LAZY;
#endif

	return mode;
}
EXPORT_SYMBOL(ceph_flags_to_mode);

int ceph_caps_for_mode(int mode)
{
	int caps = CEPH_CAP_PIN;

	if (mode & CEPH_FILE_MODE_RD)
		caps |= CEPH_CAP_FILE_SHARED |
			CEPH_CAP_FILE_RD | CEPH_CAP_FILE_CACHE;
	if (mode & CEPH_FILE_MODE_WR)
		caps |= CEPH_CAP_FILE_EXCL |
			CEPH_CAP_FILE_WR | CEPH_CAP_FILE_BUFFER |
			CEPH_CAP_AUTH_SHARED | CEPH_CAP_AUTH_EXCL |
			CEPH_CAP_XATTR_SHARED | CEPH_CAP_XATTR_EXCL;
	if (mode & CEPH_FILE_MODE_LAZY)
		caps |= CEPH_CAP_FILE_LAZYIO;

	return caps;
}
EXPORT_SYMBOL(ceph_caps_for_mode);
