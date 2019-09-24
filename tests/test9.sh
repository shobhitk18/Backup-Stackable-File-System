#!/bin/sh
# test9 - basic restore version <N> functionality

echo "############ test8 - basic restore version <N> functionality  ###########"
# get the file to be operated on
file=$1
if [ -z $file ]; then
    echo "Missing argument: user file path"
	exit 1
fi
/bin/rm -f $file

# these are the contents to be written for each version
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
# call bkpctl to restore the first version and then verify contents
../bkpctl $file -r 1 > temp.out
retval=$?
echo "return value for restore op=$retval"
if [ $retval -lt 0 ] ; then
	echo bkpctl failed with error: $retval
	exit $retval
else
	echo bkpctl program succeeded for restore op 
fi

echo "verifying output"
# put reference output i.e version 3(newest) in a .ref file
echo $ver1_str > temp.ref

# now verify if we got the correct contents
if cmp temp.ref $file ; then
	echo "PASSED: output content matches with reference"
	exit 0
else
	echo "FAILED: output content differs with reference"
exit 1
fi
