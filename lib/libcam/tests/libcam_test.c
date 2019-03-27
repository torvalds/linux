/*-
 * Copyright (c) 2017 Ngie Cooper <ngie@freebsd.org>
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
 */

/* Tests functions in lib/libcam/camlib.c */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <camlib.h>

#include <atf-c.h>

static const char *
get_cam_test_device(const atf_tc_t *tc)
{
	const char *cam_test_device;

	cam_test_device = atf_tc_get_config_var(tc, "cam_test_device");

	return (cam_test_device);
}

static void
cam_clear_error(void)
{

	strcpy(cam_errbuf, "");
}

static bool
cam_has_error(void)
{

	return (strlen(cam_errbuf) != 0);
}

ATF_TC_WITHOUT_HEAD(cam_get_device_negative_test_NULL_path);
ATF_TC_BODY(cam_get_device_negative_test_NULL_path, tc)
{
	char parsed_dev_name[DEV_IDLEN + 1];
	int parsed_unit;

	ATF_REQUIRE_MSG(cam_get_device(NULL, parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == -1,
	    "cam_get_device succeeded unexpectedly");
}

ATF_TC_WITHOUT_HEAD(cam_get_device_negative_test_bad_path);
ATF_TC_BODY(cam_get_device_negative_test_bad_path, tc)
{
	char parsed_dev_name[DEV_IDLEN + 1];
	int parsed_unit;

	ATF_REQUIRE_MSG(cam_get_device("1ada", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == -1,
	    "cam_get_device succeeded unexpectedly");
}

ATF_TC_WITHOUT_HEAD(cam_get_device_negative_test_nul_path);
ATF_TC_BODY(cam_get_device_negative_test_nul_path, tc)
{
	char parsed_dev_name[DEV_IDLEN + 1];
	int parsed_unit;

	ATF_REQUIRE_MSG(cam_get_device("", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == -1,
	    "cam_get_device succeeded unexpectedly");
}

ATF_TC_WITHOUT_HEAD(cam_get_device_negative_test_root);
ATF_TC_BODY(cam_get_device_negative_test_root, tc)
{
	char parsed_dev_name[DEV_IDLEN + 1];
	int parsed_unit;

	ATF_REQUIRE_MSG(cam_get_device("/", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == -1,
	    "cam_get_device succeeded unexpectedly");
}

ATF_TC_WITHOUT_HEAD(cam_get_device_positive_test);
ATF_TC_BODY(cam_get_device_positive_test, tc)
{
	char expected_dev_name[] = "foo";
	char parsed_dev_name[DEV_IDLEN + 1];
	int expected_unit, parsed_unit;

	expected_unit = 1;

	ATF_REQUIRE_MSG(cam_get_device("/dev/foo1", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == 0,
	    "cam_get_device failed");
	ATF_REQUIRE_STREQ(parsed_dev_name, expected_dev_name);
	ATF_REQUIRE(parsed_unit == expected_unit);

	strcpy(parsed_dev_name, "");
	parsed_unit = -1;

	ATF_REQUIRE_MSG(cam_get_device("foo1", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == 0,
	    "cam_get_device failed");
	ATF_REQUIRE_STREQ(parsed_dev_name, expected_dev_name);
	ATF_REQUIRE(parsed_unit == expected_unit);
}

/* 
 * sa(4) uniquely creates nsa and esa device nodes for non-rewind operations
 * and eject-on-close operations.  cam_get_device must special case these nodes
 * to always return the base device.
 */
ATF_TC_WITHOUT_HEAD(cam_get_device_sa_test);
ATF_TC_BODY(cam_get_device_sa_test, tc)
{
	char parsed_dev_name[DEV_IDLEN + 1];
	int parsed_unit;

	ATF_REQUIRE_MSG(cam_get_device("nsa99", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == 0,
	    "cam_get_device failed");
	ATF_REQUIRE_STREQ(parsed_dev_name, "sa");
	ATF_REQUIRE(parsed_unit == 99);

	strcpy(parsed_dev_name, "");
	parsed_unit = -1;

	ATF_REQUIRE_MSG(cam_get_device("esa99", parsed_dev_name,
	    nitems(parsed_dev_name), &parsed_unit) == 0,
	    "cam_get_device failed");
	ATF_REQUIRE_STREQ(parsed_dev_name, "sa");
	ATF_REQUIRE(parsed_unit == 99);
}

ATF_TC(cam_open_device_negative_test_O_RDONLY);
ATF_TC_HEAD(cam_open_device_negative_test_O_RDONLY, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test that cam_open_device(`cam_device`, O_RDONLY) fails to open "
	    "the underlying pass(4) device (bug 217649)");
	atf_tc_set_md_var(tc, "require.config", "cam_test_device");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_open_device_negative_test_O_RDONLY, tc)
{
	const char *cam_test_device;

	cam_test_device = get_cam_test_device(tc);

	cam_clear_error();
	ATF_CHECK(cam_open_device(cam_test_device, O_RDONLY) == NULL);
	ATF_REQUIRE(cam_has_error());
}

ATF_TC(cam_open_device_negative_test_nonexistent);
ATF_TC_HEAD(cam_open_device_negative_test_nonexistent, tc)
{

	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_open_device_negative_test_nonexistent, tc)
{

	cam_clear_error();
	ATF_REQUIRE(cam_open_device("/nonexistent", O_RDWR) == NULL);
	ATF_REQUIRE(cam_has_error());
}

ATF_TC(cam_open_device_negative_test_unprivileged);
ATF_TC_HEAD(cam_open_device_negative_test_unprivileged, tc)
{

	atf_tc_set_md_var(tc, "require.config", "cam_test_device");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(cam_open_device_negative_test_unprivileged, tc)
{
	const char *cam_test_device;

	cam_test_device = get_cam_test_device(tc);

	cam_clear_error();
	ATF_CHECK(cam_open_device(cam_test_device, O_RDONLY) == NULL);
	ATF_REQUIRE(cam_has_error());

	cam_clear_error();
	ATF_CHECK(cam_open_device(cam_test_device, O_RDWR) == NULL);
	ATF_REQUIRE(cam_has_error());
}

ATF_TC(cam_open_device_positive_test);
ATF_TC_HEAD(cam_open_device_positive_test, tc)
{

	atf_tc_set_md_var(tc, "require.config", "cam_test_device");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_open_device_positive_test, tc)
{
	struct cam_device *cam_dev;
	const char *cam_test_device;

	cam_test_device = get_cam_test_device(tc);

	cam_clear_error();
	cam_dev = cam_open_device(cam_test_device, O_RDWR);
	ATF_CHECK_MSG(cam_dev != NULL, "cam_open_device failed: %s",
	    cam_errbuf);
	ATF_REQUIRE(!cam_has_error());
	cam_close_device(cam_dev);
}

ATF_TC(cam_close_device_negative_test_NULL);
ATF_TC_HEAD(cam_close_device_negative_test_NULL, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test that cam_close_device(NULL) succeeds without error");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_close_device_negative_test_NULL, tc)
{

	cam_clear_error();
	cam_close_device(NULL);
	ATF_REQUIRE(!cam_has_error());
}

ATF_TC(cam_getccb_positive_test);
ATF_TC_HEAD(cam_getccb_positive_test, tc)
{

	atf_tc_set_md_var(tc, "require.config", "cam_test_device");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_getccb_positive_test, tc)
{
	union ccb *cam_ccb;
	struct cam_device *cam_dev;
	const char *cam_test_device;

	cam_test_device = get_cam_test_device(tc);

	cam_clear_error();
	cam_dev = cam_open_device(cam_test_device, O_RDWR);
	ATF_CHECK_MSG(cam_dev != NULL, "cam_open_device failed: %s",
	    cam_errbuf);
	ATF_REQUIRE(!cam_has_error());
	cam_ccb = cam_getccb(cam_dev);
	ATF_CHECK_MSG(cam_ccb != NULL, "get_camccb failed: %s", cam_errbuf);
	ATF_REQUIRE(!cam_has_error());
	cam_freeccb(cam_ccb);
	cam_close_device(cam_dev);
}

ATF_TC(cam_freeccb_negative_test_NULL);
ATF_TC_HEAD(cam_freeccb_negative_test_NULL, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test that cam_freeccb(NULL) succeeds without error");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(cam_freeccb_negative_test_NULL, tc)
{

	cam_clear_error();
	cam_freeccb(NULL);
	ATF_REQUIRE(!cam_has_error());
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cam_get_device_negative_test_NULL_path);
	ATF_TP_ADD_TC(tp, cam_get_device_negative_test_bad_path);
	ATF_TP_ADD_TC(tp, cam_get_device_negative_test_nul_path);
	ATF_TP_ADD_TC(tp, cam_get_device_negative_test_root);
	ATF_TP_ADD_TC(tp, cam_get_device_positive_test);
	ATF_TP_ADD_TC(tp, cam_get_device_sa_test);
	ATF_TP_ADD_TC(tp, cam_open_device_negative_test_O_RDONLY);
	ATF_TP_ADD_TC(tp, cam_open_device_negative_test_nonexistent);
	ATF_TP_ADD_TC(tp, cam_open_device_negative_test_unprivileged);
	ATF_TP_ADD_TC(tp, cam_open_device_positive_test);
	ATF_TP_ADD_TC(tp, cam_close_device_negative_test_NULL);
	ATF_TP_ADD_TC(tp, cam_getccb_positive_test);
	ATF_TP_ADD_TC(tp, cam_freeccb_negative_test_NULL);

	return (atf_no_error());
}
