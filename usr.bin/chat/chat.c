/*
 *	Chat -- a program for automatic session establishment (i.e. dial
 *		the phone and log in).
 *
 * Standard termination codes:
 *  0 - successful completion of the script
 *  1 - invalid argument, expect string too large, etc.
 *  2 - error on an I/O operation or fatal error condition.
 *  3 - timeout waiting for a simple string.
 *  4 - the first string declared as "ABORT"
 *  5 - the second string declared as "ABORT"
 *  6 - ... and so on for successive ABORT strings.
 *
 *	This software is in the public domain.
 *
 * -----------------
 *	added -T and -U option and \T and \U substitution to pass a phone
 *	number into chat script. Two are needed for some ISDN TA applications.
 *	Keith Dart <kdart@cisco.com>
 *	
 *
 *	Added SAY keyword to send output to stderr.
 *      This allows to turn ECHO OFF and to output specific, user selected,
 *      text to give progress messages. This best works when stderr
 *      exists (i.e.: pppd in nodetach mode).
 *
 * 	Added HANGUP directives to allow for us to be called
 *      back. When HANGUP is set to NO, chat will not hangup at HUP signal.
 *      We rely on timeouts in that case.
 *
 *      Added CLR_ABORT to clear previously set ABORT string. This has been
 *      dictated by the HANGUP above as "NO CARRIER" (for example) must be
 *      an ABORT condition until we know the other host is going to close
 *      the connection for call back. As soon as we have completed the
 *      first stage of the call back sequence, "NO CARRIER" is a valid, non
 *      fatal string. As soon as we got called back (probably get "CONNECT"),
 *      we should re-arm the ABORT "NO CARRIER". Hence the CLR_ABORT command.
 *      Note that CLR_ABORT packs the abort_strings[] array so that we do not
 *      have unused entries not being reclaimed.
 *
 *      In the same vein as above, added CLR_REPORT keyword.
 *
 *      Allow for comments. Line starting with '#' are comments and are
 *      ignored. If a '#' is to be expected as the first character, the 
 *      expect string must be quoted.
 *
 *
 *		Francis Demierre <Francis@SwissMail.Com>
 * 		Thu May 15 17:15:40 MET DST 1997
 *
 *
 *      Added -r "report file" switch & REPORT keyword.
 *              Robert Geer <bgeer@xmission.com>
 *
 *      Added -s "use stderr" and -S "don't use syslog" switches.
 *              June 18, 1997
 *              Karl O. Pinc <kop@meme.com>
 *
 *
 *	Added -e "echo" switch & ECHO keyword
 *		Dick Streefland <dicks@tasking.nl>
 *
 *
 *	Considerable updates and modifications by
 *		Al Longyear <longyear@pobox.com>
 *		Paul Mackerras <paulus@cs.anu.edu.au>
 *
 *
 *	The original author is:
 *
 *		Karl Fox <karl@MorningStar.Com>
 *		Morning Star Technologies, Inc.
 *		1760 Zollinger Road
 *		Columbus, OH  43221
 *		(614)451-1883
 *
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define	STR_LEN	1024

#ifndef SIGTYPE
#define SIGTYPE void
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK	O_NDELAY
#endif

#define	MAX_ABORTS		50
#define	MAX_REPORTS		50
#define	DEFAULT_CHAT_TIMEOUT	45

static int echo;
static int verbose;
static int to_log;
static int to_stderr;
static int Verbose;
static int quiet;
static int exit_code;
static FILE* report_fp;
static char *report_file;
static char *chat_file;
static char *phone_num;
static char *phone_num2;
static int timeout = DEFAULT_CHAT_TIMEOUT;

static char blank[] = "";

static int have_tty_parameters;

#define term_parms struct termios
#define get_term_param(param) tcgetattr(0, param)
#define set_term_param(param) tcsetattr(0, TCSANOW, param)
static struct termios saved_tty_parameters;

static char *abort_string[MAX_ABORTS], *fail_reason, fail_buffer[50];
static int n_aborts, abort_next, timeout_next, echo_next;
static int clear_abort_next;

static char *report_string[MAX_REPORTS];
static char  report_buffer[50];
static int n_reports, report_next, report_gathering;
static int clear_report_next;

static int say_next, hup_next;

void *dup_mem(void *b, size_t c);
void *copy_of(char *s);
static void usage(void);
void chat_logf(const char *fmt, ...);
void fatal(int code, const char *fmt, ...);
SIGTYPE sigalrm(int signo);
SIGTYPE sigint(int signo);
SIGTYPE sigterm(int signo);
SIGTYPE sighup(int signo);
void init(void);
void set_tty_parameters(void);
void echo_stderr(int);
void break_sequence(void);
void terminate(int status);
void do_file(char *chatfile);
int  get_string(char *string);
int  put_string(char *s);
int  write_char(int c);
int  put_char(int c);
int  get_char(void);
void chat_send(char *s);
char *character(int c);
void chat_expect(char *s);
char *clean(char *s, int sending);
void pack_array(char **array, int end);
char *expect_strtok(char *, const char *);
int vfmtmsg(char *, int, const char *, va_list);	/* vsprintf++ */

