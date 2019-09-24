#!/bin/sh
# test 13 : basic test for less than threshold modifications 
# args : file to be operated on

echo "######### test 13 : basic test for less than threshold modifications ###########"
# get the file to be operated on
file=$1
if [ -z $file ]; then
    echo "Missing argument: user file path"
	exit 1
fi
/bin/rm -f $file

ver1_str="little"

# create a file and write some data to it. version 1
echo $ver1_str > $file

# we should have [0] versions of the file created assuming bkp_threshold>6
# call bkpctl to list versions
../bkpctl $file -l
retval=$?
echo "num versions=$retval"

# now verify if we got the correct failure
if [ $retval -eq 0 ] ; then
	echo "PASSED: number of backups = 0"
	exit 0
else
	echo "FAILED: number of backups != 0"
	exit 1
fi
