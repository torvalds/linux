FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y software-properties-common
RUN apt-get install -y gcc-multilib
RUN apt-get install -y libncurses5-dev
RUN apt-get install -y bc
RUN apt-get install -y qemu-system-x86
RUN apt-get install -y qemu-system-arm
RUN apt-get install -y python3
RUN apt-get install -y minicom
RUN apt-get install -y git
RUN apt-get install -y wget
RUN apt-get install -y curl
RUN apt-get install -y sudo

RUN apt-get install -y iproute2
RUN apt-get install -y netcat-openbsd
RUN apt-get install -y vim
RUN apt-get install -y dnsmasq
RUN apt-get install -y iputils-ping
RUN apt-get install -y bash-completion
RUN apt-get install -y build-essential
RUN apt-get install -y bison
RUN apt-get install -y flex
RUN apt-get install -y gdb
RUN apt-get install -y asciinema

RUN apt-get install -y libssl-dev
RUN apt-get install -y git ninja-build pkg-config libglib2.0-dev libpixman-1-dev
RUN apt-get install -y lzop

RUN apt-get install -y samba # for the new make console command

RUN apt-get -y clean

RUN rm -rf /var/lib/apt/lists/*

ARG ARG_UID
ARG ARG_GID

RUN groupadd -g $ARG_GID ubuntu
RUN useradd -u $ARG_UID -g $ARG_GID -ms /bin/bash ubuntu && adduser ubuntu sudo && echo -n 'ubuntu:ubuntu' | chpasswd

# Enable passwordless sudo for users under the "sudo" group
RUN sed -i.bkp -e \
      's/%sudo\s\+ALL=(ALL\(:ALL\)\?)\s\+ALL/%sudo ALL=NOPASSWD:ALL/g' \
      /etc/sudoers

USER ubuntu
WORKDIR /home/ubuntu/
RUN echo add-auto-load-safe-path /linux/scripts/gdb/vmlinux-gdb.py > ~/.gdbinit
