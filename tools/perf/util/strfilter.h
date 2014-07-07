#ifndef __PERF_STRFILTER_H
#define __PERF_STRFILTER_H
/* General purpose glob matching filter */

#include <linux/list.h>
#include <stdbool.h>

/* A node of string filter */
struct strfilter_node {
	struct strfilter_node *l;	/* Tree left branche (for &,|) */
	struct strfilter_node *r;	/* Tree right branche (for !,&,|) */
	const char *p;		/* Operator or rule */
};

/* String filter */
struct strfilter {
	struct strfilter_node *root;
};

/**
 * strfilter__new - Create a new string filter
 * @rules: Filter rule, which is a combination of glob expressions.
 * @err: Pointer which points an error detected on @rules
 *
 * Parse @rules and return new strfilter. Return NULL if an error detected.
 * In that case, *@err will indicate where it is detected, and *@err is NULL
 * if a memory allocation is failed.
 */
struct strfilter *strfilter__new(const char *rules, const char **err);

/**
 * strfilter__compare - compare given string and a string filter
 * @filter: String filter
 * @str: target string
 *
 * Compare @str and @filter. Return true if the str match the rule
 */
bool strfilter__compare(struct strfilter *filter, const char *str);

/**
 * strfilter__delete - delete a string filter
 * @filter: String filter to delete
 *
 * Delete @filter.
 */
void strfilter__delete(struct strfilter *filter);

#endif
