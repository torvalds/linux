/* $OpenBSD: keynote-keygen.c,v 1.23 2024/02/07 17:22:01 tb Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/dsa.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "header.h"
#include "keynote.h"
#include "assertion.h"
#include "signature.h"

void	keygenusage(void);

void
keygenusage(void)
{
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "\t<AlgorithmName> <keysize> "
	    "<PublicKeyFile> <PrivateKeyFile> [<print-offset>] "
	    "[<print-length>]\n");
}

/*
 * Print the specified number of spaces.
 */
void
print_space(FILE *fp, int n)
{
    while (n--)
      fprintf(fp, " ");
}

/*
 * Output a key, properly formatted.
 */
void
print_key(FILE *fp, char *algname, char *key, int start, int length)
{
    int i, k;

    print_space(fp, start);
    fprintf(fp, "\"%s", algname);

    for (i = 0, k = strlen(algname) + 2; i < strlen(key); i++, k++)
    {
	if (k == length)
	{
	    if (i == strlen(key))
	    {
		fprintf(fp, "\"\n");
		return;
	    }

	    fprintf(fp, "\\\n");
	    print_space(fp, start);
	    i--;
	    k = 0;
	}
	else
	  fprintf(fp, "%c", key[i]);
    }

    fprintf(fp, "\"\n");
}

