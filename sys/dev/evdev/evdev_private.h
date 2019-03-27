/*-
 * Copyright (c) 2014 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015-2016 Vladimir Kondratyev <wulf@FreeBSD.org>
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

#ifndef	_DEV_EVDEV_EVDEV_PRIVATE_H
#define	_DEV_EVDEV_EVDEV_PRIVATE_H

#include <sys/bitstring.h>
#include <sys/kbio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>

#include <dev/evdev/evdev.h>
#include <dev/evdev/input.h>
#include <dev/kbd/kbdreg.h>

#define	NAMELEN		80

/*
 * bitstr_t implementation must be identical to one found in EVIOCG*
 * libevdev ioctls. Our bitstring(3) API is compatible since r299090.
 */
_Static_assert(sizeof(bitstr_t) == sizeof(unsigned long),
    "bitstr_t size mismatch");

MALLOC_DECLARE(M_EVDEV);

struct evdev_client;
struct evdev_mt;

#define	CURRENT_MT_SLOT(evdev)	((evdev)->ev_absinfo[ABS_MT_SLOT].value)
#define	MAXIMAL_MT_SLOT(evdev)	((evdev)->ev_absinfo[ABS_MT_SLOT].maximum)

enum evdev_key_events
{
	KEY_EVENT_UP,
	KEY_EVENT_DOWN,
	KEY_EVENT_REPEAT
};

/* evdev clock IDs in Linux semantic */
enum evdev_clock_id
{
	EV_CLOCK_REALTIME = 0,	/* UTC clock */
	EV_CLOCK_MONOTONIC,	/* monotonic, stops on suspend */
	EV_CLOCK_BOOTTIME	/* monotonic, suspend-awared */
};

enum evdev_lock_type
{
	EV_LOCK_INTERNAL = 0,	/* Internal evdev mutex */
	EV_LOCK_MTX,		/* Driver`s mutex */
};

struct evdev_dev
{
	char			ev_name[NAMELEN];
	char			ev_shortname[NAMELEN];
	char			ev_serial[NAMELEN];
	struct cdev *		ev_cdev;
	int			ev_unit;
	enum evdev_lock_type	ev_lock_type;
	struct mtx *		ev_lock;
	struct mtx		ev_mtx;
	struct input_id		ev_id;
	struct evdev_client *	ev_grabber;
	size_t			ev_report_size;

	/* Supported features: */
	bitstr_t		bit_decl(ev_prop_flags, INPUT_PROP_CNT);
	bitstr_t		bit_decl(ev_type_flags, EV_CNT);
	bitstr_t		bit_decl(ev_key_flags, KEY_CNT);
	bitstr_t		bit_decl(ev_rel_flags, REL_CNT);
	bitstr_t		bit_decl(ev_abs_flags, ABS_CNT);
	bitstr_t		bit_decl(ev_msc_flags, MSC_CNT);
	bitstr_t		bit_decl(ev_led_flags, LED_CNT);
	bitstr_t		bit_decl(ev_snd_flags, SND_CNT);
	bitstr_t		bit_decl(ev_sw_flags, SW_CNT);
	struct input_absinfo *	ev_absinfo;
	bitstr_t		bit_decl(ev_flags, EVDEV_FLAG_CNT);

	/* Repeat parameters & callout: */
	int			ev_rep[REP_CNT];
	struct callout		ev_rep_callout;
	uint16_t		ev_rep_key;

	/* State: */
	bitstr_t		bit_decl(ev_key_states, KEY_CNT);
	bitstr_t		bit_decl(ev_led_states, LED_CNT);
	bitstr_t		bit_decl(ev_snd_states, SND_CNT);
	bitstr_t		bit_decl(ev_sw_states, SW_CNT);
	bool			ev_report_opened;

	/* KDB state: */
	bool			ev_kdb_active;
	bitstr_t		bit_decl(ev_kdb_led_states, LED_CNT);

	/* Multitouch protocol type B state: */
	struct evdev_mt *	ev_mt;

	/* Counters: */
	uint64_t		ev_event_count;
	uint64_t		ev_report_count;

	/* Parent driver callbacks: */
	const struct evdev_methods * ev_methods;
	void *			ev_softc;

	/* Sysctl: */
	struct sysctl_ctx_list	ev_sysctl_ctx;

	LIST_ENTRY(evdev_dev) ev_link;
	LIST_HEAD(, evdev_client) ev_clients;
};

