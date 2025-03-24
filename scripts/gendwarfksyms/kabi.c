// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>

#include "gendwarfksyms.h"

#define KABI_RULE_SECTION ".discard.gendwarfksyms.kabi_rules"
#define KABI_RULE_VERSION "1"

/*
 * The rule section consists of four null-terminated strings per
 * entry:
 *
 *   1. version
 *      Entry format version. Must match KABI_RULE_VERSION.
 *
 *   2. type
 *      Type of the kABI rule. Must be one of the tags defined below.
 *
 *   3. target
 *      Rule-dependent target, typically the fully qualified name of
 *      the target DIE.
 *
 *   4. value
 *      Rule-dependent value.
 */
#define KABI_RULE_MIN_ENTRY_SIZE                                  \
	(/* version\0 */ 2 + /* type\0 */ 2 + /* target\0" */ 1 + \
	 /* value\0 */ 1)
#define KABI_RULE_EMPTY_VALUE ""

/*
 * Rule: declonly
 * - For the struct/enum/union in the target field, treat it as a
 *   declaration only even if a definition is available.
 */
#define KABI_RULE_TAG_DECLONLY "declonly"

/*
 * Rule: enumerator_ignore
 * - For the enum_field in the target field, ignore the enumerator.
 */
#define KABI_RULE_TAG_ENUMERATOR_IGNORE "enumerator_ignore"

/*
 * Rule: enumerator_value
 * - For the fqn_field in the target field, set the value to the
 *   unsigned integer in the value field.
 */
#define KABI_RULE_TAG_ENUMERATOR_VALUE "enumerator_value"

enum kabi_rule_type {
	KABI_RULE_TYPE_UNKNOWN,
	KABI_RULE_TYPE_DECLONLY,
	KABI_RULE_TYPE_ENUMERATOR_IGNORE,
	KABI_RULE_TYPE_ENUMERATOR_VALUE,
};

#define RULE_HASH_BITS 7

struct rule {
	enum kabi_rule_type type;
	const char *target;
	const char *value;
	struct hlist_node hash;
};

/* { type, target } -> struct rule */
static HASHTABLE_DEFINE(rules, 1 << RULE_HASH_BITS);

static inline unsigned int rule_values_hash(enum kabi_rule_type type,
					    const char *target)
{
	return hash_32(type) ^ hash_str(target);
}

static inline unsigned int rule_hash(const struct rule *rule)
{
	return rule_values_hash(rule->type, rule->target);
}

static inline const char *get_rule_field(const char **pos, ssize_t *left)
{
	const char *start = *pos;
	size_t len;

	if (*left <= 0)
		error("unexpected end of kABI rules");

	len = strnlen(start, *left) + 1;
	*pos += len;
	*left -= len;

	return start;
}

