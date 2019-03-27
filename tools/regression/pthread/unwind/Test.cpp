/* $FreeBSD$ */

int destructed;
int destructed2;

class Test {
public:
	Test() { printf("Test::Test()\n"); }
	~Test() { printf("Test::~Test()\n"); destructed = 1; }
};

void
cleanup_handler(void *arg)
{
	destructed2 = 1;
	printf("%s()\n", __func__);
}

void
check_destruct(void)
{
	if (!destructed)
		printf("Bug, object destructor is not called\n");
	else
		printf("OK\n");
}

void
check_destruct2(void)
{
	if (!destructed)
		printf("Bug, object destructor is not called\n");
	else if (!destructed2)
		printf("Bug, cleanup handler is not called\n");
	else
		printf("OK\n");
}
