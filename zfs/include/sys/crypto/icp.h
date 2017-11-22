/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2016, Datto, Inc. All rights reserved.
 */

#ifndef	_SYS_CRYPTO_ALGS_H
#define	_SYS_CRYPTO_ALGS_H

int aes_mod_init(void);
int aes_mod_fini(void);

int edonr_mod_init(void);
int edonr_mod_fini(void);

int sha1_mod_init(void);
int sha1_mod_fini(void);

int sha2_mod_init(void);
int sha2_mod_fini(void);

int skein_mod_init(void);
int skein_mod_fini(void);

int icp_init(void);
void icp_fini(void);

#endif /* _SYS_CRYPTO_ALGS_H */
