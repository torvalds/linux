/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _OBJTOOL_ARCH_SPECIAL_H
#define _OBJTOOL_ARCH_SPECIAL_H

/*
 * See more info about struct exception_table_entry
 * in arch/loongarch/include/asm/extable.h
 */
#define EX_ENTRY_SIZE		12
#define EX_ORIG_OFFSET		0
#define EX_NEW_OFFSET		4

/*
 * See more info about struct jump_entry
 * in include/linux/jump_label.h
 */
#define JUMP_ENTRY_SIZE		16
#define JUMP_ORIG_OFFSET	0
#define JUMP_NEW_OFFSET		4
#define JUMP_KEY_OFFSET		8

/*
 * See more info about struct alt_instr
 * in arch/loongarch/include/asm/alternative.h
 */
#define ALT_ENTRY_SIZE		12
#define ALT_ORIG_OFFSET		0
#define ALT_NEW_OFFSET		4
#define ALT_FEATURE_OFFSET	8
#define ALT_ORIG_LEN_OFFSET	10
#define ALT_NEW_LEN_OFFSET	11

#endif /* _OBJTOOL_ARCH_SPECIAL_H */
