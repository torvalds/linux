// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "debug.h"

#include "demangle-rust.h"

/*
 * Mangled Rust symbols look like this:
 *
 *     _$LT$std..sys..fd..FileDesc$u20$as$u20$core..ops..Drop$GT$::drop::hc68340e1baa4987a
 *
 * The original symbol is:
 *
 *     <std::sys::fd::FileDesc as core::ops::Drop>::drop
 *
 * The last component of the path is a 64-bit hash in lowercase hex, prefixed
 * with "h". Rust does not have a global namespace between crates, an illusion
 * which Rust maintains by using the hash to distinguish things that would
 * otherwise have the same symbol.
 *
 * Any path component not starting with a XID_Start character is prefixed with
 * "_".
 *
 * The following escape sequences are used:
 *
 *     ","  =>  $C$
 *     "@"  =>  $SP$
 *     "*"  =>  $BP$
 *     "&"  =>  $RF$
 *     "<"  =>  $LT$
 *     ">"  =>  $GT$
 *     "("  =>  $LP$
 *     ")"  =>  $RP$
 *     " "  =>  $u20$
 *     "'"  =>  $u27$
 *     "["  =>  $u5b$
 *     "]"  =>  $u5d$
 *     "~"  =>  $u7e$
 *
 * A double ".." means "::" and a single "." means "-".
 *
 * The only characters allowed in the mangled symbol are a-zA-Z0-9 and _.:$
 */

static const char *hash_prefix = "::h";
static const size_t hash_prefix_len = 3;
static const size_t hash_len = 16;

static bool is_prefixed_hash(const char *start);
static bool looks_like_rust(const char *sym, size_t len);
static bool unescape(const char **in, char **out, const char *seq, char value);

/*
 * INPUT:
 *     sym: symbol that has been through BFD-demangling
 *
 * This function looks for the following indicators:
 *
 *  1. The hash must consist of "h" followed by 16 lowercase hex digits.
 *
 *  2. As a sanity check, the hash must use between 5 and 15 of the 16 possible
 *     hex digits. This is true of 99.9998% of hashes so once in your life you
 *     may see a false negative. The point is to notice path components that
 *     could be Rust hashes but are probably not, like "haaaaaaaaaaaaaaaa". In
 *     this case a false positive (non-Rust symbol has an important path
 *     component removed because it looks like a Rust hash) is worse than a
 *     false negative (the rare Rust symbol is not demangled) so this sets the
 *     balance in favor of false negatives.
 *
 *  3. There must be no characters other than a-zA-Z0-9 and _.:$
 *
 *  4. There must be no unrecognized $-sign sequences.
 *
 *  5. There must be no sequence of three or more dots in a row ("...").
 */
bool
rust_is_mangled(const char *sym)
{
	size_t len, len_without_hash;

	if (!sym)
		return false;

	len = strlen(sym);
	if (len <= hash_prefix_len + hash_len)
		/* Not long enough to contain "::h" + hash + something else */
		return false;

	len_without_hash = len - (hash_prefix_len + hash_len);
	if (!is_prefixed_hash(sym + len_without_hash))
		return false;

	return looks_like_rust(sym, len_without_hash);
}

/*
 * A hash is the prefix "::h" followed by 16 lowercase hex digits. The hex
 * digits must comprise between 5 and 15 (inclusive) distinct digits.
 */
static bool is_prefixed_hash(const char *str)
{
	const char *end;
	bool seen[16];
	size_t i;
	int count;

	if (strncmp(str, hash_prefix, hash_prefix_len))
		return false;
	str += hash_prefix_len;

	memset(seen, false, sizeof(seen));
	for (end = str + hash_len; str < end; str++)
		if (*str >= '0' && *str <= '9')
			seen[*str - '0'] = true;
		else if (*str >= 'a' && *str <= 'f')
			seen[*str - 'a' + 10] = true;
		else
			return false;

	/* Count how many distinct digits seen */
	count = 0;
	for (i = 0; i < 16; i++)
		if (seen[i])
			count++;

	return count >= 5 && count <= 15;
}

