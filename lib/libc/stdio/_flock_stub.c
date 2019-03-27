/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * POSIX stdio FILE locking functions. These assume that the locking
 * is only required at FILE structure level, not at file descriptor
 * level too.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "un-namespace.h"

#include "local.h"


/*
 * Weak symbols for externally visible functions in this file:
 */
__weak_reference(_flockfile, flockfile);
__weak_reference(_flockfile_debug_stub, _flockfile_debug);
__weak_reference(_ftrylockfile, ftrylockfile);
__weak_reference(_funlockfile, funlockfile);

void _flockfile_debug_stub(FILE *, char *, int);
int _ftrylockfile(FILE *);

void
_flockfile(FILE *fp)
{
	pthread_t curthread = _pthread_self();

	if (fp->_fl_owner == curthread)
		fp->_fl_count++;
	else {
		/*
		 * Make sure this mutex is treated as a private
		 * internal mutex:
		 */
		_pthread_mutex_lock(&fp->_fl_mutex);
		fp->_fl_owner = curthread;
		fp->_fl_count = 1;
	}
}

/*
 * This can be overriden by the threads library if it is linked in.
 */
void
_flockfile_debug_stub(FILE *fp, char *fname, int lineno)
{
	_flockfile(fp);
}

int
_ftrylockfile(FILE *fp)
{
	pthread_t curthread = _pthread_self();
	int	ret = 0;

	if (fp->_fl_owner == curthread)
		fp->_fl_count++;
	/*
	 * Make sure this mutex is treated as a private
	 * internal mutex:
	 */
	else if (_pthread_mutex_trylock(&fp->_fl_mutex) == 0) {
		fp->_fl_owner = curthread;
		fp->_fl_count = 1;
	}
	else
		ret = -1;
	return (ret);
}

void 
_funlockfile(FILE *fp)
{
	pthread_t	curthread = _pthread_self();

	/*
	 * Check if this file is owned by the current thread:
	 */
	if (fp->_fl_owner == curthread) {
		/*
		 * Check if this thread has locked the FILE
		 * more than once:
		 */
		if (fp->_fl_count > 1)
			/*
			 * Decrement the count of the number of
			 * times the running thread has locked this
			 * file:
			 */
			fp->_fl_count--;
		else {
			/*
			 * The running thread will release the
			 * lock now:
			 */
			fp->_fl_count = 0;
			fp->_fl_owner = NULL;
			_pthread_mutex_unlock(&fp->_fl_mutex);
		}
	}
}
