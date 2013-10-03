#include <linux/export.h>
#include <linux/bug.h>
#include <linux/uaccess.h>

void copy_from_user_overflow(void)
{
	WARN(1, "Buffer overflow detected!\n");
}
EXPORT_SYMBOL(copy_from_user_overflow);
