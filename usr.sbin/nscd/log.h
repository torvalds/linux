/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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

#ifndef __NSCD_LOG_H__
#define __NSCD_LOG_H__

#define LOG_MSG_1(sender, msg, ...) __log_msg(1, sender, msg, ##__VA_ARGS__)
#define LOG_MSG_2(sender, msg, ...) __log_msg(2, sender, msg, ##__VA_ARGS__)
#define LOG_MSG_3(sender, msg, ...) __log_msg(3, sedner, msg, ##__VA_ARGS__)

#define LOG_ERR_1(sender, err, ...) __log_err(1, sender, err, ##__VA_ARGS__)
#define LOG_ERR_2(sender, err, ...) __log_err(2, sender, err, ##__VA_ARGS__)
#define LOG_ERR_3(sender, err, ...) __log_err(3, sender, err, ##__VA_ARGS__)

void __log_msg(int, const char *, const char *, ...);
void __log_err(int, const char *, const char *, ...);

#endif
