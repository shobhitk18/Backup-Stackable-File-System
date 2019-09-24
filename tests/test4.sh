#!/bin/sh
#test4 - basic delete <oldest> version functionality 

echo "############ test4 - basic delete <oldest> version functionality ###########"
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
# call bkpctl to delete the oldest version i.e ver1_str and
# then check if correct version is deleted. i.e new oldest = ver2_str
../bkpctl $file -d oldest
retval=$?
echo "return value for delete op=$retval"
if [ $retval -lt 0 ] ; then
	echo bkpctl failed with error: $retval
	exit $retval
else
	echo bkpctl program succeeded for delete op 
fi

echo "verifying output"
# put reference output i.e ver2 contents in a .ref file
echo $ver2_str > temp.ref

../bkpctl $file -v oldest > temp.out
retval=$?
echo "return value for view op=$retval"
if [ $retval -lt 0 ] ; then
	echo bkpctl failed with error: $retval
	exit $retval
else
	echo bkpctl program succeeded for view op 
fi

# now verify if we deleted the correct version
if cmp temp.ref temp.out ; then
	echo "PASSED: output content matches with reference"
	exit 0
else
	echo "FAILED: output content differs with reference"
exit 1
fi

