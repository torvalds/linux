/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#ifndef AUTOMOUNTD_H
#define	AUTOMOUNTD_H

#include <sys/queue.h>
#include <stdbool.h>

#define	AUTO_MASTER_PATH	"/etc/auto_master"
#define	AUTO_MAP_PREFIX		"/etc"
#define	AUTO_SPECIAL_PREFIX	"/etc/autofs"
#define	AUTO_INCLUDE_PATH	AUTO_SPECIAL_PREFIX "/include"

struct node {
	TAILQ_ENTRY(node)	n_next;
	TAILQ_HEAD(nodehead, node)	n_children;
	struct node		*n_parent;
	char			*n_key;
	char			*n_options;
	char			*n_location;
	char			*n_map;
	const char		*n_config_file;
	int			n_config_line;
};

struct defined_value {
	TAILQ_ENTRY(defined_value)	d_next;
	char				*d_name;
	char				*d_value;
};

void	log_init(int level);
void	log_set_peer_name(const char *name);
void	log_set_peer_addr(const char *addr);
void	log_err(int, const char *, ...)
	    __dead2 __printf0like(2, 3);
void	log_errx(int, const char *, ...)
	    __dead2 __printf0like(2, 3);
void	log_warn(const char *, ...) __printf0like(1, 2);
void	log_warnx(const char *, ...) __printflike(1, 2);
void	log_debugx(const char *, ...) __printf0like(1, 2);

char	*checked_strdup(const char *);
char	*concat(const char *s1, char separator, const char *s2);
void	create_directory(const char *path);

struct node	*node_new_root(void);
struct node	*node_new(struct node *parent, char *key, char *options,
		    char *location, const char *config_file, int config_line);
struct node	*node_new_map(struct node *parent, char *key, char *options,
		    char *map, const char *config_file, int config_line);
struct node	*node_find(struct node *root, const char *mountpoint);
bool		node_is_direct_map(const struct node *n);
bool		node_has_wildcards(const struct node *n);
char	*node_path(const struct node *n);
char	*node_options(const struct node *n);
void	node_expand_ampersand(struct node *root, const char *key);
void	node_expand_wildcard(struct node *root, const char *key);
int	node_expand_defined(struct node *root);
void	node_expand_indirect_maps(struct node *n);
void	node_print(const struct node *n, const char *cmdline_options);
void	parse_master(struct node *root, const char *path);
void	parse_map(struct node *parent, const char *map, const char *args,
	    bool *wildcards);
char	*defined_expand(const char *string);
void	defined_init(void);
void	defined_parse_and_add(char *def);
void	lesser_daemon(void);

int	main_automount(int argc, char **argv);
int	main_automountd(int argc, char **argv);
int	main_autounmountd(int argc, char **argv);

FILE	*auto_popen(const char *argv0, ...);
int	auto_pclose(FILE *iop);

/*
 * lex(1) stuff.
 */
extern int lineno;

#define	STR	1
#define	NEWLINE	2

#endif /* !AUTOMOUNTD_H */
