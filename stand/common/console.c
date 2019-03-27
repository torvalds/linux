/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
#include <string.h>

#include "bootstrap.h"
/*
 * Core console support
 */

static int	cons_set(struct env_var *ev, int flags, const void *value);
static int	cons_find(const char *name);
static int	cons_check(const char *string);
static int	cons_change(const char *string);
static int	twiddle_set(struct env_var *ev, int flags, const void *value);

/*
 * Detect possible console(s) to use.  If preferred console(s) have been
 * specified, mark them as active. Else, mark the first probed console
 * as active.  Also create the console variable.
 */
void
cons_probe(void)
{
    int			cons;
    int			active;
    char		*prefconsole;

    /* We want a callback to install the new value when this var changes. */
    env_setenv("twiddle_divisor", EV_VOLATILE, "1", twiddle_set, env_nounset);

    /* Do all console probes */
    for (cons = 0; consoles[cons] != NULL; cons++) {
	consoles[cons]->c_flags = 0;
 	consoles[cons]->c_probe(consoles[cons]);
    }
    /* Now find the first working one */
    active = -1;
    for (cons = 0; consoles[cons] != NULL && active == -1; cons++) {
	consoles[cons]->c_flags = 0;
 	consoles[cons]->c_probe(consoles[cons]);
	if (consoles[cons]->c_flags == (C_PRESENTIN | C_PRESENTOUT))
	    active = cons;
    }
    /* Force a console even if all probes failed */
    if (active == -1)
	active = 0;

    /* Check to see if a console preference has already been registered */
    prefconsole = getenv("console");
    if (prefconsole != NULL)
	prefconsole = strdup(prefconsole);
    if (prefconsole != NULL) {
	unsetenv("console");		/* we want to replace this */
	cons_change(prefconsole);
    } else {
	consoles[active]->c_flags |= C_ACTIVEIN | C_ACTIVEOUT;
	consoles[active]->c_init(0);
	prefconsole = strdup(consoles[active]->c_name);
    }

    printf("Consoles: ");
    for (cons = 0; consoles[cons] != NULL; cons++)
	if (consoles[cons]->c_flags & (C_ACTIVEIN | C_ACTIVEOUT))
	    printf("%s  ", consoles[cons]->c_desc);
    printf("\n");

    if (prefconsole != NULL) {
	env_setenv("console", EV_VOLATILE, prefconsole, cons_set,
	    env_nounset);
	free(prefconsole);
    }
}

int
getchar(void)
{
    int		cons;
    int		rv;

    /* Loop forever polling all active consoles */
    for(;;)
	for (cons = 0; consoles[cons] != NULL; cons++)
	    if ((consoles[cons]->c_flags & (C_PRESENTIN | C_ACTIVEIN)) ==
		(C_PRESENTIN | C_ACTIVEIN) &&
		((rv = consoles[cons]->c_in()) != -1))
		return(rv);
}

int
ischar(void)
{
    int		cons;

    for (cons = 0; consoles[cons] != NULL; cons++)
	if ((consoles[cons]->c_flags & (C_PRESENTIN | C_ACTIVEIN)) ==
	    (C_PRESENTIN | C_ACTIVEIN) &&
	    (consoles[cons]->c_ready() != 0))
		return(1);
    return(0);
}

void
putchar(int c)
{
    int		cons;

    /* Expand newlines */
    if (c == '\n')
	putchar('\r');

    for (cons = 0; consoles[cons] != NULL; cons++)
	if ((consoles[cons]->c_flags & (C_PRESENTOUT | C_ACTIVEOUT)) ==
	    (C_PRESENTOUT | C_ACTIVEOUT))
	    consoles[cons]->c_out(c);
}

/*
 * Find the console with the specified name.
 */
static int
cons_find(const char *name)
{
    int		cons;

    for (cons = 0; consoles[cons] != NULL; cons++)
	if (!strcmp(consoles[cons]->c_name, name))
	    return (cons);
    return (-1);
}

