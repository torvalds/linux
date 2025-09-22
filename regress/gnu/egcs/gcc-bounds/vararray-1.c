/* gcc used to barf on this with a 'not 8-byte aligned' error 
 * while correct behaviour is to ignore it */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	int sample_buffer[10][argc+10];
	memset(sample_buffer, 0, sizeof(sample_buffer));
	return 1;
}
