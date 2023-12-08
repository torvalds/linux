/*
 * Extracted fronm glob.c
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/glob.h>
#include <linux/printk.h>

/* Boot with "glob.verbose=1" to show successful tests, too */
static bool verbose = false;
module_param(verbose, bool, 0);

struct glob_test {
	char const *pat, *str;
	bool expected;
};

static bool __pure __init test(char const *pat, char const *str, bool expected)
{
	bool match = glob_match(pat, str);
	bool success = match == expected;

	/* Can't get string literals into a particular section, so... */
	static char const msg_error[] __initconst =
		KERN_ERR "glob: \"%s\" vs. \"%s\": %s *** ERROR ***\n";
	static char const msg_ok[] __initconst =
		KERN_DEBUG "glob: \"%s\" vs. \"%s\": %s OK\n";
	static char const mismatch[] __initconst = "mismatch";
	char const *message;

	if (!success)
		message = msg_error;
	else if (verbose)
		message = msg_ok;
	else
		return success;

	printk(message, pat, str, mismatch + 3*match);
	return success;
}

/*
 * The tests are all jammed together in one array to make it simpler
 * to place that array in the .init.rodata section.  The obvious
 * "array of structures containing char *" has no way to force the
 * pointed-to strings to be in a particular section.
 *
 * Anyway, a test consists of:
 * 1. Expected glob_match result: '1' or '0'.
 * 2. Pattern to match: null-terminated string
 * 3. String to match against: null-terminated string
 *
 * The list of tests is terminated with a final '\0' instead of
 * a glob_match result character.
 */
static char const glob_tests[] __initconst =
	/* Some basic tests */
	"1" "a\0" "a\0"
	"0" "a\0" "b\0"
	"0" "a\0" "aa\0"
	"0" "a\0" "\0"
	"1" "\0" "\0"
	"0" "\0" "a\0"
	/* Simple character class tests */
	"1" "[a]\0" "a\0"
	"0" "[a]\0" "b\0"
	"0" "[!a]\0" "a\0"
	"1" "[!a]\0" "b\0"
	"1" "[ab]\0" "a\0"
	"1" "[ab]\0" "b\0"
	"0" "[ab]\0" "c\0"
	"1" "[!ab]\0" "c\0"
	"1" "[a-c]\0" "b\0"
	"0" "[a-c]\0" "d\0"
	/* Corner cases in character class parsing */
	"1" "[a-c-e-g]\0" "-\0"
	"0" "[a-c-e-g]\0" "d\0"
	"1" "[a-c-e-g]\0" "f\0"
	"1" "[]a-ceg-ik[]\0" "a\0"
	"1" "[]a-ceg-ik[]\0" "]\0"
	"1" "[]a-ceg-ik[]\0" "[\0"
	"1" "[]a-ceg-ik[]\0" "h\0"
	"0" "[]a-ceg-ik[]\0" "f\0"
	"0" "[!]a-ceg-ik[]\0" "h\0"
	"0" "[!]a-ceg-ik[]\0" "]\0"
	"1" "[!]a-ceg-ik[]\0" "f\0"
	/* Simple wild cards */
	"1" "?\0" "a\0"
	"0" "?\0" "aa\0"
	"0" "??\0" "a\0"
	"1" "?x?\0" "axb\0"
	"0" "?x?\0" "abx\0"
	"0" "?x?\0" "xab\0"
	/* Asterisk wild cards (backtracking) */
	"0" "*??\0" "a\0"
	"1" "*??\0" "ab\0"
	"1" "*??\0" "abc\0"
	"1" "*??\0" "abcd\0"
	"0" "??*\0" "a\0"
	"1" "??*\0" "ab\0"
	"1" "??*\0" "abc\0"
	"1" "??*\0" "abcd\0"
	"0" "?*?\0" "a\0"
	"1" "?*?\0" "ab\0"
	"1" "?*?\0" "abc\0"
	"1" "?*?\0" "abcd\0"
	"1" "*b\0" "b\0"
	"1" "*b\0" "ab\0"
	"0" "*b\0" "ba\0"
	"1" "*b\0" "bb\0"
	"1" "*b\0" "abb\0"
	"1" "*b\0" "bab\0"
	"1" "*bc\0" "abbc\0"
	"1" "*bc\0" "bc\0"
	"1" "*bc\0" "bbc\0"
	"1" "*bc\0" "bcbc\0"
	/* Multiple asterisks (complex backtracking) */
	"1" "*ac*\0" "abacadaeafag\0"
	"1" "*ac*ae*ag*\0" "abacadaeafag\0"
	"1" "*a*b*[bc]*[ef]*g*\0" "abacadaeafag\0"
	"0" "*a*b*[ef]*[cd]*g*\0" "abacadaeafag\0"
	"1" "*abcd*\0" "abcabcabcabcdefg\0"
	"1" "*ab*cd*\0" "abcabcabcabcdefg\0"
	"1" "*abcd*abcdef*\0" "abcabcdabcdeabcdefg\0"
	"0" "*abcd*\0" "abcabcabcabcefg\0"
	"0" "*ab*cd*\0" "abcabcabcabcefg\0";

static int __init glob_init(void)
{
	unsigned successes = 0;
	unsigned n = 0;
	char const *p = glob_tests;
	static char const message[] __initconst =
		KERN_INFO "glob: %u self-tests passed, %u failed\n";

	/*
	 * Tests are jammed together in a string.  The first byte is '1'
	 * or '0' to indicate the expected outcome, or '\0' to indicate the
	 * end of the tests.  Then come two null-terminated strings: the
	 * pattern and the string to match it against.
	 */
	while (*p) {
		bool expected = *p++ & 1;
		char const *pat = p;

		p += strlen(p) + 1;
		successes += test(pat, p, expected);
		p += strlen(p) + 1;
		n++;
	}

	n -= successes;
	printk(message, successes, n);

	/* What's the errno for "kernel bug detected"?  Guess... */
	return n ? -ECANCELED : 0;
}

/* We need a dummy exit function to allow unload */
static void __exit glob_fini(void) { }

module_init(glob_init);
module_exit(glob_fini);

MODULE_DESCRIPTION("glob(7) matching tests");
MODULE_LICENSE("Dual MIT/GPL");
