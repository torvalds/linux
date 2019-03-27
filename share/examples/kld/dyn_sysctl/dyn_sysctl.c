/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2000 Andrzej Bialecki <abial@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *     $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>


/* Some example data */
static long a = 100;
static int b = 200;
static char *c = "hi there from dyn_sysctl";
static struct sysctl_oid *a_root, *a_root1, *b_root;
static struct sysctl_ctx_list clist, clist1, clist2;

static int
sysctl_dyn_sysctl_test(SYSCTL_HANDLER_ARGS)
{
	char *buf = "let's produce some text...";
	
	return (sysctl_handle_string(oidp, buf, strlen(buf), req));
}

/*
 * The function called at load/unload.
 */
static int
load(module_t mod, int cmd, void *arg)
{
	int error;

	error = 0;
	switch (cmd) {
	case MOD_LOAD:
		/* Initialize the contexts */
		printf("Initializing contexts and creating subtrees.\n\n");
		sysctl_ctx_init(&clist);
		sysctl_ctx_init(&clist1);
		sysctl_ctx_init(&clist2);
		/*
		 * Create two partially overlapping subtrees, belonging
		 * to different contexts.
		 */
		printf("TREE		ROOT		  NAME\n");
		a_root = SYSCTL_ADD_ROOT_NODE(&clist,
			OID_AUTO, "dyn_sysctl", CTLFLAG_RW, 0,
			"dyn_sysctl root node");
		a_root = SYSCTL_ADD_ROOT_NODE(&clist1,
			OID_AUTO, "dyn_sysctl", CTLFLAG_RW, 0,
			"dyn_sysctl root node");
		if (a_root == NULL) {
			printf("SYSCTL_ADD_NODE failed!\n");
			return (EINVAL);
		}
		SYSCTL_ADD_LONG(&clist, SYSCTL_CHILDREN(a_root),
		    OID_AUTO, "long_a", CTLFLAG_RW, &a, "just to try");
		SYSCTL_ADD_INT(&clist, SYSCTL_CHILDREN(a_root),
		    OID_AUTO, "int_b", CTLFLAG_RW, &b, 0, "just to try 1");
		a_root1 = SYSCTL_ADD_NODE(&clist, SYSCTL_CHILDREN(a_root),
		    OID_AUTO, "nextlevel", CTLFLAG_RD, 0, "one level down");
		SYSCTL_ADD_STRING(&clist, SYSCTL_CHILDREN(a_root1),
		    OID_AUTO, "string_c", CTLFLAG_RD, c, 0, "just to try 2");
		printf("1. (%p)	/		  dyn_sysctl\n", &clist);

		/* Add a subtree under already existing category */
		a_root1 = SYSCTL_ADD_NODE(&clist, SYSCTL_STATIC_CHILDREN(_kern),
		    OID_AUTO, "dyn_sysctl", CTLFLAG_RW, 0, "dyn_sysctl root node");
		if (a_root1 == NULL) {
			printf("SYSCTL_ADD_NODE failed!\n");
			return (EINVAL);
		}
		SYSCTL_ADD_PROC(&clist, SYSCTL_CHILDREN(a_root1),
		    OID_AUTO, "procedure", CTLTYPE_STRING | CTLFLAG_RD,
		    NULL, 0, sysctl_dyn_sysctl_test, "A",
		    "I can be here, too");
		printf("   (%p)	/kern		  dyn_sysctl\n", &clist);

		/* Overlap second tree with the first. */
		b_root = SYSCTL_ADD_NODE(&clist1, SYSCTL_CHILDREN(a_root),
		    OID_AUTO, "nextlevel", CTLFLAG_RD, 0, "one level down");
		SYSCTL_ADD_STRING(&clist1, SYSCTL_CHILDREN(b_root),
		    OID_AUTO, "string_c1", CTLFLAG_RD, c, 0, "just to try 2");
		printf("2. (%p)	/		  dyn_sysctl	(overlapping #1)\n", &clist1);

		/*
		 * And now do something stupid. Connect another subtree to
		 * dynamic oid.
		 * WARNING: this is an example of WRONG use of dynamic sysctls.
		 */
		b_root=SYSCTL_ADD_NODE(&clist2, SYSCTL_CHILDREN(a_root1),
		    OID_AUTO, "bad", CTLFLAG_RW, 0, "dependent node");
		SYSCTL_ADD_STRING(&clist2, SYSCTL_CHILDREN(b_root),
		    OID_AUTO, "string_c", CTLFLAG_RD, c, 0, "shouldn't panic");
		printf("3. (%p)	/kern/dyn_sysctl  bad		(WRONG!)\n", &clist2);
		break;
	case MOD_UNLOAD:
		printf("1. Try to free ctx1 (%p): ", &clist);
		if (sysctl_ctx_free(&clist) != 0)
			printf("failed: expected. Need to remove ctx3 first.\n");
		else
			printf("HELP! sysctl_ctx_free(%p) succeeded. EXPECT PANIC!!!\n", &clist);
		printf("2. Try to free ctx3 (%p): ", &clist2);
		if (sysctl_ctx_free(&clist2) != 0) {
			printf("sysctl_ctx_free(%p) failed!\n", &clist2);
			/* Remove subtree forcefully... */
			sysctl_remove_oid(b_root, 1, 1);
			printf("sysctl_remove_oid(%p) succeeded\n", b_root);
		} else
			printf("Ok\n");
		printf("3. Try to free ctx1 (%p) again: ", &clist);
		if (sysctl_ctx_free(&clist) != 0) {
			printf("sysctl_ctx_free(%p) failed!\n", &clist);
			/* Remove subtree forcefully... */
			sysctl_remove_oid(a_root1, 1, 1);
			printf("sysctl_remove_oid(%p) succeeded\n", a_root1);
		} else
			printf("Ok\n");
		printf("4. Try to free ctx2 (%p): ", &clist1);
		if (sysctl_ctx_free(&clist1) != 0) {
			printf("sysctl_ctx_free(%p) failed!\n", &clist1);
			/* Remove subtree forcefully... */
			sysctl_remove_oid(a_root, 1, 1);
		} else
			printf("Ok\n");
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t mod_data = {
	"dyn_sysctl",
	load,
	0
};

DECLARE_MODULE(dyn_sysctl, mod_data, SI_SUB_EXEC, SI_ORDER_ANY);
