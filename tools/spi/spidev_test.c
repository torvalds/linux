// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <linux/kernel.h>

static void pabort(const char *s)
{
	if (errno != 0)
		perror(s);
	else
		printf("%s\n", s);

	abort();
}

static const char *device = "/dev/spidev1.1";
static uint32_t mode;
static uint8_t bits = 8;
static char *input_file;
static char *output_file;
static uint32_t speed = 500000;
static uint16_t delay;
static uint16_t word_delay;
static int verbose;
static int transfer_size = -1;
static int transfers = 1;
static int iterations;
static int interval = 5; /* interval in seconds for showing transfer rate */
static int compare;
static int nonzero;
static int do_tx = 1, do_rx = 1;
static int random_input;
static int predictable;
static int input_choices;

static uint8_t default_tx[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x40, 0x00, 0x00, 0x00, 0x00, 0x95,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xF0, 0x0D,
};

static uint8_t default_rx[ARRAY_SIZE(default_tx)] = {0, };
static char *input_tx;

static void hex_dump(const void *src, size_t length, size_t line_size,
		     char *prefix)
{
	int i = 0;
	const unsigned char *address = src;
	const unsigned char *line = address;
	unsigned char c;

	printf("%s | ", prefix);

	if (length == 0)
		printf("__ ||\n");

	while (length-- > 0) {
		printf("%02X ", *address++);
		if (!(++i % line_size) || (length == 0 && i % line_size)) {
			if (length == 0) {
				while (i++ % line_size)
					printf("__ ");
			}
			printf(" |");
			while (line < address) {
				c = *line++;
				printf("%c", (c < 32 || c > 126) ? '.' : c);
			}
			printf("|\n");
			if (length > 0)
				printf("%s | ", prefix);
		}
	}
}

/*
 *  Unescape - process hexadecimal escape character
 *      converts shell input "\x23" -> 0x23
 */
static int unescape(char *src, char *dst, size_t size)
{
	char *out = dst;
	unsigned int ch;

	while (*src && size--) {
		if (src[0] == '\\' && src[1] != '\0') {
			src++;

			if (src[0] == 'x' &&
			    isxdigit((unsigned char)src[1]) &&
			    isxdigit((unsigned char)src[2]) &&
			    sscanf(&src[1], "%2x", &ch)) {
				*out++ = (unsigned char)ch;
				src += 3;
				continue;
			}

			*out++ = '\\';
			if (!size--)
				break;
		}
		*out++ = *src++;
	}
	return out - dst;
}

