/*
 * Access kernel memory without faulting.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

static __always_inline long
probe_read_common(void *dst, const void __user *src, size_t size)
{
	long ret;

	pagefault_disable();
	ret = __copy_from_user_inatomic(dst, src, size);
	pagefault_enable();

	return ret ? -EFAULT : 0;
}

/**
 * probe_kernel_read(): safely attempt to read from a kernel-space location
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from
 * @size: size of the data chunk
 *
 * Safely read from address @src to the buffer at @dst.  If a kernel fault
 * happens, handle that and return -EFAULT.
 *
 * We ensure that the copy_from_user is executed in atomic context so that
 * do_page_fault() doesn't attempt to take mmap_sem.  This makes
 * probe_kernel_read() suitable for use within regions where the caller
 * already holds mmap_sem, or other locks which nest inside mmap_sem.
 */

long __weak probe_kernel_read(void *dst, const void *src, size_t size)
    __attribute__((alias("__probe_kernel_read")));

long __probe_kernel_read(void *dst, const void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = probe_read_common(dst, (__force const void __user *)src, size);
	set_fs(old_fs);

	return ret;
}
EXPORT_SYMBOL_GPL(probe_kernel_read);

/**
 * probe_user_read(): safely attempt to read from a user-space location
 * @dst: pointer to the buffer that shall take the data
 * @src: address to read from. This must be a user address.
 * @size: size of the data chunk
 *
 * Safely read from user address @src to the buffer at @dst. If a kernel fault
 * happens, handle that and return -EFAULT.
 */

long __weak probe_user_read(void *dst, const void __user *src, size_t size)
    __attribute__((alias("__probe_user_read")));

long __probe_user_read(void *dst, const void __user *src, size_t size)
{
	long ret = -EFAULT;
	mm_segment_t old_fs = get_fs();

	set_fs(USER_DS);
	if (access_ok(VERIFY_READ, src, size))
		ret = probe_read_common(dst, src, size);
	set_fs(old_fs);

	return ret;
}
EXPORT_SYMBOL_GPL(probe_user_read);

/**
 * probe_kernel_write(): safely attempt to write to a location
 * @dst: address to write to
 * @src: pointer to the data that shall be written
 * @size: size of the data chunk
 *
 * Safely write to address @dst from the buffer at @src.  If a kernel fault
 * happens, handle that and return -EFAULT.
 */
long __weak probe_kernel_write(void *dst, const void *src, size_t size)
    __attribute__((alias("__probe_kernel_write")));

long __probe_kernel_write(void *dst, const void *src, size_t size)
{
	long ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	pagefault_disable();
	ret = __copy_to_user_inatomic((__force void __user *)dst, src, size);
	pagefault_enable();
	set_fs(old_fs);

	return ret ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(probe_kernel_write);


/**
 * strncpy_from_unsafe: - Copy a NUL terminated string from unsafe address.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @unsafe_addr: Unsafe address.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from unsafe address to kernel buffer.
 *
 * On success, returns the length of the string INCLUDING the trailing NUL.
 *
 * If access fails, returns -EFAULT (some data may have been copied
 * and the trailing NUL added).
 *
 * If @count is smaller than the length of the string, copies @count-1 bytes,
 * sets the last byte of @dst buffer to NUL and returns @count.
 */
long strncpy_from_unsafe(char *dst, const void *unsafe_addr, long count)
{
	mm_segment_t old_fs = get_fs();
	const void *src = unsafe_addr;
	long ret;

	if (unlikely(count <= 0))
		return 0;

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

/**
 * strncpy_from_unsafe_user: - Copy a NUL terminated string from unsafe user
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
long strncpy_from_unsafe_user(char *dst, const void __user *unsafe_addr,
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
 * strnlen_unsafe_user: - Get the size of a user string INCLUDING final NUL.
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
long strnlen_unsafe_user(const void __user *unsafe_addr, long count)
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
