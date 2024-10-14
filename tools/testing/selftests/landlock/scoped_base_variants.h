/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Landlock scoped_domains variants
 *
 * See the hierarchy variants from ptrace_test.c
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2024 Tahera Fahimi <fahimitahera@gmail.com>
 */

/* clang-format on */
FIXTURE_VARIANT(scoped_domains)
{
	bool domain_both;
	bool domain_parent;
	bool domain_child;
};

/*
 *        No domain
 *
 *   P1-.               P1 -> P2 : allow
 *       \              P2 -> P1 : allow
 *        'P2
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, without_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = false,
	.domain_child = false,
};

/*
 *        Child domain
 *
 *   P1--.              P1 -> P2 : allow
 *        \             P2 -> P1 : deny
 *        .'-----.
 *        |  P2  |
 *        '------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, child_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = false,
	.domain_child = true,
};

/*
 *        Parent domain
 * .------.
 * |  P1  --.           P1 -> P2 : deny
 * '------'  \          P2 -> P1 : allow
 *            '
 *            P2
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, parent_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = true,
	.domain_child = false,
};

/*
 *        Parent + child domain (siblings)
 * .------.
 * |  P1  ---.          P1 -> P2 : deny
 * '------'   \         P2 -> P1 : deny
 *         .---'--.
 *         |  P2  |
 *         '------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, sibling_domain) {
	/* clang-format on */
	.domain_both = false,
	.domain_parent = true,
	.domain_child = true,
};

/*
 *         Same domain (inherited)
 * .-------------.
 * | P1----.     |      P1 -> P2 : allow
 * |        \    |      P2 -> P1 : allow
 * |         '   |
 * |         P2  |
 * '-------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, inherited_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = false,
	.domain_child = false,
};

/*
 *         Inherited + child domain
 * .-----------------.
 * |  P1----.        |  P1 -> P2 : allow
 * |         \       |  P2 -> P1 : deny
 * |        .-'----. |
 * |        |  P2  | |
 * |        '------' |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, nested_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = false,
	.domain_child = true,
};

/*
 *         Inherited + parent domain
 * .-----------------.
 * |.------.         |  P1 -> P2 : deny
 * ||  P1  ----.     |  P2 -> P1 : allow
 * |'------'    \    |
 * |             '   |
 * |             P2  |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, nested_and_parent_domain) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = true,
	.domain_child = false,
};

/*
 *         Inherited + parent and child domain (siblings)
 * .-----------------.
 * | .------.        |  P1 -> P2 : deny
 * | |  P1  .        |  P2 -> P1 : deny
 * | '------'\       |
 * |          \      |
 * |        .--'---. |
 * |        |  P2  | |
 * |        '------' |
 * '-----------------'
 */
/* clang-format off */
FIXTURE_VARIANT_ADD(scoped_domains, forked_domains) {
	/* clang-format on */
	.domain_both = true,
	.domain_parent = true,
	.domain_child = true,
};
