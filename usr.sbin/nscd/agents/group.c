/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <assert.h>
#include <grp.h>
#include <nsswitch.h>
#include <stdlib.h>
#include <string.h>

#include "../debug.h"
#include "group.h"

static int group_marshal_func(struct group *, char *, size_t *);
static int group_lookup_func(const char *, size_t, char **, size_t *);
static void *group_mp_init_func(void);
static int group_mp_lookup_func(char **, size_t *, void *);
static void group_mp_destroy_func(void *);

static int
group_marshal_func(struct group *grp, char *buffer, size_t *buffer_size)
{
	struct group new_grp;
	size_t desired_size, size, mem_size;
	char *p, **mem;

	TRACE_IN(group_marshal_func);
	desired_size = ALIGNBYTES + sizeof(struct group) + sizeof(char *);

	if (grp->gr_name != NULL)
		desired_size += strlen(grp->gr_name) + 1;
	if (grp->gr_passwd != NULL)
		desired_size += strlen(grp->gr_passwd) + 1;

	if (grp->gr_mem != NULL) {
		mem_size = 0;
		for (mem = grp->gr_mem; *mem; ++mem) {
			desired_size += strlen(*mem) + 1;
			++mem_size;
		}

		desired_size += ALIGNBYTES + (mem_size + 1) * sizeof(char *);
	}

	if ((desired_size > *buffer_size) || (buffer == NULL)) {
		*buffer_size = desired_size;
		TRACE_OUT(group_marshal_func);
		return (NS_RETURN);
	}

	memcpy(&new_grp, grp, sizeof(struct group));
	memset(buffer, 0, desired_size);

	*buffer_size = desired_size;
	p = buffer + sizeof(struct group) + sizeof(char *);
	memcpy(buffer + sizeof(struct group), &p, sizeof(char *));
	p = (char *)ALIGN(p);

	if (new_grp.gr_name != NULL) {
		size = strlen(new_grp.gr_name);
		memcpy(p, new_grp.gr_name, size);
		new_grp.gr_name = p;
		p += size + 1;
	}

	if (new_grp.gr_passwd != NULL) {
		size = strlen(new_grp.gr_passwd);
		memcpy(p, new_grp.gr_passwd, size);
		new_grp.gr_passwd = p;
		p += size + 1;
	}

	if (new_grp.gr_mem != NULL) {
		p = (char *)ALIGN(p);
		memcpy(p, new_grp.gr_mem, sizeof(char *) * mem_size);
		new_grp.gr_mem = (char **)p;
		p += sizeof(char *) * (mem_size + 1);

		for (mem = new_grp.gr_mem; *mem; ++mem) {
			size = strlen(*mem);
			memcpy(p, *mem, size);
			*mem = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_grp, sizeof(struct group));
	TRACE_OUT(group_marshal_func);
	return (NS_SUCCESS);
}

static int
group_lookup_func(const char *key, size_t key_size, char **buffer,
	size_t *buffer_size)
{
	enum nss_lookup_type lookup_type;
	char	*name;
	size_t	size;
	gid_t	gid;

	struct group *result;

	TRACE_IN(group_lookup_func);
	assert(buffer != NULL);
	assert(buffer_size != NULL);

	if (key_size < sizeof(enum nss_lookup_type)) {
		TRACE_OUT(group_lookup_func);
		return (NS_UNAVAIL);
	}
	memcpy(&lookup_type, key, sizeof(enum nss_lookup_type));

	switch (lookup_type) {
	case nss_lt_name:
		size = key_size - sizeof(enum nss_lookup_type)	+ 1;
		name = calloc(1, size);
		assert(name != NULL);
		memcpy(name, key + sizeof(enum nss_lookup_type), size - 1);
		break;
	case nss_lt_id:
		if (key_size < sizeof(enum nss_lookup_type) +
			sizeof(gid_t)) {
			TRACE_OUT(passwd_lookup_func);
			return (NS_UNAVAIL);
		}

		memcpy(&gid, key + sizeof(enum nss_lookup_type), sizeof(gid_t));
		break;
	default:
		TRACE_OUT(group_lookup_func);
		return (NS_UNAVAIL);
	}

	switch (lookup_type) {
	case nss_lt_name:
		TRACE_STR(name);
		result = getgrnam(name);
		free(name);
		break;
	case nss_lt_id:
		result = getgrgid(gid);
		break;
	default:
		/* SHOULD NOT BE REACHED */
		break;
	}

	if (result != NULL) {
		group_marshal_func(result, NULL, buffer_size);
		*buffer = malloc(*buffer_size);
		assert(*buffer != NULL);
		group_marshal_func(result, *buffer, buffer_size);
	}

	TRACE_OUT(group_lookup_func);
	return (result == NULL ? NS_NOTFOUND : NS_SUCCESS);
}

static void *
group_mp_init_func(void)
{
	TRACE_IN(group_mp_init_func);
	setgrent();
	TRACE_OUT(group_mp_init_func);

	return (NULL);
}

static int
group_mp_lookup_func(char **buffer, size_t *buffer_size, void *mdata)
{
	struct group *result;

	TRACE_IN(group_mp_lookup_func);
	result = getgrent();
	if (result != NULL) {
		group_marshal_func(result, NULL, buffer_size);
		*buffer = malloc(*buffer_size);
		assert(*buffer != NULL);
		group_marshal_func(result, *buffer, buffer_size);
	}

	TRACE_OUT(group_mp_lookup_func);
	return (result == NULL ? NS_NOTFOUND : NS_SUCCESS);
}

static void
group_mp_destroy_func(void *mdata)
{
	TRACE_IN(group_mp_destroy_func);
	TRACE_OUT(group_mp_destroy_func);
}

struct agent *
init_group_agent(void)
{
	struct common_agent	*retval;

	TRACE_IN(init_group_agent);
	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->parent.name = strdup("group");
	assert(retval->parent.name != NULL);

	retval->parent.type = COMMON_AGENT;
	retval->lookup_func = group_lookup_func;

	TRACE_OUT(init_group_agent);
	return ((struct agent *)retval);
}

struct agent *
init_group_mp_agent(void)
{
	struct multipart_agent	*retval;

	TRACE_IN(init_group_mp_agent);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	retval->parent.name = strdup("group");
	retval->parent.type = MULTIPART_AGENT;
	retval->mp_init_func = group_mp_init_func;
	retval->mp_lookup_func = group_mp_lookup_func;
	retval->mp_destroy_func = group_mp_destroy_func;
	assert(retval->parent.name != NULL);

	TRACE_OUT(init_group_mp_agent);
	return ((struct agent *)retval);
}
