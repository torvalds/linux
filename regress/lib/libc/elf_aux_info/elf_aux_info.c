#include <sys/auxv.h>

#include <errno.h>
#include <stdio.h>

int
main(void)
{
	int ret = 0;
	int a;
	unsigned long b;

	/* Should always succeed */
	if (elf_aux_info(AT_PAGESZ, &a, sizeof(a)))
		ret |= 1;
	else
		fprintf(stderr, "AT_PAGESZ %d\n", a);

	/* Wrong size */
	if (elf_aux_info(AT_PAGESZ, &b, sizeof(b)) != EINVAL)
		ret |= 2;

	/* Invalid request */
	if (elf_aux_info(-1, &a, sizeof(a)) != EINVAL)
		ret |= 4;

	/* Should either succeed or fail with ENOENT if not supported */
	switch (elf_aux_info(AT_HWCAP, &b, sizeof(b))) {
	case 0:
		fprintf(stderr, "AT_HWCAP %lx\n", b);
		break;
	case ENOENT:
		break;
	default:
		ret |= 8;
	}

	/* Should either succeed or fail with ENOENT if not supported */
	switch (elf_aux_info(AT_HWCAP2, &b, sizeof(b))) {
	case 0:
		fprintf(stderr, "AT_HWCAP2 %lx\n", b);
		break;
	case ENOENT:
		break;
	default:
		ret |= 16;
	}

	if (ret)
		fprintf(stderr, "FAILED (status %x)\n", ret);

	return ret;
}
