/*
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2005
 *
 * Released under the terms of the GNU GPL v2.0
 */

#include <stdlib.h>
#include <string.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

static char *escape(const char* text, char *bf, int len)
{
	char *bfp = bf;
	int multiline = strchr(text, '\n') != NULL;
	int eol = 0;
	int textlen = strlen(text);

	if ((textlen > 0) && (text[textlen-1] == '\n'))
		eol = 1;

	*bfp++ = '"';
	--len;

	if (multiline) {
		*bfp++ = '"';
		*bfp++ = '\n';
		*bfp++ = '"';
		len -= 3;
	}

	while (*text != '\0' && len > 1) {
		if (*text == '"')
			*bfp++ = '\\';
		else if (*text == '\n') {
			*bfp++ = '\\';
			*bfp++ = 'n';
			*bfp++ = '"';
			*bfp++ = '\n';
			*bfp++ = '"';
			len -= 5;
			++text;
			goto next;
		}
		*bfp++ = *text++;
next:
		--len;
	}

	if (multiline && eol)
		bfp -= 3;

	*bfp++ = '"';
	*bfp = '\0';

	return bf;
}

struct file_line {
	struct file_line *next;
	char*		 file;
	int		 lineno;
};

static struct file_line *file_line__new(char *file, int lineno)
{
	struct file_line *self = malloc(sizeof(*self));

	if (self == NULL)
		goto out;

	self->file   = file;
	self->lineno = lineno;
	self->next   = NULL;
out:
	return self;
}

struct message {
	const char	 *msg;
	const char	 *option;
	struct message	 *next;
	struct file_line *files;
};

static struct message *message__list;

static struct message *message__new(const char *msg, char *option, char *file, int lineno)
{
	struct message *self = malloc(sizeof(*self));

	if (self == NULL)
		goto out;

	self->files = file_line__new(file, lineno);
	if (self->files == NULL)
		goto out_fail;

	self->msg = strdup(msg);
	if (self->msg == NULL)
		goto out_fail_msg;

	self->option = option;
	self->next = NULL;
out:
	return self;
out_fail_msg:
	free(self->files);
out_fail:
	free(self);
	self = NULL;
	goto out;
}

static struct message *mesage__find(const char *msg)
{
	struct message *m = message__list;

	while (m != NULL) {
		if (strcmp(m->msg, msg) == 0)
			break;
		m = m->next;
	}

	return m;
}

static int message__add_file_line(struct message *self, char *file, int lineno)
{
	int rc = -1;
	struct file_line *fl = file_line__new(file, lineno);

	if (fl == NULL)
		goto out;

	fl->next    = self->files;
	self->files = fl;
	rc = 0;
out:
	return rc;
}

static int message__add(const char *msg, char *option, char *file, int lineno)
{
	int rc = 0;
	char bf[16384];
	char *escaped = escape(msg, bf, sizeof(bf));
	struct message *m = mesage__find(escaped);

	if (m != NULL)
		rc = message__add_file_line(m, file, lineno);
	else {
		m = message__new(escaped, option, file, lineno);

		if (m != NULL) {
			m->next	      = message__list;
			message__list = m;
		} else
			rc = -1;
	}
	return rc;
}

void menu_build_message_list(struct menu *menu)
{
	struct menu *child;

	message__add(menu_get_prompt(menu), NULL,
		     menu->file == NULL ? "Root Menu" : menu->file->name,
		     menu->lineno);

	if (menu->sym != NULL && menu->sym->help != NULL)
		message__add(menu->sym->help, menu->sym->name,
			     menu->file == NULL ? "Root Menu" : menu->file->name,
			     menu->lineno);

	for (child = menu->list; child != NULL; child = child->next)
		if (child->prompt != NULL)
			menu_build_message_list(child);
}

static void message__print_file_lineno(struct message *self)
{
	struct file_line *fl = self->files;

	putchar('\n');
	if (self->option != NULL)
		printf("# %s:00000\n", self->option);

	printf("#: %s:%d", fl->file, fl->lineno);
	fl = fl->next;

	while (fl != NULL) {
		printf(", %s:%d", fl->file, fl->lineno);
		fl = fl->next;
	}

	putchar('\n');
}

static void message__print_gettext_msgid_msgstr(struct message *self)
{
	message__print_file_lineno(self);

	printf("msgid %s\n"
	       "msgstr \"\"\n", self->msg);
}

void menu__xgettext(void)
{
	struct message *m = message__list;

	while (m != NULL) {
		/* skip empty lines ("") */
		if (strlen(m->msg) > sizeof("\"\""))
			message__print_gettext_msgid_msgstr(m);
		m = m->next;
	}
}

int main(int ac, char **av)
{
	conf_parse(av[1]);

	menu_build_message_list(menu_get_root_menu(NULL));
	menu__xgettext();
	return 0;
}
