#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../histedit.h"


static int continuation;
volatile sig_atomic_t gotsig;
static const char hfile[] = ".whistory";

static wchar_t *
prompt(EditLine *el)
{
	static wchar_t a[] = L"\1\033[7m\1Edit$\1\033[0m\1 ";
	static wchar_t b[] = L"Edit> ";

	return continuation ? b : a;
}


static void
sig(int i)
{
	gotsig = i;
}

const char *
my_wcstombs(const wchar_t *wstr)
{
	static struct {
		char *str;
		int len;
	} buf;

	int needed = wcstombs(0, wstr, 0) + 1;
	if (needed > buf.len) {
		buf.str = malloc(needed);
		buf.len = needed;
	}
	wcstombs(buf.str, wstr, needed);
	buf.str[needed - 1] = 0;

	return buf.str;
}


static unsigned char
complete(EditLine *el, int ch)
{
	DIR *dd = opendir(".");
	struct dirent *dp;
	const wchar_t *ptr;
	char *buf, *bptr;
	const LineInfoW *lf = el_wline(el);
	int len, mblen, i;
	unsigned char res = 0;
	wchar_t dir[1024];

	/* Find the last word */
	for (ptr = lf->cursor -1; !iswspace(*ptr) && ptr > lf->buffer; --ptr)
		continue;
	len = lf->cursor - ++ptr;

	/* Convert last word to multibyte encoding, so we can compare to it */
	wctomb(NULL, 0); /* Reset shift state */
	mblen = MB_LEN_MAX * len + 1;
	buf = bptr = malloc(mblen);
	if (buf == NULL)
		err(1, "malloc");
	for (i = 0; i < len; ++i) {
		/* Note: really should test for -1 return from wctomb */
		bptr += wctomb(bptr, ptr[i]);
	}
	*bptr = 0; /* Terminate multibyte string */
	mblen = bptr - buf;

	/* Scan directory for matching name */
	for (dp = readdir(dd); dp != NULL; dp = readdir(dd)) {
		if (mblen > strlen(dp->d_name))
			continue;
		if (strncmp(dp->d_name, buf, mblen) == 0) {
			mbstowcs(dir, &dp->d_name[mblen],
			    sizeof(dir) / sizeof(*dir));
			if (el_winsertstr(el, dir) == -1)
				res = CC_ERROR;
			else
				res = CC_REFRESH;
			break;
		}
	}

	closedir(dd);
	free(buf);
	return res;
}


