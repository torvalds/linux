# $FreeBSD$
#
# PR bin/6612 / GRN r102178
#

test1: colons::colons::target

colons::colons::target:
	@echo '${.TARGET}'

test2: exclamation!target

exclamation!target:
	@echo '${.TARGET}
