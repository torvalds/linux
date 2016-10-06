#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "machine.h"
#include "api/fs/fs.h"

int arch__fix_module_text_start(u64 *start, const char *name)
{
	char path[PATH_MAX];

	snprintf(path, PATH_MAX, "module/%.*s/sections/.text",
				(int)strlen(name) - 2, name + 1);

	if (sysfs__read_ull(path, (unsigned long long *)start) < 0)
		return -1;

	return 0;
}
