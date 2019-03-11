#ifndef SRCCODE_H
#define SRCCODE_H 1

struct srccode_state {
	char	 *srcfile;
	unsigned line;
};

static inline void srccode_state_init(struct srccode_state *state)
{
	state->srcfile = NULL;
	state->line    = 0;
}

void srccode_state_free(struct srccode_state *state);

/* Result is not 0 terminated */
char *find_sourceline(char *fn, unsigned line, int *lenp);

#endif
