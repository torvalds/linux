/*-
 * Copyright (c) 2017 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _HV_KBD_H
#define _HV_KBD_H
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <dev/kbd/kbdreg.h>

#define HVKBD_DRIVER_NAME	"hvkbd"
#define IS_UNICODE		(1)
#define IS_BREAK		(2)
#define IS_E0			(4)
#define IS_E1			(8)

#define XTKBD_EMUL0		(0xe0)
#define XTKBD_EMUL1		(0xe1)
#define XTKBD_RELEASE		(0x80)

#define DEBUG_HVSC(sc, ...) do {			\
	if (sc->debug > 0) {				\
		device_printf(sc->dev, __VA_ARGS__);	\
	}						\
} while (0)
#define DEBUG_HVKBD(kbd, ...) do {			\
	hv_kbd_sc *sc = (kbd)->kb_data;			\
	DEBUG_HVSC(sc, __VA_ARGS__);				\
} while (0)

struct vmbus_channel;
struct vmbus_xact_ctx;

typedef struct keystroke_t {
	uint16_t			makecode;
	uint32_t			info;
} keystroke;

typedef struct keystroke_info {
	LIST_ENTRY(keystroke_info)	link;
	STAILQ_ENTRY(keystroke_info)	slink;
	keystroke			ks;
} keystroke_info;

typedef struct hv_kbd_sc_t {
	struct vmbus_channel		*hs_chan;
	device_t			dev;
	struct vmbus_xact_ctx		*hs_xact_ctx;
	int32_t				buflen;
	uint8_t				*buf;

	struct mtx			ks_mtx;
	LIST_HEAD(, keystroke_info)	ks_free_list;
	STAILQ_HEAD(, keystroke_info)	ks_queue;	/* keystroke info queue */

	keyboard_t			sc_kbd;
	int				sc_mode;
	int				sc_state;
	int				sc_polling;	/* polling recursion count */
	uint32_t			sc_flags;
	int				debug;
} hv_kbd_sc;

int	hv_kbd_produce_ks(hv_kbd_sc *sc, const keystroke *ks);
int	hv_kbd_fetch_top(hv_kbd_sc *sc, keystroke *top);
int	hv_kbd_modify_top(hv_kbd_sc *sc, keystroke *top);
int	hv_kbd_remove_top(hv_kbd_sc *sc);
int	hv_kbd_prod_is_ready(hv_kbd_sc *sc);
void	hv_kbd_read_channel(struct vmbus_channel *, void *);

int	hv_kbd_drv_attach(device_t dev);
int	hv_kbd_drv_detach(device_t dev);

int	hvkbd_driver_load(module_t, int, void *);
void	hv_kbd_intr(hv_kbd_sc *sc);
#endif
