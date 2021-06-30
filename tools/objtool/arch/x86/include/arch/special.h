/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _X86_ARCH_SPECIAL_H
#define _X86_ARCH_SPECIAL_H

#define EX_ENTRY_SIZE		12
#define EX_ORIG_OFFSET		0
#define EX_NEW_OFFSET		4

#define JUMP_ENTRY_SIZE		16
#define JUMP_ORIG_OFFSET	0
#define JUMP_NEW_OFFSET		4
#define JUMP_KEY_OFFSET		8

#define ALT_ENTRY_SIZE		12
#define ALT_ORIG_OFFSET		0
#define ALT_NEW_OFFSET		4
#define ALT_FEATURE_OFFSET	8
#define ALT_ORIG_LEN_OFFSET	10
#define ALT_NEW_LEN_OFFSET	11

#endif /* _X86_ARCH_SPECIAL_H */
