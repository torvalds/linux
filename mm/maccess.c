// SPDX-License-Identifier: GPL-2.0-only
/*
 * Access kernel or user memory without faulting.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

bool __weak probe_kernel_read_allowed(const void *unsafe_src, size_t size)
{
	return true;
}

#ifdef HAVE_GET_KERNEL_NOFAULT

#define probe_kernel_read_loop(dst, src, len, type, err_label)		\
	while (len >= sizeof(type)) {					\
		__get_kernel_nofault(dst, src, type, err_label);		\
		dst += sizeof(type);					\
		src += sizeof(type);					\
		len -= sizeof(type);					\
	}

long probe_kernel_read(void *dst, const void *src, size_t size)
{
	if (!probe_kernel_read_allowed(src, size))
		return -ERANGE;

	pagefault_disable();
	probe_kernel_read_loop(dst, src, size, u64, Efault);
	probe_kernel_read_loop(dst, src, size, u32, Efault);
	probe_kernel_read_loop(dst, src, size, u16, Efault);
	probe_kernel_read_loop(dst, src, size, u8, Efault);
	pagefault_enable();
	return 0;
Efault:
	pagefault_enable();
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(probe_kernel_read);

#define probe_kernel_write_loop(dst, src, len, type, err_label)		\
	while (len >= sizeof(type)) {					\
		__put_kernel_nofault(dst, src, type, err_label);		\
		dst += sizeof(type);					\
		src += sizeof(type);					\
		len -= sizeof(type);					\
	}

long probe_kernel_write(void *dst, const void *src, size_t size)
{
	pagefault_disable();
	probe_kernel_write_loop(dst, src, size, u64, Efault);
	probe_kernel_write_loop(dst, src, size, u32, Efault);
	probe_kernel_write_loop(dst, src, size, u16, Efault);
	probe_kernel_write_loop(dst, src, size, u8, Efault);
	pagefault_enable();
	return 0;
Efault:
	pagefault_enable();
	return -EFAULT;
}

long strncpy_from_kernel_nofault(char *dst, const void *unsafe_addr, long count)
{
	const void *src = unsafe_addr;

	if (unlikely(count <= 0))
		return 0;
	if (!probe_kernel_read_allowed(unsafe_addr, count))
		return -ERANGE;

	pagefault_disable();
	do {
		__get_kernel_nofault(dst, src, u8, Efault);
		dst++;
		src++;
	} while (dst[-1] && src - unsafe_addr < count);
	pagefault_enable();

	dst[-1] = '\0';
	return src - unsafe_addr;
Efault:
	pagefault_enable();
	dst[-1] = '\0';
	return -EFAULT;
}
#else /* HAVE_GET_KERNEL_NOFAULT */
/**
 * probe_kernel_read(): safely attempt to read from kernel-space
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from
 * @size: size of the data chunk
 *
 * Safely read from kernel address @src to the buffer at @dst.  If a kernel
 * fault happens, handle that and return -EFAULT.  If @src is not a valid kernel
 * address, return -ERANGE.
 *
 * We ensure that the copy_from_user is executed in atomic context so that
 * do_page_fault() doesn't attempt to take mmap_lock.  This makes
 * probe_kernel_read() suitable for use within regions where the caller
 * already holds mmap_lock, or other locks which nest inside mmap_lock.
 */
long probe_kernel_read(void *dst, const void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	if (!probe_kernel_read_allowed(src, size))
		return -ERANGE;

	set_fs(KERNEL_DS);
	pagefault_disable();
	ret = __copy_from_user_inatomic(dst, (__force const void __user *)src,
			size);
	pagefault_enable();
	set_fs(old_fs);

	if (ret)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(probe_kernel_read);

/**
 * probe_kernel_write(): safely attempt to write to a location
 * @dst: address to write to
 * @src: pointer to the data that shall be written
 * @size: size of the data chunk
 *
 * Safely write to address @dst from the buffer at @src.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
long probe_kernel_write(void *dst, const void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	pagefault_disable();
	ret = __copy_to_user_inatomic((__force void __user *)dst, src, size);
	pagefault_enable();
	set_fs(old_fs);

	if (ret)
		return -EFAULT;
	return 0;
}

/**
 * strncpy_from_kernel_nofault: - Copy a NUL terminated string from unsafe
 *				 address.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @unsafe_addr: Unsafe address.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from unsafe address to kernel buffer.
 *
 * On success, returns the length of the string INCLUDING the trailing NUL.
 *
 * If access fails, returns -EFAULT (some data may have been copied and the
 * trailing NUL added).  If @unsafe_addr is not a valid kernel address, return
 * -ERANGE.
 *
 * If @count is smaller than the length of the string, copies @count-1 bytes,
 * sets the last byte of @dst buffer to NUL and returns @count.
 */
long strncpy_from_kernel_nofault(char *dst, const void *unsafe_addr, long count)
{
	mm_segment_t old_fs = get_fs();
	const void *src = unsafe_addr;
	long ret;

	if (unlikely(count <= 0))
		return 0;
	if (!probe_kernel_read_allowed(unsafe_addr, count))
		return -ERANGE;

	set_fs(KERNEL_DS);
	pagefault_disable();

	do {
		ret = __get_user(*dst++, (const char __user __force *)src++);
	} while (dst[-1] && ret == 0 && src - unsafe_addr < count);

	dst[-1] = '\0';
	pagefault_enable();
	set_fs(old_fs);

	return ret ? -EFAULT : src - unsafe_addr;
}
#endif /* HAVE_GET_KERNEL_NOFAULT */

/**
 * probe_user_read(): safely attempt to read from a user-space location
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from. This must be a user address.
 * @size: size of the data chunk
 *
 * Safely read from user address @src to the buffer at @dst. If a kernel fault
 * happens, handle that and return -EFAULT.
 */
long probe_user_read(void *dst, const void __user *src, size_t size)
{
	long ret = -EFAULT;
	mm_segment_t old_fs = get_fs();

	set_fs(USER_DS);
	if (access_ok(src, size)) {
		pagefault_disable();
		ret = __copy_from_user_inatomic(dst, src, size);
		pagefault_enable();
	}
	set_fs(old_fs);

	if (ret)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(probe_user_read);

/**
 * probe_user_write(): safely attempt to write to a user-space location
 * @dst: address to write to
 * @src: pointer to the data that shall be written
 * @size: size of the data chunk
 *
 * Safely write to address @dst from the buffer at @src.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
long probe_user_write(void __user *dst, const void *src, size_t size)
{
	long ret = -EFAULT;
	mm_segment_t old_fs = get_fs();

	set_fs(USER_DS);
	if (access_ok(dst, size)) {
		pagefault_disable();
		ret = __copy_to_user_inatomic(dst, src, size);
		pagefault_enable();
	}
	set_fs(old_fs);

	if (ret)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(probe_user_write);

/**
 * strncpy_from_user_nofault: - Copy a NUL terminated string from unsafe user
 *				address.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @unsafe_addr: Unsafe user address.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from unsafe user address to kernel buffer.
 *
 * On success, returns the length of the string INCLUDING the trailing NUL.
 *
 * If access fails, returns -EFAULT (some data may have been copied
 * and the trailing NUL added).
 *
 * If @count is smaller than the length of the string, copies @count-1 bytes,
 * sets the last byte of @dst buffer to NUL and returns @count.
 */
long strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr,
			      long count)
{
	mm_segment_t old_fs = get_fs();
	long ret;

	if (unlikely(count <= 0))
		return 0;

	set_fs(USER_DS);
	pagefault_disable();
	ret = strncpy_from_user(dst, unsafe_addr, count);
	pagefault_enable();
	set_fs(old_fs);

	if (ret >= count) {
		ret = count;
		dst[ret - 1] = '\0';
	} else if (ret > 0) {
		ret++;
	}

	return ret;
}

/**
 * strnlen_user_nofault: - Get the size of a user string INCLUDING final NUL.
 * @unsafe_addr: The string to measure.
 * @count: Maximum count (including NUL)
 *
 * Get the size of a NUL-terminated string in user space without pagefault.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 *
 * If the string is too long, returns a number larger than @count. User
 * has to check the return value against "> count".
 * On exception (or invalid count), returns 0.
 *
 * Unlike strnlen_user, this can be used from IRQ handler etc. because
 * it disables pagefaults.
 */
long strnlen_user_nofault(const void __user *unsafe_addr, long count)
{
	mm_segment_t old_fs = get_fs();
	int ret;

	set_fs(USER_DS);
	pagefault_disable();
	ret = strnlen_user(unsafe_addr, count);
	pagefault_enable();
	set_fs(old_fs);

	return ret;
}
