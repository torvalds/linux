/*-
 * Copyright (c) 2011 Google, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include "bootstrap.h"
#include "libuserboot.h"

int console;

static struct console *userboot_comconsp;

static void userboot_cons_probe(struct console *cp);
static int userboot_cons_init(int);
static void userboot_comcons_probe(struct console *cp);
static int userboot_comcons_init(int);
static void userboot_cons_putchar(int);
static int userboot_cons_getchar(void);
static int userboot_cons_poll(void);

struct console userboot_console = {
	"userboot",
	"userboot",
	0,
	userboot_cons_probe,
	userboot_cons_init,
	userboot_cons_putchar,
	userboot_cons_getchar,
	userboot_cons_poll,
};

/*
 * Provide a simple alias to allow loader scripts to set the
 * console to comconsole without resulting in an error
 */
struct console userboot_comconsole = {
	"comconsole",
	"comconsole",
	0,
	userboot_comcons_probe,
	userboot_comcons_init,
	userboot_cons_putchar,
	userboot_cons_getchar,
	userboot_cons_poll,
};

static void
userboot_cons_probe(struct console *cp)
{

	cp->c_flags |= (C_PRESENTIN | C_PRESENTOUT);
}

static int
userboot_cons_init(int arg)
{

	return (0);
}

static void
userboot_comcons_probe(struct console *cp)
{

	/*
	 * Save the console pointer so the comcons_init routine
	 * can set the C_PRESENT* flags. They are not set
	 * here to allow the existing userboot console to
	 * be elected the default.
	 */
	userboot_comconsp = cp;
}

static int
userboot_comcons_init(int arg)
{

	/*
	 * Set the C_PRESENT* flags to allow the comconsole
	 * to be selected as the active console
	 */
	userboot_comconsp->c_flags |= (C_PRESENTIN | C_PRESENTOUT);
	return (0);
}

static void
userboot_cons_putchar(int c)
{

        CALLBACK(putc, c);
}

static int
userboot_cons_getchar()
{

	return (CALLBACK(getc));
}

static int
userboot_cons_poll()
{

	return (CALLBACK(poll));
}
