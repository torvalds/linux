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
#include <netdb.h>
#include <nsswitch.h>
#include <stdlib.h>
#include <string.h>

#include "../debug.h"
#include "services.h"

static int services_marshal_func(struct servent *, char *, size_t *);
static int services_lookup_func(const char *, size_t, char **, size_t *);
static void *services_mp_init_func(void);
static int services_mp_lookup_func(char **, size_t *, void *);
static void services_mp_destroy_func(void *);

static int
services_marshal_func(struct servent *serv, char *buffer, size_t *buffer_size)
{
	struct servent	new_serv;
	size_t	desired_size;
	char	**alias;
	char	*p;
	size_t	size;
	size_t	aliases_size;

	TRACE_IN(services_marshal_func);
	desired_size = ALIGNBYTES + sizeof(struct servent) + sizeof(char *);
	if (serv->s_name != NULL)
		desired_size += strlen(serv->s_name) + 1;
	if (serv->s_proto != NULL)
		desired_size += strlen(serv->s_proto) + 1;

	aliases_size = 0;
	if (serv->s_aliases != NULL) {
		for (alias = serv->s_aliases; *alias; ++alias) {
			desired_size += strlen(*alias) + 1;
			++aliases_size;
		}

		desired_size += ALIGNBYTES + sizeof(char *) *
		    (aliases_size + 1);
	}

	if ((*buffer_size < desired_size) || (buffer == NULL)) {
		*buffer_size = desired_size;
		TRACE_OUT(services_marshal_func);
		return (NS_RETURN);
	}

	memcpy(&new_serv, serv, sizeof(struct servent));
	memset(buffer, 0, desired_size);

	*buffer_size = desired_size;
	p = buffer + sizeof(struct servent) + sizeof(char *);
	memcpy(buffer + sizeof(struct servent), &p, sizeof(char *));
	p = (char *)ALIGN(p);

	if (new_serv.s_name != NULL) {
		size = strlen(new_serv.s_name);
		memcpy(p, new_serv.s_name, size);
		new_serv.s_name = p;
		p += size + 1;
	}

	if (new_serv.s_proto != NULL) {
		size = strlen(new_serv.s_proto);
		memcpy(p, new_serv.s_proto, size);
		new_serv.s_proto = p;
		p += size + 1;
	}

	if (new_serv.s_aliases != NULL) {
		p = (char *)ALIGN(p);
		memcpy(p, new_serv.s_aliases, sizeof(char *) * aliases_size);
		new_serv.s_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (alias = new_serv.s_aliases; *alias; ++alias) {
			size = strlen(*alias);
			memcpy(p, *alias, size);
			*alias = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_serv, sizeof(struct servent));
	TRACE_OUT(services_marshal_func);
	return (NS_SUCCESS);
}

static int
services_lookup_func(const char *key, size_t key_size, char **buffer,
	size_t *buffer_size)
{
	enum nss_lookup_type lookup_type;
	char	*name = NULL;
	char	*proto = NULL;
	size_t	size, size2;
	int	port;

	struct servent *result;

	TRACE_IN(services_lookup_func);

	assert(buffer != NULL);
	assert(buffer_size != NULL);

	if (key_size < sizeof(enum nss_lookup_type)) {
		TRACE_OUT(passwd_lookup_func);
		return (NS_UNAVAIL);
	}
	memcpy(&lookup_type, key, sizeof(enum nss_lookup_type));

	switch (lookup_type) {
	case nss_lt_name:
		size = key_size - sizeof(enum nss_lookup_type);
		name = calloc(1, size + 1);
		assert(name != NULL);
		memcpy(name, key + sizeof(enum nss_lookup_type), size);

		size2 = strlen(name) + 1;

		if (size2 < size)
			proto = name + size2;
		else
			proto = NULL;
		break;
	case nss_lt_id:
		if (key_size < sizeof(enum nss_lookup_type) +
			sizeof(int)) {
			TRACE_OUT(passwd_lookup_func);
			return (NS_UNAVAIL);
		}

		memcpy(&port, key + sizeof(enum nss_lookup_type),
			sizeof(int));

		size = key_size - sizeof(enum nss_lookup_type) - sizeof(int);
		if (size > 0) {
			proto = calloc(1, size + 1);
			assert(proto != NULL);
			memcpy(proto, key + sizeof(enum nss_lookup_type) +
				sizeof(int), size);
		}
		break;
	default:
		TRACE_OUT(passwd_lookup_func);
		return (NS_UNAVAIL);
	}

	switch (lookup_type) {
	case nss_lt_name:
		result = getservbyname(name, proto);
		free(name);
		break;
	case nss_lt_id:
		result = getservbyport(port, proto);
		free(proto);
		break;
	default:
		/* SHOULD NOT BE REACHED */
		break;
	}

	if (result != NULL) {
		services_marshal_func(result, NULL, buffer_size);
		*buffer = malloc(*buffer_size);
		assert(*buffer != NULL);
		services_marshal_func(result, *buffer, buffer_size);
	}

	TRACE_OUT(services_lookup_func);
	return (result == NULL ? NS_NOTFOUND : NS_SUCCESS);
}

static void *
services_mp_init_func(void)
{
	TRACE_IN(services_mp_init_func);
	setservent(0);
	TRACE_OUT(services_mp_init_func);

	return (NULL);
}

static int
services_mp_lookup_func(char **buffer, size_t *buffer_size, void *mdata)
{
	struct servent *result;

	TRACE_IN(services_mp_lookup_func);
	result = getservent();
	if (result != NULL) {
		services_marshal_func(result, NULL, buffer_size);
		*buffer = malloc(*buffer_size);
		assert(*buffer != NULL);
		services_marshal_func(result, *buffer, buffer_size);
	}

	TRACE_OUT(services_mp_lookup_func);
	return (result == NULL ? NS_NOTFOUND : NS_SUCCESS);
}

static void
services_mp_destroy_func(void *mdata)
{
	TRACE_IN(services_mp_destroy_func);
	TRACE_OUT(services_mp_destroy_func);
}

struct agent *
init_services_agent(void)
{
	struct common_agent	*retval;
	TRACE_IN(init_services_agent);

	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	retval->parent.name = strdup("services");
	assert(retval->parent.name != NULL);

	retval->parent.type = COMMON_AGENT;
	retval->lookup_func = services_lookup_func;

	TRACE_OUT(init_services_agent);
	return ((struct agent *)retval);
}

struct agent *
init_services_mp_agent(void)
{
	struct multipart_agent	*retval;

	TRACE_IN(init_services_mp_agent);
	retval = calloc(1,
		sizeof(*retval));
	assert(retval != NULL);

	retval->parent.name = strdup("services");
	retval->parent.type = MULTIPART_AGENT;
	retval->mp_init_func = services_mp_init_func;
	retval->mp_lookup_func = services_mp_lookup_func;
	retval->mp_destroy_func = services_mp_destroy_func;
	assert(retval->parent.name != NULL);

	TRACE_OUT(init_services_mp_agent);
	return ((struct agent *)retval);
}
