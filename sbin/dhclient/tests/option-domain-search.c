/* $FreeBSD$ */

#include <setjmp.h>
#include <stdlib.h>

#include "dhcpd.h"

jmp_buf env;

void	expand_domain_search(struct packet *packet);

void
no_option_present()
{
	int ret;
	struct option_data option;
	struct packet p;

	option.data = NULL;
	option.len  = 0;
	p.options[DHO_DOMAIN_SEARCH] = option;

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (p.options[DHO_DOMAIN_SEARCH].len != 0 ||
	    p.options[DHO_DOMAIN_SEARCH].data != NULL)
		abort();
}

void
one_domain_valid()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data     = "\007example\003org\0";
	char *expected = "example.org.";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 13;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (option->len != strlen(expected) ||
	    strcmp(option->data, expected) != 0)
		abort();

	free(option->data);
}

void
one_domain_truncated1()
{
	int ret;
	struct option_data *option;
	struct packet p;

	char *data = "\007example\003org";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 12;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
one_domain_truncated2()
{
	int ret;
	struct option_data *option;
	struct packet p;

	char *data = "\007ex";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 3;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
two_domains_valid()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data     = "\007example\003org\0\007example\003com\0";
	char *expected = "example.org. example.com.";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 26;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (option->len != strlen(expected) ||
	    strcmp(option->data, expected) != 0)
		abort();

	free(option->data);
}

void
two_domains_truncated1()
{
	int ret;
	struct option_data *option;
	struct packet p;

	char *data = "\007example\003org\0\007example\003com";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 25;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
two_domains_truncated2()
{
	int ret;
	struct option_data *option;
	struct packet p;

	char *data = "\007example\003org\0\007ex";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 16;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
two_domains_compressed()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data     = "\007example\003org\0\006foobar\xc0\x08";
	char *expected = "example.org. foobar.org.";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 22;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (option->len != strlen(expected) ||
	    strcmp(option->data, expected) != 0)
		abort();

	free(option->data);
}

void
two_domains_infloop()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data = "\007example\003org\0\006foobar\xc0\x0d";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 22;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
two_domains_forwardptr()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data = "\007example\003org\xc0\x0d\006foobar\0";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 22;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
two_domains_truncatedptr()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data = "\007example\003org\0\006foobar\xc0";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 21;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (ret != 1)
		abort();

	free(option->data);
}

void
multiple_domains_valid()
{
	int ret;
	struct packet p;
	struct option_data *option;

	char *data =
	    "\007example\003org\0\002cl\006foobar\003com\0\002fr\xc0\x10";

	char *expected = "example.org. cl.foobar.com. fr.foobar.com.";

	option = &p.options[DHO_DOMAIN_SEARCH];
	option->len  = 33;
	option->data = malloc(option->len);
	memcpy(option->data, data, option->len);

	ret = setjmp(env);
	if (ret == 0)
		expand_domain_search(&p);

	if (option->len != strlen(expected) ||
	    strcmp(option->data, expected) != 0)
		abort();

	free(option->data);
}

int
main(int argc, char *argv[])
{

	no_option_present();

	one_domain_valid();
	one_domain_truncated1();
	one_domain_truncated2();

	two_domains_valid();
	two_domains_truncated1();
	two_domains_truncated2();

	two_domains_compressed();
	two_domains_infloop();
	two_domains_forwardptr();
	two_domains_truncatedptr();

	multiple_domains_valid();

	return (0);
}