/*
 * Select one or more consoles.
 */
static int
cons_set(struct env_var *ev, int flags, const void *value)
{
    int		ret;

    if ((value == NULL) || (cons_check(value) == 0)) {
	/*
	 * Return CMD_OK instead of CMD_ERROR to prevent forth syntax error,
	 * which would prevent it processing any further loader.conf entries.
	 */
	return (CMD_OK);
    }

    ret = cons_change(value);
    if (ret != CMD_OK)
	return (ret);

    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);
    return (CMD_OK);
}

/*
 * Check that at least one the consoles listed in *string is valid
 */
static int
cons_check(const char *string)
{
    int		cons, found, failed;
    char	*curpos, *dup, *next;

    dup = next = strdup(string);
    found = failed = 0;
    while (next != NULL) {
	curpos = strsep(&next, " ,");
	if (*curpos != '\0') {
	    cons = cons_find(curpos);
	    if (cons == -1) {
		printf("console %s is invalid!\n", curpos);
		failed++;
	    } else {
		found++;
	    }
	}
    }

    free(dup);

    if (found == 0)
	printf("no valid consoles!\n");

    if (found == 0 || failed != 0) {
	printf("Available consoles:\n");
	for (cons = 0; consoles[cons] != NULL; cons++)
	    printf("    %s\n", consoles[cons]->c_name);
    }

    return (found);
}

/*
 * Activate all the valid consoles listed in *string and disable all others.
 */
static int
cons_change(const char *string)
{
    int		cons, active;
    char	*curpos, *dup, *next;

    /* Disable all consoles */
    for (cons = 0; consoles[cons] != NULL; cons++) {
	consoles[cons]->c_flags &= ~(C_ACTIVEIN | C_ACTIVEOUT);
    }

    /* Enable selected consoles */
    dup = next = strdup(string);
    active = 0;
    while (next != NULL) {
	curpos = strsep(&next, " ,");
	if (*curpos == '\0')
		continue;
	cons = cons_find(curpos);
	if (cons >= 0) {
	    consoles[cons]->c_flags |= C_ACTIVEIN | C_ACTIVEOUT;
	    consoles[cons]->c_init(0);
	    if ((consoles[cons]->c_flags & (C_PRESENTIN | C_PRESENTOUT)) ==
		(C_PRESENTIN | C_PRESENTOUT)) {
		active++;
		continue;
	    }

	    if (active != 0) {
		/* If no consoles have initialised we wouldn't see this. */
		printf("console %s failed to initialize\n", consoles[cons]->c_name);
	    }
	}
    }

    free(dup);

    if (active == 0) {
	/* All requested consoles failed to initialise, try to recover. */
	for (cons = 0; consoles[cons] != NULL; cons++) {
	    consoles[cons]->c_flags |= C_ACTIVEIN | C_ACTIVEOUT;
	    consoles[cons]->c_init(0);
	    if ((consoles[cons]->c_flags &
		(C_PRESENTIN | C_PRESENTOUT)) ==
		(C_PRESENTIN | C_PRESENTOUT))
		active++;
	}

	if (active == 0)
	    return (CMD_ERROR); /* Recovery failed. */
    }

    return (CMD_OK);
}

/*
 * Change the twiddle divisor.
 *
 * The user can set the twiddle_divisor variable to directly control how fast
 * the progress twiddle spins, useful for folks with slow serial consoles.  The
 * code to monitor changes to the variable and propagate them to the twiddle
 * routines has to live somewhere.  Twiddling is console-related so it's here.
 */
static int
twiddle_set(struct env_var *ev, int flags, const void *value)
{
    u_long tdiv;
    char * eptr;

    tdiv = strtoul(value, &eptr, 0);
    if (*(const char *)value == 0 || *eptr != 0) {
	printf("invalid twiddle_divisor '%s'\n", (const char *)value);
	return (CMD_ERROR);
    }
    twiddle_divisor((u_int)tdiv);
    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);

    return(CMD_OK);
}
