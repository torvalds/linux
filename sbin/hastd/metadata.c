/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <ebuf.h>
#include <nv.h>
#include <pjdlog.h>
#include <subr.h>

#include "metadata.h"

int
metadata_read(struct hast_resource *res, bool openrw)
{
	unsigned char *buf;
	struct ebuf *eb;
	struct nv *nv;
	ssize_t done;
	const char *str;
	int rerrno;
	bool opened_here;

	opened_here = false;
	rerrno = 0;

	/*
	 * Is this first metadata_read() call for this resource?
	 */
	if (res->hr_localfd == -1) {
		if (provinfo(res, openrw) == -1) {
			rerrno = errno;
			goto fail;
		}
		opened_here = true;
		pjdlog_debug(1, "Obtained info about %s.", res->hr_localpath);
		if (openrw) {
			if (flock(res->hr_localfd, LOCK_EX | LOCK_NB) == -1) {
				rerrno = errno;
				if (errno == EOPNOTSUPP) {
					pjdlog_warning("Unable to lock %s (operation not supported), but continuing.",
					    res->hr_localpath);
				} else {
					pjdlog_errno(LOG_ERR,
					    "Unable to lock %s",
					    res->hr_localpath);
					goto fail;
				}
			}
			pjdlog_debug(1, "Locked %s.", res->hr_localpath);
		}
	}

	eb = ebuf_alloc(METADATA_SIZE);
	if (eb == NULL) {
		rerrno = errno;
		pjdlog_errno(LOG_ERR,
		    "Unable to allocate memory to read metadata");
		goto fail;
	}
	if (ebuf_add_tail(eb, NULL, METADATA_SIZE) == -1) {
		rerrno = errno;
		pjdlog_errno(LOG_ERR,
		    "Unable to allocate memory to read metadata");
		ebuf_free(eb);
		goto fail;
	}
	buf = ebuf_data(eb, NULL);
	PJDLOG_ASSERT(buf != NULL);
	done = pread(res->hr_localfd, buf, METADATA_SIZE, 0);
	if (done == -1 || done != METADATA_SIZE) {
		rerrno = errno;
		pjdlog_errno(LOG_ERR, "Unable to read metadata");
		ebuf_free(eb);
		goto fail;
	}
	nv = nv_ntoh(eb);
	if (nv == NULL) {
		rerrno = errno;
		pjdlog_errno(LOG_ERR, "Metadata read from %s is invalid",
		    res->hr_localpath);
		ebuf_free(eb);
		goto fail;
	}

	str = nv_get_string(nv, "resource");
	if (str != NULL && strcmp(str, res->hr_name) != 0) {
		pjdlog_error("Provider %s is not part of resource %s.",
		    res->hr_localpath, res->hr_name);
		nv_free(nv);
		goto fail;
	}

	res->hr_datasize = nv_get_uint64(nv, "datasize");
	res->hr_extentsize = (int)nv_get_uint32(nv, "extentsize");
	res->hr_keepdirty = (int)nv_get_uint32(nv, "keepdirty");
	res->hr_localoff = nv_get_uint64(nv, "offset");
	res->hr_resuid = nv_get_uint64(nv, "resuid");
	if (res->hr_role != HAST_ROLE_PRIMARY) {
		/* Secondary or init role. */
		res->hr_secondary_localcnt = nv_get_uint64(nv, "localcnt");
		res->hr_secondary_remotecnt = nv_get_uint64(nv, "remotecnt");
	}
	if (res->hr_role != HAST_ROLE_SECONDARY) {
		/* Primary or init role. */
		res->hr_primary_localcnt = nv_get_uint64(nv, "localcnt");
		res->hr_primary_remotecnt = nv_get_uint64(nv, "remotecnt");
	}
	str = nv_get_string(nv, "prevrole");
	if (str != NULL) {
		if (strcmp(str, "primary") == 0)
			res->hr_previous_role = HAST_ROLE_PRIMARY;
		else if (strcmp(str, "secondary") == 0)
			res->hr_previous_role = HAST_ROLE_SECONDARY;
	}

	if (nv_error(nv) != 0) {
		errno = rerrno = nv_error(nv);
		pjdlog_errno(LOG_ERR, "Unable to read metadata from %s",
		    res->hr_localpath);
		nv_free(nv);
		goto fail;
	}
	nv_free(nv);
	return (0);
fail:
	if (opened_here) {
		close(res->hr_localfd);
		res->hr_localfd = -1;
	}
	errno = rerrno;
	return (-1);
}

int
metadata_write(struct hast_resource *res)
{
	struct ebuf *eb;
	struct nv *nv;
	unsigned char *buf, *ptr;
	size_t size;
	ssize_t done;
	int ret;

	buf = calloc(1, METADATA_SIZE);
	if (buf == NULL) {
		pjdlog_error("Unable to allocate %zu bytes for metadata.",
		    (size_t)METADATA_SIZE);
		return (-1);
	}

	ret = -1;

	nv = nv_alloc();
	nv_add_string(nv, res->hr_name, "resource");
	nv_add_uint64(nv, (uint64_t)res->hr_datasize, "datasize");
	nv_add_uint32(nv, (uint32_t)res->hr_extentsize, "extentsize");
	nv_add_uint32(nv, (uint32_t)res->hr_keepdirty, "keepdirty");
	nv_add_uint64(nv, (uint64_t)res->hr_localoff, "offset");
	nv_add_uint64(nv, res->hr_resuid, "resuid");
	if (res->hr_role == HAST_ROLE_PRIMARY ||
	    res->hr_role == HAST_ROLE_INIT) {
		nv_add_uint64(nv, res->hr_primary_localcnt, "localcnt");
		nv_add_uint64(nv, res->hr_primary_remotecnt, "remotecnt");
	} else /* if (res->hr_role == HAST_ROLE_SECONDARY) */ {
		PJDLOG_ASSERT(res->hr_role == HAST_ROLE_SECONDARY);
		nv_add_uint64(nv, res->hr_secondary_localcnt, "localcnt");
		nv_add_uint64(nv, res->hr_secondary_remotecnt, "remotecnt");
	}
	nv_add_string(nv, role2str(res->hr_role), "prevrole");
	if (nv_error(nv) != 0) {
		pjdlog_error("Unable to create metadata.");
		goto end;
	}
	res->hr_previous_role = res->hr_role;
	eb = nv_hton(nv);
	PJDLOG_ASSERT(eb != NULL);
	ptr = ebuf_data(eb, &size);
	PJDLOG_ASSERT(ptr != NULL);
	PJDLOG_ASSERT(size < METADATA_SIZE);
	bcopy(ptr, buf, size);
	done = pwrite(res->hr_localfd, buf, METADATA_SIZE, 0);
	if (done == -1 || done != METADATA_SIZE) {
		pjdlog_errno(LOG_ERR, "Unable to write metadata");
		goto end;
	}
	ret = 0;
end:
	free(buf);
	nv_free(nv);
	return (ret);
}
