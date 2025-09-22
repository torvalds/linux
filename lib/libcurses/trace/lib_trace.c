/* $OpenBSD: lib_trace.c,v 1.9 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2022,2023 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
 *	lib_trace.c - Tracing/Debugging routines
 *
 * The _tracef() function is originally from pcurses (by Pavel Curtis) in 1982.
 * pcurses allowed one to enable/disable tracing using traceon() and traceoff()
 * functions.  ncurses provides a trace() function which allows one to
 * selectively enable or disable several tracing features.
 */

#include <curses.priv.h>
#include <tic.h>

#include <ctype.h>

MODULE_ID("$Id: lib_trace.c,v 1.9 2023/10/17 09:52:09 nicm Exp $")

NCURSES_EXPORT_VAR(unsigned) _nc_tracing = 0; /* always define this */

#ifdef TRACE

#if USE_REENTRANT
NCURSES_EXPORT(const char *)
NCURSES_PUBLIC_VAR(_nc_tputs_trace) (void)
{
    return CURRENT_SCREEN ? CURRENT_SCREEN->_tputs_trace : _nc_prescreen._tputs_trace;
}
NCURSES_EXPORT(long)
NCURSES_PUBLIC_VAR(_nc_outchars) (void)
{
    return CURRENT_SCREEN ? CURRENT_SCREEN->_outchars : _nc_prescreen._outchars;
}
NCURSES_EXPORT(void)
_nc_set_tputs_trace(const char *s)
{
    if (CURRENT_SCREEN)
	CURRENT_SCREEN->_tputs_trace = s;
    else
	_nc_prescreen._tputs_trace = s;
}
NCURSES_EXPORT(void)
_nc_count_outchars(long increment)
{
    if (CURRENT_SCREEN)
	CURRENT_SCREEN->_outchars += increment;
    else
	_nc_prescreen._outchars += increment;
}
#else
NCURSES_EXPORT_VAR(const char *) _nc_tputs_trace = "";
NCURSES_EXPORT_VAR(long) _nc_outchars = 0;
#endif

#define MyFP		_nc_globals.trace_fp
#define MyFD		_nc_globals.trace_fd
#define MyInit		_nc_globals.trace_opened
#define MyPath		_nc_globals.trace_fname
#define MyLevel		_nc_globals.trace_level
#define MyNested	_nc_globals.nested_tracef
#endif /* TRACE */

#if USE_REENTRANT
#define Locked(statement) \
    do { \
	_nc_lock_global(tst_tracef); \
	statement; \
	_nc_unlock_global(tst_tracef); \
    } while (0)
#else
#define Locked(statement) statement
#endif