#define	SYSTEM_CONSOLE_LOCK	&Giant

#define	EVDEV_LOCK(evdev)		mtx_lock((evdev)->ev_lock)
#define	EVDEV_UNLOCK(evdev)		mtx_unlock((evdev)->ev_lock)
#define	EVDEV_LOCK_ASSERT(evdev)	do {				\
	if ((evdev)->ev_lock != SYSTEM_CONSOLE_LOCK)			\
		mtx_assert((evdev)->ev_lock, MA_OWNED);			\
} while (0)
#define	EVDEV_ENTER(evdev)	do {					\
	if ((evdev)->ev_lock_type == EV_LOCK_INTERNAL)			\
		EVDEV_LOCK(evdev);					\
	else								\
		EVDEV_LOCK_ASSERT(evdev);				\
} while (0)
#define	EVDEV_EXIT(evdev)	do {					\
	if ((evdev)->ev_lock_type == EV_LOCK_INTERNAL)			\
		EVDEV_UNLOCK(evdev);					\
} while (0)

struct evdev_client
{
	struct evdev_dev *	ec_evdev;
	struct mtx		ec_buffer_mtx;
	size_t			ec_buffer_size;
	size_t			ec_buffer_head;
	size_t			ec_buffer_tail;
	size_t			ec_buffer_ready;
	enum evdev_clock_id	ec_clock_id;
	struct selinfo		ec_selp;
	struct sigio *		ec_sigio;
	bool			ec_async;
	bool			ec_revoked;
	bool			ec_blocked;
	bool			ec_selected;

	LIST_ENTRY(evdev_client) ec_link;

	struct input_event	ec_buffer[];
};

#define	EVDEV_CLIENT_LOCKQ(client)	mtx_lock(&(client)->ec_buffer_mtx)
#define	EVDEV_CLIENT_UNLOCKQ(client)	mtx_unlock(&(client)->ec_buffer_mtx)
#define EVDEV_CLIENT_LOCKQ_ASSERT(client) \
    mtx_assert(&(client)->ec_buffer_mtx, MA_OWNED)
#define	EVDEV_CLIENT_EMPTYQ(client) \
    ((client)->ec_buffer_head == (client)->ec_buffer_ready)
#define	EVDEV_CLIENT_SIZEQ(client) \
    (((client)->ec_buffer_ready + (client)->ec_buffer_size - \
      (client)->ec_buffer_head) % (client)->ec_buffer_size)

/* Input device interface: */
void evdev_send_event(struct evdev_dev *, uint16_t, uint16_t, int32_t);
int evdev_inject_event(struct evdev_dev *, uint16_t, uint16_t, int32_t);
int evdev_cdev_create(struct evdev_dev *);
int evdev_cdev_destroy(struct evdev_dev *);
bool evdev_event_supported(struct evdev_dev *, uint16_t);
void evdev_set_abs_bit(struct evdev_dev *, uint16_t);
void evdev_set_absinfo(struct evdev_dev *, uint16_t, struct input_absinfo *);
void evdev_restore_after_kdb(struct evdev_dev *);

/* Client interface: */
int evdev_register_client(struct evdev_dev *, struct evdev_client *);
void evdev_dispose_client(struct evdev_dev *, struct evdev_client *);
int evdev_grab_client(struct evdev_dev *, struct evdev_client *);
int evdev_release_client(struct evdev_dev *, struct evdev_client *);
void evdev_client_push(struct evdev_client *, uint16_t, uint16_t, int32_t);
void evdev_notify_event(struct evdev_client *);
void evdev_revoke_client(struct evdev_client *);

/* Multitouch related functions: */
void evdev_mt_init(struct evdev_dev *);
void evdev_mt_free(struct evdev_dev *);
int32_t evdev_get_last_mt_slot(struct evdev_dev *);
void evdev_set_last_mt_slot(struct evdev_dev *, int32_t);
int32_t evdev_get_mt_value(struct evdev_dev *, int32_t, int16_t);
void evdev_set_mt_value(struct evdev_dev *, int32_t, int16_t, int32_t);
void evdev_send_mt_compat(struct evdev_dev *);
void evdev_send_mt_autorel(struct evdev_dev *);

/* Utility functions: */
void evdev_client_dumpqueue(struct evdev_client *);

#endif	/* _DEV_EVDEV_EVDEV_PRIVATE_H */
