# $FreeBSD$
# $OpenBSD: pf.os,v 1.27 2016/09/03 17:08:57 sthen Exp $
# passive OS fingerprinting
# -------------------------
#
# SYN signatures. Those signatures work for SYN packets only (duh!).
#
# (C) Copyright 2000-2003 by Michal Zalewski <lcamtuf@coredump.cx>
# (C) Copyright 2003 by Mike Frantzen <frantzen@w4g.org>
#
#  Permission to use, copy, modify, and distribute this software for any
#  purpose with or without fee is hereby granted, provided that the above
#  copyright notice and this permission notice appear in all copies.
#
#  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
# This fingerprint database is adapted from Michal Zalewski's p0f passive
# operating system package.  The last database sync was from a Nov 3 2003
# p0f.fp.
#
#
# Each line in this file specifies a single fingerprint. Please read the
# information below carefully before attempting to append any signatures
# reported as UNKNOWN to this file to avoid mistakes.
#
# We use the following set metrics for fingerprinting:
#
# - Window size (WSS) - a highly OS dependent setting used for TCP/IP
#   performance control (max. amount of data to be sent without ACK).
#   Some systems use a fixed value for initial packets. On other
#   systems, it is a multiple of MSS or MTU (MSS+40). In some rare
#   cases, the value is just arbitrary.
#
#   NEW SIGNATURE: if p0f reported a special value of 'Snn', the number
#   appears to be a multiple of MSS (MSS*nn); a special value of 'Tnn'
#   means it is a multiple of MTU ((MSS+40)*nn). Unless you notice the
#   value of nn is not fixed (unlikely), just copy the Snn or Tnn token
#   literally. If you know this device has a simple stack and a fixed
#   MTU, you can however multiply S value by MSS, or T value by MSS+40,
#   and put it instead of Snn or Tnn.
#
#   If WSS otherwise looks like a fixed value (for example a multiple
#   of two), or if you can confirm the value is fixed, please quote
#   it literally. If there's no apparent pattern in WSS chosen, you
#   should consider wildcarding this value.
#
# - Overall packet size - a function of all IP and TCP options and bugs.
#
#   NEW SIGNATURE: Copy this value literally.
#
# - Initial TTL - We check the actual TTL of a received packet. It can't
#   be higher than the initial TTL, and also shouldn't be dramatically
#   lower (maximum distance is defined as 40 hops).
#
#   NEW SIGNATURE: *Never* copy TTL from a p0f-reported signature literally.
#   You need to determine the initial TTL. The best way to do it is to
#   check the documentation for a remote system, or check its settings.
#   A fairly good method is to simply round the observed TTL up to
#   32, 64, 128, or 255, but it should be noted that some obscure devices
#   might not use round TTLs (in particular, some shoddy appliances use
#   "original" initial TTL settings). If not sure, you can see how many
#   hops you're away from the remote party with traceroute or mtr.
#
# - Don't fragment flag (DF) - some modern OSes set this to implement PMTU
#   discovery. Others do not bother.
#
#   NEW SIGNATURE: Copy this value literally.
#
# - Maximum segment size (MSS) - this setting is usually link-dependent. P0f
#   uses it to determine link type of the remote host.
#
#   NEW SIGNATURE: Always wildcard this value, except for rare cases when
#   you have an appliance with a fixed value, know the system supports only
#   a very limited number of network interface types, or know the system
#   is using a value it pulled out of nowhere.  Specific unique MSS
#   can be used to tell Google crawlbots from the rest of the population.
#
# - Window scaling (WSCALE) - this feature is used to scale WSS.
#   It extends the size of a TCP/IP window to 32 bits. Some modern
#   systems implement this feature.
#
#   NEW SIGNATURE: Observe several signatures. Initial WSCALE is often set
#   to zero or other low value. There's usually no need to wildcard this
#   parameter.
#
# - Timestamp - some systems that implement timestamps set them to
#   zero in the initial SYN. This case is detected and handled appropriately.
#
# - Selective ACK permitted - a flag set by systems that implement
#   selective ACK functionality.
#
# - The sequence of TCP all options (MSS, window scaling, selective ACK
#   permitted, timestamp, NOP). Other than the options previously
#   discussed, p0f also checks for timestamp option (a silly
#   extension to broadcast your uptime ;-), NOP options (used for
#   header padding) and sackOK option (selective ACK feature).
#
#   NEW SIGNATURE: Copy the sequence literally.
#
# To wildcard any value (except for initial TTL or TCP options), replace
# it with '*'. You can also use a modulo operator to match any values
# that divide by nnn - '%nnn'.
#
# Fingerprint entry format:
#
# wwww:ttt:D:ss:OOO...:OS:Version:Subtype:Details
#
# wwww     - window size (can be *, %nnn, Snn or Tnn).  The special values
#            "S" and "T" which are a multiple of MSS or a multiple of MTU
#            respectively.
# ttt      - initial TTL
# D        - don't fragment bit (0 - not set, 1 - set)
# ss       - overall SYN packet size
# OOO      - option value and order specification (see below)
# OS       - OS genre (Linux, Solaris, Windows)
# Version  - OS Version (2.0.27 on x86, etc)
# Subtype  - OS subtype or patchlevel (SP3, lo0)
# details  - Generic OS details
#
# If OS genre starts with '*', p0f will not show distance, link type
# and timestamp data. It is useful for userland TCP/IP stacks of
# network scanners and so on, where many settings are randomized or
# bogus.
#
# If OS genre starts with @, it denotes an approximate hit for a group
# of operating systems (signature reporting still enabled in this case).
# Use this feature at the end of this file to catch cases for which
# you don't have a precise match, but can tell it's Windows or FreeBSD
# or whatnot by looking at, say, flag layout alone.
#
# Option block description is a list of comma or space separated
# options in the order they appear in the packet:
#
# N	   - NOP option
# Wnnn	   - window scaling option, value nnn (or * or %nnn)
# Mnnn	   - maximum segment size option, value nnn (or * or %nnn)
# S	   - selective ACK OK
# T	   - timestamp
# T0	   - timestamp with a zero value
#
# To denote no TCP options, use a single '.'.
#
# Please report any additions to this file, or any inaccuracies or
# problems spotted, to the maintainers: lcamtuf@coredump.cx,
# frantzen@openbsd.org and bugs@openbsd.org with a tcpdump packet
# capture of the relevant SYN packet(s)
#
# A test and submission page is available at
# http://lcamtuf.coredump.cx/p0f-help/
#
#
# WARNING WARNING WARNING
# -----------------------
#
# Do not add a system X as OS Y just because NMAP says so. It is often
# the case that X is a NAT firewall. While nmap is talking to the
# device itself, p0f is fingerprinting the guy behind the firewall
# instead.
#
# When in doubt, use common sense, don't add something that looks like
# a completely different system as Linux or FreeBSD or LinkSys router.
# Check DNS name, establish a connection to the remote host and look
# at SYN+ACK - does it look similar?
#
# Some users tweak their TCP/IP settings - enable or disable RFC1323
# functionality, enable or disable timestamps or selective ACK,
# disable PMTU discovery, change MTU and so on. Always compare a new rule
# to other fingerprints for this system, and verify the system isn't
# "customized" before adding it. It is OK to add signature variants
# caused by a commonly used software (personal firewalls, security
# packages, etc), but it makes no sense to try to add every single
# possible /proc/sys/net/ipv4 tweak on Linux or so.
#
# KEEP IN MIND: Some packet firewalls configured to normalize outgoing
# traffic (OpenBSD pf with "scrub" enabled, for example) will, well,
# normalize packets. Signatures will not correspond to the originating
# system (and probably not quite to the firewall either).
#
# NOTE: Try to keep this file in some reasonable order, from most to
# least likely systems. This will speed up operation. Also keep most
# generic and broad rules near the end.
#

