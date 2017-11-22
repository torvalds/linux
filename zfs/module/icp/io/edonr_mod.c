/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

#include <sys/modctl.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/edonr.h>

/*
 * Unlike sha2 or skein, we won't expose edonr via the Kernel Cryptographic
 * Framework (KCF), because Edon-R is *NOT* suitable for general-purpose
 * cryptographic use. Users of Edon-R must interface directly to this module.
 */

static struct modlmisc modlmisc = {
	&mod_cryptoops,
	"Edon-R Message-Digest Algorithm"
};

static struct modlinkage modlinkage = {
	MODREV_1, {&modlmisc, NULL}
};

int
edonr_mod_init(void)
{
	int error;

	if ((error = mod_install(&modlinkage)) != 0)
		return (error);

	return (0);
}

int
edonr_mod_fini(void)
{
	return (mod_remove(&modlinkage));
}
