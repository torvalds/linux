/*-
 * Copyright (c) 2013-2015 Devin Teske <dteske@FreeBSD.org>
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <figpar.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_m.h>

#include "dialogrc.h"

#define STR_BUFSIZE 255

/* dialog(1) `.dialogrc' characteristics */
uint8_t use_colors = 1;
uint8_t use_shadow = 1;
char gauge_color[STR_BUFSIZE]	= "47b"; /* (BLUE,WHITE,ON) */
char separator[STR_BUFSIZE]	= "";

/* Function prototypes */
static int setattr(struct figpar_config *, uint32_t, char *, char *);
static int setbool(struct figpar_config *, uint32_t, char *, char *);
static int setnum(struct figpar_config *, uint32_t, char *, char *);
static int setstr(struct figpar_config *, uint32_t, char *, char *);

/*
 * Anatomy of DIALOGRC (~/.dialogrc by default)
 * NOTE: Must appear after private function prototypes (above)
 * NB: Brace-initialization of union requires cast to *first* member of union
 */
static struct figpar_config dialogrc_config[] = {
    /* TYPE            DIRECTIVE                      DEFAULT        HANDLER */
    {FIGPAR_TYPE_INT,  "aspect",                      {(void *)0},   &setnum},
    {FIGPAR_TYPE_STR,  "separate_widget",             {separator},   &setstr},
    {FIGPAR_TYPE_INT,  "tab_len",                     {(void *)0},   &setnum},
    {FIGPAR_TYPE_BOOL, "visit_items",                 {(void *)0},   &setbool},
    {FIGPAR_TYPE_BOOL, "use_shadow",                  {(void *)1},   &setbool},
    {FIGPAR_TYPE_BOOL, "use_colors",                  {(void *)1},   &setbool},
    {FIGPAR_TYPE_STR,  "screen_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "shadow_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "dialog_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "title_color",                 {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "border_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_active_color",         {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_inactive_color",       {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_key_active_color",     {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_key_inactive_color",   {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_label_active_color",   {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "button_label_inactive_color", {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "inputbox_color",              {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "inputbox_border_color",       {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "searchbox_color",             {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "searchbox_title_color",       {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "searchbox_border_color",      {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "position_indicator_color",    {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "menubox_color",               {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "menubox_border_color",        {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "item_color",                  {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "item_selected_color",         {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "tag_color",                   {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "tag_selected_color",          {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "tag_key_color",               {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "tag_key_selected_color",      {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "check_color",                 {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "check_selected_color",        {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "uarrow_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "darrow_color",                {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "itemhelp_color",              {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "form_active_text_color",      {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "form_text_color",             {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "form_item_readonly_color",    {NULL},        &setattr},
    {FIGPAR_TYPE_STR,  "gauge_color",                 {gauge_color}, &setattr},
    {0, NULL, {0}, NULL}
};

/*
 * figpar call-back for interpreting value as .dialogrc `Attribute'
 */
static int
setattr(struct figpar_config *option, uint32_t line __unused,
    char *directive __unused, char *value)
{
	char *cp = value;
	char *val;
	size_t len;
	char attrbuf[4];

	if (option == NULL) {
		warnx("%s:%d:%s: Missing callback parameter", __FILE__,
		    __LINE__, __func__);
		return (-1); /* Abort processing */
	}

	/* Allocate memory for the data if not already done */
	if (option->value.str == NULL) {
		if ((option->value.str = malloc(STR_BUFSIZE)) == NULL)
			return (-1);
	}

	/*
	 * If the first character is left-parenthesis, the format is
	 * `(background,foreground,highlight)' otherwise, we should take it
	 * as a reference to another color.
	 */
	if (*cp != '(') {
		/* Copy the [current] value from the referenced color */
		val = dialogrc_config_option(cp)->value.str;
		if (val != NULL)
			snprintf(option->value.str, STR_BUFSIZE, "%s", val);

		return (0);
	} else
		cp++;

	strtolower(cp);

	/* Initialize the attrbuf (fg,bg,hi,NUL) */
	attrbuf[0] = '0';
	attrbuf[1] = '0';
	attrbuf[2] = 'B'; /* \ZB = disable; \Zb = enable (see dialog(1)) */
	attrbuf[3] = '\0';

	/* Interpret the foreground color */
	if      (strncmp(cp, "red,",     4) == 0) attrbuf[0] = '1';
	else if (strncmp(cp, "green,",   6) == 0) attrbuf[0] = '2';
	else if (strncmp(cp, "yellow,",  7) == 0) attrbuf[0] = '3';
	else if (strncmp(cp, "blue,",    5) == 0) attrbuf[0] = '4';
	else if (strncmp(cp, "magenta,", 8) == 0) attrbuf[0] = '5';
	else if (strncmp(cp, "cyan,",    5) == 0) attrbuf[0] = '6';
	else if (strncmp(cp, "white,",   6) == 0) attrbuf[0] = '7';
	else if (strncmp(cp, "black,",   6) == 0) attrbuf[0] = '8';

	/* Advance to the background color */
	cp = strchr(cp, ',');
	if (cp == NULL)
		goto write_attrbuf;
	else
		cp++;

	/* Interpret the background color */
	if      (strncmp(cp, "red,",     4) == 0) attrbuf[1] = '1';
	else if (strncmp(cp, "green,",   6) == 0) attrbuf[1] = '2';
	else if (strncmp(cp, "yellow,",  7) == 0) attrbuf[1] = '3';
	else if (strncmp(cp, "blue,",    5) == 0) attrbuf[1] = '4';
	else if (strncmp(cp, "magenta,", 8) == 0) attrbuf[1] = '5';
	else if (strncmp(cp, "cyan,",    5) == 0) attrbuf[1] = '6';
	else if (strncmp(cp, "white,",   6) == 0) attrbuf[1] = '7';
	else if (strncmp(cp, "black,",   6) == 0) attrbuf[1] = '8';

	/* Advance to the highlight */
	cp = strchr(cp, ',');
	if (cp == NULL)
		goto write_attrbuf;
	else
		cp++;

	/* Trim trailing parenthesis */
	len = strlen(cp);
	if (cp[len - 1] == ')')
		cp[len - 1] = '\0';

	/* Interpret the highlight (initialized to off above) */
	if (strcmp(cp, "on") == 0 || strncmp(cp, "on,", 3) == 0)
		attrbuf[2] = 'b'; /* \Zb = enable bold (see dialog(1)) */

write_attrbuf:
	sprintf(option->value.str, "%s", attrbuf);

	return (0);
}

/*
 * figpar call-back for interpreting value as .dialogrc `Boolean'
 */
static int
setbool(struct figpar_config *option, uint32_t line __unused,
    char *directive __unused, char *value)
{

	if (option == NULL) {
		warnx("%s:%d:%s: Missing callback parameter", __FILE__,
		    __LINE__, __func__);
		return (-1); /* Abort processing */
	}

	/* Assume ON, check for OFF (case-insensitive) */
	option->value.boolean = 1;
	strtolower(value);
	if (strcmp(value, "off") == 0)
		option->value.boolean = 0;

	return (0);
}

/*
 * figpar call-back for interpreting value as .dialogrc `Number'
 */
static int
setnum(struct figpar_config *option, uint32_t line __unused,
    char *directive __unused, char *value)
{

	if (option == NULL) {
		warnx("%s:%d:%s: Missing callback parameter", __FILE__,
		    __LINE__, __func__);
		return (-1); /* Abort processing */
	}

	/* Convert the string to a 32-bit signed integer */
	option->value.num = (int32_t)strtol(value, (char **)NULL, 10);

	return (0);
}

/*
 * figpar call-back for interpreting value as .dialogrc `String'
 */
static int
setstr(struct figpar_config *option, uint32_t line __unused,
    char *directive __unused, char *value)
{
	size_t len;

	if (option == NULL) {
		warnx("%s:%d:%s: Missing callback parameter", __FILE__,
		    __LINE__, __func__);
		return (-1); /* Abort processing */
	}

	/* Allocate memory for the data if not already done */
	if (option->value.str == NULL) {
		if ((option->value.str = malloc(STR_BUFSIZE)) == NULL)
			return (-1);
	}

	/* Trim leading quote */
	if (*value == '"')
		value++;

	/* Write the data into the buffer */
	snprintf(option->value.str, STR_BUFSIZE, "%s", value);

	/* Trim trailing quote */
	len = strlen(option->value.str);
	if (option->value.str[len - 1] == '"')
		option->value.str[len - 1] = '\0';

	return (0);
}

/*
 * Parse (in order of preference) $DIALOGRC or `$HOME/.dialogrc'. Returns zero
 * on success, -1 on failure (and errno should be consulted).
 */
int
parse_dialogrc(void)
{
	char *cp;
	int res;
	size_t len;
	char path[PATH_MAX];

	/* Allow $DIALOGRC to override `$HOME/.dialogrc' default */
	if ((cp = getenv(ENV_DIALOGRC)) != NULL && *cp != '\0')
		snprintf(path, PATH_MAX, "%s", cp);
	else if ((cp = getenv(ENV_HOME)) != NULL) {
		/* Copy $HOME into buffer and append trailing `/' if missing */
		snprintf(path, PATH_MAX, "%s", cp);
		len = strlen(path);
		cp = path + len;
		if (len > 0 && len < (PATH_MAX - 1) && *(cp - 1) != '/') {
			*cp++ = '/';
			*cp = '\0';
			len++;
		}

		/* If we still have room, shove in the name of rc file */
		if (len < (PATH_MAX - 1))
			snprintf(cp, PATH_MAX - len, "%s", DIALOGRC);
	} else {
		/* Like dialog(1), don't process a file if $HOME is unset */
		errno = ENOENT;
		return (-1);
	}

	/* Process file (either $DIALOGRC if set, or `$HOME/.dialogrc') */
	res = parse_config(dialogrc_config,
		path, NULL, FIGPAR_BREAK_ON_EQUALS);

	/* Set some globals based on what we parsed */
	use_shadow = dialogrc_config_option("use_shadow")->value.boolean;
	use_colors = dialogrc_config_option("use_colors")->value.boolean;
	snprintf(gauge_color, STR_BUFSIZE, "%s",
	    dialogrc_config_option("gauge_color")->value.str);

	return (res);
}

/*
 * Return a pointer to the `.dialogrc' config option specific to `directive' or
 * static figpar_dummy_config (full of NULLs) if none found (see
 * get_config_option(3); part of figpar(3)).
 */
struct figpar_config *
dialogrc_config_option(const char *directive)
{
	return (get_config_option(dialogrc_config, directive));
}

/*
 * Free allocated items initialized by setattr() (via parse_config() callback
 * matrix [dialogrc_config] used in parse_dialogrc() above).
 */
void
dialogrc_free(void)
{
	char *value;
	uint32_t n;

	for (n = 0; dialogrc_config[n].directive != NULL; n++) {
		if (dialogrc_config[n].action != &setattr)
			continue;
		value = dialogrc_config[n].value.str;
		if (value != NULL && value != gauge_color) {
			free(dialogrc_config[n].value.str);
			dialogrc_config[n].value.str = NULL;
		}
	}
}