void *
dup_mem(void *b, size_t c)
{
    void *ans = malloc (c);
    if (!ans)
	fatal(2, "memory error!");

    memcpy (ans, b, c);
    return ans;
}

void *
copy_of(char *s)
{
    return dup_mem (s, strlen (s) + 1);
}

/*
 * chat [-esSvV] [-f chat-file] [-r report-file] [-t timeout]
 *      [-T phone-number] [-U phone-number2] [chat-script]
 * where chat-script has the form:
 *	[...[[expect[-send[-expect...]] send expect[-send[-expect]] ...]]]
 *
 * Perform a UUCP-dialer-like chat script on stdin and stdout.
 */
int
main(int argc, char *argv[])
{
    int option;

    tzset();

    while ((option = getopt(argc, argv, "ef:r:sSt:T:U:vV")) != -1) {
	switch (option) {
	case 'e':
	    ++echo;
	    break;

	case 'f':
	    if (chat_file != NULL)
		free(chat_file);
	    chat_file = copy_of(optarg);
	    break;

	case 'r':
	    if (report_fp != NULL)
		fclose(report_fp);
	    if (report_file != NULL)
		free(report_file);
	    report_file = copy_of(optarg);
	    report_fp = fopen(report_file, "a");
	    if (report_fp != NULL) {
		if (verbose)
		    fprintf(report_fp, "Opening \"%s\"...\n", report_file);
	    } else
		fatal(2, "cannot open \"%s\" for appending", report_file);
	    break;

	case 's':
	    ++to_stderr;
	    break;

	case 'S':
	    to_log = 0;
	    break;

	case 't':
	    timeout = atoi(optarg);
	    break;

	case 'T':
	    if (phone_num != NULL)
		free(phone_num);
	    phone_num = copy_of(optarg);
	    break;

	case 'U':
	    if (phone_num2 != NULL)
		free(phone_num2);
	    phone_num2 = copy_of(optarg);
	    break;

	case 'v':
	    ++verbose;
	    break;

	case 'V':
	    ++Verbose;
	    break;

	default:
	    usage();
	    break;
	}
    }

    argc -= optind;
    argv += optind;

/*
 * Default the report file to the stderr location
 */
    if (report_fp == NULL)
	report_fp = stderr;

    if (to_log) {
	openlog("chat", LOG_PID | LOG_NDELAY, LOG_LOCAL2);

	if (verbose)
	    setlogmask(LOG_UPTO(LOG_INFO));
	else
	    setlogmask(LOG_UPTO(LOG_WARNING));
    }

    if (chat_file != NULL) {
	if (*argv != NULL)
	    usage();
	else {
            init();
	    do_file(chat_file);
	}
    } else {
	init();
	while (*argv != NULL && argc > 0) {
	    chat_expect(*argv);
	    argv++;
	    argc--;

	    if (*argv != NULL && argc > 0) {
		chat_send(*argv);
		argv++;
		argc--;
	    }
	}
    }

    terminate(0);
    return 0;
}

/*
 *  Process a chat script when read from a file.
 */

void
do_file(char *chatfile)
{
    int linect, sendflg;
    char *sp, *arg, quote;
    char buf [STR_LEN];
    FILE *cfp;

    cfp = fopen (chatfile, "r");
    if (cfp == NULL)
	fatal(1, "%s -- open failed: %m", chatfile);

    linect = 0;
    sendflg = 0;

    while (fgets(buf, STR_LEN, cfp) != NULL) {
	sp = strchr (buf, '\n');
	if (sp)
	    *sp = '\0';

	linect++;
	sp = buf;

        /* lines starting with '#' are comments. If a real '#'
           is to be expected, it should be quoted .... */
        if ( *sp == '#' )
	    continue;

	while (*sp != '\0') {
	    if (*sp == ' ' || *sp == '\t') {
		++sp;
		continue;
	    }

	    if (*sp == '"' || *sp == '\'') {
		quote = *sp++;
		arg = sp;
		while (*sp != quote) {
		    if (*sp == '\0')
			fatal(1, "unterminated quote (line %d)", linect);

		    if (*sp++ == '\\') {
			if (*sp != '\0')
			    ++sp;
		    }
		}
	    }
	    else {
		arg = sp;
		while (*sp != '\0' && *sp != ' ' && *sp != '\t')
		    ++sp;
	    }

	    if (*sp != '\0')
		*sp++ = '\0';

	    if (sendflg)
		chat_send (arg);
	    else
		chat_expect (arg);
	    sendflg = !sendflg;
	}
    }
    fclose (cfp);
}

