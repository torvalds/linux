#	$OpenBSD: README,v 1.4 2004/03/30 17:14:11 jmc Exp $

#	@(#)README	8.1 (Berkeley) 6/6/93

--------------------------------------------------------------------
FILES and subdirectories of /usr/share/dict:

    words    -- common words, and important technical terms from all
		fields, that are spelled the same in British and American usage.
    web2     -- Webster's Second International Dictionary, all 234,936 words
		worth.  The 1934 copyright has lapsed.
    web2a    -- hyphenated terms as well as assorted noun and adverbial phrases
		from Webster's Second International Dictionary.
    propernames -- List of proper names, also from Webster's Second
		International Dictionary.
    american -- spellings preferred in American but not British usage.
    british  -- spellings preferred in British but not American usage.
    stop     -- forms that would otherwise be derivable by "spell" from
		words in one of the above files, but should not be accepted.
    README   -- this file
    papers/  -- an (out-of-date specialized) bibliographical database,
		used as the default by the program "refer".
    special/ -- directory of less common terms from specialized fields.
	It presently contains:

	special/4bsd -- commands and system calls (from filenames in
	    /usr/share/man/man[1238]), and builtin csh commands (named in
	    /usr/share/man/man1/csh.1) of the current version of 4bsd Unix.
	    (Supersedes old "/usr/src/usr.bin/spell/local".)
	special/math -- some mathematical terms not in /usr/share/dict/words.

--------------------------------------------------------------------

The subdirectory "special" contains lists of words in specialized fields,
which may be hashed in with the regular lists on machines having many users
working in these fields.  As of this writing, there are two such specialized
word lists, 4bsd and math (described briefly above).

It is advised that system managers create a directory /usr/local/share/dict.
This can be used to maintain files of particular interest to users of each
machine (e.g., surnames of members of the department on a departmental
machine).  These files, potentially along with files in /usr/share/dict/special,
should be placed in /usr/local/share/dict/words, which will be used by
the spell program.  The following example creates a local words list
consisting of 4BSD commands and terms as well as local surnames and
acronyms:

	# sort -df /usr/share/dict/special/4bsd \
		/usr/local/share/dict/surnames \
		/usr/local/share/dict/acronyms > /usr/local/share/dict/words

Note that word lists must be sorted in dictionary order and with case folded.
In general this means they should be passed through "sort -df".
Word lists that are not sorted in this way will not work properly
with the spell and look commands since these perform binary searches
on the word lists.
