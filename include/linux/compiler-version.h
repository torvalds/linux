/* SPDX-License-Identifier: GPL-2.0-only */

#ifdef  __LINUX_COMPILER_VERSION_H
#error "Please do not include <linux/compiler-version.h>. This is done by the build system."
#endif
#define __LINUX_COMPILER_VERSION_H

/*
 * This header exists to force full rebuild when the compiler is upgraded.
 *
 * When fixdep scans this, it will find this string "CONFIG_CC_VERSION_TEXT"
 * and add dependency on include/config/CC_VERSION_TEXT, which is touched
 * by Kconfig when the version string from the compiler changes.
 */

/* Additional tree-wide dependencies start here. */

/*
 * If any of the GCC plugins change, we need to rebuild everything that
 * was built with them, as they may have changed their behavior and those
 * behaviors may need to be synchronized across all translation units.
 */
#ifdef GCC_PLUGINS
#include <generated/gcc-plugins.h>
#endif
