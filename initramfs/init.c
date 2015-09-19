#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>

int main(int argc, char *argv[])
{
	FILE *fp;
	int fd;
	int result;
	char str_buffer[256];
	int loop_count;

	//
	// write to stdout and stderr using standard file descriptors
	//
	result = sprintf(str_buffer, "write to STDOUT_FILENO\n");
	if(result <= 0) exit(1);
	result = write(STDOUT_FILENO, str_buffer, strlen(str_buffer));
	if(result <= 0) exit(2);

	result = sprintf(str_buffer, "write to STDERR_FILENO\n");
	if(result <= 0) exit(3);
	result = write(STDERR_FILENO, str_buffer, strlen(str_buffer));
	if(result <= 0) exit(4);

	//
	// write to stdout and stderr using standard file stream pointers
	//
	result = printf("printf to stdout\n");
	if(result <= 0) exit(5);

	result = fprintf(stderr, "fprintf to stderr\n");
	if(result <= 0) exit(6);

	//
	// mount devtmpfs so we can open the LCD display driver
	//
	result = mount("none", "/dev", "devtmpfs", MS_SILENT, NULL);
	if(result != 0) exit(7);

	//
	// enter an infinite loop printing a loop count to stdout and the LCD
	// every 5 seconds
	//
	loop_count = 0;
	while(1) {

		result = sprintf(str_buffer, "loop = %d\n", loop_count);
		if(result <= 0) exit(8);

		//
		// always print loop count to stdout
		//
		result = printf("%s", str_buffer);
		if(result <= 0) exit(9);

		//
		// alternate printing loop count to LCD using FD and FP
		//
		if((loop_count & 0x1) == 0) {
			fd = open("/dev/ttyLCD0", O_WRONLY);
			if (fd < 0) exit(10);
			else {
				result = write(fd, str_buffer, strlen(str_buffer));
				close(fd);
				if(result <= 0) exit(11);
			}
		} else {

			fp = fopen("/dev/ttyLCD0", "w");
			if (fp == NULL) exit(12);
			else {
				result = fprintf(fp, "fp %s", str_buffer);
				fclose(fp);
				if(result <= 0) exit(13);
			}
		}

		sleep(5);
		loop_count++;
	}
}
