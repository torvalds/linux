/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *
 * Simple property list handling code.
 *
 * Copyright (c) 1998
 *	Jordan Hubbard.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    verbatim and that no modifications are made prior to this
 *    point in the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR HIS PETS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static properties
property_alloc(char *name, char *value)
{
    properties n;

    if ((n = (properties)malloc(sizeof(struct _property))) == NULL)
	return (NULL);
    n->next = NULL;
    if (name != NULL) {
	if ((n->name = strdup(name)) == NULL) {
	    free(n);
	    return (NULL);
	}
    } else
	n->name = NULL;
    if (value != NULL) {
	if ((n->value = strdup(value)) == NULL) {
	    free(n->name);
	    free(n);
	    return (NULL);
	}
    } else
	n->value = NULL;
    return (n);
}

properties
properties_read(int fd)
{
    properties head, ptr;
    char hold_n[PROPERTY_MAX_NAME + 1];
    char hold_v[PROPERTY_MAX_VALUE + 1];
    char buf[BUFSIZ * 4];
    int bp, n, v, max;
    enum { LOOK, COMMENT, NAME, VALUE, MVALUE, COMMIT, FILL, STOP } state, last_state;
    int ch = 0, blevel = 0;

    n = v = bp = max = 0;
    head = ptr = NULL;
    state = last_state = LOOK;
    while (state != STOP) {
	if (state != COMMIT) {
	    if (bp == max) {
		last_state = state;
		state = FILL;
	    } else
		ch = buf[bp++];
	}
	switch(state) {
	case FILL:
	    if ((max = read(fd, buf, sizeof buf)) < 0) {
		properties_free(head);
		return (NULL);
	    }
	    if (max == 0) {
		state = STOP;
	    } else {
		/*
		 * Restore the state from before the fill (which will be
		 * initialised to LOOK for the first FILL). This ensures that
		 * if we were part-way through eg., a VALUE state, when the
		 * buffer ran out, that the previous operation will be allowed
		 * to complete.
		 */
		state = last_state;
		ch = buf[0];
		bp = 0;
	    }
	    continue;

	case LOOK:
	    if (isspace((unsigned char)ch))
		continue;
	    /* Allow shell or lisp style comments */
	    else if (ch == '#' || ch == ';') {
		state = COMMENT;
		continue;
	    }
	    else if (isalnum((unsigned char)ch) || ch == '_') {
		if (n >= PROPERTY_MAX_NAME) {
		    n = 0;
		    state = COMMENT;
		}
		else {
		    hold_n[n++] = ch;
		    state = NAME;
		}
	    }
	    else
		state = COMMENT;	/* Ignore the rest of the line */
	    break;

	case COMMENT:
	    if (ch == '\n')
		state = LOOK;
	    break;

	case NAME:
	    if (ch == '\n' || !ch) {
		hold_n[n] = '\0';
		hold_v[0] = '\0';
		v = n = 0;
		state = COMMIT;
	    }
	    else if (isspace((unsigned char)ch))
		continue;
	    else if (ch == '=') {
		hold_n[n] = '\0';
		v = n = 0;
		state = VALUE;
	    }
	    else
		hold_n[n++] = ch;
	    break;

	case VALUE:
	    if (v == 0 && ch == '\n') {
	        hold_v[v] = '\0';
	        v = n = 0;
	        state = COMMIT;
	    } 
	    else if (v == 0 && isspace((unsigned char)ch))
		continue;
	    else if (ch == '{') {
		state = MVALUE;
		++blevel;
	    }
	    else if (ch == '\n' || !ch) {
		hold_v[v] = '\0';
		v = n = 0;
		state = COMMIT;
	    }
	    else {
		if (v >= PROPERTY_MAX_VALUE) {
		    state = COMMENT;
		    v = n = 0;
		    break;
		}
		else
		    hold_v[v++] = ch;
	    }
	    break;

	case MVALUE:
	    /* multiline value */
	    if (v >= PROPERTY_MAX_VALUE) {
		warn("properties_read: value exceeds max length");
		state = COMMENT;
		n = v = 0;
	    }
	    else if (ch == '}' && !--blevel) {
		hold_v[v] = '\0';
		v = n = 0;
		state = COMMIT;
	    }
	    else {
		hold_v[v++] = ch;
		if (ch == '{')
		    ++blevel;
	    }
	    break;

	case COMMIT:
	    if (head == NULL) {
		if ((head = ptr = property_alloc(hold_n, hold_v)) == NULL)
		    return (NULL);
	    } else {
		if ((ptr->next = property_alloc(hold_n, hold_v)) == NULL) {
		    properties_free(head);
		    return (NULL);
		}
		ptr = ptr->next;
	    }
	    state = LOOK;
	    v = n = 0;
	    break;

	case STOP:
	    /* we don't handle this here, but this prevents warnings */
	    break;
	}
    }
    if (head == NULL && (head = property_alloc(NULL, NULL)) == NULL)
	return (NULL);

    return (head);
}

char *
property_find(properties list, const char *name)
{
    if (list == NULL || name == NULL || !name[0])
	return (NULL);
    while (list != NULL) {
	if (list->name != NULL && strcmp(list->name, name) == 0)
	    return (list->value);
	list = list->next;
    }
    return (NULL);
}

void
properties_free(properties list)
{
    properties tmp;

    while (list) {
	tmp = list->next;
	if (list->name)
	    free(list->name);
	if (list->value)
	    free(list->value);
	free(list);
	list = tmp;
    }
}
