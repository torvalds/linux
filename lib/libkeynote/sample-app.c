/* $OpenBSD: sample-app.c,v 1.7 2004/06/29 11:35:56 msf Exp $ */
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

#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <keynote.h>


char policy_assertions[] = 
"Authorizer: \"POLICY\"\n" \
"Licensees: \"rsa-hex:3048024100d15d08ce7d2103d93ef21a87330361\\\n" \
"             ff123096b14330f9f0936e8f2064ef815ffdaabbb7d3ba47b\\\n" \
"             49fac090cf44818af7ac7d66c2910f32d8d5eb261328558e1\\\n" \
"             0203010001\"\n" \
"Comment: This is our first policy assertion\n" \
"Conditions: app_domain == \"test application\" -> \"true\";\n" \
"\n" \
"Authorizer: \"POLICY\"\n" \
"Licensees: KEY1 || KEY2\n" \
"Local-Constants: \n" \
"     KEY1 = \"rsa-base64:MEgCQQCzxWCi619s3Bqf8QOZTREBFelqWvljw\\\n" \
"              vCwktO7/5zufcz+P0UBRBFNtasWgkP6/tAIK8MnLMUnejGsye\\\n" \
"              DS2EVzAgMBAAE=\"\n" \
"     KEY2 = \"dsa-base64:MIHfAkEAhRzwrvhbRXIJH+nGfQB/tRp3ueF0j\\\n" \
"              4OqVU4GmC6eIlrmlKxR+Me6tjqtWJr5gf/AEOnzoQAPRIlpiP\\\n" \
"              VJX1mRjwJBAKHTpHS7M938wVr+lIMjq0H0Aav5T4jlxS2rphI\\\n" \
"              4fbc7tJm6wPW9p2KyHbe9GaZgzYK1OdnNXdanM/AkLW4OKz0C\\\n" \
"              FQDF69A/EHKoQC1H6DxCi0L3HfW9uwJANCLE6ViRxnv4Jj0gV\\\n" \
"              8aO/b5AD+uA63+0EXUxO0Hqp91lzhDg/61BusMxFq7mQI0CLv\\\n" \
"              S+dlCGShsYyB+VjSub7Q==\"\n" \
"Comment: A slightly more complicated policy\n" \
"Conditions: app_domain == \"test application\" && @some_num == 1 && \n" \
"            (some_var == \"some value\" || \n" \
"             some_var == \"some other value\") -> \"true\";";

char credential_assertions[] =
"KeyNote-Version: 2\n"\
"Authorizer: KEY1\n"
"Local-Constants: \n" \
"     KEY1 = \"rsa-base64:MEgCQQCzxWCi619s3Bqf8QOZTREBFelqWvljw\\\n" \
"              vCwktO7/5zufcz+P0UBRBFNtasWgkP6/tAIK8MnLMUnejGsye\\\n" \
"              DS2EVzAgMBAAE=\"\n" \
"Licensees: \"dsa-hex:3081de02402121e160209f7ecef1b6866c907e8d\\\n" \
"             d65e9a67ef0fbd6ece7760b7c8bb0d9a0b71a0dd921b949f0\\\n" \
"             9a16092eb3f50e33892bc3e9f1c8409f5298de40461493ef1\\\n" \
"             024100a60b7e77f317e156566b388aaa32c3866a086831649\\\n" \
"             1a55ab6fb8e57f7ade4a2a31e43017c383ab2a3e54f49688d\\\n" \
"             d66a326b7362beb974f2f1fb7dd573dd1bdf021500909807a\\\n" \
"             4937f198fe893be6c63a7d627f13a385b02405811292c9949\\\n" \
"             7aa80911c781a0ff51a5843423b9b4d03ad7e708ae2bfacaf\\\n" \
"             11477f4f197dbba534194f8afd1e0b73261bb0a2c04af35db\\\n" \
"             0507f5cffe74ed4f1a\"\n" \
"Conditions: app_domain == \"test application\" && \n" \
"            another_var == \"foo\" -> \"true\";\n" \
"Signature: \"sig-rsa-sha1-base64:E2OhrczI0LtAYAoJ6fSlqvlQDA4r\\\n" \
"            GiIX73T6p9eExpyHZbfjxPxXEIf6tbBre6x2Y26wBQCx/yCj5\\\n" \
"            4IS3tuY2w==\"\n";

