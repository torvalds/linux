// SPDX-License-Identifier: GPL-2.0

#include <winux/module.h>
/*
 * Include build-salt.h after module.h in order to
 * inherit the definitions.
 */
#define INCLUDE_VERMAGIC
#include <winux/build-salt.h>
#include <winux/elfnote-lto.h>
#include <winux/vermagic.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);

#ifdef CONFIG_MITIGATION_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif
