#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/uio.h>

/*
 *	Copy iovec to kernel. Returns -EFAULT on error.
 */

int memcpy_fromiovecend(unsigned char *kdata, const struct iovec *iov,
			int offset, int len)
{
	/* No data? Done! */
	if (len == 0)
		return 0;

	/* Skip over the finished iovecs */
	while (offset >= iov->iov_len) {
		offset -= iov->iov_len;
		iov++;
	}

	while (len > 0) {
		u8 __user *base = iov->iov_base + offset;
		int copy = min_t(unsigned int, len, iov->iov_len - offset);

		offset = 0;
		if (copy_from_user(kdata, base, copy))
			return -EFAULT;
		len -= copy;
		kdata += copy;
		iov++;
	}

	return 0;
}
EXPORT_SYMBOL(memcpy_fromiovecend);