##########################
# Standard OS signatures #
##########################

# ----------------- AIX ---------------------

# AIX is first because its signatures are close to NetBSD, MacOS X and
# Linux 2.0, but it uses a fairly rare MSSes, at least sometimes...
# This is a shoddy hack, though.

45046:64:0:44:M*:		AIX:4.3::AIX 4.3
16384:64:0:44:M512:		AIX:4.3:2-3:AIX 4.3.2 and earlier

16384:64:0:60:M512,N,W%2,N,N,T:		AIX:4.3:3:AIX 4.3.3-5.2
16384:64:0:60:M512,N,W%2,N,N,T:		AIX:5.1-5.2::AIX 4.3.3-5.2
32768:64:0:60:M512,N,W%2,N,N,T:		AIX:4.3:3:AIX 4.3.3-5.2
32768:64:0:60:M512,N,W%2,N,N,T:		AIX:5.1-5.2::AIX 4.3.3-5.2
65535:64:0:60:M512,N,W%2,N,N,T:		AIX:4.3:3:AIX 4.3.3-5.2
65535:64:0:60:M512,N,W%2,N,N,T:		AIX:5.1-5.2::AIX 4.3.3-5.2
65535:64:0:64:M*,N,W1,N,N,T,N,N,S:	AIX:5.3:ML1:AIX 5.3 ML1

# ----------------- Linux -------------------

# S1:64:0:44:M*:A:		Linux:1.2::Linux 1.2.x (XXX quirks support)
512:64:0:44:M*:			Linux:2.0:3x:Linux 2.0.3x
16384:64:0:44:M*:		Linux:2.0:3x:Linux 2.0.3x

# Endian snafu! Nelson says "ha-ha":
2:64:0:44:M*:			Linux:2.0:3x:Linux 2.0.3x (MkLinux) on Mac
64:64:0:44:M*:			Linux:2.0:3x:Linux 2.0.3x (MkLinux) on Mac


S4:64:1:60:M1360,S,T,N,W0:	Linux:google::Linux (Google crawlbot)

S2:64:1:60:M*,S,T,N,W0:		Linux:2.4::Linux 2.4 (big boy)
S3:64:1:60:M*,S,T,N,W0:		Linux:2.4:.18-21:Linux 2.4.18 and newer
S4:64:1:60:M*,S,T,N,W0:		Linux:2.4::Linux 2.4/2.6 <= 2.6.7
S4:64:1:60:M*,S,T,N,W0:		Linux:2.6:.1-7:Linux 2.4/2.6 <= 2.6.7

S4:64:1:60:M*,S,T,N,W5:		Linux:2.6::Linux 2.6 (newer, 1)
S4:64:1:60:M*,S,T,N,W6:		Linux:2.6::Linux 2.6 (newer, 2)
S4:64:1:60:M*,S,T,N,W7:		Linux:2.6::Linux 2.6 (newer, 3)
T4:64:1:60:M*,S,T,N,W7:		Linux:2.6::Linux 2.6 (newer, 4)