static void transfer(int fd, uint8_t const * const tx, uint8_t const * const rx, size_t len)
{
	int ret;
	int out_fd;
	size_t bytes_per_word = DIV_ROUND_UP(bits, 8);
	int effective_transfers;
	struct spi_ioc_transfer *tr;
	size_t len_per_transfer;
	const uint8_t *tx_buf = tx;
	const uint8_t *rx_buf = rx;
	size_t rem;

	if (len) {
		effective_transfers = min_t(size_t, transfers, DIV_ROUND_UP(len, bytes_per_word));
		len_per_transfer = roundup(DIV_ROUND_UP(len, effective_transfers), bytes_per_word);
	} else {
		/* zero-length transfers may be handled by the driver, deliver as ordered */
		effective_transfers = transfers;
		len_per_transfer = 0;
	}

	if (SPI_MSGSIZE(effective_transfers) == 0)
		pabort("too many transfers, SPI_IOC_MESSAGE size limit exceeded");

	tr = calloc(effective_transfers, sizeof(*tr));
	if (!tr)
		pabort("can't allocate transfer array");

	rem = len;

	for (int i = 0; i < effective_transfers; i++) {
		size_t n = min(rem, len_per_transfer);

		tr[i] = (struct spi_ioc_transfer) {
			.tx_buf = (unsigned long)tx_buf,
			.rx_buf = (unsigned long)rx_buf,
			.len = n,
			.delay_usecs = delay,
			.word_delay_usecs = word_delay,
			.speed_hz = speed,
			.bits_per_word = bits,
		};

		if (tx_buf)
			tx_buf += n;

		if (rx_buf)
			rx_buf += n;

		rem -= n;

		if (mode & SPI_TX_OCTAL)
			tr[i].tx_nbits = 8;
		else if (mode & SPI_TX_QUAD)
			tr[i].tx_nbits = 4;
		else if (mode & SPI_TX_DUAL)
			tr[i].tx_nbits = 2;
		if (mode & SPI_RX_OCTAL)
			tr[i].rx_nbits = 8;
		else if (mode & SPI_RX_QUAD)
			tr[i].rx_nbits = 4;
		else if (mode & SPI_RX_DUAL)
			tr[i].rx_nbits = 2;
		if (!(mode & SPI_LOOP)) {
			if (mode & (SPI_TX_OCTAL | SPI_TX_QUAD | SPI_TX_DUAL))
				tr[i].rx_buf = 0;
			else if (mode & (SPI_RX_OCTAL | SPI_RX_QUAD | SPI_RX_DUAL))
				tr[i].tx_buf = 0;
		}
	}

	ret = ioctl(fd, SPI_IOC_MESSAGE(effective_transfers), tr);
	free(tr);
	if (ret < 0) {
		const size_t dump_len = min_t(size_t, len, 256);
		int saved_errno = errno;

		if (tx) {
			hex_dump(tx, dump_len, 32, "TX");
			if (len > dump_len)
				printf("... (%zu more bytes)\n", len - dump_len);
		}
		errno = saved_errno;
		pabort("can't send spi message");
	}

	if (verbose && tx)
		hex_dump(tx, len, 32, "TX");

	if (output_file) {
		out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (out_fd < 0)
			pabort("could not open output file");

		ret = write(out_fd, rx, len);
		if (ret != len)
			pabort("not all bytes written to output file");

		close(out_fd);
	}

	if (verbose && rx)
		hex_dump(rx, len, 32, "RX");

	if (tx && rx && (compare || mode & SPI_LOOP)) {
		if (memcmp(tx, rx, len)) {
			fprintf(stderr, "transfer error !\n");
			hex_dump(tx, len, 32, "TX");
			hex_dump(rx, len, 32, "RX");
			exit(1);
		}
	}
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [-2348CDFHILMNOPRSTZbcdiloprstvwz]\n", prog);
	puts("general device settings:\n"
		 "  -D --device         device to use (default /dev/spidev1.1)\n"
		 "  -s --speed          max speed (Hz)\n"
		 "  -d --delay          delay (usec)\n"
		 "  -w --word-delay     word delay (usec)\n"
		 "  -l --loop           loopback\n"
		 "  -c --compare        compare RX'ed and TX'ed data\n"
		 "  -t --no-tx          don't send data\n"
		 "  -r --no-rx          don't receive data\n"
		 "spi mode:\n"
		 "  -H --cpha           clock phase\n"
		 "  -O --cpol           clock polarity\n"
		 "  -F --rx-cpha-flip   flip CPHA on Rx only xfer\n"
		 "number of wires for transmission:\n"
		 "  -2 --dual           dual transfer\n"
		 "  -4 --quad           quad transfer\n"
		 "  -8 --octal          octal transfer\n"
		 "  -3 --3wire          SI/SO signals shared\n"
		 "  -Z --3wire-hiz      high impedance turnaround\n"
		 "data:\n"
		 "  -i --input          input data from a file (e.g. \"test.bin\")\n"
		 "  -o --output         output data to a file (e.g. \"results.bin\")\n"
		 "  -p                  send data (e.g. \"1234\\xde\\xad\")\n"
		 "  -z --nonzero        don't send 0x00 or 0xff bytes\n"
		 "  -P --predictable    transfer the given number of sequential bytes\n"
		 "  -S --size           transfer the given number of random bytes\n"
		 "  -I --iter           iterations\n"
		 "additional parameters:\n"
		 "  -b --bpw            bits per word\n"
		 "  -L --lsb            least significant bit first\n"
		 "  -C --cs-high        chip select active high\n"
		 "  -N --no-cs          no chip select\n"
		 "  -R --ready          slave pulls low to pause\n"
		 "  -M --mosi-idle-low  leave mosi line low when idle\n"
		 "  -T --transfers      number of transfers\n"
		 "misc:\n"
		 "  -v --verbose        Verbose (show tx buffer)\n");
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",        1, 0, 'D' },
			{ "speed",         1, 0, 's' },
			{ "delay",         1, 0, 'd' },
			{ "word-delay",    1, 0, 'w' },
			{ "loop",          0, 0, 'l' },
			{ "compare",       0, 0, 'c' },
			{ "no-tx",         0, 0, 't' },
			{ "no-rx",         0, 0, 'r' },
			{ "cpha",          0, 0, 'H' },
			{ "cpol",          0, 0, 'O' },
			{ "rx-cpha-flip",  0, 0, 'F' },
			{ "dual",          0, 0, '2' },
			{ "quad",          0, 0, '4' },
			{ "octal",         0, 0, '8' },
			{ "3wire",         0, 0, '3' },
			{ "3wire-hiz",     0, 0, 'Z' },
			{ "input",         1, 0, 'i' },
			{ "output",        1, 0, 'o' },
			{ "size",          1, 0, 'S' },
			{ "nonzero",       0, 0, 'z' },
			{ "iter",          1, 0, 'I' },
			{ "bpw",           1, 0, 'b' },
			{ "lsb",           0, 0, 'L' },
			{ "cs-high",       0, 0, 'C' },
			{ "no-cs",         0, 0, 'N' },
			{ "ready",         0, 0, 'R' },
			{ "mosi-idle-low", 0, 0, 'M' },
			{ "predictable",   1, 0, 'P' },
			{ "transfers",     1, 0, 'T' },
			{ "verbose",       0, 0, 'v' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "D:s:d:w:b:i:o:lctrHOLC3ZFMNR248p:P:T:vS:zI:",
				lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'w':
			word_delay = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'i':
			input_file = optarg;
			input_choices++;
			break;
		case 'o':
			output_file = optarg;
			break;
		case 'l':
			mode |= SPI_LOOP;
			break;
		case 'c':
			compare = 1;
			break;
		case 't':
			do_tx = 0;
			break;
		case 'r':
			do_rx = 0;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'Z':
			mode |= SPI_3WIRE_HIZ;
			break;
		case 'F':
			mode |= SPI_RX_CPHA_FLIP;
			break;
		case 'M':
			mode |= SPI_MOSI_IDLE_LOW;
			break;
		case 'T':
			transfers = atoi(optarg);
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		case 'p':
			input_tx = optarg;
			input_choices++;
			break;
		case 'P':
			transfer_size = atoi(optarg);
			predictable = 1;
			input_choices++;
			break;
		case '2':
			mode |= SPI_TX_DUAL;
			break;
		case '4':
			mode |= SPI_TX_QUAD;
			break;
		case '8':
			mode |= SPI_TX_OCTAL;
			break;
		case 'S':
			transfer_size = atoi(optarg);
			random_input = 1;
			input_choices++;
			break;
		case 'z':
			nonzero = 1;
			break;
		case 'I':
			iterations = atoi(optarg);
			break;
		default:
			print_usage(argv[0]);
		}
	}
	if (mode & SPI_LOOP) {
		if (mode & SPI_TX_DUAL)
			mode |= SPI_RX_DUAL;
		if (mode & SPI_TX_QUAD)
			mode |= SPI_RX_QUAD;
		if (mode & SPI_TX_OCTAL)
			mode |= SPI_RX_OCTAL;
	}
}

