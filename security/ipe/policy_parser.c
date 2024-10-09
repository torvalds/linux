// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/types.h>
#include <linux/ctype.h>

#include "policy.h"
#include "policy_parser.h"
#include "digest.h"

#define START_COMMENT	'#'
#define IPE_POLICY_DELIM " \t"
#define IPE_LINE_DELIM "\n\r"

/**
 * new_parsed_policy() - Allocate and initialize a parsed policy.
 *
 * Return:
 * * a pointer to the ipe_parsed_policy structure	- Success
 * * %-ENOMEM						- Out of memory (OOM)
 */
static struct ipe_parsed_policy *new_parsed_policy(void)
{
	struct ipe_parsed_policy *p = NULL;
	struct ipe_op_table *t = NULL;
	size_t i = 0;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	p->global_default_action = IPE_ACTION_INVALID;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i) {
		t = &p->rules[i];

		t->default_action = IPE_ACTION_INVALID;
		INIT_LIST_HEAD(&t->rules);
	}

	return p;
}

/**
 * remove_comment() - Truncate all chars following START_COMMENT in a string.
 *
 * @line: Supplies a policy line string for preprocessing.
 */
static void remove_comment(char *line)
{
	line = strchr(line, START_COMMENT);

	if (line)
		*line = '\0';
}

/**
 * remove_trailing_spaces() - Truncate all trailing spaces in a string.
 *
 * @line: Supplies a policy line string for preprocessing.
 *
 * Return: The length of truncated string.
 */
static size_t remove_trailing_spaces(char *line)
{
	size_t i = 0;

	i = strlen(line);
	while (i > 0 && isspace(line[i - 1]))
		i--;

	line[i] = '\0';

	return i;
}

/**
 * parse_version() - Parse policy version.
 * @ver: Supplies a version string to be parsed.
 * @p: Supplies the partial parsed policy.
 *
 * Return:
 * * %0		- Success
 * * %-EBADMSG	- Version string is invalid
 * * %-ERANGE	- Version number overflow
 * * %-EINVAL	- Parsing error
 */
static int parse_version(char *ver, struct ipe_parsed_policy *p)
{
	u16 *const cv[] = { &p->version.major, &p->version.minor, &p->version.rev };
	size_t sep_count = 0;
	char *token;
	int rc = 0;

	while ((token = strsep(&ver, ".")) != NULL) {
		/* prevent overflow */
		if (sep_count >= ARRAY_SIZE(cv))
			return -EBADMSG;

		rc = kstrtou16(token, 10, cv[sep_count]);
		if (rc)
			return rc;

		++sep_count;
	}

	/* prevent underflow */
	if (sep_count != ARRAY_SIZE(cv))
		return -EBADMSG;

	return 0;
}

enum header_opt {
	IPE_HEADER_POLICY_NAME = 0,
	IPE_HEADER_POLICY_VERSION,
	__IPE_HEADER_MAX
};

static const match_table_t header_tokens = {
	{IPE_HEADER_POLICY_NAME,	"policy_name=%s"},
	{IPE_HEADER_POLICY_VERSION,	"policy_version=%s"},
	{__IPE_HEADER_MAX,		NULL}
};

/**
 * parse_header() - Parse policy header information.
 * @line: Supplies header line to be parsed.
 * @p: Supplies the partial parsed policy.
 *
 * Return:
 * * %0		- Success
 * * %-EBADMSG	- Header string is invalid
 * * %-ENOMEM	- Out of memory (OOM)
 * * %-ERANGE	- Version number overflow
 * * %-EINVAL	- Version parsing error
 */
static int parse_header(char *line, struct ipe_parsed_policy *p)
{
	substring_t args[MAX_OPT_ARGS];
	char *t, *ver = NULL;
	size_t idx = 0;
	int rc = 0;

	while ((t = strsep(&line, IPE_POLICY_DELIM)) != NULL) {
		int token;

		if (*t == '\0')
			continue;
		if (idx >= __IPE_HEADER_MAX) {
			rc = -EBADMSG;
			goto out;
		}

		token = match_token(t, header_tokens, args);
		if (token != idx) {
			rc = -EBADMSG;
			goto out;
		}

		switch (token) {
		case IPE_HEADER_POLICY_NAME:
			p->name = match_strdup(&args[0]);
			if (!p->name)
				rc = -ENOMEM;
			break;
		case IPE_HEADER_POLICY_VERSION:
			ver = match_strdup(&args[0]);
			if (!ver) {
				rc = -ENOMEM;
				break;
			}
			rc = parse_version(ver, p);
			break;
		default:
			rc = -EBADMSG;
		}
		if (rc)
			goto out;
		++idx;
	}

	if (idx != __IPE_HEADER_MAX)
		rc = -EBADMSG;

out:
	kfree(ver);
	return rc;
}

/**
 * token_default() - Determine if the given token is "DEFAULT".
 * @token: Supplies the token string to be compared.
 *
 * Return:
 * * %false	- The token is not "DEFAULT"
 * * %true	- The token is "DEFAULT"
 */
