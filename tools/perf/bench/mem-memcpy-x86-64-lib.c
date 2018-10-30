/*
 * From code in arch/x86/lib/usercopy_64.c, copied to keep tools/ copy
 * of the kernel's arch/x86/lib/memcpy_64.s used in 'perf bench mem memcpy'
 * happy.
 */
#include <linux/types.h>

unsigned long __memcpy_mcsafe(void *dst, const void *src, size_t cnt);
unsigned long mcsafe_handle_tail(char *to, char *from, unsigned len);

unsigned long mcsafe_handle_tail(char *to, char *from, unsigned len)
{
	for (; len; --len, to++, from++) {
		/*
		 * Call the assembly routine back directly since
		 * memcpy_mcsafe() may silently fallback to memcpy.
		 */
		unsigned long rem = __memcpy_mcsafe(to, from, 1);

		if (rem)
			break;
	}
	return len;
}
