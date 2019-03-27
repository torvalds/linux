/*-
 * Copyright (c) 2014 Pietro Cerutti <gahr@FreeBSD.org>
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/capsicum.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <utmpx.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
using namespace std;

int
main(int argc, char **)
{
	struct utmpx *ut;
	set<string> names;

	if (argc > 1) {
		cerr << "usage: users" << endl;
		return (1);
	}

	setutxent();

	if (caph_enter())
		err(1, "Failed to enter capability mode.");

	while ((ut = getutxent()) != NULL)
		if (ut->ut_type == USER_PROCESS)
			names.insert(ut->ut_user);
	endutxent();

	if (!names.empty()) {
		set<string>::iterator last = names.end();
		--last;
		copy(names.begin(), last, ostream_iterator<string>(cout, " "));
		cout << *last << endl;
	}
}
