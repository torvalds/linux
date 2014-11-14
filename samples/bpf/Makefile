# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := test_verifier test_maps

test_verifier-objs := test_verifier.o libbpf.o
test_maps-objs := test_maps.o libbpf.o

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS += -I$(objtree)/usr/include
