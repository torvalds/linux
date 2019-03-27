/* 
 * Copyright (c) 1998 Michael Smith.
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

/*
 * Manage an environment-like space in which string variables may be stored.
 * Provide support for some method-like operations for setting/retrieving
 * variables in order to allow some type strength.
 */

#include "stand.h"

#include <string.h>

static void	env_discard(struct env_var *ev);

struct env_var	*environ = NULL;

/*
 * Look up (name) and return it's env_var structure.
 */
struct env_var	*
env_getenv(const char *name)
{
    struct env_var	*ev;
    
    for (ev = environ; ev != NULL; ev = ev->ev_next)
	if (!strcmp(ev->ev_name, name))
	    break;
    return(ev);
}

/*
 * Some notes:
 *
 * If the EV_VOLATILE flag is set, a copy of the variable is made.
 * If EV_DYNAMIC is set, the variable has been allocated with
 * malloc and ownership transferred to the environment.
 * If (value) is NULL, the variable is set but has no value.
 */
int
env_setenv(const char *name, int flags, const void *value,
	   ev_sethook_t sethook, ev_unsethook_t unsethook)
{
    struct env_var	*ev, *curr, *last;

    if ((ev = env_getenv(name)) != NULL) {
	/*
	 * If there's a set hook, let it do the work (unless we are working
	 * for one already.
	 */
	if ((ev->ev_sethook != NULL) && !(flags & EV_NOHOOK))
	    return (ev->ev_sethook(ev, flags, value));

	/* If there is data in the variable, discard it. */
	if (ev->ev_value != NULL && (ev->ev_flags & EV_DYNAMIC) != 0)
	    free(ev->ev_value);
	ev->ev_value = NULL;
	ev->ev_flags &= ~EV_DYNAMIC;

    } else {

	/*
	 * New variable; create and sort into list
	 */
	ev = malloc(sizeof(struct env_var));
	ev->ev_name = strdup(name);
	ev->ev_value = NULL;
	ev->ev_flags = 0;
	/* hooks can only be set when the variable is instantiated */
	ev->ev_sethook = sethook;
	ev->ev_unsethook = unsethook;

	/* Sort into list */
	ev->ev_prev = NULL;
	ev->ev_next = NULL;
	/* Search for the record to insert before */
	for (last = NULL, curr = environ; 
	     curr != NULL; 
	     last = curr, curr = curr->ev_next) {

	    if (strcmp(ev->ev_name, curr->ev_name) < 0) {
		if (curr->ev_prev) {
		    curr->ev_prev->ev_next = ev;
		} else {
		    environ = ev;
		}
		ev->ev_next = curr;
		ev->ev_prev = curr->ev_prev;
		curr->ev_prev = ev;
		break;
	    }
	}
	if (curr == NULL) {
	    if (last == NULL) {
		environ = ev;
	    } else {
		last->ev_next = ev;
		ev->ev_prev = last;
	    }
	}
    }

    /* If we have a new value, use it */
    if (flags & EV_VOLATILE) {
	ev->ev_value = strdup(value);
	ev->ev_flags |= EV_DYNAMIC;
    } else {
	ev->ev_value = (char *)value;
	ev->ev_flags |= flags & EV_DYNAMIC;
    }

    return(0);
}

/* coverity[ -tainted_string_return_content ] */
char *
getenv(const char *name)
{
    struct env_var	*ev;

    /* Set but no value gives empty string */
    if ((ev = env_getenv(name)) != NULL) {
	if (ev->ev_value != NULL)
	    return(ev->ev_value);
	return("");
    }
    return(NULL);
}

int
setenv(const char *name, const char *value, int overwrite)
{
    /* No guarantees about state, always assume volatile */
    if (overwrite || (env_getenv(name) == NULL))
	return(env_setenv(name, EV_VOLATILE, value, NULL, NULL));
    return(0);
}

int
putenv(char *string)
{
    char	*value, *copy;
    int		result;

    copy = strdup(string);
    if ((value = strchr(copy, '=')) != NULL)
	*(value++) = 0;
    result = setenv(copy, value, 1);
    free(copy);
    return(result);
}

int
unsetenv(const char *name)
{
    struct env_var	*ev;
    int			err;

    err = 0;
    if ((ev = env_getenv(name)) == NULL) {
	err = ENOENT;
    } else {
	if (ev->ev_unsethook != NULL)
	    err = ev->ev_unsethook(ev);
	if (err == 0) {
	    env_discard(ev);
	}
    }
    return(err);
}

static void
env_discard(struct env_var *ev)
{
    if (ev->ev_prev)
	ev->ev_prev->ev_next = ev->ev_next;
    if (ev->ev_next)
	ev->ev_next->ev_prev = ev->ev_prev;
    if (environ == ev)
	environ = ev->ev_next;
    free(ev->ev_name);
    if (ev->ev_value != NULL && (ev->ev_flags & EV_DYNAMIC) != 0)
	free(ev->ev_value);
    free(ev);
}

int
env_noset(struct env_var *ev __unused, int flags __unused,
    const void *value __unused)
{
    return(EPERM);
}

int
env_nounset(struct env_var *ev __unused)
{
    return(EPERM);
}
