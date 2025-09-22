#include <unistd.h>

int main(int argc, char **argv) {
	char buf[10];
	getcwd(buf, sizeof(buf) - 1000);
	return 1;
}
