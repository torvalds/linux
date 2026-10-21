// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal helper for Landlock selftests.  Opens its own working directory
 * before exiting, which may trigger access denials depending on the sandbox
 * configuration.
 */

#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	close(open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
	return 0;
}
