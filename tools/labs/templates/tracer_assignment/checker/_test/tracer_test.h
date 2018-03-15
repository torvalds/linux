/*
 * SO2 Kprobe based tracer - test suite specific header
 *
 * Authors:
 *	Daniel Baluta <daniel.baluta@gmail.com>
 */

#ifndef TRACER_TEST_H_
#define TRACER_TEST_H_		1

#ifdef __cplusplus
extern "C" {
#endif

/* tracer test suite macros and structures */
#define MODULE_NAME		"tracer"
#define MODULE_FILENAME		MODULE_NAME ".ko"

#define HELPER_MODULE_NAME	"tracer_helper"
#define HELPER_MODULE_FILENAME	HELPER_MODULE_NAME ".ko"

#ifdef __cplusplus
}
#endif

#endif