S10:64:1:60:M*,S,T,N,W4:	Linux:3.0::Linux 3.0

S3:64:1:60:M*,S,T,N,W1:		Linux:2.5::Linux 2.5 (sometimes 2.4)
S4:64:1:60:M*,S,T,N,W1:		Linux:2.5-2.6::Linux 2.5/2.6
S3:64:1:60:M*,S,T,N,W2:		Linux:2.5::Linux 2.5 (sometimes 2.4)
S4:64:1:60:M*,S,T,N,W2:		Linux:2.5::Linux 2.5 (sometimes 2.4)

S20:64:1:60:M*,S,T,N,W0:	Linux:2.2:20-25:Linux 2.2.20 and newer
S22:64:1:60:M*,S,T,N,W0:	Linux:2.2::Linux 2.2
S11:64:1:60:M*,S,T,N,W0:	Linux:2.2::Linux 2.2

# Popular cluster config scripts disable timestamps and
# selective ACK:
S4:64:1:48:M1460,N,W0:		Linux:2.4:cluster:Linux 2.4 in cluster

# This needs to be investigated. On some systems, WSS
# is selected as a multiple of MTU instead of MSS. I got
# many submissions for this for many late versions of 2.4:
T4:64:1:60:M1412,S,T,N,W0:	Linux:2.4::Linux 2.4 (late, uncommon)

# This happens only over loopback, but let's make folks happy:
32767:64:1:60:M16396,S,T,N,W0:	Linux:2.4:lo0:Linux 2.4 (local)
S8:64:1:60:M3884,S,T,N,W0:	Linux:2.2:lo0:Linux 2.2 (local)

# Opera visitors:
16384:64:1:60:M*,S,T,N,W0:	Linux:2.2:Opera:Linux 2.2 (Opera?)
32767:64:1:60:M*,S,T,N,W0:	Linux:2.4:Opera:Linux 2.4 (Opera?)

# Some fairly common mods:
S4:64:1:52:M*,N,N,S,N,W0:	Linux:2.4:ts:Linux 2.4 w/o timestamps
S22:64:1:52:M*,N,N,S,N,W0:	Linux:2.2:ts:Linux 2.2 w/o timestamps


# ----------------- FreeBSD -----------------

16384:64:1:44:M*:		FreeBSD:2.0-2.2::FreeBSD 2.0-4.2
16384:64:1:44:M*:		FreeBSD:3.0-3.5::FreeBSD 2.0-4.2
16384:64:1:44:M*:		FreeBSD:4.0-4.2::FreeBSD 2.0-4.2
16384:64:1:60:M*,N,W0,N,N,T:	FreeBSD:4.4::FreeBSD 4.4

1024:64:1:60:M*,N,W0,N,N,T:	FreeBSD:4.4::FreeBSD 4.4

57344:64:1:44:M*:		FreeBSD:4.6-4.8:noRFC1323:FreeBSD 4.6-4.8 (no RFC1323)
57344:64:1:60:M*,N,W0,N,N,T:	FreeBSD:4.6-4.9::FreeBSD 4.6-4.9

32768:64:1:60:M*,N,W0,N,N,T:	FreeBSD:4.8-4.11::FreeBSD 4.8-5.1 (or MacOS X)
32768:64:1:60:M*,N,W0,N,N,T:	FreeBSD:5.0-5.1::FreeBSD 4.8-5.1 (or MacOS X)
65535:64:1:60:M*,N,W0,N,N,T:	FreeBSD:4.8-4.11::FreeBSD 4.8-5.2 (or MacOS X)
65535:64:1:60:M*,N,W0,N,N,T:	FreeBSD:5.0-5.2::FreeBSD 4.8-5.2 (or MacOS X)
65535:64:1:60:M*,N,W1,N,N,T:	FreeBSD:4.7-4.11::FreeBSD 4.7-5.2
65535:64:1:60:M*,N,W1,N,N,T:	FreeBSD:5.0-5.2::FreeBSD 4.7-5.2

# XXX need quirks support
# 65535:64:1:60:M*,N,W0,N,N,T:Z:FreeBSD:5.1-5.4::5.1-current (1)
# 65535:64:1:60:M*,N,W1,N,N,T:Z:FreeBSD:5.1-5.4::5.1-current (2)
# 65535:64:1:60:M*,N,W2,N,N,T:Z:FreeBSD:5.1-5.4::5.1-current (3)
# 65535:64:1:44:M*:Z:FreeBSD:5.2::FreeBSD 5.2 (no RFC1323)

# 16384:64:1:60:M*,N,N,N,N,N,N,T:FreeBSD:4.4:noTS:FreeBSD 4.4 (w/o timestamps)

# ----------------- NetBSD ------------------

