#ifndef _KERNEL_H
#define _KERNEL_H

#include "../../include/linux/kernel.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include "../../../include/linux/kconfig.h"

#define printk printf
#define pr_debug printk
#define pr_cont printk

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* _KERNEL_H */
