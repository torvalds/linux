// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

struct S {
	int	a;
	int	b;
	int	c;
};

union U {
	int	a;
	int	b;
	int	c;
};

struct S1 {
	int	a;
	int	b;
	int	c;
};

union U1 {
	int	a;
	int	b;
	int	c;
};

typedef int T;
typedef int S;
typedef int U;
typedef int T1;
typedef int S1;
typedef int U1;

struct root_struct {
	S		m_1;
	T		m_2;
	U		m_3;
	S1		m_4;
	T1		m_5;
	U1		m_6;
	struct S	m_7;
	struct S1	m_8;
	union  U	m_9;
	union  U1	m_10;
};

int func(struct root_struct *root)
{
	return 0;
}

int kfunc_a(struct root_struct *root)
{
	return 0;
}

int kfunc_b(struct root_struct *root)
{
	return 0;
}

struct root_struct *kfunc_c(struct root_struct *a, struct root_struct *b)
{
	return a;
}

int kfunc_d(struct root_struct *a, struct root_struct *b)
{
	return 0;
}

int kfunc_e(struct root_struct *a__arena,
	    struct root_struct *b__arena__nullable,
	    struct root_struct *c__arena,
	    struct root_struct *d__arena__nullable,
	    struct root_struct *e__arena)
{
	return 0;
}

int kfunc_f(struct root_struct *a, struct root_struct *b__arena, int flags)
{
	return 0;
}

struct root_struct *kfunc_g(struct root_struct *a__arena,
			    struct root_struct *b__arena__nullable)
{
	return a__arena;
}