16384:64:0:60:M*,N,W0,N,N,T:	NetBSD:1.3::NetBSD 1.3
65535:64:0:60:M*,N,W0,N,N,T0:	NetBSD:1.6:opera:NetBSD 1.6 (Opera)
16384:64:0:60:M*,N,W0,N,N,T0:	NetBSD:1.6::NetBSD 1.6
16384:64:1:60:M*,N,W0,N,N,T0:	NetBSD:1.6:df:NetBSD 1.6 (DF)
65535:64:1:60:M*,N,W1,N,N,T0:	NetBSD:1.6::NetBSD 1.6W-current (DF)
65535:64:1:60:M*,N,W0,N,N,T0:	NetBSD:1.6::NetBSD 1.6X (DF)
32768:64:1:60:M*,N,W0,N,N,T0:	NetBSD:1.6:randomization:NetBSD 1.6ZH-current (w/ ip_id randomization)

# ----------------- OpenBSD -----------------

16384:64:0:60:M*,N,W0,N,N,T:		OpenBSD:2.6::NetBSD 1.3 (or OpenBSD 2.6)
16384:64:1:64:M*,N,N,S,N,W0,N,N,T:	OpenBSD:3.0-4.8::OpenBSD 3.0-4.8
16384:64:0:64:M*,N,N,S,N,W0,N,N,T:	OpenBSD:3.0-4.8:no-df:OpenBSD 3.0-4.8 (scrub no-df)
57344:64:1:64:M*,N,N,S,N,W0,N,N,T:	OpenBSD:3.3-4.0::OpenBSD 3.3-4.0
57344:64:0:64:M*,N,N,S,N,W0,N,N,T:	OpenBSD:3.3-4.0:no-df:OpenBSD 3.3-4.0 (scrub no-df)

65535:64:1:64:M*,N,N,S,N,W0,N,N,T:	OpenBSD:3.0-4.0:opera:OpenBSD 3.0-4.0 (Opera)

16384:64:1:64:M*,N,N,S,N,W3,N,N,T:	OpenBSD:4.9::OpenBSD 4.9
16384:64:0:64:M*,N,N,S,N,W3,N,N,T:	OpenBSD:4.9:no-df:OpenBSD 4.9 (scrub no-df)

16384:64:1:64:M*,N,N,S,N,W6,N,N,T:      OpenBSD:6.1::OpenBSD 6.1
16384:64:0:64:M*,N,N,S,N,W6,N,N,T:      OpenBSD:6.1:no-df:OpenBSD 6.1 (scrub no-df)

# ----------------- DragonFly BSD -----------------

57344:64:1:60:M*,N,W0,N,N,T:		DragonFly:1.0:A:DragonFly 1.0A
57344:64:0:64:M*,N,W0,N,N,S,N,N,T:	DragonFly:1.2-1.12::DragonFly 1.2-1.12
5840:64:1:60:M*,S,T,N,W4:			DragonFly:2.0-2.1::DragonFly 2.0-2.1
57344:64:0:64:M*,N,W0,N,N,S,N,N,T:	DragonFly:2.2-2.3::DragonFly 2.2-2.3
57344:64:0:64:M*,N,W5,N,N,S,N,N,T:	DragonFly:2.4-2.7::DragonFly 2.4-2.7

# ----------------- Solaris -----------------

S17:64:1:64:N,W3,N,N,T0,N,N,S,M*:	Solaris:8:RFC1323:Solaris 8 RFC1323
S17:64:1:48:N,N,S,M*:			Solaris:8::Solaris 8
S17:255:1:44:M*:			Solaris:2.5-2.7::Solaris 2.5 to 7

S6:255:1:44:M*:				Solaris:2.6-2.7::Solaris 2.6 to 7
S23:255:1:44:M*:			Solaris:2.5:1:Solaris 2.5.1
S34:64:1:48:M*,N,N,S:			Solaris:2.9::Solaris 9
S44:255:1:44:M*:			Solaris:2.7::Solaris 7

4096:64:0:44:M1460:			SunOS:4.1::SunOS 4.1.x

S34:64:1:52:M*,N,W0,N,N,S:		Solaris:10:beta:Solaris 10 (beta)
32850:64:1:64:M*,N,N,T,N,W1,N,N,S:	Solaris:10::Solaris 10 1203

# ----------------- IRIX --------------------

49152:64:0:44:M*:			IRIX:6.4::IRIX 6.4
61440:64:0:44:M*:			IRIX:6.2-6.5::IRIX 6.2-6.5
49152:64:0:52:M*,N,W2,N,N,S:		IRIX:6.5:RFC1323:IRIX 6.5 (RFC1323)
49152:64:0:52:M*,N,W3,N,N,S:		IRIX:6.5:RFC1323:IRIX 6.5 (RFC1323)

61440:64:0:48:M*,N,N,S:			IRIX:6.5:12-21:IRIX 6.5.12 - 6.5.21
49152:64:0:48:M*,N,N,S:			IRIX:6.5:15-21:IRIX 6.5.15 - 6.5.21

49152:60:0:64:M*,N,W2,N,N,T,N,N,S:	IRIX:6.5:IP27:IRIX 6.5 IP27


# ----------------- Tru64 -------------------

32768:64:1:48:M*,N,W0:			Tru64:4.0::Tru64 4.0 (or OS/2 Warp 4)
32768:64:0:48:M*,N,W0:			Tru64:5.0::Tru64 5.0
8192:64:0:44:M1460:			Tru64:5.1:noRFC1323:Tru64 6.1 (no RFC1323) (or QNX 6)
61440:64:0:48:M*,N,W0:			Tru64:5.1a:JP4:Tru64 v5.1a JP4 (or OpenVMS 7.x on Compaq 5.x stack)