/*
 *	We got an error parsing the command line.
 */
static void
usage(void)
{
    fprintf(stderr,
      "Usage: chat [-esSvV] [-f chat-file] [-r report-file] [-t timeout]\n"
      "            [-T phone-number] [-U phone-number2] [chat-script]\n"
      "where chat-script has the form:\n"
      "            [...[[expect[-send[-expect...]] send expect[-send[-expect]] ...]]]\n");
    exit(1);
}

/*
 * Send a message to syslog and/or stderr.
 */
void
chat_logf(const char *fmt, ...)
{
    char line[1024];
    va_list args;

    va_start(args, fmt);
    vfmtmsg(line, sizeof(line), fmt, args);
    va_end(args);
    if (to_log)
	syslog(LOG_INFO, "%s", line);
    if (to_stderr)
	fprintf(stderr, "%s\n", line);
}

/*
 *	Print an error message and terminate.
 */

void
fatal(int code, const char *fmt, ...)
{
    char line[1024];
    va_list args;

    va_start(args, fmt);
    vfmtmsg(line, sizeof(line), fmt, args);
    va_end(args);
    if (to_log)
	syslog(LOG_ERR, "%s", line);
    if (to_stderr)
	fprintf(stderr, "%s\n", line);
    terminate(code);
}

static int alarmed;

SIGTYPE sigalrm(int signo __unused)
{
    int flags;

    alarm(1);
    alarmed = 1;		/* Reset alarm to avoid race window */
    signal(SIGALRM, sigalrm);	/* that can cause hanging in read() */

    if ((flags = fcntl(0, F_GETFL, 0)) == -1)
	fatal(2, "Can't get file mode flags on stdin: %m");

    if (fcntl(0, F_SETFL, flags | O_NONBLOCK) == -1)
	fatal(2, "Can't set file mode flags on stdin: %m");

    if (verbose)
	chat_logf("alarm");
}

SIGTYPE sigint(int signo __unused)
{
    fatal(2, "SIGINT");
}

SIGTYPE sigterm(int signo __unused)
{
    fatal(2, "SIGTERM");
}

SIGTYPE sighup(int signo __unused)
{
    fatal(2, "SIGHUP");
}

void init(void)
{
    signal(SIGINT, sigint);
    signal(SIGTERM, sigterm);
    signal(SIGHUP, sighup);

    set_tty_parameters();
    signal(SIGALRM, sigalrm);
    alarm(0);
    alarmed = 0;
}

void set_tty_parameters(void)
{
#if defined(get_term_param)
    term_parms t;

    if (get_term_param (&t) < 0)
	fatal(2, "Can't get terminal parameters: %m");

    saved_tty_parameters = t;
    have_tty_parameters  = 1;

    t.c_iflag     |= IGNBRK | ISTRIP | IGNPAR;
    t.c_oflag      = 0;
    t.c_lflag      = 0;
    t.c_cc[VERASE] =
    t.c_cc[VKILL]  = 0;
    t.c_cc[VMIN]   = 1;
    t.c_cc[VTIME]  = 0;

    if (set_term_param (&t) < 0)
	fatal(2, "Can't set terminal parameters: %m");
#endif
}

void break_sequence(void)
{
    tcsendbreak (0, 0);
}

void terminate(int status)
{
    echo_stderr(-1);
    if (report_file != (char *) 0 && report_fp != (FILE *) NULL) {
/*
 * Allow the last of the report string to be gathered before we terminate.
 */
	if (report_gathering) {
	    int c;
	    size_t rep_len;

	    rep_len = strlen(report_buffer);
	    while (rep_len + 1 < sizeof(report_buffer)) {
		alarm(1);
		c = get_char();
		alarm(0);
		if (c < 0 || iscntrl(c))
		    break;
		report_buffer[rep_len] = c;
		++rep_len;
	    }
	    report_buffer[rep_len] = 0;
	    fprintf (report_fp, "chat:  %s\n", report_buffer);
	}
	if (verbose)
	    fprintf (report_fp, "Closing \"%s\".\n", report_file);
	fclose (report_fp);
	report_fp = (FILE *) NULL;
    }

#if defined(get_term_param)
    if (have_tty_parameters) {
	if (set_term_param (&saved_tty_parameters) < 0)
	    fatal(2, "Can't restore terminal parameters: %m");
    }
#endif

    exit(status);
}

