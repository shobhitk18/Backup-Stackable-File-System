echo "unmounting bkpfs fs"
umount /mnt/bkpfs
echo "removing bkpfs module"
rmmod bkpfs.ko
echo "building bkpfs module"
make modules -C ../
ret=$?
if test $ret != 0; then
	echo "bkpfs build failed with return error=$ret"
	exit $ret
else
	echo "bkpfs build Succeeded"
fi
make clean; make
ret=$?
if test $ret != 0; then
	echo "bkpctl build failed with return error=$ret"
	exit $ret
else
	echo "bkpctl build Succeeded"
fi
dmesg --clear
mkdir -p /mnt/bkpfs
mkdir -p /test/dir1
echo "inserting bkpfs module"
insmod ../fs/bkpfs/bkpfs.ko
echo "mounting bkpfs fs"
mount -t bkpfs -o maxvers=3,bkp_threshold=8 /test/dir1 /mnt/bkpfs
lsmod

