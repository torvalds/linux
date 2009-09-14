#ifndef QUOTE_H
#define QUOTE_H

#include <stddef.h>
#include <stdio.h>

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', any exclamation point
 * is replaced with '\!', and the whole thing is enclosed in a
 * single quote pair.
 *
 * For example, if you are passing the result to system() as an
 * argument:
 *
 * sprintf(cmd, "foobar %s %s", sq_quote(arg0), sq_quote(arg1))
 *
 * would be appropriate.  If the system() is going to call ssh to
 * run the command on the other side:
 *
 * sprintf(cmd, "git-diff-tree %s %s", sq_quote(arg0), sq_quote(arg1));
 * sprintf(rcmd, "ssh %s %s", sq_util/quote.host), sq_quote(cmd));
 *
 * Note that the above examples leak memory!  Remember to free result from
 * sq_quote() in a real application.
 *
 * sq_quote_buf() writes to an existing buffer of specified size; it
 * will return the number of characters that would have been written
 * excluding the final null regardless of the buffer size.
 */

extern void sq_quote_print(FILE *stream, const char *src);

extern void sq_quote_buf(struct strbuf *, const char *src);
extern void sq_quote_argv(struct strbuf *, const char **argv, size_t maxlen);

/* This unwraps what sq_quote() produces in place, but returns
 * NULL if the input does not look like what sq_quote would have
 * produced.
 */
extern char *sq_dequote(char *);

/*
 * Same as the above, but can be used to unwrap many arguments in the
 * same string separated by space. "next" is changed to point to the
 * next argument that should be passed as first parameter. When there
 * is no more argument to be dequoted, "next" is updated to point to NULL.
 */
extern char *sq_dequote_step(char *arg, char **next);
extern int sq_dequote_to_argv(char *arg, const char ***argv, int *nr, int *alloc);

extern int unquote_c_style(struct strbuf *, const char *quoted, const char **endp);
extern size_t quote_c_style(const char *name, struct strbuf *, FILE *, int no_dq);
extern void quote_two_c_style(struct strbuf *, const char *, const char *, int);

extern void write_name_quoted(const char *name, FILE *, int terminator);
extern void write_name_quotedpfx(const char *pfx, ssize_t pfxlen,
                                 const char *name, FILE *, int terminator);

/* quote path as relative to the given prefix */
char *quote_path_relative(const char *in, int len,
			  struct strbuf *out, const char *prefix);

/* quoting as a string literal for other languages */
extern void perl_quote_print(FILE *stream, const char *src);
extern void python_quote_print(FILE *stream, const char *src);
extern void tcl_quote_print(FILE *stream, const char *src);

#endif
