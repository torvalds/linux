#define TEST_SUCCESS 1
#define TEST_FAILURE 0
#define MAX_MSG_LEN 60


static int g_test_pass = 0;
#define TEST(name) {				\
	int ret = do_test(#name, test_##name);	\
	if (!ret) g_test_pass = -1;		\
	}

static int do_test(char *name, int (*fn)(char *, int))
{
	char str[MAX_MSG_LEN];
	int result;

	result = fn(str, sizeof(str));
	printf("%-20s %s [%s]\n", name,
		result == TEST_SUCCESS ? "passed" : "failed", str);
	return result;
}
