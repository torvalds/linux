/*
 * ec_access.c
 *
 * Copyright (C) 2010 SUSE Linux Products GmbH
 * Author:
 *      Thomas Renninger <trenn@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <getopt.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>


#define EC_SPACE_SIZE 256
#define SYSFS_PATH "/sys/kernel/debug/ec/ec0/io"

/* TBD/Enhancements:
   - Provide param for accessing different ECs (not supported by kernel yet)
*/

static int read_mode = -1;
static int sleep_time;
static int write_byte_offset = -1;
static int read_byte_offset = -1;
static uint8_t write_value = -1;

void usage(char progname[], int exit_status)
{
	printf("Usage:\n");
	printf("1) %s -r [-s sleep]\n", basename(progname));
	printf("2) %s -b byte_offset\n", basename(progname));
	printf("3) %s -w byte_offset -v value\n\n", basename(progname));

	puts("\t-r [-s sleep]      : Dump EC registers");
	puts("\t                     If sleep is given, sleep x seconds,");
	puts("\t                     re-read EC registers and show changes");
	puts("\t-b offset          : Read value at byte_offset (in hex)");
	puts("\t-w offset -v value : Write value at byte_offset");
	puts("\t-h                 : Print this help\n\n");
	puts("Offsets and values are in hexadecimal number sytem.");
	puts("The offset and value must be between 0 and 0xff.");
	exit(exit_status);
}

void parse_opts(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "rs:b:w:v:h")) != -1) {

		switch (c) {
		case 'r':
			if (read_mode != -1)
				usage(argv[0], EXIT_FAILURE);
			read_mode = 1;
			break;
		case 's':
			if (read_mode != -1 && read_mode != 1)
				usage(argv[0], EXIT_FAILURE);

			sleep_time = atoi(optarg);
			if (sleep_time <= 0) {
				sleep_time = 0;
				usage(argv[0], EXIT_FAILURE);
				printf("Bad sleep time: %s\n", optarg);
			}
			break;
		case 'b':
			if (read_mode != -1)
				usage(argv[0], EXIT_FAILURE);
			read_mode = 1;
			read_byte_offset = strtoul(optarg, NULL, 16);
			break;
		case 'w':
			if (read_mode != -1)
				usage(argv[0], EXIT_FAILURE);
			read_mode = 0;
			write_byte_offset = strtoul(optarg, NULL, 16);
			break;
		case 'v':
			write_value = strtoul(optarg, NULL, 16);
			break;
		case 'h':
			usage(argv[0], EXIT_SUCCESS);
		default:
			fprintf(stderr, "Unknown option!\n");
			usage(argv[0], EXIT_FAILURE);
		}
	}
	if (read_mode == 0) {
		if (write_byte_offset < 0 ||
		    write_byte_offset >= EC_SPACE_SIZE) {
			fprintf(stderr, "Wrong byte offset 0x%.2x, valid: "
				"[0-0x%.2x]\n",
				write_byte_offset, EC_SPACE_SIZE - 1);
			usage(argv[0], EXIT_FAILURE);
		}
		if (write_value < 0 ||
		    write_value >= 255) {
			fprintf(stderr, "Wrong byte offset 0x%.2x, valid:"
				"[0-0xff]\n", write_byte_offset);
			usage(argv[0], EXIT_FAILURE);
		}
	}
	if (read_mode == 1 && read_byte_offset != -1) {
		if (read_byte_offset < -1 ||
		    read_byte_offset >= EC_SPACE_SIZE) {
			fprintf(stderr, "Wrong byte offset 0x%.2x, valid: "
				"[0-0x%.2x]\n",
				read_byte_offset, EC_SPACE_SIZE - 1);
			usage(argv[0], EXIT_FAILURE);
		}
	}
	/* Add additional parameter checks here */
}

void dump_ec(int fd)
{
	char buf[EC_SPACE_SIZE];
	char buf2[EC_SPACE_SIZE];
	int byte_off, bytes_read;

	bytes_read = read(fd, buf, EC_SPACE_SIZE);

	if (bytes_read == -1)
		err(EXIT_FAILURE, "Could not read from %s\n", SYSFS_PATH);

	if (bytes_read != EC_SPACE_SIZE)
		fprintf(stderr, "Could only read %d bytes\n", bytes_read);

	printf("     00  01  02  03  04  05  06  07  08  09  0A  0B  0C  0D  0E  0F");
	for (byte_off = 0; byte_off < bytes_read; byte_off++) {
		if ((byte_off % 16) == 0)
			printf("\n%.2X: ", byte_off);
		printf(" %.2x ", (uint8_t)buf[byte_off]);
	}
	printf("\n");

	if (!sleep_time)
		return;

	printf("\n");
	lseek(fd, 0, SEEK_SET);
	sleep(sleep_time);

	bytes_read = read(fd, buf2, EC_SPACE_SIZE);

	if (bytes_read == -1)
		err(EXIT_FAILURE, "Could not read from %s\n", SYSFS_PATH);

	if (bytes_read != EC_SPACE_SIZE)
		fprintf(stderr, "Could only read %d bytes\n", bytes_read);

	printf("     00  01  02  03  04  05  06  07  08  09  0A  0B  0C  0D  0E  0F");
	for (byte_off = 0; byte_off < bytes_read; byte_off++) {
		if ((byte_off % 16) == 0)
			printf("\n%.2X: ", byte_off);

		if (buf[byte_off] == buf2[byte_off])
			printf(" %.2x ", (uint8_t)buf2[byte_off]);
		else
			printf("*%.2x ", (uint8_t)buf2[byte_off]);
	}
	printf("\n");
}

void read_ec_val(int fd, int byte_offset)
{
	uint8_t buf;
	int error;

	error = lseek(fd, byte_offset, SEEK_SET);
	if (error != byte_offset)
		err(EXIT_FAILURE, "Cannot set offset to 0x%.2x", byte_offset);

	error = read(fd, &buf, 1);
	if (error != 1)
		err(EXIT_FAILURE, "Could not read byte 0x%.2x from %s\n",
		    byte_offset, SYSFS_PATH);
	printf("0x%.2x\n", buf);
	return;
}

void write_ec_val(int fd, int byte_offset, uint8_t value)
{
	int error;

	error = lseek(fd, byte_offset, SEEK_SET);
	if (error != byte_offset)
		err(EXIT_FAILURE, "Cannot set offset to 0x%.2x", byte_offset);

	error = write(fd, &value, 1);
	if (error != 1)
		err(EXIT_FAILURE, "Cannot write value 0x%.2x to offset 0x%.2x",
		    value, byte_offset);
}

int main(int argc, char *argv[])
{
	int file_mode = O_RDONLY;
	int fd;

	parse_opts(argc, argv);

	if (read_mode == 0)
		file_mode = O_WRONLY;
	else if (read_mode == 1)
		file_mode = O_RDONLY;
	else
		usage(argv[0], EXIT_FAILURE);

	fd = open(SYSFS_PATH, file_mode);
	if (fd == -1)
		err(EXIT_FAILURE, "%s", SYSFS_PATH);

	if (read_mode)
		if (read_byte_offset == -1)
			dump_ec(fd);
		else if (read_byte_offset < 0 ||
			 read_byte_offset >= EC_SPACE_SIZE)
			usage(argv[0], EXIT_FAILURE);
		else
			read_ec_val(fd, read_byte_offset);
	else
		write_ec_val(fd, write_byte_offset, write_value);
	close(fd);

	exit(EXIT_SUCCESS);
}
