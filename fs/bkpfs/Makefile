BKPFS_VERSION="0.1"

EXTRA_CFLAGS += -DBKPFS_VERSION=\"$(BKPFS_VERSION)\"

obj-$(CONFIG_WRAP_FS) += bkpfs.o

bkpfs-y := dentry.o file.o inode.o main.o super.o lookup.o mmap.o
