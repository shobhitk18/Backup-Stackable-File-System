#!/bin/sh
# test 15 : basic test for combination of options delete + list
# args : file to be operated on

echo "######### test 15 : basic test for combination of options delete + list ###########"
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
# call bkpctl to delete the newest versions and then list versions
../bkpctl $file -d newest -l
retval=$?
echo "return value for delete + list op=$retval"
if [ $retval -eq 2 ] ; then
	echo "PASSED: correct listed versions after delete"
	exit 0
else
	echo "FAILED: incorrectly listed version after delete"
	exit 1
fi
