#include <cap-ng.h>
#include <err.h>
#include <linux/capability.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/auxv.h>

#ifndef PR_CAP_AMBIENT
#define PR_CAP_AMBIENT			47
# define PR_CAP_AMBIENT_IS_SET		1
# define PR_CAP_AMBIENT_RAISE		2
# define PR_CAP_AMBIENT_LOWER		3
# define PR_CAP_AMBIENT_CLEAR_ALL	4
#endif

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 19)
# define HAVE_GETAUXVAL
#endif

static bool bool_arg(char **argv, int i)
{
	if (!strcmp(argv[i], "0"))
		return false;
	else if (!strcmp(argv[i], "1"))
		return true;
	else
		errx(1, "wrong argv[%d]", i);
}

int main(int argc, char **argv)
{
	const char *atsec = "";

	/*
	 * Be careful just in case a setgid or setcapped copy of this
	 * helper gets out.
	 */

	if (argc != 5)
		errx(1, "wrong argc");

#ifdef HAVE_GETAUXVAL
	if (getauxval(AT_SECURE))
		atsec = " (AT_SECURE is set)";
	else
		atsec = " (AT_SECURE is not set)";
#endif

	capng_get_caps_process();

	if (capng_have_capability(CAPNG_EFFECTIVE, CAP_NET_BIND_SERVICE) != bool_arg(argv, 1)) {
		printf("[FAIL]\tWrong effective state%s\n", atsec);
		return 1;
	}
	if (capng_have_capability(CAPNG_PERMITTED, CAP_NET_BIND_SERVICE) != bool_arg(argv, 2)) {
		printf("[FAIL]\tWrong permitted state%s\n", atsec);
		return 1;
	}
	if (capng_have_capability(CAPNG_INHERITABLE, CAP_NET_BIND_SERVICE) != bool_arg(argv, 3)) {
		printf("[FAIL]\tWrong inheritable state%s\n", atsec);
		return 1;
	}

	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, CAP_NET_BIND_SERVICE, 0, 0, 0) != bool_arg(argv, 4)) {
		printf("[FAIL]\tWrong ambient state%s\n", atsec);
		return 1;
	}

	printf("[OK]\tCapabilities after execve were correct\n");
	return 0;
}