# ----------------- OpenVMS -----------------

6144:64:1:60:M*,N,W0,N,N,T:		OpenVMS:7.2::OpenVMS 7.2 (Multinet 4.4 stack)

# ----------------- MacOS -------------------

# XXX Need EOL tcp opt support
# S2:255:1:48:M*,W0,E:.:MacOS:8.6 classic

# XXX some of these use EOL too
16616:255:1:48:M*,W0:			MacOS:7.3-7.6:OTTCP:MacOS 7.3-8.6 (OTTCP)
16616:255:1:48:M*,W0:			MacOS:8.0-8.6:OTTCP:MacOS 7.3-8.6 (OTTCP)
16616:255:1:48:M*,N,N,N:		MacOS:8.1-8.6:OTTCP:MacOS 8.1-8.6 (OTTCP)
32768:255:1:48:M*,W0,N:			MacOS:9.0-9.2::MacOS 9.0-9.2
65535:255:1:48:M*,N,N,N,N:		MacOS:9.1::MacOS 9.1 (OT 2.7.4)


# ----------------- Windows -----------------

# Windows TCP/IP stack is a mess. For most recent XP, 2000 and
# even 98, the patchlevel, not the actual OS version, is more
# relevant to the signature. They share the same code, so it would
# seem. Luckily for us, almost all Windows 9x boxes have an
# awkward MSS of 536, which I use to tell one from another
# in most difficult cases.

8192:32:1:44:M*:			Windows:3.11::Windows 3.11 (Tucows)
S44:64:1:64:M*,N,W0,N,N,T0,N,N,S:	Windows:95::Windows 95
8192:128:1:64:M*,N,W0,N,N,T0,N,N,S:	Windows:95:b:Windows 95b

# There were so many tweaking tools and so many stack versions for
# Windows 98 it is no longer possible to tell them from each other
# without some very serious research. Until then, there's an insane
# number of signatures, for your amusement:

S44:32:1:48:M*,N,N,S:			Windows:98:lowTTL:Windows 98 (low TTL)
8192:32:1:48:M*,N,N,S:			Windows:98:lowTTL:Windows 98 (low TTL)
%8192:64:1:48:M536,N,N,S:		Windows:98::Windows 98
%8192:128:1:48:M536,N,N,S:		Windows:98::Windows 98
S4:64:1:48:M*,N,N,S:			Windows:98::Windows 98
S6:64:1:48:M*,N,N,S:			Windows:98::Windows 98
S12:64:1:48:M*,N,N,S:			Windows:98::Windows 98
T30:64:1:64:M1460,N,W0,N,N,T0,N,N,S:	Windows:98::Windows 98
32767:64:1:48:M*,N,N,S:			Windows:98::Windows 98
37300:64:1:48:M*,N,N,S:			Windows:98::Windows 98
46080:64:1:52:M*,N,W3,N,N,S:		Windows:98:RFC1323:Windows 98 (RFC1323)
65535:64:1:44:M*:			Windows:98:noSack:Windows 98 (no sack)
S16:128:1:48:M*,N,N,S:			Windows:98::Windows 98
S16:128:1:64:M*,N,W0,N,N,T0,N,N,S:	Windows:98::Windows 98
S26:128:1:48:M*,N,N,S:			Windows:98::Windows 98
T30:128:1:48:M*,N,N,S:			Windows:98::Windows 98
32767:128:1:52:M*,N,W0,N,N,S:		Windows:98::Windows 98
60352:128:1:48:M*,N,N,S:		Windows:98::Windows 98
60352:128:1:64:M*,N,W2,N,N,T0,N,N,S:	Windows:98::Windows 98

# What's with 1414 on NT?
T31:128:1:44:M1414:			Windows:NT:4.0:Windows NT 4.0 SP6a
64512:128:1:44:M1414:			Windows:NT:4.0:Windows NT 4.0 SP6a
8192:128:1:44:M*:			Windows:NT:4.0:Windows NT 4.0 (older)

# Windows XP and 2000. Most of the signatures that were
# either dubious or non-specific (no service pack data)
# were deleted and replaced with generics at the end.

65535:128:1:48:M*,N,N,S:		Windows:2000:SP4:Windows 2000 SP4, XP SP1
65535:128:1:48:M*,N,N,S:		Windows:XP:SP1:Windows 2000 SP4, XP SP1
%8192:128:1:48:M*,N,N,S:		Windows:2000:SP2+:Windows 2000 SP2, XP SP1 (seldom 98 4.10.2222)
%8192:128:1:48:M*,N,N,S:		Windows:XP:SP1:Windows 2000 SP2, XP SP1 (seldom 98 4.10.2222)
S20:128:1:48:M*,N,N,S:			Windows:2000::Windows 2000/XP SP3
S20:128:1:48:M*,N,N,S:			Windows:XP:SP3:Windows 2000/XP SP3
S45:128:1:48:M*,N,N,S:			Windows:2000:SP4:Windows 2000 SP4, XP SP 1
S45:128:1:48:M*,N,N,S:			Windows:XP:SP1:Windows 2000 SP4, XP SP 1
40320:128:1:48:M*,N,N,S:		Windows:2000:SP4:Windows 2000 SP4

