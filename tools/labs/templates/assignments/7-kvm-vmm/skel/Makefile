CFLAGS = -Wall -Wextra -O2

run64: kvm-hello-world
	./kvm-hello-world

kvm-hello-world: vmm.o payload.o virtual_machine.o virtual_cpu.o
	$(CC) $^ -o $@

payload.o: payload.ld guest64.img.o guest16.o
	$(LD) -T $< -o $@

guest64.o: guest_code.c 
	$(CC) $(CFLAGS) -m64 -ffreestanding -fno-pic -c -o $@ $^

guest64.img: guest64.o
	$(LD) -T guest.ld $^ -o $@

%.img.o: %.img
	$(LD) -b binary -r $^ -o $@

.PHONY: clean
clean:
	$(RM) kvm-hello-world kvm-hello-world.o payload.o \
		*.o *.img