/*
 *	'Clean up' this string.
 */
char *
clean(char *s, int sending)
{
    char temp[STR_LEN], cur_chr;
    char *s1, *phchar;
    int add_return = sending;
#define isoctal(chr) (((chr) >= '0') && ((chr) <= '7'))

    s1 = temp;
    /* Don't overflow buffer, leave room for chars we append later */
    while (*s && s1 - temp < (off_t)(sizeof(temp) - 2 - add_return)) {
	cur_chr = *s++;
	if (cur_chr == '^') {
	    cur_chr = *s++;
	    if (cur_chr == '\0') {
		*s1++ = '^';
		break;
	    }
	    cur_chr &= 0x1F;
	    if (cur_chr != 0) {
		*s1++ = cur_chr;
	    }
	    continue;
	}

	if (cur_chr != '\\') {
	    *s1++ = cur_chr;
	    continue;
	}

	cur_chr = *s++;
	if (cur_chr == '\0') {
	    if (sending) {
		*s1++ = '\\';
		*s1++ = '\\';
	    }
	    break;
	}

	switch (cur_chr) {
	case 'b':
	    *s1++ = '\b';
	    break;

	case 'c':
	    if (sending && *s == '\0')
		add_return = 0;
	    else
		*s1++ = cur_chr;
	    break;

	case '\\':
	case 'K':
	case 'p':
	case 'd':
	    if (sending)
		*s1++ = '\\';

	    *s1++ = cur_chr;
	    break;

	case 'T':
	    if (sending && phone_num) {
		for ( phchar = phone_num; *phchar != '\0'; phchar++) 
		    *s1++ = *phchar;
	    }
	    else {
		*s1++ = '\\';
		*s1++ = 'T';
	    }
	    break;

	case 'U':
	    if (sending && phone_num2) {
		for ( phchar = phone_num2; *phchar != '\0'; phchar++) 
		    *s1++ = *phchar;
	    }
	    else {
		*s1++ = '\\';
		*s1++ = 'U';
	    }
	    break;

	case 'q':
	    quiet = 1;
	    break;

	case 'r':
	    *s1++ = '\r';
	    break;

	case 'n':
	    *s1++ = '\n';
	    break;

	case 's':
	    *s1++ = ' ';
	    break;

	case 't':
	    *s1++ = '\t';
	    break;

	case 'N':
	    if (sending) {
		*s1++ = '\\';
		*s1++ = '\0';
	    }
	    else
		*s1++ = 'N';
	    break;
	    
	default:
	    if (isoctal (cur_chr)) {
		cur_chr &= 0x07;
		if (isoctal (*s)) {
		    cur_chr <<= 3;
		    cur_chr |= *s++ - '0';
		    if (isoctal (*s)) {
			cur_chr <<= 3;
			cur_chr |= *s++ - '0';
		    }
		}

		if (cur_chr != 0 || sending) {
		    if (sending && (cur_chr == '\\' || cur_chr == 0))
			*s1++ = '\\';
		    *s1++ = cur_chr;
		}
		break;
	    }

	    if (sending)
		*s1++ = '\\';
	    *s1++ = cur_chr;
	    break;
	}
    }

    if (add_return)
	*s1++ = '\r';

    *s1++ = '\0'; /* guarantee closure */
    *s1++ = '\0'; /* terminate the string */
    return dup_mem (temp, (size_t) (s1 - temp)); /* may have embedded nuls */
}

/*
 * A modified version of 'strtok'. This version skips \ sequences.
 */

char *
expect_strtok (char *s, const char *term)
{
    static  char *str   = blank;
    int	    escape_flag = 0;
    char   *result;

/*
 * If a string was specified then do initial processing.
 */
    if (s)
	str = s;

/*
 * If this is the escape flag then reset it and ignore the character.
 */
    if (*str)
	result = str;
    else
	result = (char *) 0;

    while (*str) {
	if (escape_flag) {
	    escape_flag = 0;
	    ++str;
	    continue;
	}

	if (*str == '\\') {
	    ++str;
	    escape_flag = 1;
	    continue;
	}

/*
 * If this is not in the termination string, continue.
 */
	if (strchr (term, *str) == (char *) 0) {
	    ++str;
	    continue;
	}

/*
 * This is the terminator. Mark the end of the string and stop.
 */
	*str++ = '\0';
	break;
    }
    return (result);
}

