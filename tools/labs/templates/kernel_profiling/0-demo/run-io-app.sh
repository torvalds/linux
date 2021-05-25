#!/bin/bash

dd if=/dev/urandom of=lab008.data bs=10K count=1K
./io-app
