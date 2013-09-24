#ifndef _PRINTK_BRAILLE_H
#define _PRINTK_BRAILLE_H

#ifdef CONFIG_A11Y_BRAILLE_CONSOLE

static inline void
braille_set_options(struct console_cmdline *c, char *brl_options)
{
	c->brl_options = brl_options;
}

char *
_braille_console_setup(char **str, char **brl_options);

int
_braille_register_console(struct console *console, struct console_cmdline *c);

int
_braille_unregister_console(struct console *console);

#else

static inline void
braille_set_options(struct console_cmdline *c, char *brl_options)
{
}

static inline char *
_braille_console_setup(char **str, char **brl_options)
{
	return NULL;
}

static inline int
_braille_register_console(struct console *console, struct console_cmdline *c)
{
	return 0;
}

static inline int
_braille_unregister_console(struct console *console)
{
	return 0;
}

#endif

#endif
