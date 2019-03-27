/*
 * Copyright 2002 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * I din't use "namespace.h" here because none of the relevant utilities
 * are threaded, so I'm not concerned about cancellation points or other
 * niceties.
 */
#include <sys/limits.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	_PATH_UTIL_COMPAT	"/etc/compat-FreeBSD-4-util"
#define	_ENV_UTIL_COMPAT	"_COMPAT_FreeBSD_4"

int
check_utility_compat(const char *utility)
{
	char buf[PATH_MAX];
	char *p, *bp;
	int len;

	if ((p = getenv(_ENV_UTIL_COMPAT)) != NULL) {
		strlcpy(buf, p, sizeof buf);
	} else {
		if ((len = readlink(_PATH_UTIL_COMPAT, buf, sizeof(buf) - 1)) < 0)
			return 0;
		buf[len] = '\0';
	}
	if (buf[0] == '\0')
		return 1;

	bp = buf;
	while ((p = strsep(&bp, ",")) != NULL) {
		if (strcmp(p, utility) == 0)
			return 1;
	}
	return 0;
}
