/*-
 * Copyright (c) 2004 Michael Bushkov <bushman@rsu.ru>
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

#ifndef __NSCD_DEBUG_H__
#define __NSCD_DEBUG_H__

#define TRACE_WANTED 32

#ifndef NDEBUG
#define TRACE_IN(x)	nscd_trace_in(#x, __FILE__, __LINE__)
#define TRACE_POINT()	nscd_trace_point(__FILE__, __LINE__)
#define TRACE_MSG(x)	nscd_trace_msg(x, __FILE__, __LINE__)
#define TRACE_PTR(p)	nscd_trace_ptr(#p, p, __FILE__, __LINE__)
#define TRACE_INT(i)	nscd_trace_int(#i, i, __FILE__, __LINE__)
#define TRACE_STR(s)	nscd_trace_str(#s, s, __FILE__, __LINE__)
#define TRACE_OUT(x)	nscd_trace_out(#x, __FILE__, __LINE__)
#define TRACE_ON()	nscd_trace_on()
#define TRACE_OFF()	nscd_trace_off()
#else
#define TRACE_IN(x)	(void)0
#define TRACE_POINT()	(void)0
#define TRACE_MSG(x)	(void)0
#define TRACE_PTR(p)	(void)0
#define TRACE_INT(i)	(void)0
#define TRACE_STR(s)	(void)0
#define TRACE_OUT(x)	(void)0
#define TRACE_ON()	(void)0
#define TRACE_OFF()	(void)0
#endif

void nscd_trace_in(const char *, const char *, int);
void nscd_trace_point(const char *, int);
void nscd_trace_msg(const char *, const char *, int);
void nscd_trace_ptr(const char *, const void *, const char *, int);
void nscd_trace_int(const char *, int, const char *, int);
void nscd_trace_str(const char *, const char *, const char *, int);
void nscd_trace_out(const char *, const char *, int);
void nscd_trace_on(void);
void nscd_trace_off(void);

#endif
