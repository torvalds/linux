/*
 * SO2 Transport Protocol - test suite specific header
 */

#ifndef STP_TEST_H_
#define STP_TEST_H_		1

#ifdef __cplusplus
extern "C" {
#endif

/* STP test suite macros and structures */
#define MODULE_NAME		"af_stp"
#define MODULE_FILENAME		MODULE_NAME ".ko"

#define SEM_NAME_RECEIVER	"/receiver_sem"
#define SEM_NAME_SENDER		"/sender_sem"

/* timeouts waiting for receiver/sender */
#define RECV_TIMEOUT			1
#define SENDRECV_TIMEOUT		3

/* messages used for "ping-pong" between sender and receiver */
#define DEFAULT_SENDER_MESSAGE		"You called down the thunder"
#define DEFAULT_RECEIVER_MESSAGE	"now reap the whirlwind"

#ifdef __cplusplus
}
#endif

#endif
