// SPDX-License-Identifier: GPL-2.0

#include <linux/fwctl.h>

#if IS_ENABLED(CONFIG_RUST_FWCTL_ABSTRACTIONS)

__rust_helper struct fwctl_device *rust_helper_fwctl_get(struct fwctl_device *fwctl)
{
	return fwctl_get(fwctl);
}

__rust_helper void rust_helper_fwctl_put(struct fwctl_device *fwctl)
{
	fwctl_put(fwctl);
}

#endif
