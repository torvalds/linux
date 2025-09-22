# Zone files

Zone files are text files that contain resource records (RRs) in text form.
Zones can be defined by expressing them in the form of a list of RRs.

Zone files were originally specified in RFC1035 Section 5, but the DNS
has seen many additions since and the specification is rather ambiguous.
Consequently, various name servers implement slightly different dialects. This
document aims to clarify the format by listing (some of) the relevant
specifications and then proceed to explain why certain design decisions were
made in simdzone.

* [RFC 1034 Section 3.6.1][rfc1034#3.6.1]
* [RFC 1035 Section 5][rfc1035#5]
* [RFC 2065 Section 4.5][rfc2065#4.5]
* [RFC 2181 Section 8][rfc2181#8]
* [RFC 2308 Section 4][rfc2308#4]
* [RFC 3597 Section 5][rfc3597#5]
* [RFC 9460 Section 2.1][rfc9460#2.1]


## Clarification (work-in-progress)

> NOTE: BIND behavior is more-or-less considered the de facto standard.

Historically, master files where edited by hand, which is reflected in the
syntax. Consider the format a tabular serialization format with provisions
for convenient editing. i.e. the owner, class and ttl fields may be omitted
(provided the line starts with \<blank\> for the owner) and $INCLUDE directives
can be used for templating.

The format is NOT context-free. The field following the owner (if specified)
may represent either a type, class or ttl and a symbolic constant, e.g. A
or NS, may have a different meaning if specified as an RDATA field.

The DNS is intentionally extensible. The specification is not explicit about
how that affects syntax, but it explains why no specific notation for
data-types can be enforced by RFC 1035. To make it easier for data-types to
be added at a later stage the syntax cannot enforce a certain notation (or
the scanner would need to be revised). Consequently, the scanner only
identifies items (or fields) and structural characters, which can be
expressed as either a contiguous set of characters without interior spaces,
or as a quoted string.

The format allows for including structural characters in fields by means of
escaping the actual character or enclosing the field in quotes. The example
provided by the specification here is using ASCII dots in domain name labels.
The dot is normally a label separator, replaced by the length of the label
on the wire. If a domain name includes an actual ASCII dot, the character
must be escaped in the textual representation (`\X` or `\DDD`).

Note that ASCII dot characters strictly speaking do not have to be escaped
in a quoted string. RFC 1035 clearly states labels in domain names are
expressed as character strings. However, behavior differs across
implementations, so support for quoted labels is best dropped (see below).

RFC 1035 states both \<contiguous\> and \<quoted\> are \<character-string\>.
Meaning, items can be either \<contiguous\> or \<quoted\>. Wether a specific
item is interpreted as a \<character-string\> depends on type of value for
that item. E.g., TTLs are decimal integers and therefore cannot be expressed
as \<quoted\> as it is not a \<character-string\>. Similarly, base64
sequences are encoded binary blobs, not \<character-string\>s and therefore
cannot be expressed as such. Escape sequences are valid only in
\<character-string\>s.

* Mnemonics are NOT character strings.

  > BIND does not accept quoted fields for A or NS RDATA. TTL values in SOA
  > RDATA, base64 Signature in DNSKEY RDATA, as well as type, class and TTL
  > header fields all result in a syntax error too if quoted.

* Some integer fields allow for using mnemonics too. E.g., the algorithm
  field in RRSIG records.

* RFC 1035 states: A freestanding @ denotes the current origin.
  There has been discussion in which locations @ is interpreted as the origin.
  e.g. how is a freestanding @ be interpreted in the RDATA section of a TXT RR.
  Note that there is no mention of text expansion in the original text. A
  freestanding @ denotes the origin. As such, it stands to reason that it's
  use is limited to locations where domain names are expressed, which also
  happens to be the most practical way to implement the functionality.

  > This also seems to be the behavior that other name servers implement (at
  > least BIND and PowerDNS). The BIND manual states: "When used in the label
  > (or name) field, the asperand or at-sign (@) symbol represents the current
  > origin. At the start of the zone file, it is the \<zone\_name\>, followed
  > by a trailing dot (.).

  > It may also make sense to interpret a quoted freestanding @ differently
  > than a non-quoted one. At least, BIND throws an error if a quoted
  > freestanding @ is encountered in the RDATA sections for CNAME and NS RRs.
  > However, a quoted freestanding @ is accepted and interpreted as origin
  > if specified as the OWNER.

  > Found mentions of what happens when a zone that uses freestanding @ in
  > RDATA is written to disk. Of course, this particular scenario rarely occurs
  > as it does not need to be written to disk when loaded on a primary and no
  > file exists if received over AXFR/IXFR. However, it may make sense to
  > implement optimistic compression of this form, and make it configurable.

* Class and type names are mutually exclusive in practice.
  RFC1035 states: The RR begins with optional TTL and class fields, ...
  Therefore, if a type name matches a class name, the parser cannot distinguish
  between the two in text representation and must resort to generic notation
  (RFC3597) or, depending on the RDATA format for the record type, a
  look-ahead may be sufficient. Realistically, it is highly likely that because
  of this, no type name will ever match a class name.

  > This means both can reside in the same table.

* The encoding is non-ASCII. Some characters have special meaning, but users
  are technically allowed to put in non-printable octets outside the ASCII
  range without custom encoding. Of course, this rarely occurs in practice
  and users are encouraged to use the \DDD encoding for "special".

* Parenthesis may not be nested.

* $ORIGIN must be an absolute domain.

* Escape sequences must NOT be unescaped in the scanner as is common with
  programming languages like C that have a preprocessor. Instead, the
  original text is necessary in the parsing stage to distinguish between
  label separators (dots).

* RFC 1035 specifies that the current origin should be restored after an
  $INCLUDE, but it is silent on whether the current domain name should also be
  restored. BIND 9 restores both of them. This could be construed as a
  deviation from RFC 1035, a feature, or both.

* RFC 1035 states: and text literals can contain CRLF within the text.
  BIND, however, does not allow newlines in text (escaped or not). For
  performance reasons, we may adopt the same behavior as that would relieve
  the need to keep track of possibly embedded newlines.

* From: http://www.zytrax.com/books/dns/ch8/include.html (mentioned in chat)
  > Source states: The RFC is silent on the topic of embedded `$INCLUDE`s in
  > `$INCLUDE`d files - BIND 9 documentation is similarly silent. Assume they
  > are not permitted.

  All implementations, including BIND, allow for embedded `$INCLUDE`s.
  The current implementation is such that (embedded) includes are allowed by
  default. However, `$INCLUDE` directives can be disabled, which is useful
  when parsing from an untrusted source. There is also protection against
  cyclic includes.

  > There is no maximum to the amount of embedded includes (yet). NSD limits
  > the number of includes to 10 by default (compile option). For security, it
  > must be possible to set a hard limit.

* Default values for TTLs can be quite complicated.

  A [commit to ldns](https://github.com/NLnetLabs/ldns/commit/cb101c9) by
  @wtoorop nicely sums it up in code.

  RFC 1035 section 5.1:
  > Omitted class and TTL values are default to the last explicitly stated
  > values.

  This behavior is updated by RFC 2308 section 4:
  > All resource records appearing after the directive, and which do not
  > explicitly include a TTL value, have their TTL set to the TTL given
  > in the $TTL directive.  SIG records without a explicit TTL get their
  > TTL from the "original TTL" of the SIG record [RFC 2065 Section 4.5].

  The TTL rules for `SIG` RRs stated in RFC 2065 Section 4.5:
  > If the original TTL, which applies to the type signed, is the same as
  > the TTL of the SIG RR itself, it may be omitted.  The date field
  > which follows it is larger than the maximum possible TTL so there is
  > no ambiguity.

  The same applies applies to `RRSIG` RRs, although not stated as explicitly
  in RFC 4034 Section 3:
  > The TTL value of an RRSIG RR MUST match the TTL value of the RRset it
  > covers.  This is an exception to the [RFC2181] rules for TTL values
  > of individual RRs within a RRset: individual RRSIG RRs with the same
  > owner name will have different TTL values if the RRsets they cover
  > have different TTL values.

  Logic spanning RRs must not be handled during deserialization. The order in
  which RRs appear in the zone file is not relevant and keeping a possibly
  infinite backlog of RRs to handle it "automatically" is inefficient. As
  the name server retains RRs in a database already it seems most elegant to
  signal the TTL value was omitted and a default was used so that it may be
  updated in some post processing step.

  [RFC 2181 Section 8][rfc2181#8] contains additional notes on the maximum
  value for TTLs. During deserialization, any value exceeding 2147483647 is
  considered an error in primary mode, or a warning in secondary mode.
  [RFC 8767 Section 4][rfc8767#4] updates the text, but the update does not
  update handling during deserialization.

  [RFC 2181 Section 5][rfc2181#5.2] states the TTLs of all RRs in an RRSet
  must be the same. As with default values for `SIG` and `RRSIG` RRs, this
  must NOT be handled during deserialization. Presumably, the application
  should transparently fix TTLs (NLnetLabs/nsd#178).

* Do NOT allow for quoted labels in domain names.
  [RFC 1035 Section 5][rfc1035#5] states:
  > The labels in the domain name are expressed as character strings and
  > separated by dots.

  [RFC 1035 section 5][rfc1035#5] states:
  > \<character-string\> is expressed in one or two ways: as a contiguous set
  > of characters without interior spaces, or as string beginning with a " and
  > ending with a ".

  However, quoted labels in domain names are very uncommon and implementations
  handle quoted names both in OWNER and RDATA very differently. The Flex+Bison
  based parser used in NSD before was the only parser that got it right.

  * BIND
    * owner: yes, interpreted as quoted
      ```
      dig @127.0.0.1 A quoted.example.com.
      ```
      ```
      quoted.example.com.  xxx  IN  A  x.x.x.x
      ```
    * rdata: no, syntax error (even with `check-names master ignored;`)
  * Knot
    * owner: no, syntax error
    * rdata: no, syntax error
  * PowerDNS
    * owner: no, not interpreted as quoted
      ```
      pdnsutil list-zone example.com.
      ```
      ```
      "quoted".example.com  xxx  IN  A  x.x.x.x
      ```
    * rdata: no, not interpreted as quoted
      ```
      dig @127.0.0.1 NS example.com.
      ```
      ```
      example.com.  xxx  IN  NS  \"quoted.example.com.\".example.com.
      ```

  > [libzscanner](https://github.com/CZ-NIC/knot/tree/master/src/libzscanner),
  > the (standalone) zone parser used by Knot seems mosts consistent.

  Drop support for quoted labels or domain names for consistent behavior.

* Should any domain names that are not valid host names as specified by
  RFC 1123 section 2, i.e. use characters not in the preferred naming syntax
  as specified by RFC 1035 section 2.3.1, be accepted? RFC 2181 section 11 is
  very specific on this topic, but it merely states that labels may contain
  characters outside the set on the wire, it does not address what is, or is
  not, allowed in zone files.

  BIND's zone parser throws a syntax error for any name that is not a valid
  hostname unless `check-names master ignored;` is specified. Knot
  additionally accepts `-`, `_` and `/` according to
  [NOTES](https://github.com/CZ-NIC/knot/blob/master/src/libzscanner/NOTES).

  * [RFC1035 Section 2.3.1][rfc1035#2.3.1]
  * [RFC1123 Section 2][rfc1123#2]
  * [RFC2181 Section 11][rfc2181#11]

* RFC 1035 specifies two control directives "$INCLUDE" and "$ORIGIN". RFC 2308
  specifies the "$TTL" directive. BIND additionally implements the "$DATE" and
  "$GENERATE" directives. Since "$" (dollar sign) is not reserved, both
  "$DATE" and "$GENERATE" (and "$TTL" before RFC2308) are considered valid
  domain names in other implementations (based on what is accepted for domain
  names, see earlier points). It seems "$" is better considered a reserved
  character (possibly limiting its special status to the start of the
  line), to allow for reliable extensibility in the future.

  > BIND seems to already throw an error if "$" is encountered, see
  > `lib/dns/master.c`. Presumably, the "$DATE" directive is written when the
  > zone is written to disk(?) In the code it is referred to as
  > __dump_time__ and later used to calculate __ttl_offset__.

* BIND10 had a nice writeup on zone files, kindly provided by Shane Kerr.
  [Zone File Loading Requirements on Wayback Machine](https://web.archive.org/web/20140928215002/http://bind10.isc.org:80/wiki/ZoneLoadingRequirements)

* `TYPE0` is sometimes used for debugging and therefore may occur in type
  bitmaps or as unknown RR type.

* `pdns/master/regression-tests/zones/test.com` contains regression tests
  that may be useful for testing simdzone.

* Some implementations (Knot, possibly PowerDNS) will silently split-up
  strings longer than 255 characters. Others (BIND, simdzone) will throw a
  syntax error.

* How do we handle the corner case where the first record does not have a TTL
  when the file does not define a zone? (from @shane-kerr).

  At this point in time, the application provides a default TTL value before
  parsing. Whether that is the right approach is unclear, but it is what NSD
  did before.

* Leading zeroes in integers appear to be allowed judging by the zone file
  generated for the [socket10kxfr][socket10kxfr.pre#L64] test in NSD. BIND
  and Knot parsed it without problems too.

[rfc1034#3.6.1]: https://datatracker.ietf.org/doc/html/rfc1034#section-3.6.1
[rfc1035#5]: https://datatracker.ietf.org/doc/html/rfc1035#section-5
[rfc1035#2.3.1]: https://datatracker.ietf.org/doc/html/rfc1035#section-2.3.1
[rfc1123#2]: https://datatracker.ietf.org/doc/html/rfc1123#section-2
[rfc2065#4.5]: https://datatracker.ietf.org/doc/html/rfc2065#section-4.5
[rfc2181#5.2]: https://datatracker.ietf.org/doc/html/rfc2181#section-5.2
[rfc2181#8]: https://datatracker.ietf.org/doc/html/rfc2181#section-8
[rfc2181#11]: https://datatracker.ietf.org/doc/html/rfc2181#section-11
[rfc2308#4]: https://datatracker.ietf.org/doc/html/rfc2308#section-4
[rfc3597#5]: https://datatracker.ietf.org/doc/html/rfc3597#section-5
[rfc8767#4]: https://www.rfc-editor.org/rfc/rfc8767#section-4
[rfc9460#2.1]: https://datatracker.ietf.org/doc/html/rfc9460#section-2.1

[socket10kxfr.pre#L64]: https://github.com/NLnetLabs/nsd/blob/86a6961f2ca64f169d7beece0ed8a5e1dd1cd302/tpkg/long/socket10kxfr.tdir/socket10kxfr.pre#L64
