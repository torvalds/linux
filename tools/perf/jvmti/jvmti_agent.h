/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __JVMTI_AGENT_H__
#define __JVMTI_AGENT_H__

#include <sys/types.h>
#include <stdint.h>
#include <jvmti.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	unsigned long	pc;
	int		line_number;
	int		discrim; /* discriminator -- 0 for now */
	jmethodID	methodID;
} jvmti_line_info_t;

void *jvmti_open(void);
int   jvmti_close(void *agent);
int   jvmti_write_code(void *agent, char const *symbol_name,
		       uint64_t vma, void const *code,
		       const unsigned int code_size);

int   jvmti_write_debug_info(void *agent, uint64_t code, int nr_lines,
			     jvmti_line_info_t *li,
			     const char * const * file_names);

#if defined(__cplusplus)
}

#endif
#endif /* __JVMTI_H__ */
