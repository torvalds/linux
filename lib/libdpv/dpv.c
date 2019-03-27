/*-
 * Copyright (c) 2013-2016 Devin Teske <dteske@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dialog.h>
#include <err.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_m.h>
#include <unistd.h>

#include "dialog_util.h"
#include "dialogrc.h"
#include "dprompt.h"
#include "dpv.h"
#include "dpv_private.h"
#include "status.h"
#include "util.h"

/* Test Mechanics (Only used when dpv_config.options |= DPV_TEST_MODE) */
#define INCREMENT		1	/* Increment % per-pass test-mode */
#define XDIALOG_INCREMENT	15	/* different for slower Xdialog(1) */
static uint8_t increment = INCREMENT;

/* Debugging */
uint8_t debug = FALSE;

/* Data to process */
int dpv_interrupt = FALSE;
int dpv_abort = FALSE;
unsigned int dpv_nfiles = 0;

/* Data processing */
long long dpv_overall_read = 0;
static char pathbuf[PATH_MAX];

/* Extra display information */
uint8_t keep_tite = FALSE;	/* dpv_config.keep_tite */
uint8_t no_labels = FALSE;	/* dpv_config.options & DPV_NO_LABELS */
uint8_t wide = FALSE;		/* dpv_config.options & DPV_WIDE_MODE */
char *aprompt = NULL;		/* dpv_config.aprompt */
char *msg_done = NULL;		/* dpv_config.msg_done */
char *msg_fail = NULL;		/* dpv_config.msg_fail */
char *msg_pending = NULL;	/* dpv_config.msg_pending */
char *pprompt = NULL;		/* dpv_config.pprompt */

/* Status-Line format for when using dialog(3) */
static const char *status_format_custom = NULL;
static char status_format_default[DPV_STATUS_FORMAT_MAX];

/*
 * Takes a pointer to a dpv_config structure containing layout details and
 * pointer to initial element in a linked-list of dpv_file_node structures,
 * each presenting a file to process. Executes the `action' function passed-in
 * as a member to the `config' structure argument.
 */