/*
 * Process the expect string
 */

void
chat_expect(char *s)
{
    char *expect;
    char *reply;

    if (strcmp(s, "HANGUP") == 0) {
	++hup_next;
        return;
    }
 
    if (strcmp(s, "ABORT") == 0) {
	++abort_next;
	return;
    }

    if (strcmp(s, "CLR_ABORT") == 0) {
	++clear_abort_next;
	return;
    }

    if (strcmp(s, "REPORT") == 0) {
	++report_next;
	return;
    }

    if (strcmp(s, "CLR_REPORT") == 0) {
	++clear_report_next;
	return;
    }

    if (strcmp(s, "TIMEOUT") == 0) {
	++timeout_next;
	return;
    }

    if (strcmp(s, "ECHO") == 0) {
	++echo_next;
	return;
    }

    if (strcmp(s, "SAY") == 0) {
	++say_next;
	return;
    }

/*
 * Fetch the expect and reply string.
 */
    for (;;) {
	expect = expect_strtok (s, "-");
	s      = (char *) 0;

	if (expect == (char *) 0)
	    return;

	reply = expect_strtok (s, "-");

/*
 * Handle the expect string. If successful then exit.
 */
	if (get_string (expect))
	    return;

/*
 * If there is a sub-reply string then send it. Otherwise any condition
 * is terminal.
 */
	if (reply == (char *) 0 || exit_code != 3)
	    break;

	chat_send (reply);
    }

/*
 * The expectation did not occur. This is terminal.
 */
    if (fail_reason)
	chat_logf("Failed (%s)", fail_reason);
    else
	chat_logf("Failed");
    terminate(exit_code);
}

/*
 * Translate the input character to the appropriate string for printing
 * the data.
 */

char *
character(int c)
{
    static char string[10];
    const char *meta;

    meta = (c & 0x80) ? "M-" : "";
    c &= 0x7F;

    if (c < 32)
	sprintf(string, "%s^%c", meta, (int)c + '@');
    else if (c == 127)
	sprintf(string, "%s^?", meta);
    else
	sprintf(string, "%s%c", meta, c);

    return (string);
}

/*
 *  process the reply string
 */
void
chat_send(char *s)
{
    if (say_next) {
	say_next = 0;
	s = clean(s,0);
	write(STDERR_FILENO, s, strlen(s));
        free(s);
	return;
    }

    if (hup_next) {
        hup_next = 0;
	if (strcmp(s, "OFF") == 0)
           signal(SIGHUP, SIG_IGN);
        else
           signal(SIGHUP, sighup);
        return;
    }

    if (echo_next) {
	echo_next = 0;
	echo = (strcmp(s, "ON") == 0);
	return;
    }

    if (abort_next) {
	char *s1;
	
	abort_next = 0;
	
	if (n_aborts >= MAX_ABORTS)
	    fatal(2, "Too many ABORT strings");
	
	s1 = clean(s, 0);
	
	if (strlen(s1) > strlen(s)
	    || strlen(s1) + 1 > sizeof(fail_buffer))
	    fatal(1, "Illegal or too-long ABORT string ('%v')", s);

	abort_string[n_aborts++] = s1;

	if (verbose)
	    chat_logf("abort on (%v)", s);
	return;
    }

    if (clear_abort_next) {
	char *s1;
	int   i;
        int   old_max;
	int   pack = 0;
	
	clear_abort_next = 0;
	
	s1 = clean(s, 0);
	
	if (strlen(s1) > strlen(s)
	    || strlen(s1) + 1 > sizeof(fail_buffer))
	    fatal(1, "Illegal or too-long CLR_ABORT string ('%v')", s);

        old_max = n_aborts;
	for (i=0; i < n_aborts; i++) {
	    if ( strcmp(s1,abort_string[i]) == 0 ) {
		free(abort_string[i]);
		abort_string[i] = NULL;
		pack++;
		n_aborts--;
		if (verbose)
		    chat_logf("clear abort on (%v)", s);
	    }
	}
        free(s1);
	if (pack)
	    pack_array(abort_string,old_max);
	return;
    }

    if (report_next) {
	char *s1;
	
	report_next = 0;
	if (n_reports >= MAX_REPORTS)
	    fatal(2, "Too many REPORT strings");
	
	s1 = clean(s, 0);
	
	if (strlen(s1) > strlen(s) || strlen(s1) > sizeof fail_buffer - 1)
	    fatal(1, "Illegal or too-long REPORT string ('%v')", s);
	
	report_string[n_reports++] = s1;
	
	if (verbose)
	    chat_logf("report (%v)", s);
	return;
    }

    if (clear_report_next) {
	char *s1;
	int   i;
	int   old_max;
	int   pack = 0;
	
	clear_report_next = 0;
	
	s1 = clean(s, 0);
	
	if (strlen(s1) > strlen(s) || strlen(s1) > sizeof fail_buffer - 1)
	    fatal(1, "Illegal or too-long REPORT string ('%v')", s);

	old_max = n_reports;
	for (i=0; i < n_reports; i++) {
	    if ( strcmp(s1,report_string[i]) == 0 ) {
		free(report_string[i]);
		report_string[i] = NULL;
		pack++;
		n_reports--;
		if (verbose)
		    chat_logf("clear report (%v)", s);
	    }
	}
        free(s1);
        if (pack)
	    pack_array(report_string,old_max);
	
	return;
    }

    if (timeout_next) {
	timeout_next = 0;
	timeout = atoi(s);
	
	if (timeout <= 0)
	    timeout = DEFAULT_CHAT_TIMEOUT;

	if (verbose)
	    chat_logf("timeout set to %d seconds", timeout);

	return;
    }

    if (strcmp(s, "EOT") == 0)
	s = strdup("^D\\c");
    else if (strcmp(s, "BREAK") == 0)
	s = strdup("\\K\\c");

    if (!put_string(s))
	fatal(1, "Failed");
}

