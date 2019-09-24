#!/bin/sh
# test 14 : basic test for checking backup operations on directory 
# args : file to be operated on

echo "######### test 14 : basic test for less than threshold modifications ###########"
# get the file to be operated on
dir=${1}_dir
/bin/rm -rf $dir

mkdir -p $dir

# we should have [0] versions of the file created assuming bkp_threshold>6
# call bkpctl to list versions
../bkpctl $dir -l
retval=$?
echo "num versions=$retval"

# now verify if we got the correct failure
if [ $retval -eq 255 ] ; then
	echo "PASSED: operation on directory detected"
	/bin/rm -rf $dir
	exit 0
else
	echo "FAILED: operation on directory not detected"
	/bin/rm -rf $dir
	exit 1
fi
