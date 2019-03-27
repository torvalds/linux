/*-
 * Copyright (c) 2017 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _TSLOG_H_
#define	_TSLOG_H_

#ifdef TSLOG
#include <sys/_types.h>
#include <sys/pcpu.h>
#endif

#define TS_ENTER	0
#define TS_EXIT		1
#define TS_THREAD	2
#define TS_EVENT	3

#define TSENTER() TSRAW(curthread, TS_ENTER, __func__, NULL)
#define TSENTER2(x) TSRAW(curthread, TS_ENTER, __func__, x)
#define TSEXIT() TSRAW(curthread, TS_EXIT, __func__, NULL)
#define TSEXIT2(x) TSRAW(curthread, TS_EXIT, __func__, x)
#define TSTHREAD(td, x) TSRAW(td, TS_THREAD, x, NULL)
#define TSEVENT(x) TSRAW(curthread, TS_EVENT, x, NULL)
#define TSEVENT2(x, y) TSRAW(curthread, TS_EVENT, x, y)
#define TSLINE() TSEVENT2(__FILE__, __XSTRING(__LINE__))
#define TSWAIT(x) TSEVENT2("WAIT", x);
#define TSUNWAIT(x) TSEVENT2("UNWAIT", x);
#define TSHOLD(x) TSEVENT2("HOLD", x);
#define TSRELEASE(x) TSEVENT2("RELEASE", x);

#ifdef TSLOG
#define TSRAW(a, b, c, d) tslog(a, b, c, d)
void tslog(void *, int, const char *, const char *);
#else
#define TSRAW(a, b, c, d)		/* Timestamp logging disabled */
#endif

#endif /* _TSLOG_H_ */
