// SPDX-License-Identifier: GPL-2.0
#include <sys/capability.h>
#include <linux/capability.h>

int main(void)
{
	cap_flag_value_t val;
	cap_t caps = cap_get_proc();

	if (!caps)
		return 1;

	if (cap_get_flag(caps, CAP_SYS_ADMIN, CAP_EFFECTIVE, &val) != 0)
		return 1;

	if (cap_free(caps) != 0)
		return 1;

	return 0;
}
