/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <libsecureboot.h>

#include <vse.h>

/**
 * @brief
 * verify signed file
 *
 * We look for a signature using the extensions
 * recorded in signature_exts.
 * If we find a match we pass it to a suitable verify method.
 *
 * @return content of verified file or NULL on error.
 */
unsigned char *
verify_signed(const char *filename, int flags)
{
	struct stat st;
	char buf[MAXPATHLEN];
	const char **se;

	for (se = signature_exts; *se; se++) {
		snprintf(buf, sizeof(buf), "%s.%s", filename, *se);
		if (stat(buf, &st) < 0 || !S_ISREG(st.st_mode))
			continue;
		DEBUG_PRINTF(5, ("verify_signed: %s\n", buf));
#ifdef VE_OPENPGP_SUPPORT
		if (strncmp(*se, "asc", 3) == 0)
			return (verify_asc(buf, flags));
#endif
		return (verify_sig(buf, flags));
	}
	return (NULL);
}
