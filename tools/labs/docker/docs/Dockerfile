FROM ubuntu:20.04

ENV DEBIAN_FRONTEND noninteractive
RUN apt-get update
RUN apt-get install -y apt-utils
RUN apt-get install -y software-properties-common
RUN apt-get install -y sudo
RUN apt-get install -y make
RUN apt-get install -y git
RUN apt-get install -y python3
RUN update-alternatives --install /usr/bin/python python /usr/bin/python3 1
RUN apt-get install -y python3-pip
RUN update-alternatives --install /usr/bin/pip pip /usr/bin/pip3 1
RUN apt-get install -y ditaa
RUN apt-get install -y graphviz
RUN apt-get install -y imagemagick
RUN apt-get install -y dvipng
RUN apt-get install -y python3-venv
RUN apt-get install -y fonts-noto-cjk
RUN apt-get install -y latexmk
RUN apt-get install -y librsvg2-bin
RUN apt-get install -y texlive-xetex
RUN pip install Sphinx==1.6.7 sphinx_rtd_theme==1.3.0 hieroglyph==1.0 Jinja2==2.11.3 markupsafe==2.0.1
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
