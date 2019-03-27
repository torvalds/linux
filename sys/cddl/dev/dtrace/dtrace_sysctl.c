/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 *
 */

/* Report registered DTrace providers. */
static int
sysctl_dtrace_providers(SYSCTL_HANDLER_ARGS)
{
	char	*p_name	= NULL;
	dtrace_provider_t
		*prov	= dtrace_provider;
	int	error	= 0;
	size_t	len	= 0;

	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	/* Compute the length of the space-separated provider name string. */
	while (prov != NULL) {
		len += strlen(prov->dtpv_name) + 1;
		prov = prov->dtpv_next;
	}

	if ((p_name = kmem_alloc(len, KM_SLEEP)) == NULL)
		error = ENOMEM;
	else {
		/* Start with an empty string. */
		*p_name = '\0';

		/* Point to the first provider again. */
		prov = dtrace_provider;

		/* Loop through the providers, appending the names. */
		while (prov != NULL) {
			if (prov != dtrace_provider)
				(void) strlcat(p_name, " ", len);

			(void) strlcat(p_name, prov->dtpv_name, len);

			prov = prov->dtpv_next;
		}
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	if (p_name != NULL) {
		error = sysctl_handle_string(oidp, p_name, len, req);

		kmem_free(p_name, 0);
	}

	return (error);
}

SYSCTL_NODE(_debug, OID_AUTO, dtrace, CTLFLAG_RD, 0, "DTrace debug parameters");

SYSCTL_PROC(_debug_dtrace, OID_AUTO, providers, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, sysctl_dtrace_providers, "A", "available DTrace providers");

SYSCTL_NODE(_kern, OID_AUTO, dtrace, CTLFLAG_RD, 0, "DTrace parameters");

SYSCTL_INT(_kern_dtrace, OID_AUTO, err_verbose, CTLFLAG_RW,
    &dtrace_err_verbose, 0,
    "print DIF and DOF validation errors to the message buffer");

SYSCTL_INT(_kern_dtrace, OID_AUTO, memstr_max, CTLFLAG_RW, &dtrace_memstr_max,
    0, "largest allowed argument to memstr(), 0 indicates no limit");

SYSCTL_QUAD(_kern_dtrace, OID_AUTO, dof_maxsize, CTLFLAG_RW,
    &dtrace_dof_maxsize, 0, "largest allowed DOF table");

SYSCTL_QUAD(_kern_dtrace, OID_AUTO, helper_actions_max, CTLFLAG_RW,
    &dtrace_helper_actions_max, 0, "maximum number of allowed helper actions");

SYSCTL_INT(_security_bsd, OID_AUTO, allow_destructive_dtrace, CTLFLAG_RDTUN,
    &dtrace_allow_destructive, 1, "Allow destructive mode DTrace scripts");
