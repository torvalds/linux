#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

echo "BAR tests"
echo

bar=0

while [ $bar -lt 6 ]
do
	pcitest -b $bar
	bar=`expr $bar + 1`
done
echo

echo "Interrupt tests"
echo

pcitest -i 0
pcitest -l

pcitest -i 1
msi=1

while [ $msi -lt 33 ]
do
        pcitest -m $msi
        msi=`expr $msi + 1`
done
echo

pcitest -i 2
msix=1

while [ $msix -lt 2049 ]
do
        pcitest -x $msix
        msix=`expr $msix + 1`
done
echo

echo "Read Tests"
echo

pcitest -i 1

pcitest -r -s 1
pcitest -r -s 1024
pcitest -r -s 1025
pcitest -r -s 1024000
pcitest -r -s 1024001
echo

echo "Write Tests"
echo

pcitest -w -s 1
pcitest -w -s 1024
pcitest -w -s 1025
pcitest -w -s 1024000
pcitest -w -s 1024001
echo

echo "Copy Tests"
echo

pcitest -c -s 1
pcitest -c -s 1024
pcitest -c -s 1025
pcitest -c -s 1024000
pcitest -c -s 1024001
echo
