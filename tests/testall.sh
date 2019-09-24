#!/bin/sh

mount_dir=$1
myfile=${mount_dir}/myfile.txt

if [ -z $mount_dir ]; then
	echo "Missing argument: mount directory path"
	exit 1
fi

TOTAL_TESTS=15
rm -rf result.txt
rm -rf *.ref *.out

for num in $(seq 1 $TOTAL_TESTS)
	do 
		echo "Running test$num on $myfile" 
		sh test${num}.sh $myfile >> result.txt
	done

echo "Total Tests Run: $TOTAL_TESTS"
NUM_PASSED=$(grep "PASSED" ./result.txt | wc -l)
echo "Total Tests Passed: $NUM_PASSED"
