#!/bin/bash

PROG=./scrypt-genpass
RESULTS=test/test_results.log

$PROG -t > $RESULTS 2>&1
($PROG -h |grep -v Version)>> $RESULTS 2>&1
$PROG -v -p b a >> $RESULTS 2>&1
$PROG -v -k test/keyfile1.dat -p abc ghi >> $RESULTS 2>&1
$PROG -v -l 2 -p a a >> $RESULTS 2>&1
$PROG -v -l 65 -p a a >> $RESULTS 2>&1
$PROG -v -l 64 -p a a >> $RESULTS 2>&1
$PROG -v -n -l 4 -p "Speak, friend, and enter." "The Doors of Durin" >> $RESULTS 2>&1

diff $RESULTS test/test_results.reference
