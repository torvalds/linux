#ifndef __SUBCMD_HELP_H
#define __SUBCMD_HELP_H

#include <sys/types.h>

struct cmdnames {
	size_t alloc;
	size_t cnt;
	struct cmdname {
		size_t len; /* also used for similarity index in help.c */
		char name[];
	} **names;
};

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

void load_command_list(const char *prefix,
		struct cmdnames *main_cmds,
		struct cmdnames *other_cmds);
void add_cmdname(struct cmdnames *cmds, const char *name, size_t len);
void clean_cmdnames(struct cmdnames *cmds);
int cmdname_compare(const void *a, const void *b);
void uniq(struct cmdnames *cmds);
/* Here we require that excludes is a sorted list. */
void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes);
int is_in_cmdlist(struct cmdnames *c, const char *s);
void list_commands(const char *title, struct cmdnames *main_cmds,
		   struct cmdnames *other_cmds);

#endif /* __SUBCMD_HELP_H */