S6:128:1:48:M*,N,N,S:			Windows:2000:SP2:Windows XP, 2000 SP2+
S6:128:1:48:M*,N,N,S:			Windows:XP::Windows XP, 2000 SP2+
S12:128:1:48:M*,N,N,S:			Windows:XP:SP1:Windows XP SP1
S44:128:1:48:M*,N,N,S:			Windows:2000:SP3:Windows Pro SP1, 2000 SP3
S44:128:1:48:M*,N,N,S:			Windows:XP:SP1:Windows Pro SP1, 2000 SP3
64512:128:1:48:M*,N,N,S:		Windows:2000:SP3:Windows SP1, 2000 SP3
64512:128:1:48:M*,N,N,S:		Windows:XP:SP1:Windows SP1, 2000 SP3
32767:128:1:48:M*,N,N,S:		Windows:2000:SP4:Windows SP1, 2000 SP4
32767:128:1:48:M*,N,N,S:		Windows:XP:SP1:Windows SP1, 2000 SP4

8192:128:1:52:M*,N,W2,N,N,S:		Windows:Vista::Windows Vista/7

# Odds, ends, mods:

S52:128:1:48:M1260,N,N,S:		Windows:2000:cisco:Windows XP/2000 via Cisco
S52:128:1:48:M1260,N,N,S:		Windows:XP:cisco:Windows XP/2000 via Cisco
65520:128:1:48:M*,N,N,S:		Windows:XP::Windows XP bare-bone
16384:128:1:52:M536,N,W0,N,N,S:		Windows:2000:ZoneAlarm:Windows 2000 w/ZoneAlarm?
2048:255:0:40:.:			Windows:.NET::Windows .NET Enterprise Server

44620:64:0:48:M*,N,N,S:			Windows:ME::Windows ME no SP (?)
S6:255:1:48:M536,N,N,S:			Windows:95:winsock2:Windows 95 winsock 2
32768:32:1:52:M1460,N,W0,N,N,S:		Windows:2003:AS:Windows 2003 AS


# No need to be more specific, it passes:
# *:128:1:48:M*,N,N,S:U:-Windows:XP/2000 while downloading (leak!) XXX quirk
# there is an equiv similar generic sig w/o the quirk

# ----------------- HP/UX -------------------

32768:64:1:44:M*:			HP-UX:B.10.20::HP-UX B.10.20
32768:64:0:48:M*,W0,N:			HP-UX:11.0::HP-UX 11.0
32768:64:1:48:M*,W0,N:			HP-UX:11.10::HP-UX 11.0 or 11.11
32768:64:1:48:M*,W0,N:			HP-UX:11.11::HP-UX 11.0 or 11.11

# Whoa. Hardcore WSS.
0:64:0:48:M*,W0,N:			HP-UX:B.11.00:A:HP-UX B.11.00 A (RFC1323)

# ----------------- RiscOS ------------------

# We don't yet support the ?12 TCP option
#16384:64:1:68:M1460,N,W0,N,N,T,N,N,?12:	RISCOS:3.70-4.36::RISC OS 3.70-4.36
12288:32:0:44:M536:				RISC OS:3.70:4.10:RISC OS 3.70 inet 4.10

# XXX quirk
# 4096:64:1:56:M1460,N,N,T:T:			RISC OS:3.70:freenet:RISC OS 3.70 freenet 2.00



# ----------------- BSD/OS ------------------

# Once again, power of two WSS is also shared by MacOS X with DF set
8192:64:1:60:M1460,N,W0,N,N,T:		BSD/OS:3.1::BSD/OS 3.1-4.3 (or MacOS X 10.2 w/DF)
8192:64:1:60:M1460,N,W0,N,N,T:		BSD/OS:4.0-4.3::BSD/OS 3.1-4.3 (or MacOS X 10.2)


# ---------------- NewtonOS -----------------

4096:64:0:44:M1420:		NewtonOS:2.1::NewtonOS 2.1

# ---------------- NeXTSTEP -----------------

S4:64:0:44:M1024:		NeXTSTEP:3.3::NeXTSTEP 3.3
S8:64:0:44:M512:		NeXTSTEP:3.3::NeXTSTEP 3.3

# ------------------ BeOS -------------------

1024:255:0:48:M*,N,W0:		BeOS:5.0-5.1::BeOS 5.0-5.1
12288:255:0:44:M1402:		BeOS:5.0::BeOS 5.0.x

# ------------------ OS/400 -----------------

8192:64:1:60:M1440,N,W0,N,N,T:	OS/400:VR4::OS/400 VR4/R5
8192:64:1:60:M1440,N,W0,N,N,T:	OS/400:VR5::OS/400 VR4/R5
4096:64:1:60:M1440,N,W0,N,N,T:	OS/400:V4R5:CF67032:OS/400 V4R5 + CF67032

# XXX quirk
# 28672:64:0:44:M1460:A:OS/390:?

# ------------------ ULTRIX -----------------

16384:64:0:40:.:		ULTRIX:4.5::ULTRIX 4.5

# ------------------- QNX -------------------

S16:64:0:44:M512:		QNX:::QNX demodisk

# ------------------ Novell -----------------

16384:128:1:44:M1460:		Novell:NetWare:5.0:Novel Netware 5.0
6144:128:1:44:M1460:		Novell:IntranetWare:4.11:Novell IntranetWare 4.11
6144:128:1:44:M1368:		Novell:BorderManager::Novell BorderManager ?

