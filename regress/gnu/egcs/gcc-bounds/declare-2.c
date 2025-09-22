void my_func(char *, int)
	__attribute__((__bounded__(__string__,3,4)));

int main(int argc, char **argv) {
	return 1;
}