int
get_char(void)
{
    int status;
    char c;

    status = read(STDIN_FILENO, &c, 1);

    switch (status) {
    case 1:
	return ((int)c & 0x7F);

    default:
	chat_logf("warning: read() on stdin returned %d", status);

    case -1:
	if ((status = fcntl(0, F_GETFL, 0)) == -1)
	    fatal(2, "Can't get file mode flags on stdin: %m");

	if (fcntl(0, F_SETFL, status & ~O_NONBLOCK) == -1)
	    fatal(2, "Can't set file mode flags on stdin: %m");
	
	return (-1);
    }
}

int put_char(int c)
{
    int status;
    char ch = c;

    usleep(10000);		/* inter-character typing delay (?) */

    status = write(STDOUT_FILENO, &ch, 1);

    switch (status) {
    case 1:
	return (0);
	
    default:
	chat_logf("warning: write() on stdout returned %d", status);
	
    case -1:
	if ((status = fcntl(0, F_GETFL, 0)) == -1)
	    fatal(2, "Can't get file mode flags on stdin, %m");

	if (fcntl(0, F_SETFL, status & ~O_NONBLOCK) == -1)
	    fatal(2, "Can't set file mode flags on stdin: %m");
	
	return (-1);
    }
}

int
write_char(int c)
{
    if (alarmed || put_char(c) < 0) {
	alarm(0);
	alarmed = 0;

	if (verbose) {
	    if (errno == EINTR || errno == EWOULDBLOCK)
		chat_logf(" -- write timed out");
	    else
		chat_logf(" -- write failed: %m");
	}
	return (0);
    }
    return (1);
}

int
put_string(char *s)
{
    quiet = 0;
    s = clean(s, 1);

    if (verbose)
        chat_logf("send (%v)", quiet ? "??????" : s);

    alarm(timeout); alarmed = 0;

    while (*s) {
	char c = *s++;

	if (c != '\\') {
	    if (!write_char (c))
		return 0;
	    continue;
	}

	c = *s++;
	switch (c) {
	case 'd':
	    sleep(1);
	    break;

	case 'K':
	    break_sequence();
	    break;

	case 'p':
	    usleep(10000); 	/* 1/100th of a second (arg is microseconds) */
	    break;

	default:
	    if (!write_char (c))
		return 0;
	    break;
	}
    }

    alarm(0);
    alarmed = 0;
    return (1);
}

/*
 *	Echo a character to stderr.
 *	When called with -1, a '\n' character is generated when
 *	the cursor is not at the beginning of a line.
 */
void
echo_stderr(int n)
{
    static int need_lf;
    char *s;

    switch (n) {
    case '\r':		/* ignore '\r' */
	break;
    case -1:
	if (need_lf == 0)
	    break;
	/* FALLTHROUGH */
    case '\n':
	write(STDERR_FILENO, "\n", 1);
	need_lf = 0;
	break;
    default:
	s = character(n);
	write(STDERR_FILENO, s, strlen(s));
	need_lf = 1;
	break;
    }
}

/*
 *	'Wait for' this string to appear on this file descriptor.
 */
