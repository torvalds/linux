void my_func(char *, int, int)
	__attribute__((__bounded__(__minbytes__,1,TEST_SIZE)));

int main(int argc, char **argv) {
	return 1;
}
