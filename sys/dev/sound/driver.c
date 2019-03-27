/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Cameron Grant <cg@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

static int
snd_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		break;
	default:
		return (ENOTSUP);
		break;
	}
	return 0;
}

static moduledata_t snd_mod = {
	"snd_driver",
	snd_modevent,
	NULL
};
DECLARE_MODULE(snd_driver, snd_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(snd_driver, 1);

MODULE_DEPEND(snd_driver, snd_ad1816, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_als4000, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_atiixp, 1, 1, 1);
/* MODULE_DEPEND(snd_driver, snd_aureal, 1, 1, 1); */
MODULE_DEPEND(snd_driver, snd_cmi, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_cs4281, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_csa, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_csapcm, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_ds1, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_emu10kx, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_envy24, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_envy24ht, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_es137x, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_ess, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_fm801, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_gusc, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_hda, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_ich, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_maestro, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_maestro3, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_mss, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_neomagic, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_sb16, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_sb8, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_sbc, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_solo, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_spicds, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_t4dwave, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_via8233, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_via82c686, 1, 1, 1);
MODULE_DEPEND(snd_driver, snd_vibes, 1, 1, 1);
