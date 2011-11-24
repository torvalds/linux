
#!/bin/bash

mkdir ../btfilter.bk
cp -r ../btfilter ../btfilter.bk

mkdir include
mkdir host
mkdir host/btfilter
mkdir host/include
mkdir host/os
mkdir host/os/linux
mkdir host/os/linux/include

mv *.c host/btfilter
mv *.h host/btfilter
mv Makefile host/btfilter
mv Android_BT.mk host/btfilter/Android.mk

cp Android.mk host
cp ../include/*.h host/include
cp ../os/linux/include/*.h host/os/linux/include
cp ../../include/*.h include