int
get_string(char *string)
{
    char temp[STR_LEN];
    int c;
    size_t len, minlen;
    char *s = temp, *end = s + STR_LEN;
    char *logged = temp;

    fail_reason = (char *)0;

    if (strlen(string) > STR_LEN) {
	chat_logf("expect string is too long");
	exit_code = 1;
	return 0;
    }

    string = clean(string, 0);
    len = strlen(string);
    minlen = (len > sizeof(fail_buffer)? len: sizeof(fail_buffer)) - 1;

    if (verbose)
	chat_logf("expect (%v)", string);

    if (len == 0) {
	if (verbose)
	    chat_logf("got it");
	return (1);
    }

    alarm(timeout);
    alarmed = 0;

    while ( ! alarmed && (c = get_char()) >= 0) {
	int n, abort_len, report_len;

	if (echo)
	    echo_stderr(c);
	if (verbose && c == '\n') {
	    if (s == logged)
		chat_logf("");	/* blank line */
	    else
		chat_logf("%0.*v", s - logged, logged);
	    logged = s + 1;
	}

	*s++ = c;

	if (verbose && s >= logged + 80) {
	    chat_logf("%0.*v", s - logged, logged);
	    logged = s;
	}

	if (Verbose) {
	   if (c == '\n')
	       fputc( '\n', stderr );
	   else if (c != '\r')
	       fprintf( stderr, "%s", character(c) );
	}

	if (!report_gathering) {
	    for (n = 0; n < n_reports; ++n) {
		if ((report_string[n] != (char*) NULL) &&
		    s - temp >= (report_len = strlen(report_string[n])) &&
		    strncmp(s - report_len, report_string[n], report_len) == 0) {
		    time_t time_now   = time ((time_t*) NULL);
		    struct tm* tm_now = localtime (&time_now);

		    strftime (report_buffer, 20, "%b %d %H:%M:%S ", tm_now);
		    strcat (report_buffer, report_string[n]);

		    report_string[n] = (char *) NULL;
		    report_gathering = 1;
		    break;
		}
	    }
	}
	else {
	    if (!iscntrl (c)) {
		int rep_len = strlen (report_buffer);
		report_buffer[rep_len]     = c;
		report_buffer[rep_len + 1] = '\0';
	    }
	    else {
		report_gathering = 0;
		fprintf (report_fp, "chat:  %s\n", report_buffer);
	    }
	}

	if ((size_t)(s - temp) >= len &&
	    c == string[len - 1] &&
	    strncmp(s - len, string, len) == 0) {
	    if (verbose) {
		if (s > logged)
		    chat_logf("%0.*v", s - logged, logged);
		chat_logf(" -- got it\n");
	    }

	    alarm(0);
	    alarmed = 0;
	    return (1);
	}

	for (n = 0; n < n_aborts; ++n) {
	    if (s - temp >= (abort_len = strlen(abort_string[n])) &&
		strncmp(s - abort_len, abort_string[n], abort_len) == 0) {
		if (verbose) {
		    if (s > logged)
			chat_logf("%0.*v", s - logged, logged);
		    chat_logf(" -- failed");
		}

		alarm(0);
		alarmed = 0;
		exit_code = n + 4;
		strcpy(fail_reason = fail_buffer, abort_string[n]);
		return (0);
	    }
	}

	if (s >= end) {
	    if (logged < s - minlen) {
		chat_logf("%0.*v", s - logged, logged);
		logged = s;
	    }
	    s -= minlen;
	    memmove(temp, s, minlen);
	    logged = temp + (logged - s);
	    s = temp + minlen;
	}

	if (alarmed && verbose)
	    chat_logf("warning: alarm synchronization problem");
    }

    alarm(0);
    
    exit_code = 3;
    alarmed   = 0;
    return (0);
}

void
pack_array(char **array, int end)
{
    int i, j;

    for (i = 0; i < end; i++) {
	if (array[i] == NULL) {
	    for (j = i+1; j < end; ++j)
		if (array[j] != NULL)
		    array[i++] = array[j];
	    for (; i < end; ++i)
		array[i] = NULL;
	    break;
	}
    }
}

/*
 * vfmtmsg - format a message into a buffer.  Like vsprintf except we
 * also specify the length of the output buffer, and we handle the
 * %m (error message) format.
 * Doesn't do floating-point formats.
 * Returns the number of chars put into buf.
 */
#define OUTCHAR(c)	(buflen > 0? (--buflen, *buf++ = (c)): 0)

