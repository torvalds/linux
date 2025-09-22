ASICS=cayman evergreen r100 r200 r300 r420 r600 rn50 rs600 rv515

all:	${ASICS:S/$/_reg_safe.h/}

mkregtable: ../mkregtable.c
	${CC} ${CFLAGS} $< -o $@

.DEFAULT: mkregtable
	mkregtable ${@:S/_reg_safe.h//} > ../$@
