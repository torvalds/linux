/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_FB_SPLASHREG_H_
#define _DEV_FB_SPLASHREG_H_

#define SPLASH_IMAGE	"splash_image_data"

struct video_adapter;

struct image_decoder {
	char		*name;
	int		(*init)(struct video_adapter *adp);
	int		(*term)(struct video_adapter *adp);
	int		(*splash)(struct video_adapter *adp, int on);
	char		*data_type;
	void		*data;
	size_t		data_size;
};

typedef struct image_decoder	splash_decoder_t;
typedef struct image_decoder	scrn_saver_t;

#define SPLASH_DECODER(name, sw)				\
	static int name##_modevent(module_t mod, int type, void *data) \
	{							\
		switch ((modeventtype_t)type) {			\
		case MOD_LOAD:					\
			return splash_register(&sw);		\
		case MOD_UNLOAD:				\
			return splash_unregister(&sw);		\
		default:					\
			return EOPNOTSUPP;			\
			break;					\
		}						\
		return 0;					\
	}							\
	static moduledata_t name##_mod = {			\
		#name, 						\
		name##_modevent,				\
		NULL						\
	};							\
	DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_ANY); \
	MODULE_DEPEND(name, splash, 1, 1, 1)

#define SAVER_MODULE(name, sw)					\
	static int name##_modevent(module_t mod, int type, void *data) \
	{							\
		switch ((modeventtype_t)type) {			\
		case MOD_LOAD:					\
			return splash_register(&sw);		\
		case MOD_UNLOAD:				\
			return splash_unregister(&sw);		\
		default:					\
			return EOPNOTSUPP;			\
			break;					\
		}						\
		return 0;					\
	}							\
	static moduledata_t name##_mod = {			\
		#name, 						\
		name##_modevent,				\
		NULL						\
	};							\
	DECLARE_MODULE(name, name##_mod, SI_SUB_PSEUDO, SI_ORDER_MIDDLE); \
	MODULE_DEPEND(name, splash, 1, 1, 1)

/* entry point for the splash image decoder */
int	splash_register(splash_decoder_t *decoder);
int	splash_unregister(splash_decoder_t *decoder);

/* entry points for the console driver */
int	splash_init(video_adapter_t *adp, int (*callback)(int, void *),
		    void *arg);
int	splash_term(video_adapter_t *adp);
int	splash(video_adapter_t *adp, int on);

/* event types for the callback function */
#define SPLASH_INIT	0
#define SPLASH_TERM	1

#endif /* _DEV_FB_SPLASHREG_H_ */
