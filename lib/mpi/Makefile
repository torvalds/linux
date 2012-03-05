#
# MPI multiprecision maths library (from gpg)
#

obj-$(CONFIG_MPILIB) = mpi.o

mpi-y = \
	generic_mpih-lshift.o		\
	generic_mpih-mul1.o		\
	generic_mpih-mul2.o		\
	generic_mpih-mul3.o		\
	generic_mpih-rshift.o		\
	generic_mpih-sub1.o		\
	generic_mpih-add1.o		\
	mpicoder.o			\
	mpi-bit.o			\
	mpih-cmp.o			\
	mpih-div.o			\
	mpih-mul.o			\
	mpi-pow.o			\
	mpiutil.o

mpi-$(CONFIG_MPILIB_EXTRA) += \
	mpi-add.o			\
	mpi-div.o			\
	mpi-cmp.o			\
	mpi-gcd.o			\
	mpi-inline.o			\
	mpi-inv.o			\
	mpi-mpow.o			\
	mpi-mul.o			\
	mpi-scan.o
