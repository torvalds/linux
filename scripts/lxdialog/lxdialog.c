/*
 *  dialog - Display simple dialog boxes from shell scripts
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "dialog.h"

static void Usage (const char *name);

typedef int (jumperFn) (const char *title, int argc, const char * const * argv);

struct Mode {
    char *name;
    int argmin, argmax, argmod;
    jumperFn *jumper;
};

jumperFn j_menu, j_checklist, j_radiolist, j_yesno, j_textbox, j_inputbox;
jumperFn j_msgbox, j_infobox;

static struct Mode modes[] =
{
    {"--menu", 9, 0, 3, j_menu},
    {"--checklist", 9, 0, 3, j_checklist},
    {"--radiolist", 9, 0, 3, j_radiolist},
    {"--yesno",    5,5,1, j_yesno},
    {"--textbox",  5,5,1, j_textbox},
    {"--inputbox", 5, 6, 1, j_inputbox},
    {"--msgbox", 5, 5, 1, j_msgbox},
    {"--infobox", 5, 5, 1, j_infobox},
    {NULL, 0, 0, 0, NULL}
};

static struct Mode *modePtr;

#ifdef LOCALE
#include <locale.h>
#endif

int
main (int argc, const char * const * argv)
{
    int offset = 0, opt_clear = 0, end_common_opts = 0, retval;
    const char *title = NULL;

#ifdef LOCALE
    (void) setlocale (LC_ALL, "");
#endif

#ifdef TRACE
    trace(TRACE_CALLS|TRACE_UPDATE);
#endif
    if (argc < 2) {
	Usage (argv[0]);
	exit (-1);
    }

    while (offset < argc - 1 && !end_common_opts) {	/* Common options */
	if (!strcmp (argv[offset + 1], "--title")) {
	    if (argc - offset < 3 || title != NULL) {
		Usage (argv[0]);
		exit (-1);
	    } else {
		title = argv[offset + 2];
		offset += 2;
	    }
        } else if (!strcmp (argv[offset + 1], "--backtitle")) {
            if (backtitle != NULL) {
                Usage (argv[0]);
                exit (-1);
            } else {
                backtitle = argv[offset + 2];
                offset += 2;
            }
	} else if (!strcmp (argv[offset + 1], "--clear")) {
	    if (opt_clear) {		/* Hey, "--clear" can't appear twice! */
		Usage (argv[0]);
		exit (-1);
	    } else if (argc == 2) {	/* we only want to clear the screen */
		init_dialog ();
		refresh ();	/* init_dialog() will clear the screen for us */
		end_dialog ();
		return 0;
	    } else {
		opt_clear = 1;
		offset++;
	    }
	} else			/* no more common options */
	    end_common_opts = 1;
    }

    if (argc - 1 == offset) {	/* no more options */
	Usage (argv[0]);
	exit (-1);
    }
    /* use a table to look for the requested mode, to avoid code duplication */

    for (modePtr = modes; modePtr->name; modePtr++)	/* look for the mode */
	if (!strcmp (argv[offset + 1], modePtr->name))
	    break;

    if (!modePtr->name)
	Usage (argv[0]);
    if (argc - offset < modePtr->argmin)
	Usage (argv[0]);
    if (modePtr->argmax && argc - offset > modePtr->argmax)
	Usage (argv[0]);



    init_dialog ();
    retval = (*(modePtr->jumper)) (title, argc - offset, argv + offset);

    if (opt_clear) {		/* clear screen before exit */
	attr_clear (stdscr, LINES, COLS, screen_attr);
	refresh ();
    }
    end_dialog();

    exit (retval);
}

/*
 * Print program usage
 */
static void
Usage (const char *name)
{
    fprintf (stderr, "\
\ndialog, by Savio Lam (lam836@cs.cuhk.hk).\
\n  patched by Stuart Herbert (S.Herbert@shef.ac.uk)\
\n  modified/gutted for use as a Linux kernel config tool by \
\n  William Roadcap (roadcapw@cfw.com)\
\n\
\n* Display dialog boxes from shell scripts *\
\n\
\nUsage: %s --clear\
\n       %s [--title <title>] [--backtitle <backtitle>] --clear <Box options>\
\n\
\nBox options:\
\n\
\n  --menu      <text> <height> <width> <menu height> <tag1> <item1>...\
\n  --checklist <text> <height> <width> <list height> <tag1> <item1> <status1>...\
\n  --radiolist <text> <height> <width> <list height> <tag1> <item1> <status1>...\
\n  --textbox   <file> <height> <width>\
\n  --inputbox  <text> <height> <width> [<init>]\
\n  --yesno     <text> <height> <width>\
\n", name, name);
    exit (-1);
}

/*
 * These are the program jumpers
 */

int
j_menu (const char *t, int ac, const char * const * av)
{
    return dialog_menu (t, av[2], atoi (av[3]), atoi (av[4]),
			atoi (av[5]), av[6], (ac - 6) / 2, av + 7);
}

int
j_checklist (const char *t, int ac, const char * const * av)
{
    return dialog_checklist (t, av[2], atoi (av[3]), atoi (av[4]),
	atoi (av[5]), (ac - 6) / 3, av + 6, FLAG_CHECK);
}

int
j_radiolist (const char *t, int ac, const char * const * av)
{
    return dialog_checklist (t, av[2], atoi (av[3]), atoi (av[4]),
	atoi (av[5]), (ac - 6) / 3, av + 6, FLAG_RADIO);
}

int
j_textbox (const char *t, int ac, const char * const * av)
{
    return dialog_textbox (t, av[2], atoi (av[3]), atoi (av[4]));
}

int
j_yesno (const char *t, int ac, const char * const * av)
{
    return dialog_yesno (t, av[2], atoi (av[3]), atoi (av[4]));
}

int
j_inputbox (const char *t, int ac, const char * const * av)
{
    int ret = dialog_inputbox (t, av[2], atoi (av[3]), atoi (av[4]),
                            ac == 6 ? av[5] : (char *) NULL);
    if (ret == 0)
        fprintf(stderr, dialog_input_result);
    return ret;
}

int
j_msgbox (const char *t, int ac, const char * const * av)
{
    return dialog_msgbox (t, av[2], atoi (av[3]), atoi (av[4]), 1);
}

int
j_infobox (const char *t, int ac, const char * const * av)
{
    return dialog_msgbox (t, av[2], atoi (av[3]), atoi (av[4]), 0);
}

