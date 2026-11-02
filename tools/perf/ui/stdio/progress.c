// SPDX-License-Identifier: GPL-2.0
/*
 * Progress feedback for the stdio (non-TUI/GTK) case, enabled with
 * 'perf report --progress'.
 */
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/kernel.h>
#include <subcmd/pager.h>
#include "../../util/debug.h"
#include "../../util/units.h"
#include "../progress.h"

/*
 * Phases can be nested, so keep track of the ones started so far to be
 * able to complete the right one on ui_progress__finish(), which gets
 * no arguments.
 */
#define STDIO_PROGRESS__MAX_DEPTH 8

struct stdio_progress_phase {
	struct ui_progress	*p;
	u64			last_printed;
	size_t			last_len;
};

static struct stdio_progress_phase stdio_progress__stack[STDIO_PROGRESS__MAX_DEPTH];
static int stdio_progress__depth;
static FILE *stdio_progress__out;
static bool stdio_progress__is_tty;
/* Phases that didn't fit on the stack are not shown. */
static int stdio_progress__dropped;

static void stdio_progress__print_phase(struct stdio_progress_phase *phase,
					u64 curr)
{
	struct ui_progress *p = phase->p;
	char buf_cur[20], buf_tot[20], buf[128];
	double percent = p->total ? 100.0 * (double)curr / (double)p->total : 0.0;
	size_t len;

	/*
	 * Only the completion line shows 100.0%: a 99.99% progress would round
	 * up to it and look like a duplicate at finish time.
	 */
	if (curr < p->total && percent > 99.9)
		percent = 99.9;

	if (p->size) {
		unit_number__scnprintf(buf_cur, sizeof(buf_cur), curr);
		unit_number__scnprintf(buf_tot, sizeof(buf_tot), p->total);
		len = scnprintf(buf, sizeof(buf), "%s [%5.1f%%] %s / %s",
				p->title, percent, buf_cur, buf_tot);
	} else {
		len = scnprintf(buf, sizeof(buf), "%s [%5.1f%%] %" PRIu64 " / %" PRIu64,
				p->title, percent, curr, p->total);
	}

	if (!stdio_progress__is_tty) {
		fprintf(stdio_progress__out, "%s\n", buf);
		goto out;
	}

	/* Pad to the length of the previous line to erase its leftovers. */
	fprintf(stdio_progress__out, "\r%s%*s", buf,
		(int)(len < phase->last_len ? phase->last_len - len : 0), "");
	phase->last_len = len;
out:
	phase->last_printed = curr;
	fflush(stdio_progress__out);
}

static void __stdio_progress__init(struct ui_progress *p)
{
	/* The default step is meant for the TUI bar, use 1% steps for stdio. */
	p->next = p->step = p->total / 100 ?: 1;

	if (stdio_progress__depth == STDIO_PROGRESS__MAX_DEPTH) {
		/*
		 * Out of room: don't start this phase, its finish() is
		 * swallowed and its updates ignored below.
		 */
		pr_warning("progress phases nested deeper than %d, not showing progress for %s\n",
			   STDIO_PROGRESS__MAX_DEPTH, p->title);
		stdio_progress__dropped++;
		return;
	}

	/* Start a nested phase in a line of its own. */
	if (stdio_progress__depth && stdio_progress__is_tty)
		fputc('\n', stdio_progress__out);

	stdio_progress__stack[stdio_progress__depth++] =
		(struct stdio_progress_phase) {
			.p = p,
			.last_printed = 0,
			.last_len = 0,
		};

	stdio_progress__print_phase(&stdio_progress__stack[stdio_progress__depth - 1],
				    p->curr);
}

static void stdio_progress__update(struct ui_progress *p)
{
	/*
	 * An update that doesn't match the innermost phase means something
	 * is out of sync: print nothing rather than another phase's
	 * numbers, or read past the stack.
	 */
	if (!stdio_progress__depth ||
	    stdio_progress__stack[stdio_progress__depth - 1].p != p)
		return;

	stdio_progress__print_phase(&stdio_progress__stack[stdio_progress__depth - 1],
				    p->curr);
}

static void stdio_progress__finish(void)
{
	struct stdio_progress_phase *phase;

	/*
	 * Being the innermost phase, its finish() comes first: swallow it, or
	 * it would complete the phase that encloses it.
	 */
	if (stdio_progress__dropped) {
		stdio_progress__dropped--;
		return;
	}

	if (!stdio_progress__depth)
		return;

	phase = &stdio_progress__stack[--stdio_progress__depth];

	/*
	 * The last line may have stopped short of the total, close this phase
	 * showing it as complete unless that was already printed.
	 */
	if (phase->last_printed != phase->p->total)
		stdio_progress__print_phase(phase, phase->p->total);

	phase->last_printed	= 0;
	phase->last_len		= 0;

	if (stdio_progress__is_tty)
		fputc('\n', stdio_progress__out);

	fflush(stdio_progress__out);
}

static struct ui_progress_ops stdio_progress__ops = {
	.init	= __stdio_progress__init,
	.update	= stdio_progress__update,
	.finish	= stdio_progress__finish,
};

void stdio_progress__init(void)
{
	if (pager_in_use()) {
		/* stderr leads to the pager, the updates go to the terminal. */
		stdio_progress__out = fopen("/dev/tty", "w");
	}
	if (!stdio_progress__out)
		stdio_progress__out = stderr;
	stdio_progress__is_tty = isatty(fileno(stdio_progress__out)) == 1;
	ui_progress__ops = &stdio_progress__ops;
}
