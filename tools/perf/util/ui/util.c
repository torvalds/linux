#include <newt.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ttydefaults.h>

#include "../cache.h"
#include "../debug.h"
#include "browser.h"
#include "helpline.h"
#include "util.h"

static void newt_form__set_exit_keys(newtComponent self)
{
	newtFormAddHotKey(self, NEWT_KEY_LEFT);
	newtFormAddHotKey(self, NEWT_KEY_ESCAPE);
	newtFormAddHotKey(self, 'Q');
	newtFormAddHotKey(self, 'q');
	newtFormAddHotKey(self, CTRL('c'));
}

static newtComponent newt_form__new(void)
{
	newtComponent self = newtForm(NULL, NULL, 0);
	if (self)
		newt_form__set_exit_keys(self);
	return self;
}

int ui__popup_menu(int argc, char * const argv[])
{
	struct newtExitStruct es;
	int i, rc = -1, max_len = 5;
	newtComponent listbox, form = newt_form__new();

	if (form == NULL)
		return -1;

	listbox = newtListbox(0, 0, argc, NEWT_FLAG_RETURNEXIT);
	if (listbox == NULL)
		goto out_destroy_form;

	newtFormAddComponent(form, listbox);

	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (len > max_len)
			max_len = len;
		if (newtListboxAddEntry(listbox, argv[i], (void *)(long)i))
			goto out_destroy_form;
	}

	newtCenteredWindow(max_len, argc, NULL);
	newtFormRun(form, &es);
	rc = newtListboxGetCurrent(listbox) - NULL;
	if (es.reason == NEWT_EXIT_HOTKEY)
		rc = -1;
	newtPopWindow();
out_destroy_form:
	newtFormDestroy(form);
	return rc;
}

int ui__help_window(const char *text)
{
	struct newtExitStruct es;
	newtComponent tb, form = newt_form__new();
	int rc = -1;
	int max_len = 0, nr_lines = 0;
	const char *t;

	if (form == NULL)
		return -1;

	t = text;
	while (1) {
		const char *sep = strchr(t, '\n');
		int len;

		if (sep == NULL)
			sep = strchr(t, '\0');
		len = sep - t;
		if (max_len < len)
			max_len = len;
		++nr_lines;
		if (*sep == '\0')
			break;
		t = sep + 1;
	}

	tb = newtTextbox(0, 0, max_len, nr_lines, 0);
	if (tb == NULL)
		goto out_destroy_form;

	newtTextboxSetText(tb, text);
	newtFormAddComponent(form, tb);
	newtCenteredWindow(max_len, nr_lines, NULL);
	newtFormRun(form, &es);
	newtPopWindow();
	rc = 0;
out_destroy_form:
	newtFormDestroy(form);
	return rc;
}

static const char yes[] = "Yes", no[] = "No";

bool ui__dialog_yesno(const char *msg)
{
	/* newtWinChoice should really be accepting const char pointers... */
	return newtWinChoice(NULL, (char *)yes, (char *)no, (char *)msg) == 1;
}