6144:128:1:52:M*,W0,N,S,N,N:	Novell:Netware:6:Novell Netware 6 SP3


# ----------------- SCO ------------------
S3:64:1:60:M1460,N,W0,N,N,T:	SCO:UnixWare:7.1:SCO UnixWare 7.1
S17:64:1:60:M1380,N,W0,N,N,T:	SCO:UnixWare:7.1:SCO UnixWare 7.1.3 MP3
S23:64:1:44:M1380:		SCO:OpenServer:5.0:SCO OpenServer 5.0

# ------------------- DOS -------------------

2048:255:0:44:M536:		DOS:WATTCP:1.05:DOS Arachne via WATTCP/1.05
T2:255:0:44:M984:		DOS:WATTCP:1.05Arachne:Arachne via WATTCP/1.05 (eepro)

# ------------------ OS/2 -------------------

S56:64:0:44:M512:		OS/2:4::OS/2 4
28672:64:0:44:M1460:		OS/2:4::OS/2 Warp 4.0

# ----------------- TOPS-20 -----------------

# Another hardcore MSS, one of the ACK leakers hunted down.
# XXX QUIRK 0:64:0:44:M1460:A:TOPS-20:version 7
0:64:0:44:M1460:		TOPS-20:7::TOPS-20 version 7

# ----------------- FreeMiNT ----------------

S44:255:0:44:M536:		FreeMiNT:1:16A:FreeMiNT 1 patch 16A (Atari)

# ------------------ AMIGA ------------------

# XXX TCP option 12
# S32:64:1:56:M*,N,N,S,N,N,?12:.:AMIGA:3.9 BB2 with Miami stack

# ------------------ Plan9 ------------------

65535:255:0:48:M1460,W0,N:	Plan9:4::Plan9 edition 4

# ----------------- AMIGAOS -----------------

16384:64:1:48:M1560,N,N,S:	AMIGAOS:3.9::AMIGAOS 3.9 BB2 MiamiDX

###########################################
# Appliance / embedded / other signatures #
###########################################

# ---------- Firewalls / routers ------------

S12:64:1:44:M1460:			@Checkpoint:::Checkpoint (unknown 1)
S12:64:1:48:N,N,S,M1460:		@Checkpoint:::Checkpoint (unknown 2)
4096:32:0:44:M1460:			ExtremeWare:4.x::ExtremeWare 4.x

# XXX TCP option 12
# S32:64:0:68:M512,N,W0,N,N,T,N,N,?12:.:Nokia:IPSO w/Checkpoint NG FP3
# S16:64:0:68:M1024,N,W0,N,N,T,N,N,?12:.:Nokia:IPSO 3.7 build 026

S4:64:1:60:W0,N,S,T,M1460:		FortiNet:FortiGate:50:FortiNet FortiGate 50

8192:64:1:44:M1460:			Eagle:::Eagle Secure Gateway

S52:128:1:48:M1260,N,N,N,N:		LinkSys:WRV54G::LinkSys WRV54G VPN router



# ------- Switches and other stuff ----------

4128:255:0:44:M*:			Cisco:::Cisco Catalyst 3500, 7500 etc
S8:255:0:44:M*:				Cisco:12008::Cisco 12008
60352:128:1:64:M1460,N,W2,N,N,T,N,N,S:	Alteon:ACEswitch::Alteon ACEswitch
64512:128:1:44:M1370:			Nortel:Contivity Client::Nortel Conectivity Client


# ---------- Caches and whatnots ------------

S4:64:1:52:M1460,N,N,S,N,W0:		AOL:web cache::AOL web cache

32850:64:1:64:N,W1,N,N,T,N,N,S,M*:	NetApp:5.x::NetApp Data OnTap 5.x
16384:64:1:64:M1460,N,N,S,N,W0,N:	NetApp:5.3:1:NetApp 5.3.1
65535:64:0:64:M1460,N,N,S,N,W*,N,N,T:	NetApp:5.3-5.5::NetApp 5.3-5.5
65535:64:0:60:M1460,N,W0,N,N,T:		NetApp:CacheFlow::NetApp CacheFlow
8192:64:1:64:M1460,N,N,S,N,W0,N,N,T:	NetApp:5.2:1:NetApp NetCache 5.2.1
20480:64:1:64:M1460,N,N,S,N,W0,N,N,T:	NetApp:4.1::NetApp NetCache4.1

65535:64:0:60:M1460,N,W0,N,N,T:		CacheFlow:4.1::CacheFlow CacheOS 4.1
8192:64:0:60:M1380,N,N,N,N,N,N,T:	CacheFlow:1.1::CacheFlow CacheOS 1.1

S4:64:0:48:M1460,N,N,S:			Cisco:Content Engine::Cisco Content Engine

27085:128:0:40:.:			Dell:PowerApp cache::Dell PowerApp (Linux-based)

65535:255:1:48:N,W1,M1460:		Inktomi:crawler::Inktomi crawler
S1:255:1:60:M1460,S,T,N,W0:		LookSmart:ZyBorg::LookSmart ZyBorg

