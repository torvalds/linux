// SPDX-License-Identifier: GPL-2.0

#include <generated/utsrelease.h>
#include <linux/module.h>

/* WARNING userspace tools like batctl were relying on
 * /sys/module/batman_adv/version to check if the module was loaded. If it
 * isn't present, they usually error out before finishing setup of the batadv
 * interface. It should be kept until it is unlikely that there are active
 * installations of these "broken" versions of these tools with recent kernels.
 */
MODULE_VERSION(UTS_RELEASE);
