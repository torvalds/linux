# $FreeBSD$
# This test checks the code reading archive files. The archive file
# is a BSD4.4 file with __.SYMTAB and #1/N long file names.

#############################################################################

# should be ok
test1: libtest.a(short.o)
	@:

libtest.a(short.o): ood
	@:

#############################################################################

# should be ok
test2: libtest.a(exactly15char.o)
	@:

libtest.a(exactly15char.o): ood
	@:

#############################################################################

# should be ok
test3: libtest.a(exactly16chars.o)
	@:

libtest.a(exactly16chars.o): ood
	@:

#############################################################################

# should be ok
test4: libtest.a(verylongobjectname.o)
	@:

libtest.a(verylongobjectname.o): ood
	@:

#############################################################################

# Truncated to 16 characters
# should fail
test5: libtest.a(verylongobjectna)
	@:

#############################################################################

# Truncated to 15 characters
# should fail
test6: libtest.a(verylongobjectn)
	@:

#############################################################################

# should fail
test7: libtest.a(\#1/20)
	@:

ood:
