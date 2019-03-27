/*
 * Copyright (c) 2010 The FreeBSD Foundation 
 * All rights reserved. 
 * 
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation. 
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

provider plockstat {
	probe mutex__acquire(void *mutex, int rec, int spincount);
	probe mutex__release(void *mutex, int rec);
	probe mutex__block(void *mutex);
	probe mutex__spin(void *mutex);
	probe mutex__spun(void *mutex, int success, int spincount);
	probe mutex__blocked(void *mutex, int success);
	probe mutex__error(void *mutex, int err);

	probe rw__acquire(void *lock, int wr);
	probe rw__release(void *lock, int wr);
	probe rw__block(void *lock, int wr);
	probe rw__blocked(void *lock, int wr, int success);
	probe rw__error(void *lock, int wr, int err);
};