static void transfer_escaped_string(int fd, char *str)
{
	size_t size = strlen(str);
	uint8_t *tx;
	uint8_t *rx = NULL;

	tx = malloc(size);
	if (!tx)
		pabort("can't allocate tx buffer");

	if (do_rx) {
		rx = malloc(size);
		if (!rx)
			pabort("can't allocate rx buffer");
	}

	size = unescape(str, (char *)tx, size);
	transfer(fd, tx, rx, size);
	free(rx);
	free(tx);
}

static void transfer_file(int fd, char *filename)
{
	ssize_t bytes;
	struct stat sb;
	int tx_fd;
	uint8_t *tx;
	uint8_t *rx = NULL;

	if (stat(filename, &sb) == -1)
		pabort("can't stat input file");

	tx_fd = open(filename, O_RDONLY);
	if (tx_fd < 0)
		pabort("can't open input file");

	tx = malloc(sb.st_size);
	if (!tx)
		pabort("can't allocate tx buffer");


	if (do_rx) {
		rx = malloc(sb.st_size);
		if (!rx)
			pabort("can't allocate rx buffer");
	}

	bytes = read(tx_fd, tx, sb.st_size);
	if (bytes != sb.st_size)
		pabort("failed to read input file");

	transfer(fd, tx, rx, sb.st_size);
	free(rx);
	free(tx);
	close(tx_fd);
}

static uint64_t _read_count;
static uint64_t _write_count;

