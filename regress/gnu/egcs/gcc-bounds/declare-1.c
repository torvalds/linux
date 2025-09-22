void my_func(char *, int)
	__attribute__((__bounded__(__string__,1,2)));

int main(int argc, char **argv) {
	return 1;
}
