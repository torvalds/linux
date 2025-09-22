#include <unistd.h>

int main(int argc, char **argv) {
	char buf[10];
	getwd(buf);			/* not safe */
	getcwd(buf, sizeof buf);	/* safe */
	return 1;
}
