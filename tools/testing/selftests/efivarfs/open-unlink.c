#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char **argv)
{
	const char *path;
	char buf[5];
	int fd, rc;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path>\n", argv[0]);
		return EXIT_FAILURE;
	}

	path = argv[1];

	/* attributes: EFI_VARIABLE_NON_VOLATILE |
	 *		EFI_VARIABLE_BOOTSERVICE_ACCESS |
	 *		EFI_VARIABLE_RUNTIME_ACCESS
	 */
	*(uint32_t *)buf = 0x7;
	buf[4] = 0;

	/* create a test variable */
	fd = open(path, O_WRONLY | O_CREAT);
	if (fd < 0) {
		perror("open(O_WRONLY)");
		return EXIT_FAILURE;
	}

	rc = write(fd, buf, sizeof(buf));
	if (rc != sizeof(buf)) {
		perror("write");
		return EXIT_FAILURE;
	}

	close(fd);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return EXIT_FAILURE;
	}

	if (unlink(path) < 0) {
		perror("unlink");
		return EXIT_FAILURE;
	}

	rc = read(fd, buf, sizeof(buf));
	if (rc > 0) {
		fprintf(stderr, "reading from an unlinked variable "
				"shouldn't be possible\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