int
dpv(struct dpv_config *config, struct dpv_file_node *file_list)
{
	char c;
	uint8_t keep_going;
	uint8_t nls = FALSE; /* See dialog_prompt_nlstate() */
	uint8_t no_overrun = FALSE;
	uint8_t pprompt_nls = FALSE; /* See dialog_prompt_nlstate() */
	uint8_t shrink_label_size = FALSE;
	mode_t mask;
	uint16_t options;
	char *cp;
	char *fc;
	char *last;
	char *name;
	char *output;
	const char *status_fmt;
	const char *path_fmt;
	enum dpv_display display_type;
	enum dpv_output output_type;
	enum dpv_status status;
	int (*action)(struct dpv_file_node *file, int out);
	int backslash;
	int dialog_last_update = 0;
	int dialog_old_nthfile = 0;
	int dialog_old_seconds = -1;
	int dialog_out = STDOUT_FILENO;
	int dialog_update_usec = 0;
	int dialog_updates_per_second;
	int files_left;
	int max_cols;
	int nthfile = 0;
	int output_out;
	int overall = 0;
	int pct;
	int res;
	int seconds;
	int status_last_update = 0;
	int status_old_nthfile = 0;
	int status_old_seconds = -1;
	int status_update_usec = 0;
	int status_updates_per_second;
	pid_t output_pid;
	pid_t pid;
	size_t len;
	struct dpv_file_node *curfile;
	struct dpv_file_node *first_file;
	struct dpv_file_node *list_head;
	struct timeval now;
	struct timeval start;
	char init_prompt[PROMPT_MAX + 1] = "";

	/* Initialize globals to default values */
	aprompt		= NULL;
	pprompt		= NULL;
	options		= 0;
	action		= NULL;
	backtitle	= NULL;
	debug		= FALSE;
	dialog_test	= FALSE;
	dialog_updates_per_second = DIALOG_UPDATES_PER_SEC;
	display_limit	= DISPLAY_LIMIT_DEFAULT;
	display_type	= DPV_DISPLAY_LIBDIALOG;
	keep_tite	= FALSE;
	label_size	= LABEL_SIZE_DEFAULT;
	msg_done	= NULL;
	msg_fail	= NULL;
	msg_pending	= NULL;
	no_labels	= FALSE;
	output		= NULL;
	output_type	= DPV_OUTPUT_NONE;
	pbar_size	= PBAR_SIZE_DEFAULT;
	status_format_custom = NULL;
	status_updates_per_second = STATUS_UPDATES_PER_SEC;
	title		= NULL;
	wide		= FALSE;

	/* Process config options (overriding defaults) */
	if (config != NULL) {
		if (config->aprompt != NULL) {
			if (aprompt == NULL) {
				aprompt = malloc(DPV_APROMPT_MAX);
				if (aprompt == NULL)
					return (-1);
			}
			snprintf(aprompt, DPV_APROMPT_MAX, "%s",
			    config->aprompt);
		}
		if (config->pprompt != NULL) {
			if (pprompt == NULL) {
				pprompt = malloc(DPV_PPROMPT_MAX + 2);
				/* +2 is for implicit "\n" appended later */
				if (pprompt == NULL)
					return (-1);
			}
			snprintf(pprompt, DPV_APROMPT_MAX, "%s",
			    config->pprompt);
		}

		options		= config->options;
		action		= config->action;
		backtitle	= config->backtitle;
		debug		= config->debug;
		dialog_test	= ((options & DPV_TEST_MODE) != 0);
		dialog_updates_per_second = config->dialog_updates_per_second;
		display_limit	= config->display_limit;
		display_type	= config->display_type;
		keep_tite	= config->keep_tite;
		label_size	= config->label_size;
		msg_done	= (char *)config->msg_done;
		msg_fail	= (char *)config->msg_fail;
		msg_pending	= (char *)config->msg_pending;
		no_labels	= ((options & DPV_NO_LABELS) != 0);
		no_overrun	= ((options & DPV_NO_OVERRUN) != 0);
		output          = config->output;
		output_type	= config->output_type;
		pbar_size	= config->pbar_size;
		status_updates_per_second = config->status_updates_per_second;
		title		= config->title;
		wide		= ((options & DPV_WIDE_MODE) != 0);

		/* Enforce some minimums (pedantic) */
		if (display_limit < -1)
			display_limit = -1;
		if (label_size < -1)
			label_size = -1;
		if (pbar_size < -1)
			pbar_size = -1;

		/* For the mini-pbar, -1 means hide, zero is invalid unless
		 * only one file is given */
		if (pbar_size == 0) {
			if (file_list == NULL || file_list->next == NULL)
				pbar_size = -1;
			else
				pbar_size = PBAR_SIZE_DEFAULT;
		}

		/* For the label, -1 means auto-size, zero is invalid unless
		 * specifically requested through the use of options flag */
		if (label_size == 0 && no_labels == FALSE)
			label_size = LABEL_SIZE_DEFAULT;

		/* Status update should not be zero */
		if (status_updates_per_second == 0)
			status_updates_per_second = STATUS_UPDATES_PER_SEC;
	} /* config != NULL */

	/* Process the type of display we've been requested to produce */
	switch (display_type) {
	case DPV_DISPLAY_STDOUT:
		debug		= TRUE;
		use_color	= FALSE;
		use_dialog	= FALSE;
		use_libdialog	= FALSE;
		use_xdialog	= FALSE;
		break;
	case DPV_DISPLAY_DIALOG:
		use_color	= TRUE;
		use_dialog	= TRUE;
		use_libdialog	= FALSE;
		use_xdialog	= FALSE;
		break;
	case DPV_DISPLAY_XDIALOG:
		snprintf(dialog, PATH_MAX, XDIALOG);
		use_color	= FALSE;
		use_dialog	= FALSE;
		use_libdialog	= FALSE;
		use_xdialog	= TRUE;
		break;
	default:
		use_color	= TRUE;
		use_dialog	= FALSE;
		use_libdialog	= TRUE;
		use_xdialog	= FALSE;
		break;
	} /* display_type */

	/* Enforce additional minimums that require knowing our display type */
	if (dialog_updates_per_second == 0)
		dialog_updates_per_second = use_xdialog ?
			XDIALOG_UPDATES_PER_SEC : DIALOG_UPDATES_PER_SEC;

	/* Allow forceful override of use_color */
	if (config != NULL && (config->options & DPV_USE_COLOR) != 0)
		use_color = TRUE;

	/* Count the number of files in provided list of dpv_file_node's */
	if (use_dialog && pprompt != NULL && *pprompt != '\0')
		pprompt_nls = dialog_prompt_nlstate(pprompt);

	max_cols = dialog_maxcols();
	if (label_size == -1)
		shrink_label_size = TRUE;

	/* Process file arguments */
	for (curfile = file_list; curfile != NULL; curfile = curfile->next) {
		dpv_nfiles++;

		/* dialog(3) only expands literal newlines */
		if (use_libdialog) strexpandnl(curfile->name);

		/* Optionally calculate label size for file */
		if (shrink_label_size) {
			nls = FALSE;
			name = curfile->name;
			if (curfile == file_list)
				nls = pprompt_nls;
			last = (char *)dialog_prompt_lastline(name, nls);
			if (use_dialog) {
				c = *last;
				*last = '\0';
				nls = dialog_prompt_nlstate(name);
				*last = c;
			}
			len = dialog_prompt_longestline(last, nls);
			if ((int)len > (label_size - 3)) {
				if (label_size > 0)
					label_size += 3;
				label_size = len;
				/* Room for ellipsis (unless NULL) */
				if (label_size > 0)
					label_size += 3;
			}

			if (max_cols > 0 && label_size > (max_cols - pbar_size
			    - 9))
				label_size = max_cols - pbar_size - 9;
		}

		if (debug)
			warnx("label=[%s] path=[%s] size=%lli",
			    curfile->name, curfile->path, curfile->length);
	} /* file_list */

	/* Optionally process the contents of DIALOGRC (~/.dialogrc) */
	if (use_dialog) {
		res = parse_dialogrc();
		if (debug && res == 0) {
			warnx("Successfully read `%s' config file", DIALOGRC);
			warnx("use_shadow = %i (Boolean)", use_shadow);
			warnx("use_colors = %i (Boolean)", use_colors);
			warnx("gauge_color=[%s] (FBH)", gauge_color);
		}
	} else if (use_libdialog) {
		init_dialog(stdin, stdout);
		use_shadow = dialog_state.use_shadow;
		use_colors = dialog_state.use_colors;
		gauge_color[0] = 48 + dlg_color_table[GAUGE_ATTR].fg;
		gauge_color[1] = 48 + dlg_color_table[GAUGE_ATTR].bg;
		gauge_color[2] = dlg_color_table[GAUGE_ATTR].hilite ?
		    'b' : 'B';
		gauge_color[3] = '\0';
		end_dialog();
		if (debug) {
			warnx("Finished initializing dialog(3) library");
			warnx("use_shadow = %i (Boolean)", use_shadow);
			warnx("use_colors = %i (Boolean)", use_colors);
			warnx("gauge_color=[%s] (FBH)", gauge_color);
		}
	}

	/* Enable mini progress bar automatically for stdin streams if unable
	 * to calculate progress (missing `lines:' syntax). */
	if (dpv_nfiles <= 1 && file_list != NULL && file_list->length < 0 &&
	    !dialog_test)
		pbar_size = PBAR_SIZE_DEFAULT;

	/* If $USE_COLOR is set and non-NULL enable color; otherwise disable */
	if ((cp = getenv(ENV_USE_COLOR)) != 0)
		use_color = *cp != '\0' ? 1 : 0;

	/* Print error and return `-1' if not given at least one name */
	if (dpv_nfiles == 0) {
		warnx("%s: no labels provided", __func__);
		return (-1);
	} else if (debug)
		warnx("%s: %u label%s provided", __func__, dpv_nfiles,
		    dpv_nfiles == 1 ? "" : "s");

	/* If only one file and pbar size is zero, default to `-1' */
	if (dpv_nfiles <= 1 && pbar_size == 0)
		pbar_size = -1;

	/* Print some debugging information */
	if (debug) {
		warnx("%s: %s(%i) max rows x cols = %i x %i",
		    __func__, use_xdialog ? XDIALOG : DIALOG,
		    use_libdialog ? 3 : 1, dialog_maxrows(),
		    dialog_maxcols());
	}

	/* Xdialog(1) updates a lot slower than dialog(1) */
	if (dialog_test && use_xdialog)
		increment = XDIALOG_INCREMENT;

	/* Always add implicit newline to pprompt (when specified) */
	if (pprompt != NULL && *pprompt != '\0') {
		len = strlen(pprompt);
		/*
		 * NOTE: pprompt = malloc(PPROMPT_MAX + 2)
		 * NOTE: (see getopt(2) section above for pprompt allocation)
		 */
		pprompt[len++] = '\\';
		pprompt[len++] = 'n';
		pprompt[len++] = '\0';
	}

	/* Xdialog(1) requires newlines (a) escaped and (b) in triplicate */
	if (use_xdialog && pprompt != NULL) {
		/* Replace `\n' with `\n\\n\n' in pprompt */
		len = strlen(pprompt);
		len += strcount(pprompt, "\\n") * 2;
		if (len > DPV_PPROMPT_MAX)
			errx(EXIT_FAILURE, "%s: Oops, pprompt buffer overflow "
			    "(%zu > %i)", __func__, len, DPV_PPROMPT_MAX);
		if (replaceall(pprompt, "\\n", "\n\\n\n") < 0)
			err(EXIT_FAILURE, "%s: replaceall()", __func__);
	}
	/* libdialog requires literal newlines */
	else if (use_libdialog && pprompt != NULL)
		strexpandnl(pprompt);

	/* Xdialog(1) requires newlines (a) escaped and (b) in triplicate */
	if (use_xdialog && aprompt != NULL) {
		/* Replace `\n' with `\n\\n\n' in aprompt */
		len = strlen(aprompt);
		len += strcount(aprompt, "\\n") * 2;
		if (len > DPV_APROMPT_MAX)
			errx(EXIT_FAILURE, "%s: Oops, aprompt buffer overflow "
			    " (%zu > %i)", __func__, len, DPV_APROMPT_MAX);
		if (replaceall(aprompt, "\\n", "\n\\n\n") < 0)
			err(EXIT_FAILURE, "%s: replaceall()", __func__);
	}
	/* libdialog requires literal newlines */
	else if (use_libdialog && aprompt != NULL)
		strexpandnl(aprompt);

	/*
	 * Warn user about an obscure dialog(1) bug (neither Xdialog(1) nor
	 * libdialog are affected) in the `--gauge' widget. If the first non-
	 * whitespace letter of "{new_prompt}" in "XXX\n{new_prompt}\nXXX\n"
	 * is a number, the number can sometimes be mistaken for a percentage
	 * to the overall progressbar. Other nasty side-effects such as the
	 * entire prompt not displaying or displaying improperly are caused by
	 * this bug too.
	 *
	 * NOTE: When we can use color, we have a work-around... prefix the
	 * output with `\Zn' (used to terminate ANSI and reset to normal).
	 */
	if (use_dialog && !use_color) {
		backslash = 0;

		/* First, check pprompt (falls through if NULL) */
		fc = pprompt;
		while (fc != NULL && *fc != '\0') {
			if (*fc == '\n') /* leading literal newline OK */
				break;
			if (!isspace(*fc) && *fc != '\\' && backslash == 0)
				break;
			else if (backslash > 0 && *fc != 'n')
				break;
			else if (*fc == '\\') {
				backslash++;
				if (backslash > 2)
					break; /* we're safe */
			}
			fc++;
		}
		/* First non-whitespace character that dialog(1) will see */
		if (fc != NULL && *fc >= '0' && *fc <= '9')
			warnx("%s: WARNING! text argument to `-p' begins with "
			    "a number (not recommended)", __func__);
		else if (fc > pprompt)
			warnx("%s: WARNING! text argument to `-p' begins with "
			    "whitespace (not recommended)", __func__);

		/*
		 * If no pprompt or pprompt is all whitespace, check the first
		 * file name provided to make sure it is alright too.
		 */
		if ((pprompt == NULL || *fc == '\0') && file_list != NULL) {
			first_file = file_list;
			fc = first_file->name;
			while (fc != NULL && *fc != '\0' && isspace(*fc))
				fc++;
			/* First non-whitespace char that dialog(1) will see */
			if (fc != NULL && *fc >= '0' && *fc <= '9')
				warnx("%s: WARNING! File name `%s' begins "
				    "with a number (use `-p text' for safety)",
				    __func__, first_file->name);
		}
	}

	dprompt_init(file_list);
		/* Reads: label_size pbar_size pprompt aprompt dpv_nfiles */
		/* Inits: dheight and dwidth */

	/* Default localeconv(3) settings for dialog(3) status */
	setlocale(LC_NUMERIC,
		getenv("LC_ALL") == NULL && getenv("LC_NUMERIC") == NULL ?
		LC_NUMERIC_DEFAULT : "");

	if (!debug) {
		/* Internally create the initial `--gauge' prompt text */
		dprompt_recreate(file_list, (struct dpv_file_node *)NULL, 0);

		/* Spawn [X]dialog(1) `--gauge', returning pipe descriptor */
		if (use_libdialog) {
			status_printf("");
			dprompt_libprint(pprompt, aprompt, 0);
		} else {
			dprompt_sprint(init_prompt, pprompt, aprompt);
			dialog_out = dialog_spawn_gauge(init_prompt, &pid);
			dprompt_dprint(dialog_out, pprompt, aprompt, 0);
		}
	} /* !debug */

	/* Seed the random(3) generator */
	if (dialog_test)
		srandom(0xf1eeface);

	/* Set default/custom status line format */
	if (dpv_nfiles > 1) {
		snprintf(status_format_default, DPV_STATUS_FORMAT_MAX, "%s",
		    DPV_STATUS_MANY);
		status_format_custom = config->status_many;
	} else {
		snprintf(status_format_default, DPV_STATUS_FORMAT_MAX, "%s",
		    DPV_STATUS_SOLO);
		status_format_custom = config->status_solo;
	}

	/* Add test mode identifier to default status line if enabled */
	if (dialog_test && (strlen(status_format_default) + 12) <
	    DPV_STATUS_FORMAT_MAX)
		strcat(status_format_default, " [TEST MODE]");

	/* Verify custom status format */
	status_fmt = fmtcheck(status_format_custom, status_format_default);
	if (status_format_custom != NULL &&
	    status_fmt == status_format_default) {
		warnx("WARNING! Invalid status_format configuration `%s'",
		      status_format_custom);
		warnx("Default status_format `%s'", status_format_default);
	}

	/* Record when we started (used to prevent updating too quickly) */
	(void)gettimeofday(&start, (struct timezone *)NULL);

	/* Calculate number of microseconds in-between sub-second updates */
	if (status_updates_per_second != 0)
		status_update_usec = 1000000 / status_updates_per_second;
	if (dialog_updates_per_second != 0)
		dialog_update_usec = 1000000 / dialog_updates_per_second;

	/*
	 * Process the file list [serially] (one for each argument passed)
	 */
	files_left = dpv_nfiles;
	list_head = file_list;
	for (curfile = file_list; curfile != NULL; curfile = curfile->next) {
		keep_going = TRUE;
		output_out = -1;
		pct = 0;
		nthfile++;
		files_left--;

		if (dpv_interrupt)
			break;
		if (dialog_test)
			pct = 0 - increment;

		/* Attempt to spawn output program for this file */
		if (!dialog_test && output != NULL) {
			mask = umask(0022);
			(void)umask(mask);

			switch (output_type) {
			case DPV_OUTPUT_SHELL:
				output_out = shell_spawn_pipecmd(output,
				    curfile->name, &output_pid);
				break;
			case DPV_OUTPUT_FILE:
				path_fmt = fmtcheck(output, "%s");
				if (path_fmt == output)
					len = snprintf(pathbuf,
					    PATH_MAX, output, curfile->name);
				else
					len = snprintf(pathbuf,
					    PATH_MAX, "%s", output);
				if (len >= PATH_MAX) {
					warnx("%s:%d:%s: pathbuf[%u] too small"
					    "to hold output argument",
					    __FILE__, __LINE__, __func__,
					    PATH_MAX);
					return (-1);
				}
				if ((output_out = open(pathbuf,
				    O_CREAT|O_WRONLY, DEFFILEMODE & ~mask))
				    < 0) {
					warn("%s", pathbuf);
					return (-1);
				}
				break;
			default:
				break;
			}
		}

		while (!dpv_interrupt && keep_going) {
			if (dialog_test) {
				usleep(50000);
				pct += increment;
				dpv_overall_read +=
				    (int)(random() / 512 / dpv_nfiles);
				    /* 512 limits fake readout to Megabytes */
			} else if (action != NULL)
				pct = action(curfile, output_out);

			if (no_overrun || dialog_test)
				keep_going = (pct < 100);
			else {
				status = curfile->status;
				keep_going = (status == DPV_STATUS_RUNNING);
			}

			/* Get current time and calculate seconds elapsed */
			gettimeofday(&now, (struct timezone *)NULL);
			now.tv_sec = now.tv_sec - start.tv_sec;
			now.tv_usec = now.tv_usec - start.tv_usec;
			if (now.tv_usec < 0)
				now.tv_sec--, now.tv_usec += 1000000;
			seconds = now.tv_sec + (now.tv_usec / 1000000.0);

			/* Update dialog (be it dialog(3), dialog(1), etc.) */
			if ((dialog_updates_per_second != 0 &&
			   (
			    seconds != dialog_old_seconds ||
			    now.tv_usec - dialog_last_update >=
			        dialog_update_usec ||
			    nthfile != dialog_old_nthfile
			   )) || pct == 100
			) {
				/* Calculate overall progress (rounding up) */
				overall = (100 * nthfile - 100 + pct) /
				    dpv_nfiles;
				if (((100 * nthfile - 100 + pct) * 10 /
				    dpv_nfiles % 100) > 50)
					overall++;

				dprompt_recreate(list_head, curfile, pct);

				if (use_libdialog && !debug) {
					/* Update dialog(3) widget */
					dprompt_libprint(pprompt, aprompt,
					    overall);
				} else {
					/* stdout, dialog(1), or Xdialog(1) */
					dprompt_dprint(dialog_out, pprompt,
					    aprompt, overall);
					fsync(dialog_out);
				}
				dialog_old_seconds = seconds;
				dialog_old_nthfile = nthfile;
				dialog_last_update = now.tv_usec;
			}

			/* Update the status line */
			if ((use_libdialog && !debug) &&
			    status_updates_per_second != 0 &&
			   (
			    keep_going != TRUE ||
			    seconds != status_old_seconds ||
			    now.tv_usec - status_last_update >=
			        status_update_usec ||
			    nthfile != status_old_nthfile
			   )
			) {
				status_printf(status_fmt, dpv_overall_read,
				    (dpv_overall_read / (seconds == 0 ? 1 :
					seconds) * 1.0),
				    1, /* XXX until we add parallelism XXX */
				    files_left);
				status_old_seconds = seconds;
				status_old_nthfile = nthfile;
				status_last_update = now.tv_usec;
			}
		}

		if (!dialog_test && output_out >= 0) {
			close(output_out);
			waitpid(output_pid, (int *)NULL, 0);	
		}

		if (dpv_abort)
			break;

		/* Advance head of list when we hit the max display lines */
		if (display_limit > 0 && nthfile % display_limit == 0)
			list_head = curfile->next;
	}

	if (!debug) {
		if (use_libdialog)
			end_dialog();
		else {
			close(dialog_out);
			waitpid(pid, (int *)NULL, 0);	
		}
		if (!keep_tite && !dpv_interrupt)
			printf("\n");
	} else
		warnx("%s: %lli overall read", __func__, dpv_overall_read);

	if (dpv_interrupt || dpv_abort)
		return (-1);
	else
		return (0);
}

/*
 * Free allocated items initialized by dpv()
 */
void
dpv_free(void)
{
	dialogrc_free();
	dprompt_free();
	dialog_maxsize_free();
	if (aprompt != NULL) {
		free(aprompt);
		aprompt = NULL;
	}
	if (pprompt != NULL) {
		free(pprompt);
		pprompt = NULL;
	}
	status_free();
}
