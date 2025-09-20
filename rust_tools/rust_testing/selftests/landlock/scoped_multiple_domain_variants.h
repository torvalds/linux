/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock variants for three processes with various domains.
 *
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

enum sandbox_type {
	NO_SANDBOX,
	SCOPE_SANDBOX,
	/* Any other type of sandboxing domain */
	OTHER_SANDBOX,
};

/* clang-format on */
FIXTURE_VARIANT(scoped_vs_unscoped)
{
	const int domain_all;
	const int domain_parent;
	const int domain_children;
	const int domain_child;
	const int domain_grand_child;
};

/*
 * .-----------------.
 * |         ####### |  P3 -> P2 : allow
 * |   P1----# P2  # |  P3 -> P1 : deny
 * |         #  |  # |
 * |         # P3  # |
 * |         ####### |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, deny_scoped) {
	.domain_all = OTHER_SANDBOX,
	.domain_parent = NO_SANDBOX,
	.domain_children = SCOPE_SANDBOX,
	.domain_child = NO_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 * ###################
 * #         ####### #  P3 -> P2 : allow
 * #   P1----# P2  # #  P3 -> P1 : deny
 * #         #  |  # #
 * #         # P3  # #
 * #         ####### #
 * ###################
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, all_scoped) {
	.domain_all = SCOPE_SANDBOX,
	.domain_parent = NO_SANDBOX,
	.domain_children = SCOPE_SANDBOX,
	.domain_child = NO_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 * .-----------------.
 * |         .-----. |  P3 -> P2 : allow
 * |   P1----| P2  | |  P3 -> P1 : allow
 * |         |     | |
 * |         | P3  | |
 * |         '-----' |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, allow_with_other_domain) {
	.domain_all = OTHER_SANDBOX,
	.domain_parent = NO_SANDBOX,
	.domain_children = OTHER_SANDBOX,
	.domain_child = NO_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 *  .----.    ######   P3 -> P2 : allow
 *  | P1 |----# P2 #   P3 -> P1 : allow
 *  '----'    ######
 *              |
 *              P3
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, allow_with_one_domain) {
	.domain_all = NO_SANDBOX,
	.domain_parent = OTHER_SANDBOX,
	.domain_children = NO_SANDBOX,
	.domain_child = SCOPE_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 *  ######    .-----.   P3 -> P2 : allow
 *  # P1 #----| P2  |   P3 -> P1 : allow
 *  ######    '-----'
 *              |
 *              P3
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, allow_with_grand_parent_scoped) {
	.domain_all = NO_SANDBOX,
	.domain_parent = SCOPE_SANDBOX,
	.domain_children = NO_SANDBOX,
	.domain_child = OTHER_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 *  ######    ######   P3 -> P2 : allow
 *  # P1 #----# P2 #   P3 -> P1 : allow
 *  ######    ######
 *               |
 *             .----.
 *             | P3 |
 *             '----'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, allow_with_parents_domain) {
	.domain_all = NO_SANDBOX,
	.domain_parent = SCOPE_SANDBOX,
	.domain_children = NO_SANDBOX,
	.domain_child = SCOPE_SANDBOX,
	.domain_grand_child = NO_SANDBOX,
	/* clang-format on */
};

/*
 *  ######		P3 -> P2 : deny
 *  # P1 #----P2	P3 -> P1 : deny
 *  ######     |
 *	       |
 *	     ######
 *           # P3 #
 *           ######
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_vs_unscoped, deny_with_self_and_grandparent_domain) {
	.domain_all = NO_SANDBOX,
	.domain_parent = SCOPE_SANDBOX,
	.domain_children = NO_SANDBOX,
	.domain_child = NO_SANDBOX,
	.domain_grand_child = SCOPE_SANDBOX,
	/* clang-format on */
};
