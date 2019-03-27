# $FreeBSD$
#
# Test handling of escaped newlines.
#

.ifmake test1

# This should succeed
\
\
\
test1:
	@echo ok

.elifmake test2

# This should print ok because the second assignment to foo is actually
# a continued comment.
FOO=ok
#\
\
FOO=not ok
test2:
	@echo "${FOO}"

.elifmake test3

# Make sure an escaped newline inserts a space
test3: a\
b

a:
	@echo a

b:
	@echo b

.elifmake test4

# Make sure an escaped newline inserts exactly one space
FOO=a\
			      b

test4:
	@echo "${FOO}"

.elifmake test5

# Make sure each escaped newline inserts exactly one space
FOO=a\
\
\
\
b

test5:
	@echo "${FOO}"
    
.endif
