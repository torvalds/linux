#ifndef __JVMTI_AGENT_H__
#define __JVMTI_AGENT_H__

#include <sys/types.h>
#include <stdint.h>
#include <jvmti.h>

#define __unused __attribute__((unused))

#if defined(__cplusplus)
extern "C" {
#endif

void *jvmti_open(void);
int   jvmti_close(void *agent);
int   jvmti_write_code(void *agent, char const *symbol_name,
		       uint64_t vma, void const *code,
		       const unsigned int code_size);
int   jvmti_write_debug_info(void *agent,
		             uint64_t code,
			     const char *file,
			     jvmtiAddrLocationMap const *map,
			     jvmtiLineNumberEntry *tab, jint nr);

#if defined(__cplusplus)
}

#endif
#endif /* __JVMTI_H__ */
