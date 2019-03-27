/*
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/stdint.h>

struct s1 {
	int		f1_int;
	char		*f2_str;
	short		f3_short;
	uint64_t	f4_uint64;
	intmax_t	f5_intmax;
	void*		f6_ptr;
};

struct s2 {
	char		f1_buf[20];
	struct s1	*f2_s1;
};

struct s3 {
	struct s1	f1_s1;
	uint32_t	f2_int32;
};

enum f3_t {
	f3_val0, f3_val1
};

struct s4 {
	struct s1	f1_s1;
	uint32_t	f2_int32;
	enum f3_t	f3_enum;
};

typedef int i32;

int	func1(int a, int b);
int	func2(int64_t a, uint64_t b);
void	func3(struct s1 *s);
void	func4(struct s1 s);
int32_t	func5(i32 a, void *b, struct s2 *s);
int	func6__compat(char a, struct s3 *s);
int	func6(char a, struct s4 *s);

int
func1(int a, int b)
{
	return (a - b);
}

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
func6(char a, struct s4 *s)
{
	return (0);
}

int
func6__compat(char a, struct s3 *s)
{
	return (0);
}

__sym_compat(func6, func6__compat, TEST_1.0);