void kabi_read_rules(int fd)
{
	GElf_Shdr shdr_mem;
	GElf_Shdr *shdr;
	Elf_Data *rule_data = NULL;
	Elf_Scn *scn;
	Elf *elf;
	size_t shstrndx;
	const char *rule_str;
	ssize_t left;
	int i;

	const struct {
		enum kabi_rule_type type;
		const char *tag;
	} rule_types[] = {
		{
			.type = KABI_RULE_TYPE_DECLONLY,
			.tag = KABI_RULE_TAG_DECLONLY,
		},
		{
			.type = KABI_RULE_TYPE_ENUMERATOR_IGNORE,
			.tag = KABI_RULE_TAG_ENUMERATOR_IGNORE,
		},
		{
			.type = KABI_RULE_TYPE_ENUMERATOR_VALUE,
			.tag = KABI_RULE_TAG_ENUMERATOR_VALUE,
		},
	};

	if (!stable)
		return;

	if (elf_version(EV_CURRENT) != EV_CURRENT)
		error("elf_version failed: %s", elf_errmsg(-1));

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf)
		error("elf_begin failed: %s", elf_errmsg(-1));

	if (elf_getshdrstrndx(elf, &shstrndx) < 0)
		error("elf_getshdrstrndx failed: %s", elf_errmsg(-1));

	scn = elf_nextscn(elf, NULL);

	while (scn) {
		const char *sname;

		shdr = gelf_getshdr(scn, &shdr_mem);
		if (!shdr)
			error("gelf_getshdr failed: %s", elf_errmsg(-1));

		sname = elf_strptr(elf, shstrndx, shdr->sh_name);
		if (!sname)
			error("elf_strptr failed: %s", elf_errmsg(-1));

		if (!strcmp(sname, KABI_RULE_SECTION)) {
			rule_data = elf_getdata(scn, NULL);
			if (!rule_data)
				error("elf_getdata failed: %s", elf_errmsg(-1));
			break;
		}

		scn = elf_nextscn(elf, scn);
	}

	if (!rule_data) {
		debug("kABI rules not found");
		check(elf_end(elf));
		return;
	}

	rule_str = rule_data->d_buf;
	left = shdr->sh_size;

	if (left < KABI_RULE_MIN_ENTRY_SIZE)
		error("kABI rule section too small: %zd bytes", left);

	if (rule_str[left - 1] != '\0')
		error("kABI rules are not null-terminated");

	while (left > KABI_RULE_MIN_ENTRY_SIZE) {
		enum kabi_rule_type type = KABI_RULE_TYPE_UNKNOWN;
		const char *field;
		struct rule *rule;

		/* version */
		field = get_rule_field(&rule_str, &left);

		if (strcmp(field, KABI_RULE_VERSION))
			error("unsupported kABI rule version: '%s'", field);

		/* type */
		field = get_rule_field(&rule_str, &left);

		for (i = 0; i < ARRAY_SIZE(rule_types); i++) {
			if (!strcmp(field, rule_types[i].tag)) {
				type = rule_types[i].type;
				break;
			}
		}

		if (type == KABI_RULE_TYPE_UNKNOWN)
			error("unsupported kABI rule type: '%s'", field);

		rule = xmalloc(sizeof(struct rule));

		rule->type = type;
		rule->target = xstrdup(get_rule_field(&rule_str, &left));
		rule->value = xstrdup(get_rule_field(&rule_str, &left));

		hash_add(rules, &rule->hash, rule_hash(rule));

		debug("kABI rule: type: '%s', target: '%s', value: '%s'", field,
		      rule->target, rule->value);
	}

	if (left > 0)
		warn("unexpected data at the end of the kABI rules section");

	check(elf_end(elf));
}

bool kabi_is_declonly(const char *fqn)
{
	struct rule *rule;

	if (!stable)
		return false;
	if (!fqn || !*fqn)
		return false;

	hash_for_each_possible(rules, rule, hash,
			       rule_values_hash(KABI_RULE_TYPE_DECLONLY, fqn)) {
		if (rule->type == KABI_RULE_TYPE_DECLONLY &&
		    !strcmp(fqn, rule->target))
			return true;
	}

	return false;
}

static char *get_enumerator_target(const char *fqn, const char *field)
{
	char *target = NULL;

	if (asprintf(&target, "%s %s", fqn, field) < 0)
		error("asprintf failed for '%s %s'", fqn, field);

	return target;
}

static unsigned long get_ulong_value(const char *value)
{
	unsigned long result = 0;
	char *endptr = NULL;

	errno = 0;
	result = strtoul(value, &endptr, 10);

	if (errno || *endptr)
		error("invalid unsigned value '%s'", value);

	return result;
}

bool kabi_is_enumerator_ignored(const char *fqn, const char *field)
{
	bool match = false;
	struct rule *rule;
	char *target;

	if (!stable)
		return false;
	if (!fqn || !*fqn || !field || !*field)
		return false;

	target = get_enumerator_target(fqn, field);

	hash_for_each_possible(
		rules, rule, hash,
		rule_values_hash(KABI_RULE_TYPE_ENUMERATOR_IGNORE, target)) {
		if (rule->type == KABI_RULE_TYPE_ENUMERATOR_IGNORE &&
		    !strcmp(target, rule->target)) {
			match = true;
			break;
		}
	}

	free(target);
	return match;
}

bool kabi_get_enumerator_value(const char *fqn, const char *field,
			       unsigned long *value)
{
	bool match = false;
	struct rule *rule;
	char *target;

	if (!stable)
		return false;
	if (!fqn || !*fqn || !field || !*field)
		return false;

	target = get_enumerator_target(fqn, field);

	hash_for_each_possible(rules, rule, hash,
			       rule_values_hash(KABI_RULE_TYPE_ENUMERATOR_VALUE,
						target)) {
		if (rule->type == KABI_RULE_TYPE_ENUMERATOR_VALUE &&
		    !strcmp(target, rule->target)) {
			*value = get_ulong_value(rule->value);
			match = true;
			break;
		}
	}

	free(target);
	return match;
}

void kabi_free(void)
{
	struct hlist_node *tmp;
	struct rule *rule;

	hash_for_each_safe(rules, rule, tmp, hash) {
		free((void *)rule->target);
		free((void *)rule->value);
		free(rule);
	}

	hash_init(rules);
}