16384:255:0:40:.:			Proxyblocker:::Proxyblocker (what's this?)

65535:255:0:48:M*,N,N,S:		Redline:::Redline T|X 2200

32696:128:0:40:M1460:			Spirent:Avalanche::Spirent Web Avalanche HTTP benchmarking engine

# ----------- Embedded systems --------------

S9:255:0:44:M536:			PalmOS:Tungsten:C:PalmOS Tungsten C
S5:255:0:44:M536:			PalmOS:3::PalmOS 3/4
S5:255:0:44:M536:			PalmOS:4::PalmOS 3/4
S4:255:0:44:M536:			PalmOS:3:5:PalmOS 3.5
2948:255:0:44:M536:			PalmOS:3:5:PalmOS 3.5.3 (Handera)
S29:255:0:44:M536:			PalmOS:5::PalmOS 5.0
16384:255:0:44:M1398:			PalmOS:5.2:Clie:PalmOS 5.2 (Clie)
S14:255:0:44:M1350:			PalmOS:5.2:Treo:PalmOS 5.2.1 (Treo)

S23:64:1:64:N,W1,N,N,T,N,N,S,M1460:	SymbianOS:7::SymbianOS 7

8192:255:0:44:M1460:			SymbianOS:6048::Symbian OS 6048 (Nokia 7650?)
8192:255:0:44:M536:			SymbianOS:9210::Symbian OS (Nokia 9210?)
S22:64:1:56:M1460,T,S:			SymbianOS:P800::Symbian OS ? (SE P800?)
S36:64:1:56:M1360,T,S:			SymbianOS:6600::Symbian OS 60xx (Nokia 6600?)


# Perhaps S4?
5840:64:1:60:M1452,S,T,N,W1:		Zaurus:3.10::Zaurus 3.10

32768:128:1:64:M1460,N,W0,N,N,T0,N,N,S:	PocketPC:2002::PocketPC 2002

S1:255:0:44:M346:			Contiki:1.1:rc0:Contiki 1.1-rc0

4096:128:0:44:M1460:			Sega:Dreamcast:3.0:Sega Dreamcast Dreamkey 3.0
T5:64:0:44:M536:			Sega:Dreamcast:HKT-3020:Sega Dreamcast HKT-3020 (browser disc 51027)
S22:64:1:44:M1460:			Sony:PS2::Sony Playstation 2 (SOCOM?)

S12:64:0:44:M1452:			AXIS:5600:v5.64:AXIS Printer Server 5600 v5.64

3100:32:1:44:M1460:			Windows:CE:2.0:Windows CE 2.0

####################
# Fancy signatures #
####################

1024:64:0:40:.:				*NMAP:syn scan:1:NMAP syn scan (1)
2048:64:0:40:.:				*NMAP:syn scan:2:NMAP syn scan (2)
3072:64:0:40:.:				*NMAP:syn scan:3:NMAP syn scan (3)
4096:64:0:40:.:				*NMAP:syn scan:4:NMAP syn scan (4)

# Requires quirks support
# 1024:64:0:40:.:A:*NMAP:TCP sweep probe (1)
# 2048:64:0:40:.:A:*NMAP:TCP sweep probe (2)
# 3072:64:0:40:.:A:*NMAP:TCP sweep probe (3)
# 4096:64:0:40:.:A:*NMAP:TCP sweep probe (4)

1024:64:0:60:W10,N,M265,T:		*NMAP:OS:1:NMAP OS detection probe (1)
2048:64:0:60:W10,N,M265,T:		*NMAP:OS:2:NMAP OS detection probe (2)
3072:64:0:60:W10,N,M265,T:		*NMAP:OS:3:NMAP OS detection probe (3)
4096:64:0:60:W10,N,M265,T:		*NMAP:OS:4:NMAP OS detection probe (4)

32767:64:0:40:.:			*NAST:::NASTsyn scan

# Requires quirks support
# 12345:255:0:40:.:A:-p0f:sendsyn utility


#####################################
# Generic signatures - just in case #
#####################################

#*:64:1:60:M*,N,W*,N,N,T:		@FreeBSD:4.0-4.9::FreeBSD 4.x/5.x
#*:64:1:60:M*,N,W*,N,N,T:		@FreeBSD:5.0-5.1::FreeBSD 4.x/5.x

*:128:1:52:M*,N,W0,N,N,S:		@Windows:XP:RFC1323:Windows XP/2000 (RFC1323 no tstamp)
*:128:1:52:M*,N,W0,N,N,S:		@Windows:2000:RFC1323:Windows XP/2000 (RFC1323 no tstamp)
*:128:1:52:M*,N,W*,N,N,S:		@Windows:XP:RFC1323:Windows XP/2000 (RFC1323 no tstamp)
*:128:1:52:M*,N,W*,N,N,S:		@Windows:2000:RFC1323:Windows XP/2000 (RFC1323 no tstamp)
*:128:1:64:M*,N,W0,N,N,T0,N,N,S:	@Windows:XP:RFC1323:Windows XP/2000 (RFC1323)
*:128:1:64:M*,N,W0,N,N,T0,N,N,S:	@Windows:2000:RFC1323:Windows XP/2000 (RFC1323)
*:128:1:64:M*,N,W*,N,N,T0,N,N,S:	@Windows:XP:RFC1323:Windows XP (RFC1323, w+)
*:128:1:48:M536,N,N,S:			@Windows:98::Windows 98
*:128:1:48:M*,N,N,S:			@Windows:XP::Windows XP/2000
*:128:1:48:M*,N,N,S:			@Windows:2000::Windows XP/2000


