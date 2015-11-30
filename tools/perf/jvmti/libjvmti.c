#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>
#include <jvmti.h>
#include <limits.h>

#include "jvmti_agent.h"

static int has_line_numbers;
void *jvmti_agent;

static void JNICALL
compiled_method_load_cb(jvmtiEnv *jvmti,
			jmethodID method,
			jint code_size,
			void const *code_addr,
			jint map_length,
			jvmtiAddrLocationMap const *map,
			void const *compile_info __unused)
{
	jvmtiLineNumberEntry *tab = NULL;
	jclass decl_class;
	char *class_sign = NULL;
	char *func_name = NULL;
	char *func_sign = NULL;
	char *file_name= NULL;
	char fn[PATH_MAX];
	uint64_t addr = (uint64_t)(uintptr_t)code_addr;
	jvmtiError ret;
	jint nr_lines = 0;
	size_t len;

	ret = (*jvmti)->GetMethodDeclaringClass(jvmti, method,
						&decl_class);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: cannot get declaring class");
		return;
	}

	if (has_line_numbers && map && map_length) {

		ret = (*jvmti)->GetLineNumberTable(jvmti, method, &nr_lines, &tab);
		if (ret != JVMTI_ERROR_NONE) {
			warnx("jvmti: cannot get line table for method");
		} else {
			ret = (*jvmti)->GetSourceFileName(jvmti, decl_class, &file_name);
			if (ret != JVMTI_ERROR_NONE) {
				warnx("jvmti: cannot get source filename ret=%d", ret);
				nr_lines = 0;
			}
		}
	}

	ret = (*jvmti)->GetClassSignature(jvmti, decl_class,
					  &class_sign, NULL);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: getclassignature failed");
		goto error;
	}

	ret = (*jvmti)->GetMethodName(jvmti, method, &func_name,
				      &func_sign, NULL);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: failed getmethodname");
		goto error;
	}

	/*
	 * Assume path name is class hierarchy, this is a common practice with Java programs
	 */
	if (*class_sign == 'L') {
		int j, i = 0;
		char *p = strrchr(class_sign, '/');
		if (p) {
			/* drop the 'L' prefix and copy up to the final '/' */
			for (i = 0; i < (p - class_sign); i++)
				fn[i] = class_sign[i+1];
		}
		/*
		 * append file name, we use loops and not string ops to avoid modifying
		 * class_sign which is used later for the symbol name
		 */
		for (j = 0; i < (PATH_MAX - 1) && file_name && j < strlen(file_name); j++, i++)
			fn[i] = file_name[j];
		fn[i] = '\0';
	} else {
		/* fallback case */
		strcpy(fn, file_name);
	}
	/*
	 * write source line info record if we have it
	 */
	if (jvmti_write_debug_info(jvmti_agent, addr, fn, map, tab, nr_lines))
		warnx("jvmti: write_debug_info() failed");

	len = strlen(func_name) + strlen(class_sign) + strlen(func_sign) + 2;
	{
		char str[len];
		snprintf(str, len, "%s%s%s", class_sign, func_name, func_sign);
		if (jvmti_write_code(jvmti_agent, str, addr, code_addr, code_size))
			warnx("jvmti: write_code() failed");
	}
error:
	(*jvmti)->Deallocate(jvmti, (unsigned char *)func_name);
	(*jvmti)->Deallocate(jvmti, (unsigned char *)func_sign);
	(*jvmti)->Deallocate(jvmti, (unsigned char *)class_sign);
	(*jvmti)->Deallocate(jvmti, (unsigned char *)tab);
	(*jvmti)->Deallocate(jvmti, (unsigned char *)file_name);
}

static void JNICALL
code_generated_cb(jvmtiEnv *jvmti,
		  char const *name,
		  void const *code_addr,
		  jint code_size)
{
	uint64_t addr = (uint64_t)(unsigned long)code_addr;
	int ret;

	ret = jvmti_write_code(jvmti_agent, name, addr, code_addr, code_size);
	if (ret)
		warnx("jvmti: write_code() failed for code_generated");
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *jvm, char *options, void *reserved __unused)
{
	jvmtiEventCallbacks cb;
	jvmtiCapabilities caps1;
	jvmtiJlocationFormat format;
	jvmtiEnv *jvmti = NULL;
	jint ret;

	jvmti_agent = jvmti_open();
	if (!jvmti_agent) {
		warnx("jvmti: open_agent failed");
		return -1;
	}

	/*
	 * Request a JVMTI interface version 1 environment
	 */
	ret = (*jvm)->GetEnv(jvm, (void *)&jvmti, JVMTI_VERSION_1);
	if (ret != JNI_OK) {
		warnx("jvmti: jvmti version 1 not supported");
		return -1;
	}

	/*
	 * acquire method_load capability, we require it
	 * request line numbers (optional)
	 */
	memset(&caps1, 0, sizeof(caps1));
	caps1.can_generate_compiled_method_load_events = 1;

	ret = (*jvmti)->AddCapabilities(jvmti, &caps1);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: acquire compiled_method capability failed");
		return -1;
	}
	ret = (*jvmti)->GetJLocationFormat(jvmti, &format);
        if (ret == JVMTI_ERROR_NONE && format == JVMTI_JLOCATION_JVMBCI) {
                memset(&caps1, 0, sizeof(caps1));
                caps1.can_get_line_numbers = 1;
                caps1.can_get_source_file_name = 1;
		ret = (*jvmti)->AddCapabilities(jvmti, &caps1);
                if (ret == JVMTI_ERROR_NONE)
                        has_line_numbers = 1;
        }

	memset(&cb, 0, sizeof(cb));

	cb.CompiledMethodLoad   = compiled_method_load_cb;
	cb.DynamicCodeGenerated = code_generated_cb;

	ret = (*jvmti)->SetEventCallbacks(jvmti, &cb, sizeof(cb));
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: cannot set event callbacks");
		return -1;
	}

	ret = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
			JVMTI_EVENT_COMPILED_METHOD_LOAD, NULL);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: setnotification failed for method_load");
		return -1;
	}

	ret = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
			JVMTI_EVENT_DYNAMIC_CODE_GENERATED, NULL);
	if (ret != JVMTI_ERROR_NONE) {
		warnx("jvmti: setnotification failed on code_generated");
		return -1;
	}
	return 0;
}

JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *jvm __unused)
{
	int ret;

	ret = jvmti_close(jvmti_agent);
	if (ret)
		errx(1, "Error: op_close_agent()");
}