static bool token_default(char *token)
{
	return !strcmp(token, "DEFAULT");
}

/**
 * free_rule() - Free the supplied ipe_rule struct.
 * @r: Supplies the ipe_rule struct to be freed.
 *
 * Free a ipe_rule struct @r. Note @r must be removed from any lists before
 * calling this function.
 */
static void free_rule(struct ipe_rule *r)
{
	struct ipe_prop *p, *t;

	if (IS_ERR_OR_NULL(r))
		return;

	list_for_each_entry_safe(p, t, &r->props, next) {
		list_del(&p->next);
		ipe_digest_free(p->value);
		kfree(p);
	}

	kfree(r);
}

static const match_table_t operation_tokens = {
	{IPE_OP_EXEC,			"op=EXECUTE"},
	{IPE_OP_FIRMWARE,		"op=FIRMWARE"},
	{IPE_OP_KERNEL_MODULE,		"op=KMODULE"},
	{IPE_OP_KEXEC_IMAGE,		"op=KEXEC_IMAGE"},
	{IPE_OP_KEXEC_INITRAMFS,	"op=KEXEC_INITRAMFS"},
	{IPE_OP_POLICY,			"op=POLICY"},
	{IPE_OP_X509,			"op=X509_CERT"},
	{IPE_OP_INVALID,		NULL}
};

/**
 * parse_operation() - Parse the operation type given a token string.
 * @t: Supplies the token string to be parsed.
 *
 * Return: The parsed operation type.
 */
static enum ipe_op_type parse_operation(char *t)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(t, operation_tokens, args);
}

static const match_table_t action_tokens = {
	{IPE_ACTION_ALLOW,	"action=ALLOW"},
	{IPE_ACTION_DENY,	"action=DENY"},
	{IPE_ACTION_INVALID,	NULL}
};

/**
 * parse_action() - Parse the action type given a token string.
 * @t: Supplies the token string to be parsed.
 *
 * Return: The parsed action type.
 */
static enum ipe_action_type parse_action(char *t)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(t, action_tokens, args);
}

static const match_table_t property_tokens = {
	{IPE_PROP_BOOT_VERIFIED_FALSE,	"boot_verified=FALSE"},
	{IPE_PROP_BOOT_VERIFIED_TRUE,	"boot_verified=TRUE"},
	{IPE_PROP_DMV_ROOTHASH,		"dmverity_roothash=%s"},
	{IPE_PROP_DMV_SIG_FALSE,	"dmverity_signature=FALSE"},
	{IPE_PROP_DMV_SIG_TRUE,		"dmverity_signature=TRUE"},
	{IPE_PROP_FSV_DIGEST,		"fsverity_digest=%s"},
	{IPE_PROP_FSV_SIG_FALSE,	"fsverity_signature=FALSE"},
	{IPE_PROP_FSV_SIG_TRUE,		"fsverity_signature=TRUE"},
	{IPE_PROP_INVALID,		NULL}
};

/**
 * parse_property() - Parse a rule property given a token string.
 * @t: Supplies the token string to be parsed.
 * @r: Supplies the ipe_rule the parsed property will be associated with.
 *
 * This function parses and associates a property with an IPE rule based
 * on a token string.
 *
 * Return:
 * * %0		- Success
 * * %-ENOMEM	- Out of memory (OOM)
 * * %-EBADMSG	- The supplied token cannot be parsed
 */
static int parse_property(char *t, struct ipe_rule *r)
{
	substring_t args[MAX_OPT_ARGS];
	struct ipe_prop *p = NULL;
	int rc = 0;
	int token;
	char *dup = NULL;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	token = match_token(t, property_tokens, args);

	switch (token) {
	case IPE_PROP_DMV_ROOTHASH:
	case IPE_PROP_FSV_DIGEST:
		dup = match_strdup(&args[0]);
		if (!dup) {
			rc = -ENOMEM;
			goto err;
		}
		p->value = ipe_digest_parse(dup);
		if (IS_ERR(p->value)) {
			rc = PTR_ERR(p->value);
			goto err;
		}
		fallthrough;
	case IPE_PROP_BOOT_VERIFIED_FALSE:
	case IPE_PROP_BOOT_VERIFIED_TRUE:
	case IPE_PROP_DMV_SIG_FALSE:
	case IPE_PROP_DMV_SIG_TRUE:
	case IPE_PROP_FSV_SIG_FALSE:
	case IPE_PROP_FSV_SIG_TRUE:
		p->type = token;
		break;
	default:
		rc = -EBADMSG;
		break;
	}
	if (rc)
		goto err;
	list_add_tail(&p->next, &r->props);

out:
	kfree(dup);
	return rc;
err:
	kfree(p);
	goto out;
}

/**
 * parse_rule() - parse a policy rule line.
 * @line: Supplies rule line to be parsed.
 * @p: Supplies the partial parsed policy.
 *
 * Return:
 * * 0		- Success
 * * %-ENOMEM	- Out of memory (OOM)
 * * %-EBADMSG	- Policy syntax error
 */
