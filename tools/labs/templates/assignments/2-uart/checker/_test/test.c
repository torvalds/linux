#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "uart16550.h"

#define UART16550_MAJOR		42
#define COM1_MAJOR		42
#define COM2_MAJOR		42
#define STR(x)			#x
#define XSTR(x)			STR(x)
#define OPTION_COM1_ONLY	1
#define OPTION_COM2_ONLY	2
#define OPTION_BOTH		3

#define MODULE_NAME		"uart16550"
#define SOLUTION_NAME		"solutie"

#define PAD_CHARS		60

#define UART0			"/dev/uart0"
#define UART1			"/dev/uart1"
#define UART10			"/dev/uart10"

#define INFILE			"testfile.in"
#define OUTFILE			"testfile.out"

#define fail(s)		do {				\
		printf("%s:%d: ", __func__, __LINE__);	\
		fflush(stdout);				\
		perror(s);				\
		exit(EXIT_FAILURE);			\
	} while (0)

#define test(d, v, e)			do_test((d), (v), (e), 0, 0)
#define not_test(d, v, e)		do_test((d), (v), (e), 1, 0)
#define fatal_test(d, v, e)		do_test((d), (v), (e), 0, 1)

static int
do_test(const char *description, int value, int expected, int negate, int fatal)
{
	int num_chars;

	num_chars = printf("%s", description);
	for (; num_chars < PAD_CHARS - strlen("passed"); num_chars++)
		putchar('.');

	fflush(stdout);
	if (!negate) {
		if (value == expected) {
			printf("passed\n");
			return 0;
		} else {
			printf("failed\n");
			if (fatal)
				exit(EXIT_FAILURE);
		}
	} else {
		if (value != expected) {
			printf("passed\n");
			return 0;
		} else {
			printf("failed\n");
			if (fatal)
				exit(EXIT_FAILURE);
		}
	}
	return 1;
}

static void
test_title(const char *title)
{
	int len = strlen(title);
	int pad = (PAD_CHARS - len) / 2 - 1;
	int mod = (PAD_CHARS - len) % 2;
	int i;

	assert(pad >= 1);
	putchar('\n');
	for (i = 0; i < pad; i++)
		putchar('=');
	printf(" %s ", title);
	for (i = 0; i < pad + mod; i++)
		putchar('=');
	putchar('\n');
}

static void
make_nodes(void)
{
	mknod(UART0, S_IFCHR, COM1_MAJOR<<8);
	mknod(UART1, S_IFCHR, (COM2_MAJOR<<8) + 1);
	mknod(UART10, S_IFCHR, (UART16550_MAJOR<<8)+10);
}

static void
remove_nodes(void)
{
	unlink(UART0);
	unlink(UART1);
	unlink(UART10);
}