int
main(int argc, char *argv[])
{
	EditLine *el = NULL;
	int numc, ncontinuation;
	const wchar_t *line;
	TokenizerW *tok;
	HistoryW *hist;
	HistEventW ev;
#ifdef DEBUG
	int i;
#endif

	setlocale(LC_ALL, "");

	(void)signal(SIGINT,  sig);
	(void)signal(SIGQUIT, sig);
	(void)signal(SIGHUP,  sig);
	(void)signal(SIGTERM, sig);

	hist = history_winit();		/* Init built-in history     */
	history_w(hist, &ev, H_SETSIZE, 100);	/* Remember 100 events	     */
	history_w(hist, &ev, H_LOAD, hfile);

	tok = tok_winit(NULL);			/* Init the tokenizer	     */

	el = el_init(argv[0], stdin, stdout, stderr);

	el_wset(el, EL_EDITOR, L"vi");		/* Default editor is vi	     */
	el_wset(el, EL_SIGNAL, 1);		/* Handle signals gracefully */
	el_wset(el, EL_PROMPT_ESC, prompt, '\1'); /* Set the prompt function */

	el_wset(el, EL_HIST, history_w, hist);	/* FIXME - history_w? */

					/* Add a user-defined function	*/
	el_wset(el, EL_ADDFN, L"ed-complete", L"Complete argument", complete);

					/* Bind <tab> to it */
	el_wset(el, EL_BIND, L"^I", L"ed-complete", NULL);

	/*
	* Bind j, k in vi command mode to previous and next line, instead
	* of previous and next history.
	*/
	el_wset(el, EL_BIND, L"-a", L"k", L"ed-prev-line", NULL);
	el_wset(el, EL_BIND, L"-a", L"j", L"ed-next-line", NULL);

	/* Source the user's defaults file. */
	el_source(el, NULL);

	while((line = el_wgets(el, &numc)) != NULL && numc != 0) {
		int ac, cc, co, rc;
		const wchar_t **av;

		const LineInfoW *li;
		li = el_wline(el);

#ifdef DEBUG
		(void)fwprintf(stderr, L"==> got %d %ls", numc, line);
		(void)fwprintf(stderr, L"  > li `%.*ls_%.*ls'\n",
		    (li->cursor - li->buffer), li->buffer,
		    (li->lastchar - 1 - li->cursor),
		    (li->cursor >= li->lastchar) ? L"" : li->cursor);
#endif

		if (gotsig) {
			(void)fprintf(stderr, "Got signal %d.\n", (int)gotsig);
			gotsig = 0;
			el_reset(el);
		}

		if(!continuation && numc == 1)
			continue;	/* Only got a linefeed */

		ac = cc = co = 0;
		ncontinuation = tok_wline(tok, li, &ac, &av, &cc, &co);
		if (ncontinuation < 0) {
			(void) fprintf(stderr, "Internal error\n");
			continuation = 0;
			continue;
		}

#ifdef DEBUG
		(void)fprintf(stderr, "  > nc %d ac %d cc %d co %d\n",
			ncontinuation, ac, cc, co);
#endif
		history_w(hist, &ev, continuation ? H_APPEND : H_ENTER, line);

		continuation = ncontinuation;
		ncontinuation = 0;
		if(continuation)
			continue;

#ifdef DEBUG
		for (i = 0; i < ac; ++i) {
			(void)fwprintf(stderr, L"  > arg# %2d ", i);
			if (i != cc)
				(void)fwprintf(stderr, L"`%ls'\n", av[i]);
			else
				(void)fwprintf(stderr, L"`%.*ls_%ls'\n",
				    co, av[i], av[i] + co);
		}
#endif

		if (wcscmp (av[0], L"history") == 0) {
			switch(ac) {
			case 1:
				for(rc = history_w(hist, &ev, H_LAST);
				     rc != -1;
				     rc = history_w(hist, &ev, H_PREV))
					(void)fwprintf(stdout, L"%4d %ls",
					     ev.num, ev.str);
				break;
			case 2:
				if (wcscmp(av[1], L"clear") == 0)
					history_w(hist, &ev, H_CLEAR);
				else
					goto badhist;
				break;
			case 3:
				if (wcscmp(av[1], L"load") == 0)
					history_w(hist, &ev, H_LOAD,
					    my_wcstombs(av[2]));
				else if (wcscmp(av[1], L"save") == 0)
					history_w(hist, &ev, H_SAVE,
					    my_wcstombs(av[2]));
				else
					goto badhist;
				break;
			badhist:
			default:
				(void)fprintf(stderr,
				    "Bad history arguments\n");
				break;
			}
		} else if (el_wparse(el, ac, av) == -1) {
			switch (fork()) {
			case 0: {
				Tokenizer *ntok = tok_init(NULL);
				int nargc;
				const char **nav;
				tok_str(ntok, my_wcstombs(line), &nargc, &nav);
				execvp(nav[0],(char **)nav);
				perror(nav[0]);
				_exit(1);
				/* NOTREACHED */
				break;
			}
			case -1:
				perror("fork");
				break;
			default:
				if (wait(&rc) == -1)
					perror("wait");
				(void)fprintf(stderr, "Exit %x\n", rc);
				break;
			}
		}

		tok_wreset(tok);
	}

	el_end(el);
	tok_wend(tok);
	history_w(hist, &ev, H_SAVE, hfile);
	history_wend(hist);

	fprintf(stdout, "\n");
	return 0;
}


