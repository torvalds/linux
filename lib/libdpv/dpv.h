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
 *
 * $FreeBSD$
 */

#ifndef _DPV_H_
#define _DPV_H_

#include <sys/types.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* localeconv(3) */
#define LC_NUMERIC_DEFAULT	"en_US.ISO8859-1"

/* Data to process */
extern long long dpv_overall_read;

/* Interrupt flag */
extern int dpv_interrupt;	/* Set to TRUE in interrupt handler */
extern int dpv_abort;		/* Set to true in callback to abort */

/*
 * Display types for use with display_type member of dpv_config structure
 */
enum dpv_display {
	DPV_DISPLAY_LIBDIALOG = 0,	/* Display using dialog(3) (default) */
	DPV_DISPLAY_STDOUT,		/* Display on stdout */
	DPV_DISPLAY_DIALOG,		/* Display using spawned dialog(1) */
	DPV_DISPLAY_XDIALOG,		/* Display using spawned Xdialog(1) */
};

/*
 * Output types for use with output_type member of dpv_config structure
 */
enum dpv_output {
	DPV_OUTPUT_NONE = 0,	/* No output (default) */
	DPV_OUTPUT_FILE,	/* Read `output' member as file path */
	DPV_OUTPUT_SHELL,	/* Read `output' member as shell cmd */
};

/*
 * Activity types for use with status member of dpv_file_node structure.
 * If you set a status other than DPV_STATUS_RUNNING on the current file in the
 * action callback of dpv_config structure, you'll end callbacks for that
 * dpv_file_node.
 */
enum dpv_status {
	DPV_STATUS_RUNNING = 0,	/* Running (default) */
	DPV_STATUS_DONE,	/* Completed */
	DPV_STATUS_FAILED,	/* Oops, something went wrong */
};

/*
 * Anatomy of file option; pass an array of these as dpv() file_list argument
 * terminated with a NULL pointer.
 */
struct dpv_file_node {
	enum dpv_status		status; /* status of read operation */
	char			*msg;	/* display instead of "Done/Fail" */
	char			*name;	/* name of file to read */
	char			*path;	/* path to file */
	long long		length;	/* expected size */
	long long		read;	/* number units read (e.g., bytes) */
	struct dpv_file_node	*next;	/* pointer to next (end with NULL) */
};

/*
 * Anatomy of config option to pass as dpv() config argument
 */
struct dpv_config {
	uint8_t	keep_tite;		/* Prevent visually distracting exit */
	enum dpv_display display_type;	/* Display (default TYPE_LIBDIALOG) */
	enum dpv_output  output_type;	/* Output (default TYPE_NONE) */
	int	debug;			/* Enable debugging output on stderr */
	int	display_limit;		/* Files per `page'. Default -1 */
	int	label_size;		/* Label size. Default 28 */
	int	pbar_size;		/* Mini-progress size. See dpv(3) */
	int	dialog_updates_per_second; /* Progress updates/s. Default 16 */
	int	status_updates_per_second; /* dialog(3) status updates/second.
	   	                            * Default 2 */
	uint16_t options;	/* Special options. Default 0 */
	char	*title;		/* widget title */
	char	*backtitle;	/* Widget backtitle */
	char	*aprompt;	/* Prompt append. Default NULL */
	char	*pprompt;	/* Prompt prefix. Default NULL */
	char	*msg_done;	/* Progress text. Default `Done' */
	char	*msg_fail;	/* Progress text. Default `Fail' */
	char	*msg_pending;	/* Progress text. Default `Pending' */
	char	*output;	/* Output format string; see dpv(3) */
	const char *status_solo; /* dialog(3) solo-status format.
	                          * Default DPV_STATUS_SOLO */
	const char *status_many; /* dialog(3) many-status format.
	                          * Default DPV_STATUS_MANY */

	/*
	 * Function pointer; action to perform data transfer
	 */
	int (*action)(struct dpv_file_node *file, int out);
};

/*
 * Macros for dpv() options bitmask argument
 */
#define DPV_TEST_MODE		0x0001	/* Test mode (fake reading data) */
#define DPV_WIDE_MODE		0x0002	/* prefix/append bump dialog width */
#define DPV_NO_LABELS		0x0004	/* Hide file_node.name labels */
#define DPV_USE_COLOR		0x0008	/* Override to force color output */
#define DPV_NO_OVERRUN		0x0010	/* Stop transfers when they hit 100% */

/*
 * Limits (modify with extreme care)
 */
#define DPV_APROMPT_MAX		4096	/* Buffer size for `-a text' */
#define DPV_DISPLAY_LIMIT	10	/* Max file progress lines */
#define DPV_PPROMPT_MAX		4096	/* Buffer size for `-p text' */
#define DPV_STATUS_FORMAT_MAX	80	/* Buffer size for `-u format' */

/*
 * Extra display information
 */
#define DPV_STATUS_SOLO		"%'10lli bytes read @ %'9.1f bytes/sec."
#define DPV_STATUS_MANY		(DPV_STATUS_SOLO " [%i/%i busy/wait]")

/*
 * Strings
 */
#define DPV_DONE_DEFAULT	"Done"
#define DPV_FAIL_DEFAULT	"Fail"
#define DPV_PENDING_DEFAULT	"Pending"

__BEGIN_DECLS
void	dpv_free(void);
int	dpv(struct dpv_config *_config, struct dpv_file_node *_file_list);
__END_DECLS

#endif /* !_DPV_H_ */