char action_authorizer[] = 
"dsa-hex:3081de02402121e160209f7ecef1b6866c907e8d" \
"d65e9a67ef0fbd6ece7760b7c8bb0d9a0b71a0dd921b949f0" \
"9a16092eb3f50e33892bc3e9f1c8409f5298de40461493ef1" \
"024100a60b7e77f317e156566b388aaa32c3866a086831649" \
"1a55ab6fb8e57f7ade4a2a31e43017c383ab2a3e54f49688d" \
"d66a326b7362beb974f2f1fb7dd573dd1bdf021500909807a" \
"4937f198fe893be6c63a7d627f13a385b02405811292c9949" \
"7aa80911c781a0ff51a5843423b9b4d03ad7e708ae2bfacaf" \
"11477f4f197dbba534194f8afd1e0b73261bb0a2c04af35db" \
"0507f5cffe74ed4f1a";

#define NUM_RETURN_VALUES 2

char *returnvalues[NUM_RETURN_VALUES];

/* 
 * Sample application. We do the following:
 * - create a session
 * - read a "file" with our KeyNote policy assertions
 * - obtain a credential
 * - obtain the requester's public key
 * - construct an action attribute set
 * - do the query
 *
 * Since this is a sample application, we won't be actually reading any
 * real files or sockets. See the comments in the code below.
 */

