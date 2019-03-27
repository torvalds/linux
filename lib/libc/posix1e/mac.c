/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
 * Copyright (c) 2002, 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mac.h>

static int	internal_initialized;

/*
 * Maintain a list of default label preparations for various object
 * types.  Each name will appear only once in the list.
 *
 * XXXMAC: Not thread-safe.
 */
static LIST_HEAD(, label_default) label_default_head;
struct label_default {
	char				*ld_name;
	char				*ld_labels;
	LIST_ENTRY(label_default)	 ld_entries;
};

static void
mac_destroy_labels(void)
{
	struct label_default *ld;

	while ((ld = LIST_FIRST(&label_default_head))) {
		free(ld->ld_name);
		free(ld->ld_labels);
		LIST_REMOVE(ld, ld_entries);
		free(ld);
	}
}

static void
mac_destroy_internal(void)
{

	mac_destroy_labels();

	internal_initialized = 0;
}

static int
mac_add_type(const char *name, const char *labels)
{
	struct label_default *ld, *ld_new;
	char *name_dup, *labels_dup;

	/*
	 * Speculatively allocate all the memory now to avoid allocating
	 * later when we will someday hold a mutex.
	 */
	name_dup = strdup(name);
	if (name_dup == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	labels_dup = strdup(labels);
	if (labels_dup == NULL) {
		free(name_dup);
		errno = ENOMEM;
		return (-1);
	}
	ld_new = malloc(sizeof(*ld));
	if (ld_new == NULL) {
		free(name_dup);
		free(labels_dup);
		errno = ENOMEM;
		return (-1);
	}

	/*
	 * If the type is already present, replace the current entry
	 * rather than add a new instance.
	 */
	for (ld = LIST_FIRST(&label_default_head); ld != NULL;
	    ld = LIST_NEXT(ld, ld_entries)) {
		if (strcmp(name, ld->ld_name) == 0)
			break;
	}

	if (ld != NULL) {
		free(ld->ld_labels);
		ld->ld_labels = labels_dup;
		labels_dup = NULL;
	} else {
		ld = ld_new;
		ld->ld_name = name_dup;
		ld->ld_labels = labels_dup;

		ld_new = NULL;
		name_dup = NULL;
		labels_dup = NULL;

		LIST_INSERT_HEAD(&label_default_head, ld, ld_entries);
	}

	if (name_dup != NULL)
		free(name_dup);
	if (labels_dup != NULL)
		free(labels_dup);
	if (ld_new != NULL)
		free(ld_new);

	return (0);
}

static char *
next_token(char **string)
{
	char *token;

	token = strsep(string, " \t");
	while (token != NULL && *token == '\0')
		token = strsep(string, " \t");

	return (token);
}

static int
mac_init_internal(int ignore_errors)
{
	const char *filename;
	char line[LINE_MAX];
	FILE *file;
	int error;

	error = 0;

	LIST_INIT(&label_default_head);

	if (!issetugid() && getenv("MAC_CONFFILE") != NULL)
		filename = getenv("MAC_CONFFILE");
	else
		filename = MAC_CONFFILE;
	file = fopen(filename, "re");
	if (file == NULL)
		return (0);

	while (fgets(line, LINE_MAX, file)) {
		char *comment, *parse, *statement;

		if (line[strlen(line)-1] == '\n')
			line[strlen(line)-1] = '\0';
		else {
			if (ignore_errors)
				continue;
			fclose(file);
			error = EINVAL;
			goto just_return;
		}

		/* Remove any comment. */
		comment = line;
		parse = strsep(&comment, "#");

		/* Blank lines OK. */
		statement = next_token(&parse);
		if (statement == NULL)
			continue;

		if (strcmp(statement, "default_labels") == 0) {
			char *name, *labels;

			name = next_token(&parse);
			labels = next_token(&parse);
			if (name == NULL || labels == NULL ||
			    next_token(&parse) != NULL) {
				if (ignore_errors)
					continue;
				error = EINVAL;
				fclose(file);
				goto just_return;
			}

			if (mac_add_type(name, labels) == -1) {
				if (ignore_errors)
					continue;
				fclose(file);
				goto just_return;
			}
		} else if (strcmp(statement, "default_ifnet_labels") == 0 ||
		    strcmp(statement, "default_file_labels") == 0 ||
		    strcmp(statement, "default_process_labels") == 0) {
			char *labels, *type;

			if (strcmp(statement, "default_ifnet_labels") == 0)
				type = "ifnet";
			else if (strcmp(statement, "default_file_labels") == 0)
				type = "file";
			else if (strcmp(statement, "default_process_labels") ==
			    0)
				type = "process";

			labels = next_token(&parse);
			if (labels == NULL || next_token(&parse) != NULL) {
				if (ignore_errors)
					continue;
				error = EINVAL;
				fclose(file);
				goto just_return;
			}

			if (mac_add_type(type, labels) == -1) {
				if (ignore_errors)
					continue;
				fclose(file);
				goto just_return;
			}
		} else {
			if (ignore_errors)
				continue;
			fclose(file);
			error = EINVAL;
			goto just_return;
		}
	}

	fclose(file);

	internal_initialized = 1;

just_return:
	if (error != 0)
		mac_destroy_internal();
	return (error);
}

static int
mac_maybe_init_internal(void)
{

	if (!internal_initialized)
		return (mac_init_internal(1));
	else
		return (0);
}

int
mac_reload(void)
{

	if (internal_initialized)
		mac_destroy_internal();
	return (mac_init_internal(0));
}

int
mac_free(struct mac *mac)
{

	if (mac->m_string != NULL)
		free(mac->m_string);
	free(mac);

	return (0);
}

int
mac_from_text(struct mac **mac, const char *text)
{

	*mac = (struct mac *) malloc(sizeof(**mac));
	if (*mac == NULL)
		return (ENOMEM);

	(*mac)->m_string = strdup(text);
	if ((*mac)->m_string == NULL) {
		free(*mac);
		*mac = NULL;
		return (ENOMEM);
	}

	(*mac)->m_buflen = strlen((*mac)->m_string)+1;

	return (0);
}

int
mac_to_text(struct mac *mac, char **text)
{

	*text = strdup(mac->m_string);
	if (*text == NULL)
		return (ENOMEM);
	return (0);
}

int
mac_prepare(struct mac **mac, const char *elements)
{

	if (strlen(elements) >= MAC_MAX_LABEL_BUF_LEN)
		return (EINVAL);

	*mac = (struct mac *) malloc(sizeof(**mac));
	if (*mac == NULL)
		return (ENOMEM);

	(*mac)->m_string = malloc(MAC_MAX_LABEL_BUF_LEN);
	if ((*mac)->m_string == NULL) {
		free(*mac);
		*mac = NULL;
		return (ENOMEM);
	}

	strcpy((*mac)->m_string, elements);
	(*mac)->m_buflen = MAC_MAX_LABEL_BUF_LEN;

	return (0);
}

int
mac_prepare_type(struct mac **mac, const char *name)
{
	struct label_default *ld;
	int error;

	error = mac_maybe_init_internal();
	if (error != 0)
		return (error);

	for (ld = LIST_FIRST(&label_default_head); ld != NULL;
	    ld = LIST_NEXT(ld, ld_entries)) {
		if (strcmp(name, ld->ld_name) == 0)
			return (mac_prepare(mac, ld->ld_labels));
	}

	errno = ENOENT;
	return (-1);		/* XXXMAC: ENOLABEL */
}

int
mac_prepare_ifnet_label(struct mac **mac)
{

	return (mac_prepare_type(mac, "ifnet"));
}

int
mac_prepare_file_label(struct mac **mac)
{

	return (mac_prepare_type(mac, "file"));
}

int
mac_prepare_packet_label(struct mac **mac)
{

	return (mac_prepare_type(mac, "packet"));
}

int
mac_prepare_process_label(struct mac **mac)
{

	return (mac_prepare_type(mac, "process"));
}

/*
 * Simply test whether the TrustedBSD/MAC MIB tree is present; if so,
 * return 1 to indicate that the system has MAC enabled overall or for
 * a given policy.
 */
int
mac_is_present(const char *policyname)
{
	int mib[5];
	size_t siz;
	char *mibname;
	int error;

	if (policyname != NULL) {
		if (policyname[strcspn(policyname, ".=")] != '\0') {
			errno = EINVAL;
			return (-1);
		}
		mibname = malloc(sizeof("security.mac.") - 1 +
		    strlen(policyname) + sizeof(".enabled"));
		if (mibname == NULL)
			return (-1);
		strcpy(mibname, "security.mac.");
		strcat(mibname, policyname);
		strcat(mibname, ".enabled");
		siz = 5;
		error = sysctlnametomib(mibname, mib, &siz);
		free(mibname);
	} else {
		siz = 3;
		error = sysctlnametomib("security.mac", mib, &siz);
	}
	if (error == -1) {
		switch (errno) {
		case ENOTDIR:
		case ENOENT:
			return (0);
		default:
			return (error);
		}
	}
	return (1);
}