void
keynote_keygen(int argc, char *argv[])
{
    int begin = KEY_PRINT_OFFSET, prlen = KEY_PRINT_LENGTH;
    char *foo, *privalgname, seed[SEED_LEN];
    int alg, enc, ienc, len = 0, counter;
    struct keynote_deckey dc;
    unsigned long h;
    DSA *dsa;
    RSA *rsa;
    FILE *fp;
    char *algname;

    if ((argc != 5) && (argc != 6) && (argc != 7))
    {
	keygenusage();
	exit(0);
    }

    /* Fix algorithm name */
    if (argv[1][strlen(argv[1]) - 1] != ':')
    {
	int len = strlen(argv[1]) + 2;

        fprintf(stderr, "Algorithm name [%s] should be terminated with a "
		"colon, fixing.\n", argv[1]);
	algname = calloc(len, sizeof(char));
	if (algname == NULL)
	{
	    perror("calloc()");
	    exit(1);
	}

	strlcpy(algname, argv[1], len);
	algname[strlen(algname)] = ':';
    }
    else
	algname = argv[1];

    if (argc > 5)
    {
	begin = atoi(argv[5]);
	if (begin <= -1)
	{
	    fprintf(stderr, "Erroneous value for print-offset parameter.\n");
	    exit(1);
	}
    }

    if (argc > 6)
    {
	prlen = atoi(argv[6]);
	if (prlen <= 0)
	{
	    fprintf(stderr, "Erroneous value for print-length parameter.\n");
	    exit(1);
	}
    }

    if (strlen(algname) + 2 > prlen)
    {
	fprintf(stderr, "Parameter ``print-length'' should be larger "
		"than the length of AlgorithmName (%lu)\n",
		(unsigned long) strlen(algname));
	exit(1);
    }

    alg = keynote_get_key_algorithm(algname, &enc, &ienc);
    len = atoi(argv[2]);

    if (len <= 0)
    {
	fprintf(stderr, "Invalid specified keysize %d\n", len);
	exit(1);
    }

    if ((alg == KEYNOTE_ALGORITHM_DSA) &&
	(ienc == INTERNAL_ENC_ASN1) &&
	((enc == ENCODING_HEX) || (enc == ENCODING_BASE64)))
    {
        RAND_bytes(seed, SEED_LEN);

	dsa = DSA_new();

	if (dsa == NULL)
	{
	    ERR_print_errors_fp(stderr);
	    exit(1);
	}

	if (DSA_generate_parameters_ex(dsa, len, seed, SEED_LEN,
	    &counter, &h, NULL) != 1)
	{
	    ERR_print_errors_fp(stderr);
	    exit(1);
	}

	if (DSA_generate_key(dsa) != 1)
	{
	    ERR_print_errors_fp(stderr);
	    exit(1);
	}

	dc.dec_algorithm = KEYNOTE_ALGORITHM_DSA;
	dc.dec_key = (void *) dsa;

	foo = kn_encode_key(&dc, ienc, enc, KEYNOTE_PUBLIC_KEY);
	if (foo == NULL)
	{
	    fprintf(stderr, "Error encoding key (errno %d)\n", keynote_errno);
	    exit(1);
	}

	if (!strcmp(argv[3], "-"))
	  fp = stdout;
	else
	{
	    fp = fopen(argv[3], "w");
	    if (fp == NULL)
	    {
		perror(argv[3]);
		exit(1);
	    }
	}

	print_key(fp, algname, foo, begin, prlen);
	free(foo);

	if (strcmp(argv[3], "-"))
	  fclose(fp);

	foo = kn_encode_key(&dc, ienc, enc, KEYNOTE_PRIVATE_KEY);
	if (foo == NULL)
	{
	    fprintf(stderr, "Error encoding key (errno %d)\n", keynote_errno);
	    exit(1);
	}

	if (!strcmp(argv[4], "-"))
	{
	    fp = stdout;
	    if (!strcmp(argv[3], "-"))
	      printf("===========================\n");
	}
	else
	{
	    fp = fopen(argv[4], "w");
	    if (fp == NULL)
	    {
		perror(argv[4]);
		exit(1);
	    }
	}

	len = strlen(KEYNOTE_PRIVATE_KEY_PREFIX) + strlen(foo) + 1;
	privalgname = calloc(len, sizeof(char));
	if (privalgname == NULL)
	{
	    perror("calloc()");
	    exit(1);
	}
	snprintf(privalgname, len, "%s%s", KEYNOTE_PRIVATE_KEY_PREFIX, algname);
	print_key(fp, privalgname, foo, begin, prlen);
	free(privalgname);
	free(foo);

	if (strcmp(argv[4], "-"))
	  fclose(fp);

	exit(0);
    }

    if ((alg == KEYNOTE_ALGORITHM_RSA) &&
	(ienc == INTERNAL_ENC_PKCS1) &&
	((enc == ENCODING_HEX) || (enc == ENCODING_BASE64)))
    {
	rsa = RSA_generate_key(len, DEFAULT_PUBLIC, NULL, NULL);

	if (rsa == NULL)
	{
	    ERR_print_errors_fp(stderr);
	    exit(1);
	}

	dc.dec_algorithm = KEYNOTE_ALGORITHM_RSA;
	dc.dec_key = (void *) rsa;

	foo = kn_encode_key(&dc, ienc, enc, KEYNOTE_PUBLIC_KEY);
	if (foo == NULL)
	{
	    fprintf(stderr, "Error encoding key (errno %d)\n", keynote_errno);
	    exit(1);
	}

	if (!strcmp(argv[3], "-"))
	  fp = stdout;
	else
	{
	    fp = fopen(argv[3], "w");
	    if (fp == NULL)
	    {
		perror(argv[3]);
		exit(1);
	    }
	}

	print_key(fp, algname, foo, begin, prlen);
	free(foo);

	if (strcmp(argv[3], "-"))
	  fclose(fp);

	foo = kn_encode_key(&dc, ienc, enc, KEYNOTE_PRIVATE_KEY);
	if (foo == NULL)
	{
	    fprintf(stderr, "Error encoding key (errno %d)\n", keynote_errno);
	    exit(1);
	}

	if (!strcmp(argv[4], "-"))
	{
	    fp = stdout;
	    if (!strcmp(argv[3], "-"))
	      printf("===========================\n");
	}
	else
	{
	    fp = fopen(argv[4], "w");
	    if (fp == NULL)
	    {
		perror(argv[4]);
		exit(1);
	    }
	}

	len = strlen(KEYNOTE_PRIVATE_KEY_PREFIX) + strlen(foo) + 1;
	privalgname = calloc(len, sizeof(char));
	if (privalgname == NULL)
	{
	    perror("calloc()");
	    exit(1);
	}
	snprintf(privalgname, len, "%s%s", KEYNOTE_PRIVATE_KEY_PREFIX, algname);
	print_key(fp, privalgname, foo, begin, prlen);
	free(privalgname);
	free(foo);

	if (strcmp(argv[4], "-"))
	  fclose(fp);

	exit(0);
    }

    /* More algorithms here */

    fprintf(stderr, "Unknown/unsupported algorithm [%s]\n", algname);
    exit(1);
}
