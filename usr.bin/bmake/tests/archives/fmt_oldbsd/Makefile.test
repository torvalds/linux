# $FreeBSD$
# This test checks the code reading archive files. The archive file
# is an old BSD file with __.SYMTAB and no long file names. Filenames
# are truncated to 16 characters.

#############################################################################

# must be ok
test1: libtest.a(short.o)
	@:

libtest.a(short.o): ood
	@:

#############################################################################

# must be ok
test2: libtest.a(exactly15char.o)
	@:

libtest.a(exactly15char.o): ood
	@:

#############################################################################

# must be ok
test3: libtest.a(exactly16chars.o)
	@:

libtest.a(exactly16chars.o): ood
	@:

#############################################################################

# must be ok
test4: libtest.a(verylongobjectname.o)
	@:

libtest.a(verylongobjectname.o): ood
	@:

#############################################################################

# Truncated to 16 characters
# must be ok
test5: libtest.a(verylongobjectna)
	@:

#############################################################################

# Truncated to 15 characters
# must fail
test6: libtest.a(verylongobjectn)
	@:

#############################################################################

# must fail
test7: libtest.a(\#1/20)
	@:

ood:
