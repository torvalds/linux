# $FreeBSD$

# we don't do anything in these dirs
# they are just for triggering building of other things

UPDATE_DEPENDFILE= no

.if !target(all)
all:
	@echo Done!
.endif