static int parse_rule(char *line, struct ipe_parsed_policy *p)
{
	enum ipe_action_type action = IPE_ACTION_INVALID;
	enum ipe_op_type op = IPE_OP_INVALID;
	bool is_default_rule = false;
	struct ipe_rule *r = NULL;
	bool first_token = true;
	bool op_parsed = false;
	int rc = 0;
	char *t;

	if (IS_ERR_OR_NULL(line))
		return -EBADMSG;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	INIT_LIST_HEAD(&r->next);
	INIT_LIST_HEAD(&r->props);

	while (t = strsep(&line, IPE_POLICY_DELIM), line) {
		if (*t == '\0')
			continue;
		if (first_token && token_default(t)) {
			is_default_rule = true;
		} else {
			if (!op_parsed) {
				op = parse_operation(t);
				if (op == IPE_OP_INVALID)
					rc = -EBADMSG;
				else
					op_parsed = true;
			} else {
				rc = parse_property(t, r);
			}
		}

		if (rc)
			goto err;
		first_token = false;
	}

	action = parse_action(t);
	if (action == IPE_ACTION_INVALID) {
		rc = -EBADMSG;
		goto err;
	}

	if (is_default_rule) {
		if (!list_empty(&r->props)) {
			rc = -EBADMSG;
		} else if (op == IPE_OP_INVALID) {
			if (p->global_default_action != IPE_ACTION_INVALID)
				rc = -EBADMSG;
			else
				p->global_default_action = action;
		} else {
			if (p->rules[op].default_action != IPE_ACTION_INVALID)
				rc = -EBADMSG;
			else
				p->rules[op].default_action = action;
		}
	} else if (op != IPE_OP_INVALID && action != IPE_ACTION_INVALID) {
		r->op = op;
		r->action = action;
	} else {
		rc = -EBADMSG;
	}

	if (rc)
		goto err;
	if (!is_default_rule)
		list_add_tail(&r->next, &p->rules[op].rules);
	else
		free_rule(r);

	return rc;
err:
	free_rule(r);
	return rc;
}

/**
 * ipe_free_parsed_policy() - free a parsed policy structure.
 * @p: Supplies the parsed policy.
 */
void ipe_free_parsed_policy(struct ipe_parsed_policy *p)
{
	struct ipe_rule *pp, *t;
	size_t i = 0;

	if (IS_ERR_OR_NULL(p))
		return;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i)
		list_for_each_entry_safe(pp, t, &p->rules[i].rules, next) {
			list_del(&pp->next);
			free_rule(pp);
		}

	kfree(p->name);
	kfree(p);
}

/**
 * validate_policy() - validate a parsed policy.
 * @p: Supplies the fully parsed policy.
 *
 * Given a policy structure that was just parsed, validate that all
 * operations have their default rules or a global default rule is set.
 *
 * Return:
 * * %0		- Success
 * * %-EBADMSG	- Policy is invalid
 */
static int validate_policy(const struct ipe_parsed_policy *p)
{
	size_t i = 0;

	if (p->global_default_action != IPE_ACTION_INVALID)
		return 0;

	for (i = 0; i < ARRAY_SIZE(p->rules); ++i) {
		if (p->rules[i].default_action == IPE_ACTION_INVALID)
			return -EBADMSG;
	}

	return 0;
}

/**
 * ipe_parse_policy() - Given a string, parse the string into an IPE policy.
 * @p: partially filled ipe_policy structure to populate with the result.
 *     it must have text and textlen set.
 *
 * Return:
 * * %0		- Success
 * * %-EBADMSG	- Policy is invalid
 * * %-ENOMEM	- Out of Memory
 * * %-ERANGE	- Policy version number overflow
 * * %-EINVAL	- Policy version parsing error
 */
int ipe_parse_policy(struct ipe_policy *p)
{
	struct ipe_parsed_policy *pp = NULL;
	char *policy = NULL, *dup = NULL;
	bool header_parsed = false;
	char *line = NULL;
	size_t len;
	int rc = 0;

	if (!p->textlen)
		return -EBADMSG;

	policy = kmemdup_nul(p->text, p->textlen, GFP_KERNEL);
	if (!policy)
		return -ENOMEM;
	dup = policy;

	pp = new_parsed_policy();
	if (IS_ERR(pp)) {
		rc = PTR_ERR(pp);
		goto out;
	}

	while ((line = strsep(&policy, IPE_LINE_DELIM)) != NULL) {
		remove_comment(line);
		len = remove_trailing_spaces(line);
		if (!len)
			continue;

		if (!header_parsed) {
			rc = parse_header(line, pp);
			if (rc)
				goto err;
			header_parsed = true;
		} else {
			rc = parse_rule(line, pp);
			if (rc)
				goto err;
		}
	}

	if (!header_parsed || validate_policy(pp)) {
		rc = -EBADMSG;
		goto err;
	}

	p->parsed = pp;

out:
	kfree(dup);
	return rc;
err:
	ipe_free_parsed_policy(pp);
	goto out;
}
