#!/bin/perl

$a = 113549;

while ($a >= 128) {
        $x = $a % 128;
        $a /= 128;
        print "$x\n";
}

