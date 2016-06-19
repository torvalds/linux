#include <linux/kernel.h>
#include "ubifs.h"

/* Normal UBIFS messages */
void ubifs_msg(const struct ubifs_info *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_notice("UBIFS (ubi%d:%d): %pV\n",
		  c->vi.ubi_num, c->vi.vol_id, &vaf);

	va_end(args);
}								    \

/* UBIFS error messages */
void ubifs_err(const struct ubifs_info *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("UBIFS error (ubi%d:%d pid %d): %ps: %pV\n",
	       c->vi.ubi_num, c->vi.vol_id, current->pid,
	       __builtin_return_address(0),
	       &vaf);

	va_end(args);
}								    \

/* UBIFS warning messages */
void ubifs_warn(const struct ubifs_info *c, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_warn("UBIFS warning (ubi%d:%d pid %d): %ps: %pV\n",
		c->vi.ubi_num, c->vi.vol_id, current->pid,
		__builtin_return_address(0),
		&vaf);

	va_end(args);
}
