FROM ubuntu:18.04

RUN apt-get update
RUN apt-get install -y software-properties-common
RUN apt-get install -y sudo
RUN apt-get install -y make
RUN apt-get install -y git
RUN apt-get install -y python
RUN apt-get install -y python-pip
RUN apt-get install -y ditaa
RUN apt-get install -y graphviz
RUN pip install Sphinx==1.6.7 sphinx_rtd_theme hieroglyph==1.0
# append new packages here, to minimize docker rebuild time
RUN rm -rf /var/lib/apt/lists/*

RUN useradd -ms /bin/bash ubuntu && adduser ubuntu sudo && echo -n 'ubuntu:ubuntu' | chpasswd

# Enable passwordless sudo for users under the "sudo" group
RUN sed -i.bkp -e \
      's/%sudo\s\+ALL=(ALL\(:ALL\)\?)\s\+ALL/%sudo ALL=NOPASSWD:ALL/g' \
      /etc/sudoers

USER ubuntu
WORKDIR /home/ubuntu/

ENV PATH ${PATH}:/home/ubuntu/.local
