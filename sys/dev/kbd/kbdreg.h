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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_KBD_KBDREG_H_
#define _DEV_KBD_KBDREG_H_

/* forward declarations */
typedef struct keyboard keyboard_t;
struct keymap;
struct accentmap;
struct fkeytab;
struct cdevsw;

/* call back funcion */
typedef int		kbd_callback_func_t(keyboard_t *kbd, int event,
					    void *arg);
/* event types */
#define KBDIO_KEYINPUT	0
#define KBDIO_UNLOADING	1

typedef struct keyboard_callback {
	kbd_callback_func_t *kc_func;
	void		*kc_arg;
} keyboard_callback_t;

/* keyboard */
struct keyboard {
	/* the following fields are managed by kbdio */
	int		kb_index;	/* kbdio index# */
	int		kb_minor;	/* minor number of the sub-device */
	int		kb_flags;	/* internal flags */
#define KB_VALID	(1 << 16)	/* this entry is valid */
#define KB_NO_DEVICE	(1 << 17)	/* device not present */
#define KB_PROBED	(1 << 18)	/* device probed */
#define KB_INITIALIZED	(1 << 19)	/* device initialized */
#define KB_REGISTERED	(1 << 20)	/* device registered to kbdio */
#define KB_BUSY		(1 << 21)	/* device used by a client */
#define KB_POLLED	(1 << 22)	/* device is polled */
	int		kb_active;	/* 0: inactive */
	void		*kb_token;	/* id of the current client */
	keyboard_callback_t kb_callback;/* callback function */

	/*
	 * Device configuration flags:
	 * The upper 16 bits are common between various keyboard devices.
	 * The lower 16 bits are device-specific.
	 */
	int		kb_config;	
#define KB_CONF_PROBE_ONLY (1 << 16)	/* probe only, don't initialize */

	/* the following fields are set up by the driver */
	char		*kb_name;	/* driver name */
	int		kb_unit;	/* unit # */
	int		kb_type;	/* KB_84, KB_101, KB_OTHER,... */
	int		kb_io_base;	/* port# if any */
	int		kb_io_size;	/* # of occupied port */
	int		kb_led;		/* LED status */
	struct keymap	*kb_keymap;	/* key map */
	struct accentmap *kb_accentmap;	/* accent map */
	struct fkeytab	*kb_fkeytab;	/* function key strings */
	int		kb_fkeytab_size;/* # of function key strings */
	void		*kb_data;	/* the driver's private data */
	int		kb_delay1;
	int		kb_delay2;
#define KB_DELAY1	500
#define KB_DELAY2	100
	unsigned long	kb_count;	/* # of processed key strokes */
	u_char		kb_lastact[NUM_KEYS/2];
	struct cdev *kb_dev;
};

#define KBD_IS_VALID(k)		((k)->kb_flags & KB_VALID)
#define KBD_VALID(k)		((k)->kb_flags |= KB_VALID)
#define KBD_INVALID(k)		((k)->kb_flags &= ~KB_VALID)
#define KBD_HAS_DEVICE(k)	(!((k)->kb_flags & KB_NO_DEVICE))
#define KBD_FOUND_DEVICE(k)	((k)->kb_flags &= ~KB_NO_DEVICE)
#define KBD_IS_PROBED(k)	((k)->kb_flags & KB_PROBED)
#define KBD_PROBE_DONE(k)	((k)->kb_flags |= KB_PROBED)
#define KBD_IS_INITIALIZED(k)	((k)->kb_flags & KB_INITIALIZED)
#define KBD_INIT_DONE(k)	((k)->kb_flags |= KB_INITIALIZED)
#define KBD_IS_CONFIGURED(k)	((k)->kb_flags & KB_REGISTERED)
#define KBD_CONFIG_DONE(k)	((k)->kb_flags |= KB_REGISTERED)
#define KBD_IS_BUSY(k)		((k)->kb_flags & KB_BUSY)
#define KBD_BUSY(k)		((k)->kb_flags |= KB_BUSY)
#define KBD_UNBUSY(k)		((k)->kb_flags &= ~KB_BUSY)
#define KBD_IS_POLLED(k)	((k)->kb_flags & KB_POLLED)
#define KBD_POLL(k)		((k)->kb_flags |= KB_POLLED)
#define KBD_UNPOLL(k)		((k)->kb_flags &= ~KB_POLLED)
#define KBD_IS_ACTIVE(k)	((k)->kb_active)
#define KBD_ACTIVATE(k)		(++(k)->kb_active)
#define KBD_DEACTIVATE(k)	(--(k)->kb_active)
#define KBD_LED_VAL(k)		((k)->kb_led)

