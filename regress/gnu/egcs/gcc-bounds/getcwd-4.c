#include <unistd.h>

int main(int argc, char **argv) {
	/* XXX - is this worth warning for? right now nothing */
	char buf[1024];
	getcwd(buf, 10);
	return 1;
}