static void show_transfer_rate(void)
{
	static uint64_t prev_read_count, prev_write_count;
	double rx_rate, tx_rate;

	rx_rate = ((_read_count - prev_read_count) * 8) / (interval*1000.0);
	tx_rate = ((_write_count - prev_write_count) * 8) / (interval*1000.0);

	printf("rate: tx %.1fkbps, rx %.1fkbps\n", rx_rate, tx_rate);

	prev_read_count = _read_count;
	prev_write_count = _write_count;
}

static void transfer_buf(int fd, int len)
{
	uint8_t *tx = NULL;
	uint8_t *rx = NULL;
	int i;

	if (do_tx) {
		tx = malloc(len);
		if (!tx)
			pabort("can't allocate tx buffer");
		for (i = 0; i < len; i++) {
			if (predictable) {
				int v = i - iterations;

				if (nonzero)
					v = ((v % 254) + 254) % 254 + 1;
				tx[i] = (char)v;
			} else {
				do {
					tx[i] = random();
				} while (nonzero && (tx[i] == 0x0 || tx[i] == 0xff));
			}
		}
	}

	if (do_rx) {
		rx = malloc(len);
		if (!rx)
			pabort("can't allocate rx buffer");
	}

	transfer(fd, tx, rx, len);
	if (do_tx)
		_write_count += len;
	if (do_rx)
		_read_count += len;

	free(rx);
	free(tx);
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd;
	uint32_t request;

	parse_opts(argc, argv);

	if (input_choices > 1)
		pabort("at most one of -S (--size), -p, -i (--input), -P (--predictable) may be selected, "
		       "and each may be specified only once");

	if ((random_input || predictable) && transfer_size < 0)
		pabort("a size argument is mandatory for -S (--size) and -P (--predictable)");

	if (iterations && transfer_size < 0)
		pabort("-I (--iter) is only implemented for -S (--size) and -P (--predictable)");

	if (nonzero && transfer_size < 0)
		pabort("-z (--nonzero) is only implemented for -S (--size) and -P (--predictable)");

	if (compare && (!do_tx || !do_rx))
		pabort("-c (--compare) conflicts with -t (--no-tx) or -r (--no-rx)");

	if (!do_rx && output_file)
		pabort("-r (--no-rx) conflicts with -o (--output)");

	if (!do_tx && transfer_size < 0)
		pabort("-t (--no-tx) is only implemented for -S (--size) and -P (--predictable)");

	if (compare && mode & (SPI_TX_OCTAL | SPI_TX_QUAD | SPI_TX_DUAL))
		pabort("-c (--compare) conflicts with -2 (--dual), -4 (--quad) or -8 (--octal)");

	if (transfers < 1)
		pabort("-T (--transfers) must be 1 or above");

	if (bits < 1)
		pabort("-b (--bpw) must be 1 or above");

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");

	/*
	 * spi mode
	 */
	/* WR is make a request to assign 'mode' */
	request = mode;
	ret = ioctl(fd, SPI_IOC_WR_MODE32, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	/* RD is read what mode the device actually is in */
	ret = ioctl(fd, SPI_IOC_RD_MODE32, &mode);
	if (ret == -1)
		pabort("can't get spi mode");
	/* Drivers can reject some mode bits without returning an error.
	 * Read the current value to identify what mode it is in, and if it
	 * differs from the requested mode, warn the user.
	 */
	if (request != mode)
		printf("WARNING device does not support requested mode 0x%x\n",
			request);

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");

	printf("spi mode: 0x%x\n", mode);
	printf("bits per word: %u\n", bits);
	printf("max speed: %u Hz (%u kHz)\n", speed, speed/1000);

	if (input_tx)
		transfer_escaped_string(fd, input_tx);
	else if (input_file)
		transfer_file(fd, input_file);
	else if (transfer_size >= 0) {
		struct timespec last_stat;

		clock_gettime(CLOCK_MONOTONIC, &last_stat);

		while (iterations-- > 0) {
			struct timespec current;

			transfer_buf(fd, transfer_size);

			clock_gettime(CLOCK_MONOTONIC, &current);
			if (current.tv_sec - last_stat.tv_sec > interval) {
				show_transfer_rate();
				last_stat = current;
			}
		}
		printf("total: tx %.1fKB, rx %.1fKB\n",
		       _write_count/1024.0, _read_count/1024.0);
	} else
		transfer(fd, default_tx, do_rx ? default_rx : NULL, sizeof(default_tx));

	close(fd);

	return ret;
}
