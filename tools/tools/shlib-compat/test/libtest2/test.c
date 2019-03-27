/*
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/stdint.h>

struct s1 {
	int32_t		f1_int;
	char		*f2_str;
	short		f3_short;
	uint64_t	f4_uint64;
	intmax_t	f5_intmax;
	void*		f6_ptr;
};

struct s2 {
	char		f1_buf[30];
	struct s1	*f2_s1;
};

struct s3 {
	struct s1	f1_s1;
	uint32_t	f2_int32;
};

int	func1(uint64_t a, uint64_t b);
int	compat_func1(int a, int b);
int	func2(int64_t a, uint64_t b);
void	func3(struct s1 *s);
void	func4(struct s1 s);
int	func5(int a, void *b, struct s2 *s);
int	func6(char a, struct s3 *s);

int
func1(uint64_t a, uint64_t b)
{
	return (a - b);
}

int
compat_func1(int a, int b)
{
	return func1(a, b);
}
__sym_compat(func1, compat_func1, TEST_1.0);

int
func2(int64_t a, uint64_t b)
{
	return (a - b);
}

void
func3(struct s1 *s)
{
}

void
func4(struct s1 s)
{
}

int
func5(int a, void *b, struct s2 *s)
{
	return (0);
}

int
func6(char a, struct s3 *s)
{
	return (0);
}
