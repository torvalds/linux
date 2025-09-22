void my_func(char *, int, int)
	__attribute__((__bounded__(__foo__,1,2,3)));

int main(int argc, char **argv) {
	return 1;
}