int
main(int argc, char **argv)
{
    int sessionid, num, i, j;
    char **decomposed;

    /*
     * We are creating a new KeyNote session here. A session may be temporary
     * (we create it, add policies, credentials, authorizers, action set, do
     *  the query, and then we destroy it), or it may be persistent (if, for
     * example, policies remain the same for a while).
     *
     * In this example, we'll just assume the session is temporary, but there
     * will be comments as to what to do if this were a persistent session.
     */
    sessionid = kn_init();
    if (sessionid == -1)
    {
	fprintf(stderr, "Failed to create a new session.\n");
	exit(1);
    }

    /*
     * Assume we have read a file, or somehow securely acquired our policy
     * assertions, and we have stored them in policy_assertions.
     */

    /* Let's find how many policies we just "read". */
    decomposed = kn_read_asserts(policy_assertions, strlen(policy_assertions),
				 &num);
    if (decomposed == NULL)
    {
	fprintf(stderr, "Failed to allocate memory for policy assertions.\n");
	exit(1);
    }

    /*
     * If there were no assertions in the first argument to kn_read_asserts,
     * we'll get a valid pointer back, which we need to free. Note that this
     * is an error; we always MUST have at least one policy assertion.
     */
    if (num == 0)
    {
	free(decomposed);
	fprintf(stderr, "No policy assertions provided.\n");
	exit(1);
    }

    /*
     * We no longer need a copy of policy_assertions, so we could 
     * free it here.
     */

    /*
     * decomposed now contains num pointers to strings, each containing a
     * single assertion. We now add them all to the session. Note that
     * we must provide the ASSERT_FLAG_LOCAL flag to indicate that these
     * are policy assertions and thus do not have a signature field.
     */
    for (i = 0; i < num; i++)
    {
	j = kn_add_assertion(sessionid, decomposed[i],
			     strlen(decomposed[i]), ASSERT_FLAG_LOCAL);
	if (j == -1)
	{
	    switch (keynote_errno)
	    {
		case ERROR_MEMORY:
		    fprintf(stderr, "Out of memory, trying to add policy "
			    "assertion %d.\n", j);
		    break;

		case ERROR_SYNTAX:
		    fprintf(stderr, "Syntax error parsing policy "
			    "assertion %d.\n", j);
		    break;

		case ERROR_NOTFOUND:
		    fprintf(stderr, "Session %d not found while adding "
			    "policy assertion %d.\n", sessionid, j);
		default:
		    fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			    "while adding policy assertion %d.\n",
			    keynote_errno, j);
		    break;
	    }

	    /* We don't need the assertion any more. */
	    free(decomposed[i]);
	}
    }

    /* Now free decomposed itself. */
    free(decomposed);

    /*
     * Now, assume we have somehow acquired (through some application-dependent
     * means) one or more KeyNote credentials, and the key of the action
     * authorizer. For example, if this were an HTTP authorization application,
     * we would have acquired the credential(s) and the key after completing
     * an SSL protocol exchange.
     *
     * So, we have some credentials in credential_assertions, and a key
     * in action_authorizer.
     */

    /* Let's find how many credentials we just "received". */
    decomposed = kn_read_asserts(credential_assertions,
				 strlen(credential_assertions), &num);
    if (decomposed == NULL)
    {
	fprintf(stderr, "Failed to allocate memory for credential "
		"assertions.\n");
	exit(1);
    }

    /*
     * If there were no assertions in the first argument to kn_read_asserts,
     * we'll get a valid pointer back, which we need to free. Note that
     * it is legal to have zero credentials.
     */
    if (num == 0)
    {
	free(decomposed);
	fprintf(stderr, "No credential assertions provided.\n");
    }

    /*
     * We no longer need a copy of credential_assertions, so we could 
     * free it here.
     */

    /*
     * decomposed now contains num pointers to strings, each containing a
     * single assertion. We now add them all to the session. Note that here
     * we must NOT provide the ASSERT_FLAG_LOCAL flag, since these are
     * all credential assertions and need to be cryptographically verified.
     */
    for (i = 0; i < num; i++)
    {
	/*
	 * The value returned by kn_add_assertion() is an ID for that
	 * assertion (unless it's a -1, which indicates an error). We could
	 * use this ID to remove the assertion from the session in the future,
	 * if we needed to. We would need to store the IDs somewhere of
	 * course.
	 *
	 * If this were a persistent session, it may make sense to delete
	 * the credentials we just added after we are done with the query,
	 * simply to conserve memory. On the other hand, we could just leave
	 * them in the session; this has no security implications.
	 *
	 * Also note that we could do the same with policy assertions.
	 * However, if we want to delete policy assertions, it usually then
	 * makes sense to just destroy the whole session via kn_close(),
	 * which frees all allocated resources.
	 */
	j = kn_add_assertion(sessionid, decomposed[i],
			     strlen(decomposed[i]), 0);
	if (j == -1)
	{
	    switch (keynote_errno)
	    {
		case ERROR_MEMORY:
		    fprintf(stderr, "Out of memory, trying to add credential "
			    "assertion %d.\n", j);
		    break;

		case ERROR_SYNTAX:
		    fprintf(stderr, "Syntax error parsing credential "
			    "assertion %d.\n", j);
		    break;

		case ERROR_NOTFOUND:
		    fprintf(stderr, "Session %d not found while adding "
			    "credential assertion %d.\n", sessionid, j);
		default:
		    fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			    "while adding credential assertion %d.\n",
			    keynote_errno, j);
		    break;
	    }

	    /* We don't need the assertion any more. */
	    free(decomposed[i]);
	}
    }

    /* No longer needed. */
    free(decomposed);

    /*
     * Now add the action authorizer. If we have more than one, just
     * repeat. Note that the value returned here is just a success or
     * failure indicator. If we want to later on delete an authorizer from
     * the session (which we MUST do if this is a persistent session),
     * we must keep a copy of the key.
     */
    if (kn_add_authorizer(sessionid, action_authorizer) == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory while adding action "
			"authorizer.\n");
		break;

	    case ERROR_SYNTAX:
		fprintf(stderr, "Malformed action authorizer.\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while adding action "
			"authorizer.\n", sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error while adding action "
			"authorizer.\n");
		break;
	}
    }

    /*
     * If we don't need action_authorizer any more (i.e., this is a temporary
     * session), we could free it now.
     */

    /*
     * Now we need to construct the action set. In a real application, we
     * would be gathering the relevant information. Here, we just construct
     * a fixed action set.
     */

    /*
     * Add the relevant action attributes. Flags is zero, since we are not
     * using any callback functions (ENVIRONMENT_FLAG_FUNC) or a regular
     * expression for action attribute names (ENVIRONMENT_FLAG_REGEX).
     */
    if (kn_add_action(sessionid, "app_domain", "test application", 0) == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_SYNTAX:
		fprintf(stderr, "Invalid name action attribute name "
			"[app_domain]\n");
		break;

	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory adding action attribute "
			"[app_domain = \"test application\"]\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while adding action "
			"attribute [app_domain = \"test application\"]\n",
			sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			"while adding action attribute [app_domain = "
			"\"test application\"]\n", keynote_errno);
		break;
	}
    }

    if (kn_add_action(sessionid, "some_num", "1", 0) == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_SYNTAX:
		fprintf(stderr, "Invalid name action attribute name "
			"[some_num]\n");
		break;

	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory adding action attribute "
			"[some_num = \"1\"]\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while adding action "
			"attribute [some_num = \"1\"]\n", sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			"while adding action attribute [some_num = \"1\"]",
			keynote_errno);
		break;
	}
    }

    if (kn_add_action(sessionid, "some_var", "some other value", 0) == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_SYNTAX:
		fprintf(stderr, "Invalid name action attribute name "
			"[some_var]\n");
		break;

	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory adding action attribute "
			"[some_var = \"some other value\"]\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while adding action "
			"attribute [some_var = \"some other value\"]\n",
			sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			"while adding action attribute [some_var = "
			"\"some other value\"]\n", keynote_errno);
		break;
	}
    }

    if (kn_add_action(sessionid, "another_var", "foo", 0) == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_SYNTAX:
		fprintf(stderr, "Invalid name action attribute name "
			"[another_var]\n");
		break;

	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory adding action attribute "
			"[another_var = \"foo\"]\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while adding action "
			"attribute [another_var = \"foo\"]\n", sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			"while adding action attribute [another_var = "
			"\"foo\"]\n", keynote_errno);
		break;
	}
    }

    /* Set the return values for this application -- just "false" and "true" */
    returnvalues[0] = "false";
    returnvalues[1] = "true";

    /* Just do the query. */
    j = kn_do_query(sessionid, returnvalues, NUM_RETURN_VALUES);
    if (j == -1)
    {
	switch (keynote_errno)
	{
	    case ERROR_MEMORY:
		fprintf(stderr, "Out of memory while performing authorization "
			"query.\n");
		break;

	    case ERROR_NOTFOUND:
		fprintf(stderr, "Session %d not found while performing "
			"authorization query.\n", sessionid);
		break;

	    default:
		fprintf(stderr, "Unspecified error %d (shouldn't happen) "
			"while performing authorization query.\n",
			keynote_errno);
		break;
	}
    }
    else
    {
	fprintf(stdout, "Return value is [%s]\n", returnvalues[j]);
    }

    /*
     * Once the query is done, we can find what assertions failed in what way.
     * One way is just going through the list of assertions, as shown here
     * for assertions that failed due to memory exhaustion.
     */
    j = 0;

    do
    {
	i = kn_get_failed(sessionid, KEYNOTE_ERROR_MEMORY, j++);
	if (i != -1)
	  fprintf(stderr, "Assertion %d failed due to memory exhaustion.\n",
		  i);
    } while (i != -1);

    /*
     * Another way is to go through the list of failed assertions by deleting
     * the "first" one.
     */
    do
    {
	i = kn_get_failed(sessionid, KEYNOTE_ERROR_SYNTAX, 0);
	if (i != -1)
	{
	    fprintf(stderr, "Assertion %d failed due to some syntax error.\n",
		    i);
	    kn_remove_assertion(sessionid, i);  /* Delete assertion */
	}
    } while (i != -1);

    /*
     * Signature failures, another way.
     */
    for (j = 0, i = kn_get_failed(sessionid, KEYNOTE_ERROR_SIGNATURE, 0);
	 i != -1; i = kn_get_failed(sessionid, KEYNOTE_ERROR_SIGNATURE, j++))
      fprintf(stderr, "Failed to verify signature on assertion %d.\n", i);

    /*
     * Here's how to find all errors.
     */
    for (i = kn_get_failed(sessionid, KEYNOTE_ERROR_ANY, 0); i != -1;
	 i = kn_get_failed(sessionid, KEYNOTE_ERROR_ANY, 0))
    {
	fprintf(stderr, "Unspecified error in processing assertion %d.\n", i);
	kn_remove_assertion(sessionid, i);
    }

    /* Destroy the session, freeing all allocated memory. */
    kn_close(sessionid);

    exit(0);
}
