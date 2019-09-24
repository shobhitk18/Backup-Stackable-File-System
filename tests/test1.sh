#!/bin/sh
# test 1 : simple listing of versions
# args : file to be operated on

echo "######### test 1: simple listing of versions ###########"
# get the file to be operated on
file=$1
if [ -z $file ]; then
    echo "Missing argument: user file path"
	exit 1
fi

/bin/rm -f $file

ver1_str="hello world..this is some random data for version 1"
ver2_str="hello world..this is some random data for version 2"
ver3_str="hello world..this is some random data for version 3" 

# create a file and write some data to it. version 1
echo $ver1_str > $file

# add some more data to file. version 2
echo $ver2_str > $file

# and some more changes to the file. version 3
echo $ver3_str > $file

# we should have [3] versions of the file created by now.
# call bkpctl to list versions
../bkpctl $file -l
retval=$?
echo "num versions=$retval"

if [ $retval -lt 0 ] ; then
	echo bkpctl failed with error: $retval
	exit $retval
else
	echo bkpctl program succeeded for list
fi

# now verify if we got the correct number of versions listed
if [ $retval -eq 3 ] ; then
	echo "PASSED: correct number of versions listed"
	exit 0
else
	echo "FAILED: incorrect number of versions listed"
	exit 1
fi
