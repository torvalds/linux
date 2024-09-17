/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 2001 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Prototypes for functions exported from the compressed isofs subsystem
 */

#ifdef CONFIG_ZISOFS
extern const struct address_space_operations zisofs_aops;
extern int __init zisofs_init(void);
extern void zisofs_cleanup(void);
#endif
