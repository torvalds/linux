/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Pengutronix, Oleksij Rempel <kernel@pengutronix.de> */

#ifndef __DSCP_H__
#define __DSCP_H__

/*
 * DSCP Pools and Codepoint Space Division:
 *
 * The Differentiated Services (Diffserv) architecture defines a method for
 * classifying and managing network traffic using the DS field in IPv4 and IPv6
 * packet headers. This field can carry one of 64 distinct DSCP (Differentiated
 * Services Code Point) values, which are divided into three pools based on
 * their Least Significant Bits (LSB) patterns and intended usage. Each pool has
 * a specific registration procedure for assigning DSCP values:
 *
 * Pool 1 (Standards Action Pool):
 * - Codepoint Space: xxxxx0
 *   This pool includes DSCP values ending in '0' (binary), allocated via
 *   Standards Action. It is intended for globally recognized traffic classes,
 *   ensuring interoperability across the internet. This pool encompasses
 *   well-known DSCP values such as CS0-CS7, AFxx, EF, and VOICE-ADMIT.
 *
 * Pool 2 (Experimental/Local Use Pool):
 * - Codepoint Space: xxxx11
 *   Reserved for DSCP values ending in '11' (binary), this pool is designated
 *   for Experimental or Local Use. It allows for private or temporary traffic
 *   marking schemes not intended for standardized global use, facilitating
 *   testing and network-specific configurations without impacting
 *   interoperability.
 *
 * Pool 3 (Preferential Standardization Pool):
 * - Codepoint Space: xxxx01
 *   Initially reserved for experimental or local use, this pool now serves as
 *   a secondary standardization resource should Pool 1 become exhausted. DSCP
 *   values ending in '01' (binary) are assigned via Standards Action, with a
 *   focus on adopting new, standardized traffic classes as the need arises.
 *
 * For pool updates see:
 * https://www.iana.org/assignments/dscp-registry/dscp-registry.xhtml
 */

/* Pool 1: Standardized DSCP values as per [RFC8126] */
#define DSCP_CS0 0		/* 000000, [RFC2474] */
/* CS0 is some times called default (DF) */
#define DSCP_DF 0		/* 000000, [RFC2474] */
#define DSCP_CS1 8		/* 001000, [RFC2474] */
#define DSCP_CS2 16		/* 010000, [RFC2474] */
#define DSCP_CS3 24		/* 011000, [RFC2474] */
#define DSCP_CS4 32		/* 100000, [RFC2474] */
#define DSCP_CS5 40		/* 101000, [RFC2474] */
#define DSCP_CS6 48		/* 110000, [RFC2474] */
#define DSCP_CS7 56		/* 111000, [RFC2474] */
#define DSCP_AF11 10		/* 001010, [RFC2597] */
#define DSCP_AF12 12		/* 001100, [RFC2597] */
#define DSCP_AF13 14		/* 001110, [RFC2597] */
#define DSCP_AF21 18		/* 010010, [RFC2597] */
#define DSCP_AF22 20		/* 010100, [RFC2597] */
#define DSCP_AF23 22		/* 010110, [RFC2597] */
#define DSCP_AF31 26		/* 011010, [RFC2597] */
#define DSCP_AF32 28		/* 011100, [RFC2597] */
#define DSCP_AF33 30		/* 011110, [RFC2597] */
#define DSCP_AF41 34		/* 100010, [RFC2597] */
#define DSCP_AF42 36		/* 100100, [RFC2597] */
#define DSCP_AF43 38		/* 100110, [RFC2597] */
#define DSCP_EF 46		/* 101110, [RFC3246] */
#define DSCP_VOICE_ADMIT 44	/* 101100, [RFC5865] */

/* Pool 3: Standardized assignments, previously available for experimental/local
 * use
 */
#define DSCP_LE 1		/* 000001, [RFC8622] */

#define DSCP_MAX 64

#endif /* __DSCP_H__ */