static bool looks_like_rust(const char *str, size_t len)
{
	const char *end = str + len;

	while (str < end)
		switch (*str) {
		case '$':
			if (!strncmp(str, "$C$", 3))
				str += 3;
			else if (!strncmp(str, "$SP$", 4)
					|| !strncmp(str, "$BP$", 4)
					|| !strncmp(str, "$RF$", 4)
					|| !strncmp(str, "$LT$", 4)
					|| !strncmp(str, "$GT$", 4)
					|| !strncmp(str, "$LP$", 4)
					|| !strncmp(str, "$RP$", 4))
				str += 4;
			else if (!strncmp(str, "$u20$", 5)
					|| !strncmp(str, "$u27$", 5)
					|| !strncmp(str, "$u5b$", 5)
					|| !strncmp(str, "$u5d$", 5)
					|| !strncmp(str, "$u7e$", 5))
				str += 5;
			else
				return false;
			break;
		case '.':
			/* Do not allow three or more consecutive dots */
			if (!strncmp(str, "...", 3))
				return false;
			/* Fall through */
		case 'a' ... 'z':
		case 'A' ... 'Z':
		case '0' ... '9':
		case '_':
		case ':':
			str++;
			break;
		default:
			return false;
		}

	return true;
}

/*
 * INPUT:
 *     sym: symbol for which rust_is_mangled(sym) returns true
 *
 * The input is demangled in-place because the mangled name is always longer
 * than the demangled one.
 */
void
rust_demangle_sym(char *sym)
{
	const char *in;
	char *out;
	const char *end;

	if (!sym)
		return;

	in = sym;
	out = sym;
	end = sym + strlen(sym) - (hash_prefix_len + hash_len);

	while (in < end)
		switch (*in) {
		case '$':
			if (!(unescape(&in, &out, "$C$", ',')
					|| unescape(&in, &out, "$SP$", '@')
					|| unescape(&in, &out, "$BP$", '*')
					|| unescape(&in, &out, "$RF$", '&')
					|| unescape(&in, &out, "$LT$", '<')
					|| unescape(&in, &out, "$GT$", '>')
					|| unescape(&in, &out, "$LP$", '(')
					|| unescape(&in, &out, "$RP$", ')')
					|| unescape(&in, &out, "$u20$", ' ')
					|| unescape(&in, &out, "$u27$", '\'')
					|| unescape(&in, &out, "$u5b$", '[')
					|| unescape(&in, &out, "$u5d$", ']')
					|| unescape(&in, &out, "$u7e$", '~'))) {
				pr_err("demangle-rust: unexpected escape sequence");
				goto done;
			}
			break;
		case '_':
			/*
			 * If this is the start of a path component and the next
			 * character is an escape sequence, ignore the
			 * underscore. The mangler inserts an underscore to make
			 * sure the path component begins with a XID_Start
			 * character.
			 */
			if ((in == sym || in[-1] == ':') && in[1] == '$')
				in++;
			else
				*out++ = *in++;
			break;
		case '.':
			if (in[1] == '.') {
				/* ".." becomes "::" */
				*out++ = ':';
				*out++ = ':';
				in += 2;
			} else {
				/* "." becomes "-" */
				*out++ = '-';
				in++;
			}
			break;
		case 'a' ... 'z':
		case 'A' ... 'Z':
		case '0' ... '9':
		case ':':
			*out++ = *in++;
			break;
		default:
			pr_err("demangle-rust: unexpected character '%c' in symbol\n",
				*in);
			goto done;
		}

done:
	*out = '\0';
}

static bool unescape(const char **in, char **out, const char *seq, char value)
{
	size_t len = strlen(seq);

	if (strncmp(*in, seq, len))
		return false;

	**out = value;

	*in += len;
	*out += 1;

	return true;
}
