#!/bin/sh
# test 10 : invalid delete option arguments
# args : file to be operated on

echo "######### test 10 : invalid delete option arguments  ###########"
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
# call bkpctl to delete version with invalid args
../bkpctl $file -d 10
retval=$?
echo "return value=$retval"

# now verify if we got the correct failure
if [ $retval -eq 255 ] ; then
	echo "PASSED: incorrect arguments detected"
	exit 0
else
	echo "FAILED: incorrect arguments not detected"
	exit 1
fi