int
vfmtmsg(char *buf, int buflen, const char *fmt, va_list args)
{
    int c, i, n;
    int width, prec, fillch;
    int base, len, neg, quoted;
    unsigned long val = 0;
    char *str, *buf0;
    const char *f;
    unsigned char *p;
    char num[32];
    static char hexchars[] = "0123456789abcdef";

    buf0 = buf;
    --buflen;
    while (buflen > 0) {
	for (f = fmt; *f != '%' && *f != 0; ++f)
	    ;
	if (f > fmt) {
	    len = f - fmt;
	    if (len > buflen)
		len = buflen;
	    memcpy(buf, fmt, len);
	    buf += len;
	    buflen -= len;
	    fmt = f;
	}
	if (*fmt == 0)
	    break;
	c = *++fmt;
	width = prec = 0;
	fillch = ' ';
	if (c == '0') {
	    fillch = '0';
	    c = *++fmt;
	}
	if (c == '*') {
	    width = va_arg(args, int);
	    c = *++fmt;
	} else {
	    while (isdigit(c)) {
		width = width * 10 + c - '0';
		c = *++fmt;
	    }
	}
	if (c == '.') {
	    c = *++fmt;
	    if (c == '*') {
		prec = va_arg(args, int);
		c = *++fmt;
	    } else {
		while (isdigit(c)) {
		    prec = prec * 10 + c - '0';
		    c = *++fmt;
		}
	    }
	}
	str = NULL;
	base = 0;
	neg = 0;
	++fmt;
	switch (c) {
	case 'd':
	    i = va_arg(args, int);
	    if (i < 0) {
		neg = 1;
		val = -i;
	    } else
		val = i;
	    base = 10;
	    break;
	case 'o':
	    val = va_arg(args, unsigned int);
	    base = 8;
	    break;
	case 'x':
	    val = va_arg(args, unsigned int);
	    base = 16;
	    break;
	case 'p':
	    val = (unsigned long) va_arg(args, void *);
	    base = 16;
	    neg = 2;
	    break;
	case 's':
	    str = va_arg(args, char *);
	    break;
	case 'c':
	    num[0] = va_arg(args, int);
	    num[1] = 0;
	    str = num;
	    break;
	case 'm':
	    str = strerror(errno);
	    break;
	case 'v':		/* "visible" string */
	case 'q':		/* quoted string */
	    quoted = c == 'q';
	    p = va_arg(args, unsigned char *);
	    if (fillch == '0' && prec > 0) {
		n = prec;
	    } else {
		n = strlen((char *)p);
		if (prec > 0 && prec < n)
		    n = prec;
	    }
	    while (n > 0 && buflen > 0) {
		c = *p++;
		--n;
		if (!quoted && c >= 0x80) {
		    OUTCHAR('M');
		    OUTCHAR('-');
		    c -= 0x80;
		}
		if (quoted && (c == '"' || c == '\\'))
		    OUTCHAR('\\');
		if (c < 0x20 || (0x7f <= c && c < 0xa0)) {
		    if (quoted) {
			OUTCHAR('\\');
			switch (c) {
			case '\t':	OUTCHAR('t');	break;
			case '\n':	OUTCHAR('n');	break;
			case '\b':	OUTCHAR('b');	break;
			case '\f':	OUTCHAR('f');	break;
			default:
			    OUTCHAR('x');
			    OUTCHAR(hexchars[c >> 4]);
			    OUTCHAR(hexchars[c & 0xf]);
			}
		    } else {
			if (c == '\t')
			    OUTCHAR(c);
			else {
			    OUTCHAR('^');
			    OUTCHAR(c ^ 0x40);
			}
		    }
		} else
		    OUTCHAR(c);
	    }
	    continue;
	default:
	    *buf++ = '%';
	    if (c != '%')
		--fmt;		/* so %z outputs %z etc. */
	    --buflen;
	    continue;
	}
	if (base != 0) {
	    str = num + sizeof(num);
	    *--str = 0;
	    while (str > num + neg) {
		*--str = hexchars[val % base];
		val = val / base;
		if (--prec <= 0 && val == 0)
		    break;
	    }
	    switch (neg) {
	    case 1:
		*--str = '-';
		break;
	    case 2:
		*--str = 'x';
		*--str = '0';
		break;
	    }
	    len = num + sizeof(num) - 1 - str;
	} else {
	    len = strlen(str);
	    if (prec > 0 && len > prec)
		len = prec;
	}
	if (width > 0) {
	    if (width > buflen)
		width = buflen;
	    if ((n = width - len) > 0) {
		buflen -= n;
		for (; n > 0; --n)
		    *buf++ = fillch;
	    }
	}
	if (len > buflen)
	    len = buflen;
	memcpy(buf, str, len);
	buf += len;
	buflen -= len;
    }
    *buf = 0;
    return buf - buf0;
}
