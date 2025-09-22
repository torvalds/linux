/*	$OpenBSD: hidkbdsc.h,v 1.3 2022/11/09 10:05:18 robert Exp $	*/
/*      $NetBSD: ukbd.c,v 1.85 2003/03/11 16:44:00 augustss Exp $        */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define MAXKEYCODE 6
#define MAXVARS 128

#define MAXKEYS (MAXVARS+2*MAXKEYCODE)

/* quirks */
#define HIDKBD_SPUR_BUT_UP 0x001 /* spurious button up events */

struct hidkbd_variable {
	struct hid_location loc;
	u_int8_t	mask;
	u_int8_t	key;
};

struct hidkbd_data {
	u_int8_t	keycode[MAXKEYCODE];
	u_int8_t	var[MAXVARS];
};

struct hidkbd {
	/* stored data */
	struct hidkbd_data sc_ndata;
	struct hidkbd_data sc_odata;

	/* input reports */
	u_int sc_nvar;
	struct hidkbd_variable *sc_var;

	struct hid_location sc_keycodeloc;
	u_int sc_nkeycode;

	/* output reports */
	struct hid_location sc_numloc;
	struct hid_location sc_capsloc;
	struct hid_location sc_scroloc;
	struct hid_location sc_compose;
	int sc_leds;

	/* optional extra input source used by sc_munge */
	struct hid_location sc_fn;

	/* state information */
	struct device *sc_device;
	struct device *sc_wskbddev;
	char sc_enabled;

	char sc_console_keyboard;	/* we are the console keyboard */

	char sc_debounce;		/* for quirk handling */
	struct timeout sc_delay;	/* for quirk handling */
	struct hidkbd_data sc_data;	/* for quirk handling */

	/* key repeat logic */
#if defined(WSDISPLAY_COMPAT_RAWKBD)
	int sc_rawkbd;
#endif /* defined(WSDISPLAY_COMPAT_RAWKBD) */

	int sc_polling;
	int sc_npollchar;
	u_int16_t sc_pollchars[MAXKEYS];

	void (*sc_munge)(void *, uint8_t *, u_int);
};

struct hidkbd_translation {
	uint8_t original;
	uint8_t translation;
};

int	hidkbd_attach(struct device *, struct hidkbd *, int, uint32_t,
	    int, void *, int);
void	hidkbd_attach_wskbd(struct hidkbd *, kbd_t,
	    const struct wskbd_accessops *);
void	hidkbd_bell(u_int, u_int, u_int, int);
void	hidkbd_cngetc(struct hidkbd *, u_int *, int *);
int	hidkbd_detach(struct hidkbd *, int);
int	hidkbd_enable(struct hidkbd *, int);
void	hidkbd_input(struct hidkbd *, uint8_t *, u_int);
int	hidkbd_ioctl(struct hidkbd *, u_long, caddr_t, int, struct proc *);
int	hidkbd_set_leds(struct hidkbd *, int, uint8_t *);
uint8_t	hidkbd_translate(const struct hidkbd_translation *, size_t, uint8_t);
void	hidkbd_apple_munge(void *, uint8_t *, u_int);
void	hidkbd_apple_tb_munge(void *, uint8_t *, u_int);
void	hidkbd_apple_iso_munge(void *, uint8_t *, u_int);
void	hidkbd_apple_mba_munge(void *, uint8_t *, u_int);
void	hidkbd_apple_iso_mba_munge(void *, uint8_t *, u_int);

extern int hidkbd_is_console;