/* keyboard function table */
typedef int		kbd_probe_t(int unit, void *arg, int flags);
typedef int		kbd_init_t(int unit, keyboard_t **kbdp, void *arg,
				   int flags);
typedef int		kbd_term_t(keyboard_t *kbd);
typedef int		kbd_intr_t(keyboard_t *kbd, void *arg);
typedef int		kbd_test_if_t(keyboard_t *kbd);
typedef int		kbd_enable_t(keyboard_t *kbd);
typedef int		kbd_disable_t(keyboard_t *kbd);
typedef int		kbd_read_t(keyboard_t *kbd, int wait);
typedef int		kbd_check_t(keyboard_t *kbd);
typedef u_int		kbd_read_char_t(keyboard_t *kbd, int wait);
typedef int		kbd_check_char_t(keyboard_t *kbd);
typedef int		kbd_ioctl_t(keyboard_t *kbd, u_long cmd, caddr_t data);
typedef int		kbd_lock_t(keyboard_t *kbd, int lock);
typedef void		kbd_clear_state_t(keyboard_t *kbd);
typedef int		kbd_get_state_t(keyboard_t *kbd, void *buf, size_t len);
typedef int		kbd_set_state_t(keyboard_t *kbd, void *buf, size_t len);
typedef u_char		*kbd_get_fkeystr_t(keyboard_t *kbd, int fkey,
					   size_t *len);
typedef int		kbd_poll_mode_t(keyboard_t *kbd, int on);
typedef void		kbd_diag_t(keyboard_t *kbd, int level);

typedef struct keyboard_switch {
	kbd_probe_t	*probe;
	kbd_init_t	*init;
	kbd_term_t	*term;
	kbd_intr_t	*intr;
	kbd_test_if_t	*test_if;
	kbd_enable_t	*enable;
	kbd_disable_t	*disable;
	kbd_read_t	*read;
	kbd_check_t	*check;
	kbd_read_char_t	*read_char;
	kbd_check_char_t *check_char;
	kbd_ioctl_t	*ioctl;
	kbd_lock_t	*lock;
	kbd_clear_state_t *clear_state;
	kbd_get_state_t	*get_state;
	kbd_set_state_t	*set_state;
	kbd_get_fkeystr_t *get_fkeystr;
	kbd_poll_mode_t *poll;
	kbd_diag_t	*diag;
} keyboard_switch_t;

/*
 * Keyboard disciplines: call actual handlers via kbdsw[].
 */
#define kbdd_probe(kbd, unit, arg, flags)				\
	(*kbdsw[(kbd)->kb_index]->probe)((unit), (arg), (flags))
#define kbdd_init(kbd, unit, kbdpp, arg, flags)				\
	(*kbdsw[(kbd)->kb_index]->init)((unit), (kbdpp), (arg), (flags))
#define kbdd_term(kbd)							\
	(*kbdsw[(kbd)->kb_index]->term)((kbd))
#define kbdd_intr(kbd, arg)						\
	(*kbdsw[(kbd)->kb_index]->intr)((kbd), (arg))
#define kbdd_test_if(kbd)						\
	(*kbdsw[(kbd)->kb_index]->test_if)((kbd))
#define kbdd_enable(kbd)						\
	(*kbdsw[(kbd)->kb_index]->enable)((kbd))
#define kbdd_disable(kbd)						\
	(*kbdsw[(kbd)->kb_index]->disable)((kbd))
#define kbdd_read(kbd, wait)						\
	(*kbdsw[(kbd)->kb_index]->read)((kbd), (wait))
#define kbdd_check(kbd)							\
	(*kbdsw[(kbd)->kb_index]->check)((kbd))
#define kbdd_read_char(kbd, wait)					\
	(*kbdsw[(kbd)->kb_index]->read_char)((kbd), (wait))
#define kbdd_check_char(kbd)						\
	(*kbdsw[(kbd)->kb_index]->check_char)((kbd))
#define kbdd_ioctl(kbd, cmd, arg)					\
	(((kbd) == NULL) ?						\
	    ENODEV :							\
 	    (*kbdsw[(kbd)->kb_index]->ioctl)((kbd), (cmd), (arg)))
#define kbdd_lock(kbd, lockf)						\
	(*kbdsw[(kbd)->kb_index]->lock)((kbd), (lockf))
#define kbdd_clear_state(kbd)						\
	(*kbdsw[(kbd)->kb_index]->clear_state)((kbd))
#define kbdd_get_state(kbd, buf, len)					\
	(*kbdsw[(kbd)->kb_index]->get_state)((kbd), (buf), (len))
#define kbdd_set_state(kbd, buf, len)					\
	(*kbdsw[(kbd)->kb_index]->set_state)((kbd), (buf), (len))
