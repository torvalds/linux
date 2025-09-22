/*	$OpenBSD: fpu.c,v 1.3 2021/06/17 12:55:38 kettenis Exp $	*/

#include <err.h>
#include <fenv.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
	fexcept_t flag;
	int rv;

	/* Set up the FPU control word register. */
	rv = fesetround(FE_UPWARD);
	if (rv != 0)
		errx(2, "fesetround FE_UPWARD returned %d", rv);
	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_DIVBYZERO);

	/* Set the FPU exception flags. */
	flag = FE_OVERFLOW;
	rv = fesetexceptflag(&flag, FE_ALL_EXCEPT);
	if (rv != 0)
		errx(2, "fesetexceptflag returned %d", rv);

	/* Schedule another process, to check if kernel preserves state. */
	rv = system("true");
	if (rv == -1)
		err(2, "system");
	if (rv != 0)
		errx(2, "true: %d", rv);

	/* Verify that the FPU control word is preserved. */
	rv = fegetround();
	if (rv != FE_UPWARD)
		errx(1, "fegetround returned %d, not FE_UPWARD", rv);
#if !defined(__arm__) && !defined(__aarch64__) && !defined(__riscv)
	rv = fegetexcept();
	if (rv != FE_DIVBYZERO)
		errx(1, "fegetexcept returned %d, not FE_DIVBYZERO",
		    rv);
#endif

	/* Verify that the FPU exception flags weren't clobbered. */
	flag = 0;
	rv = fegetexceptflag(&flag, FE_ALL_EXCEPT);
	if (rv != 0)
		errx(2, "fegetexceptflag returned %d", rv);
	if (flag != FE_OVERFLOW)
		errx(1, "except flag is %d, no FE_OVERFLOW", rv);

	return (0);
}