NCURSES_EXPORT(unsigned)
curses_trace(unsigned tracelevel)
{
    unsigned result;

#if defined(TRACE)
    int bit;

#define DATA(name) { name, #name }
    static struct {
	unsigned mask;
	const char *name;
    } trace_names[] = {
	DATA(TRACE_TIMES),
	    DATA(TRACE_TPUTS),
	    DATA(TRACE_UPDATE),
	    DATA(TRACE_MOVE),
	    DATA(TRACE_CHARPUT),
	    DATA(TRACE_CALLS),
	    DATA(TRACE_VIRTPUT),
	    DATA(TRACE_IEVENT),
	    DATA(TRACE_BITS),
	    DATA(TRACE_ICALLS),
	    DATA(TRACE_CCALLS),
	    DATA(TRACE_DATABASE),
	    DATA(TRACE_ATTRS)
    };
#undef DATA

    Locked(result = _nc_tracing);

    if ((MyFP == 0) && tracelevel) {
	MyInit = TRUE;
	if (MyFD >= 0) {
	    MyFP = fdopen(MyFD, BIN_W);
	} else {
	    if (MyPath[0] == '\0') {
		size_t size = sizeof(MyPath) - 12;
		if (getcwd(MyPath, size) == 0) {
		    perror("curses: Can't get working directory");
		    exit(EXIT_FAILURE);
		}
		MyPath[size] = '\0';
		assert(strlen(MyPath) <= size);
		_nc_STRCAT(MyPath, "/trace", sizeof(MyPath));
		if (_nc_is_dir_path(MyPath)) {
		    _nc_STRCAT(MyPath, ".log", sizeof(MyPath));
		}
	    }
#define SAFE_MODE (O_CREAT | O_EXCL | O_RDWR)
	    if (_nc_access(MyPath, W_OK) < 0
		|| (MyFD = safe_open3(MyPath, SAFE_MODE, 0600)) < 0
		|| (MyFP = fdopen(MyFD, BIN_W)) == 0) {
		;		/* EMPTY */
	    }
	}
	Locked(_nc_tracing = tracelevel);
	/* Try to set line-buffered mode, or (failing that) unbuffered,
	 * so that the trace-output gets flushed automatically at the
	 * end of each line.  This is useful in case the program dies.
	 */
	if (MyFP != 0) {
#if HAVE_SETVBUF		/* ANSI */
	    (void) setvbuf(MyFP, (char *) 0, _IOLBF, (size_t) 0);
#elif HAVE_SETBUF /* POSIX */
	    (void) setbuffer(MyFP, (char *) 0);
#endif
	}
	_tracef("TRACING NCURSES version %s.%d (tracelevel=%#x)",
		NCURSES_VERSION,
		NCURSES_VERSION_PATCH,
		tracelevel);

#define SPECIAL_MASK(mask) \
	    if ((tracelevel & mask) == mask) \
		_tracef("- %s (%u)", #mask, mask)

	for (bit = 0; bit < TRACE_SHIFT; ++bit) {
	    unsigned mask = (1U << bit) & tracelevel;
	    if ((mask & trace_names[bit].mask) != 0) {
		_tracef("- %s (%u)", trace_names[bit].name, mask);
	    }
	}
	SPECIAL_MASK(TRACE_MAXIMUM);
	else
	SPECIAL_MASK(TRACE_ORDINARY);

	if (tracelevel > TRACE_MAXIMUM) {
	    _tracef("- DEBUG_LEVEL(%u)", tracelevel >> TRACE_SHIFT);
	}
    } else if (tracelevel == 0) {
	if (MyFP != 0) {
	    MyFD = dup(MyFD);	/* allow reopen of same file */
	    fclose(MyFP);
	    MyFP = 0;
	}
	Locked(_nc_tracing = tracelevel);
    } else if (_nc_tracing != tracelevel) {
	Locked(_nc_tracing = tracelevel);
	_tracef("tracelevel=%#x", tracelevel);
    }
#else
    (void) tracelevel;
    result = 0;
#endif
    return result;
}

#if defined(TRACE)
NCURSES_EXPORT(void)
trace(const unsigned int tracelevel)
{
    curses_trace(tracelevel);
}

static void
_nc_va_tracef(const char *fmt, va_list ap)
{
    static const char Called[] = T_CALLED("");
    static const char Return[] = T_RETURN("");

    bool before = FALSE;
    bool after = FALSE;
    unsigned doit = _nc_tracing;
    int save_err = errno;
    FILE *fp = MyFP;

#ifdef TRACE
    /* verbose-trace in the command-line utilities relies on this */
    if (fp == 0 && !MyInit && _nc_tracing >= DEBUG_LEVEL(1))
	fp = stderr;
#endif

    if (strlen(fmt) >= sizeof(Called) - 1) {
	if (!strncmp(fmt, Called, sizeof(Called) - 1)) {
	    before = TRUE;
	    MyLevel++;
	} else if (!strncmp(fmt, Return, sizeof(Return) - 1)) {
	    after = TRUE;
	}
	if (before || after) {
	    if ((MyLevel <= 1)
		|| (doit & TRACE_ICALLS) != 0)
		doit &= (TRACE_CALLS | TRACE_CCALLS);
	    else
		doit = 0;
	}
    }

    if (doit != 0 && fp != 0) {
#ifdef USE_PTHREADS
	/*
	 * TRACE_ICALLS is "really" needed to show normal use with threaded
	 * applications, since anything can be running during a napms(),
	 * making it appear in the hierarchical trace as it other functions
	 * are being called.
	 *
	 * Rather than add the complication of a per-thread stack, just
	 * show the thread-id in each line of the trace.
	 */
# if USE_WEAK_SYMBOLS
	if ((pthread_self))
# endif
	    fprintf(fp, "%#" PRIxPTR ":",
#ifdef _NC_WINDOWS
		    CASTxPTR(pthread_self().p)
#else
		    CASTxPTR(pthread_self())
#endif
		);
#endif
	if (before || after) {
	    int n;
	    for (n = 1; n < MyLevel; n++)
		fputs("+ ", fp);
	}
	vfprintf(fp, fmt, ap);
	fputc('\n', fp);
	fflush(fp);
    }

    if (after && MyLevel)
	MyLevel--;

    errno = save_err;
}

NCURSES_EXPORT(void)
_tracef(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    _nc_va_tracef(fmt, ap);
    va_end(ap);
}

/* Trace 'bool' return-values */
NCURSES_EXPORT(NCURSES_BOOL)
_nc_retrace_bool(int code)
{
    T((T_RETURN("%s"), code ? "TRUE" : "FALSE"));
    return code;
}

/* Trace 'char' return-values */
NCURSES_EXPORT(char)
_nc_retrace_char(int code)
{
    T((T_RETURN("%c"), code));
    return (char) code;
}

/* Trace 'int' return-values */
NCURSES_EXPORT(int)
_nc_retrace_int(int code)
{
    T((T_RETURN("%d"), code));
    return code;
}

/* Trace 'unsigned' return-values */
NCURSES_EXPORT(unsigned)
_nc_retrace_unsigned(unsigned code)
{
    T((T_RETURN("%#x"), code));
    return code;
}

/* Trace 'char*' return-values */
NCURSES_EXPORT(char *)
_nc_retrace_ptr(char *code)
{
    T((T_RETURN("%s"), _nc_visbuf(code)));
    return code;
}

/* Trace 'const char*' return-values */
NCURSES_EXPORT(const char *)
_nc_retrace_cptr(const char *code)
{
    T((T_RETURN("%s"), _nc_visbuf(code)));
    return code;
}

/* Trace 'NCURSES_CONST void*' return-values */
NCURSES_EXPORT(NCURSES_CONST void *)
_nc_retrace_cvoid_ptr(NCURSES_CONST void *code)
{
    T((T_RETURN("%p"), code));
    return code;
}

/* Trace 'void*' return-values */
NCURSES_EXPORT(void *)
_nc_retrace_void_ptr(void *code)
{
    T((T_RETURN("%p"), code));
    return code;
}

/* Trace 'SCREEN *' return-values */
NCURSES_EXPORT(SCREEN *)
_nc_retrace_sp(SCREEN *code)
{
    T((T_RETURN("%p"), (void *) code));
    return code;
}

/* Trace 'WINDOW *' return-values */
NCURSES_EXPORT(WINDOW *)
_nc_retrace_win(WINDOW *code)
{
    T((T_RETURN("%p"), (void *) code));
    return code;
}

NCURSES_EXPORT(char *)
_nc_fmt_funcptr(char *target, const char *source, size_t size)
{
    size_t n;
    char *dst = target;
    bool leading = TRUE;

    union {
	int value;
	char bytes[sizeof(int)];
    } byteorder;

    byteorder.value = 0x1234;

    *dst++ = '0';
    *dst++ = 'x';

    for (n = 0; n < size; ++n) {
	unsigned ch = ((byteorder.bytes[0] == 0x34)
		       ? UChar(source[size - n - 1])
		       : UChar(source[n]));
	if (ch != 0 || (n + 1) >= size)
	    leading = FALSE;
	if (!leading) {
	    _nc_SPRINTF(dst, _nc_SLIMIT(TR_FUNC_LEN - (size_t) (dst - target))
			"%02x", ch & 0xff);
	    dst += 2;
	}
    }
    *dst = '\0';
    return target;
}

#if USE_REENTRANT
/*
 * Check if the given trace-mask is enabled.
 *
 * This function may be called from within one of the functions that fills
 * in parameters for _tracef(), but in that case we do not want to lock the
 * mutex, since it is already locked.
 */
NCURSES_EXPORT(int)
_nc_use_tracef(unsigned mask)
{
    bool result = FALSE;

    _nc_lock_global(tst_tracef);
    if (!MyNested++) {
	if ((result = (_nc_tracing & (mask))) != 0
	    && _nc_try_global(tracef) == 0) {
	    /* we will call _nc_locked_tracef(), no nesting so far */
	} else {
	    /* we will not call _nc_locked_tracef() */
	    MyNested = 0;
	}
    } else {
	/* we may call _nc_locked_tracef(), but with nested_tracef > 0 */
	result = (_nc_tracing & (mask));
    }
    _nc_unlock_global(tst_tracef);
    return result;
}

/*
 * We call this if _nc_use_tracef() returns true, which means we must unlock
 * the tracef mutex.
 */
NCURSES_EXPORT(void)
_nc_locked_tracef(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    _nc_va_tracef(fmt, ap);
    va_end(ap);

    if (--(MyNested) == 0) {
	_nc_unlock_global(tracef);
    }
}
#endif /* USE_REENTRANT */

#endif /* TRACE */
