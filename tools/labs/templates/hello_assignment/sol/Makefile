KDIR=/usr/src/linux-so2

kbuild:
	make -C $(KDIR) M=`pwd`

clean:
	make -C $(KDIR) M=`pwd` clean

pack:
	zip -r hello-world-solution.zip hello-world.c Makefile Kbuild

deploy: pack
	scp hello-world-solution.zip so2@elf.cs.pub.ro:res/current/teme/