#define kbdd_get_fkeystr(kbd, fkey, len)				\
	(*kbdsw[(kbd)->kb_index]->get_fkeystr)((kbd), (fkey), (len))
#define kbdd_poll(kbd, on)						\
	(*kbdsw[(kbd)->kb_index]->poll)((kbd), (on))
#define kbdd_diag(kbd, level)						\
	(*kbdsw[(kbd)->kb_index]->diag)((kbd), (leve))

/* keyboard driver */
typedef struct keyboard_driver {
    SLIST_ENTRY(keyboard_driver) link;
    char		*name;
    keyboard_switch_t	*kbdsw;
    int			(*configure)(int); /* backdoor for the console driver */
} keyboard_driver_t;

#ifdef _KERNEL

#define KEYBOARD_DRIVER(name, sw, config)		\
	static struct keyboard_driver name##_kbd_driver = { \
		{ NULL }, #name, &sw, config		\
	};						\
	DATA_SET(kbddriver_set, name##_kbd_driver);

/* global variables */
extern keyboard_switch_t **kbdsw;

/* functions for the keyboard driver */
int			kbd_add_driver(keyboard_driver_t *driver);
int			kbd_delete_driver(keyboard_driver_t *driver);
int			kbd_register(keyboard_t *kbd);
int			kbd_unregister(keyboard_t *kbd);
keyboard_switch_t	*kbd_get_switch(char *driver);
void			kbd_init_struct(keyboard_t *kbd, char *name, int type,
					int unit, int config, int port,
					int port_size);
void			kbd_set_maps(keyboard_t *kbd, struct keymap *keymap,
				     struct accentmap *accmap,
				     struct fkeytab *fkeymap, int fkeymap_size);

/* functions for the keyboard client */
int			kbd_allocate(char *driver, int unit, void *id,
				     kbd_callback_func_t *func, void *arg);
int			kbd_release(keyboard_t *kbd, void *id);
int			kbd_change_callback(keyboard_t *kbd, void *id,
				     kbd_callback_func_t *func, void *arg);
int			kbd_find_keyboard(char *driver, int unit);
int			kbd_find_keyboard2(char *driver, int unit, int index);
keyboard_t 		*kbd_get_keyboard(int index);

/* a back door for the console driver to tickle the keyboard driver XXX */
int			kbd_configure(int flags);
			/* see `kb_config' above for flag bit definitions */

/* evdev2kbd mappings */
void			kbd_ev_event(keyboard_t *kbd, uint16_t type,
				    uint16_t code, int32_t value);

#ifdef KBD_INSTALL_CDEV

/* virtual keyboard cdev driver functions */
int			kbd_attach(keyboard_t *kbd);
int			kbd_detach(keyboard_t *kbd);

#endif /* KBD_INSTALL_CDEV */

/* generic low-level keyboard functions */

/* shift key state */
#define SHIFTS1		(1 << 16)
#define SHIFTS2		(1 << 17)
#define SHIFTS		(SHIFTS1 | SHIFTS2)
#define CTLS1		(1 << 18)
#define CTLS2		(1 << 19)
#define CTLS		(CTLS1 | CTLS2)
#define ALTS1		(1 << 20)
#define ALTS2		(1 << 21)
#define ALTS		(ALTS1 | ALTS2)
#define AGRS1		(1 << 22)
#define AGRS2		(1 << 23)
#define AGRS		(AGRS1 | AGRS2)
#define METAS1		(1 << 24)
#define METAS2		(1 << 25)
#define METAS		(METAS1 | METAS2)
#define NLKDOWN		(1 << 26)
#define SLKDOWN		(1 << 27)
#define CLKDOWN		(1 << 28)
#define ALKDOWN		(1 << 29)
#define SHIFTAON	(1 << 30)
/* lock key state (defined in sys/kbio.h) */
/*
#define CLKED		LED_CAP
#define NLKED		LED_NUM
#define SLKED		LED_SCR
#define ALKED		(1 << 3)
#define LOCK_MASK	(CLKED | NLKED | SLKED | ALKED)
#define LED_CAP		(1 << 0)
#define LED_NUM		(1 << 1)
#define LED_SCR		(1 << 2)
#define LED_MASK	(LED_CAP | LED_NUM | LED_SCR)
*/

kbd_get_fkeystr_t	genkbd_get_fkeystr;
kbd_diag_t		genkbd_diag;

int 	genkbd_commonioctl(keyboard_t *kbd, u_long cmd, caddr_t arg);
int 	genkbd_keyaction(keyboard_t *kbd, int keycode, int up,
			 int *shiftstate, int *accents);

#endif /* _KERNEL */

#endif /* !_DEV_KBD_KBDREG_H_ */
