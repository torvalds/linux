#define TEST_SUCCESS 1
#define TEST_FAILURE 0
#define MAX_MSG_LEN 60

static int g_test_pass = 0;
#define TEST(name, ...)						\
{								\
	char str[MAX_MSG_LEN] = { 0, };				\
	int (*fn)(char *str, int len, ...);			\
	int ret;						\
								\
	fn = (int (*)(char *str, int len, ...))test_##name;	\
	ret = fn(str, sizeof(str), ##__VA_ARGS__);		\
	if (!ret)						\
		g_test_pass = -1;				\
								\
	printf("%-20s %s [%s]\n", #name,			\
	       ret == TEST_SUCCESS ? "passed" : "failed", str);	\
	fflush(stdout);						\
}

