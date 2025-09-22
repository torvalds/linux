void my_func(char *, int)
	__attribute__((__bounded__(__minbytes__,1,1024,2,3)));

int main(int argc, char **argv) {
	return 1;
}
