// SPDX-License-Identifier: GPL-2.0
#include "cap_helpers.h"

/* Avoid including <sys/capability.h> from the libcap-devel package,
 * so directly declare them here and use them from glibc.
 */
int capget(cap_user_header_t header, cap_user_data_t data);
int capset(cap_user_header_t header, const cap_user_data_t data);

int cap_enable_effective(__u64 caps, __u64 *old_caps)
{
	struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
	struct __user_cap_header_struct hdr = {
		.version = _LINUX_CAPABILITY_VERSION_3,
	};
	__u32 cap0 = caps;
	__u32 cap1 = caps >> 32;
	int err;

	err = capget(&hdr, data);
	if (err)
		return err;

	if (old_caps)
		*old_caps = (__u64)(data[1].effective) << 32 | data[0].effective;

	if ((data[0].effective & cap0) == cap0 &&
	    (data[1].effective & cap1) == cap1)
		return 0;

	data[0].effective |= cap0;
	data[1].effective |= cap1;
	err = capset(&hdr, data);
	if (err)
		return err;

	return 0;
}

int cap_disable_effective(__u64 caps, __u64 *old_caps)
{
	struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3];
	struct __user_cap_header_struct hdr = {
		.version = _LINUX_CAPABILITY_VERSION_3,
	};
	__u32 cap0 = caps;
	__u32 cap1 = caps >> 32;
	int err;

	err = capget(&hdr, data);
	if (err)
		return err;

	if (old_caps)
		*old_caps = (__u64)(data[1].effective) << 32 | data[0].effective;

	if (!(data[0].effective & cap0) && !(data[1].effective & cap1))
		return 0;

	data[0].effective &= ~cap0;
	data[1].effective &= ~cap1;
	err = capset(&hdr, data);
	if (err)
		return err;

	return 0;
}
