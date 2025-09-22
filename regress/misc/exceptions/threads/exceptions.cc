/*	$OpenBSD: exceptions.cc,v 1.2 2021/10/06 12:43:14 bluhm Exp $	*/
/*
 *	Written by Otto Moerbeek <otto@drijf.net> 2021 Public Domain
 */

#include <string>
#include <iostream>
#include <err.h>
#include <pthread.h>

void
a()
{
	try {
		throw std::string("foo");
	}
	catch (const std::string& ex) {
		if (ex != "foo")
			errx(1, "foo");
	}
}

void
b()
{
	a();
}

void *
c(void *)
{
	b();
	return NULL;
}

#define N 100

int
main()
{
	int i;
	pthread_t p[N];

	for (i = 0; i < N; i++)
		if (pthread_create(&p[i], NULL, c, NULL) != 0)
			err(1, NULL);
	for (i = 0; i < N; i++)
		if (pthread_join(p[i], NULL) != 0)
			err(1, NULL);
	std::cout << ".";
	return 0;
}