static int
test1(void)
{
	int err = 0;

	test_title("Test 1. Module insertion and removal");

	/* Insert module with default params and test. */
	err |= fatal_test("insmod " MODULE_NAME ", default options",
			system("insmod " MODULE_NAME ".ko"), 0);
	err |= test("major",
			system("cat /proc/devices | grep '" XSTR(COM1_MAJOR) " " MODULE_NAME "' >/dev/null 2>&1"),
			0);
	err |= test("ioports COM1",
			system("cat /proc/ioports | grep '03f8-03ff : " MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("ioports COM2",
			system("cat /proc/ioports | grep '02f8-02ff : " MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("interrupts COM1",
			system("cat /proc/interrupts | grep '4:.*" MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("interrupts COM2",
			system("cat /proc/interrupts | grep '3:.*" MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("rmmod", system("rmmod " MODULE_NAME), 0);
	if (err)
		return err;

	/* Insert module with different major. */
	err |= fatal_test("insmod " MODULE_NAME ", major=" XSTR(COM2_MAJOR),
			system("insmod " MODULE_NAME ".ko major=" XSTR(COM2_MAJOR)), 0);
	err |= test("major",
			system("cat /proc/devices | grep '" XSTR(COM2_MAJOR) " " MODULE_NAME "' >/dev/null 2>&1"),
			0);
	err |= test("rmmod", system("rmmod " MODULE_NAME), 0);
	if (err)
		return err;

	/* Insert module only for COM2, check that it works side by side
	 * with solution.
	 */
	err |= fatal_test("insmod " MODULE_NAME ", COM2 only",
			system("insmod " MODULE_NAME ".ko option=" XSTR(OPTION_COM2_ONLY)),
			0);
	err |= fatal_test("insmod " SOLUTION_NAME ", COM1 only",
			system("insmod " SOLUTION_NAME ".ko option=" XSTR(OPTION_COM1_ONLY)),
			0);
	err |= test("ioports COM1",
			system("cat /proc/ioports | grep '03f8-03ff : " SOLUTION_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("ioports COM2",
			system("cat /proc/ioports | grep '02f8-02ff : " MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("interrupts COM1",
			system("cat /proc/interrupts | grep '4:.*" SOLUTION_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("interrupts COM2",
			system("cat /proc/interrupts | grep '3:.*" MODULE_NAME "' > /dev/null 2>&1"),
			0);
	err |= test("rmmod " MODULE_NAME, system("rmmod " MODULE_NAME), 0);
	err |= test("rmmod " SOLUTION_NAME, system("rmmod " SOLUTION_NAME), 0);

	return err;
}

static int
test2(void)
{
	int err = 0;
	int fd;

	test_title("Test 2. Invalid parameters");

	/* Check ioctl sanity. */
	err |= fatal_test("insmod", system("insmod " MODULE_NAME ".ko"), 0);
	fd = open(UART0, O_RDWR);
	if (fd == -1)
		fail("open " UART0);
#define ioctl_test(n)	test("invalid ioctl " XSTR((n)), \
		ioctl(fd, UART16550_IOCTL_SET_LINE, (n)), -1)
	err |= ioctl_test(0xdeadbeef);
	err |= ioctl_test(0x1337cafe);
#undef ioctl_test
	err |= test("invalid ioctl wrong operation", ioctl(fd, 0xffff), -1);
	close(fd);
	err |= test("rmmod", system("rmmod " MODULE_NAME), 0);

	/* Check invalid module parameters. */
	err |= not_test("insmod " MODULE_NAME ", option=0xdeadbabe",
			system("insmod " MODULE_NAME ".ko option=0xdeadbabe"),
			0);

	return err;
}

/* Speed sets:
 *	0 -> 1200, 2400, 4800
 *	1 -> 9600, 19200, 38400, 56000
 *	2 -> 115200
 */
static const struct {
	int num;
	unsigned char speed[4];
	int bufsizes[2];		/* min and max */
} speed_sets[3] = {
	{
		.num = 3,
		.speed = { UART16550_BAUD_1200,
			UART16550_BAUD_2400,
			UART16550_BAUD_4800, -1 },
		.bufsizes = { 128, 256 },
	},
	{
		.num = 4,
		.speed = { UART16550_BAUD_9600,
			UART16550_BAUD_19200,
			UART16550_BAUD_38400,
			UART16550_BAUD_56000 },
		.bufsizes = { 256, 1024 },
	},
	{
		.num = 1,
		.speed = { UART16550_BAUD_115200, -1, -1, -1 },
		.bufsizes = { 2048, 2048 },
	},
};

static void
gen_params(struct uart16550_line_info *line, int speed_set)
{
	int r;

	line->baud = speed_sets[speed_set].speed[rand() %
		speed_sets[speed_set].num];
	line->len = UART16550_LEN_8;
	line->stop = rand() % 2 * 4;
	r = rand() % 4;
	line->par = r < 2 ? r*8 : 0x18 + (r-2) * 8;
}

int do_read(int fd, unsigned char *buffer, int size)
{
	int n, from = 0;

	while (1) {
		n = read(fd, &buffer[from], size - from);
		if (n <= 0)
			return -1;
		if (n + from == size)
			return 0;
		from += n;
	}
}

int do_write(int fd, unsigned char *buffer, unsigned int size)
{
	int n, from = 0;

	while (1) {
		n = write(fd, &buffer[from], size - from);
		if (n <= 0) {
			perror("write");
			return -1;
		}
		if (n + from == size)
			return 0;
		from += n;
	}
}

static int
gen_test_file(char *fname, int speed_set)
{
	int size, min, max;
	char comm[1024];

	min = speed_sets[speed_set].bufsizes[0];
	max = speed_sets[speed_set].bufsizes[1];
	size = (min == max) ? min : rand() % (min - max) + min;
	sprintf(comm,
			"dd if=/dev/urandom of=%s bs=1 count=%d >/dev/null 2>/dev/null",
			fname,
			size);
	if (system(comm))
		fprintf(stderr, "failed to generate random file (%s)\n", comm);
	return size;
}

static void
copy_file(int fdr, int fdw, int len)
{
#define COPY_BUF_SIZE		128
	unsigned char buf[COPY_BUF_SIZE];

	do {
		int partial, rc;

		partial = len < COPY_BUF_SIZE ? len : COPY_BUF_SIZE;
		if (partial == 0)
			break;
		rc = read(fdr, buf, partial);
		if (rc == 0)
			break;
		if (rc == -1)
			fail("read");
		len -= rc;
		rc = do_write(fdw, buf, rc);
		if (rc < 0)
			fail("write");
	} while (1);
}

static int
copy_test(int fd0, int fd1, int speed_set)
{
	pid_t rpid, wpid;
	int len, status, rc, fd;

	len = gen_test_file(INFILE, speed_set);
	rpid = fork();
	switch (rpid) {
	case 0:
		fd = open(INFILE, O_RDONLY);
		if (fd < 0)
			fail("open " INFILE);
		copy_file(fd, fd0, len);
		close(fd);
		exit(EXIT_SUCCESS);
		break;
	default:
		break;
	}

	wpid = fork();
	switch (wpid) {
	case 0:
		fd = open(OUTFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0)
			fail("open " OUTFILE);
		copy_file(fd1, fd, len);
		close(fd);
		exit(EXIT_SUCCESS);
		break;
	default:
		break;
	}

	rc = waitpid(rpid, &status, 0);
	if (rc < 0)
		return rc;
	if (WEXITSTATUS(status))
		return WEXITSTATUS(status);

	rc = waitpid(wpid, &status, 0);
	if (rc < 0)
		return rc;
	if (WEXITSTATUS(status))
		return WEXITSTATUS(status);

	return system("diff " INFILE " " OUTFILE "> /dev/null 2> /dev/null");
}

static int
generic_test(const char *reader, const char *writer, int speed_set,
		int num_tests)
{
	int fd0, fd1, i, err = 0;
	char dbuf[1024], cbuf[1024];
	struct uart16550_line_info uli;

	if (reader != writer) {
		sprintf(dbuf, "insmod %s", reader);
		sprintf(cbuf, "insmod %s.ko option=%d",
				reader, OPTION_COM2_ONLY);
		fatal_test(dbuf, system(cbuf), 0);
		sprintf(dbuf, "insmod %s", writer);
		sprintf(cbuf, "insmod %s.ko option=%d",
				writer, OPTION_COM1_ONLY);
		fatal_test(dbuf, system(cbuf), 0);
	} else {
		sprintf(dbuf, "insmod %s", reader);
		sprintf(cbuf, "insmod %s.ko", reader);
		fatal_test(dbuf, system(cbuf), 0);
	}

	gen_params(&uli, speed_set);
	fd0 = open(UART0, O_WRONLY);
	if (fd0 == -1)
		fail("open " UART0);
	fd1 = open(UART1, O_RDONLY);
	if (fd1 == -1)
		fail("open " UART1);
	err |= test("ioctl reader",
			ioctl(fd1, UART16550_IOCTL_SET_LINE, &uli), 0);
	err |= test("ioctl writer",
			ioctl(fd0, UART16550_IOCTL_SET_LINE, &uli), 0);

	for (i = 0; i < num_tests; i++) {
		sprintf(dbuf, "test %02d", i + 1);
		test(dbuf, copy_test(fd0, fd1, speed_set), 0);
	}

	close(fd0);
	close(fd1);

	if (reader != writer) {
		sprintf(dbuf, "rmmod %s", reader);
		sprintf(cbuf, "rmmod %s.ko", reader);
		fatal_test(dbuf, system(cbuf), 0);
		sprintf(dbuf, "rmmod %s", writer);
		sprintf(cbuf, "rmmod %s.ko", writer);
		fatal_test(dbuf, system(cbuf), 0);
	} else {
		sprintf(dbuf, "rmmod %s", reader);
		sprintf(cbuf, "rmmod %s.ko", reader);
		fatal_test(dbuf, system(cbuf), 0);
	}

	return err;
}

#define choose_one(rd, wr)		do {			\
		int r = rand() % 2;				\
		if (r == 0) {					\
			rd = MODULE_NAME;			\
			wr = SOLUTION_NAME;			\
		} else {					\
			rd = SOLUTION_NAME;			\
			wr = MODULE_NAME;			\
		}						\
	} while (0)

static int
test3(void)
{
	const char *rd, *wr;

	rd = MODULE_NAME;
	wr = SOLUTION_NAME;
	test_title("Test 3. Read, small speed");
	return generic_test(rd, wr, 0, 5);
}

static int
test4(void)
{
	const char *rd, *wr;

	rd = SOLUTION_NAME;
	wr = MODULE_NAME;
	test_title("Test 4. Write, small speed");
	return generic_test(rd, wr, 0, 5);
}

static int
test5(void)
{
	const char *rd, *wr;

	rd = wr = MODULE_NAME;
	test_title("Test 5. Back-to-back, small speed");
	return generic_test(rd, wr, 0, 5);
}

static int
test6(void)
{
	const char *rd, *wr;

	choose_one(rd, wr);
	test_title("Test 6. Read/Write, medium speed");
	return generic_test(rd, wr, 1, 5);
}

static int
test7(void)
{
	const char *rd, *wr;

	rd = wr = MODULE_NAME;
	test_title("Test 7. Back-to-back, medium speed");
	return generic_test(rd, wr, 1, 5);
}

static int
test8(void)
{
	const char *rd, *wr;

	choose_one(rd, wr);
	test_title("Test 8. Read/Write, high speed");
	return generic_test(rd, wr, 2, 5);
}

static int
test9(void)
{
	const char *rd, *wr;

	rd = wr = MODULE_NAME;
	test_title("Test 9. Back-to-back, high speed");
	return generic_test(rd, wr, 2, 5);
}

int
main(void)
{
	int num_passed = 0;
	const int total = 9;

	srand(time(NULL));
	make_nodes();

	if (test1() == 0)
		num_passed++;
	if (test2() == 0)
		num_passed++;
	if (test3() == 0)
		num_passed++;
	if (test4() == 0)
		num_passed++;
	if (test5() == 0)
		num_passed++;
	if (test6() == 0)
		num_passed++;
	if (test7() == 0)
		num_passed++;
	if (test8() == 0)
		num_passed++;
	if (test9() == 0)
		num_passed++;

	remove_nodes();
	unlink(INFILE);
	unlink(OUTFILE);
	printf("\nFinal score: %d/%d\n", num_passed, total);

	return 0;
}

/* Extra 2 lines so the file is the proper size. */
