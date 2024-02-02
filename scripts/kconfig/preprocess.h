/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PREPROCESS_H
#define PREPROCESS_H

enum variable_flavor {
	VAR_SIMPLE,
	VAR_RECURSIVE,
	VAR_APPEND,
};

struct gstr;
void env_write_dep(struct gstr *gs);
void variable_add(const char *name, const char *value,
		  enum variable_flavor flavor);
void variable_all_del(void);
char *expand_dollar(const char **str);
char *expand_one_token(const char **str);

#endif /* PREPROCESS_H */
