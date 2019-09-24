#ifndef COMMON_H
#define COMMON_H

#include <linux/ioctl.h>

/* 
 *  The major device number. We can't rely on dynamic 
 *  registration any more, because ioctls need to know 
 *  it. 
 */
#define MAJOR_NUM 484
#define IOCTL_GET_MAX_VERS 		_IOR  (MAJOR_NUM, 0, long)
#define IOCTL_GET_NUM_VERS 		_IOR  (MAJOR_NUM, 1, long)
#define IOCTL_LIST_VERS			_IOR  (MAJOR_NUM, 2, long)	
#define IOCTL_RESTORE_VERS 		_IOW  (MAJOR_NUM, 3, long)
#define IOCTL_DELETE_VERS 		_IOW  (MAJOR_NUM, 4, long)
#define IOCTL_VIEW_VERS 		_IOWR (MAJOR_NUM, 5, long)
#define IOCTL_GET_FILE_SIZE		_IOWR (MAJOR_NUM, 6, long)

struct ioctl_args
{
	unsigned int in_arg_size; 	// size of in_arg
	void  *in_arg; 				// this will contain ioctl specific arguments
	unsigned int buff_size; 	// size of the buffer
	void *buff; 				// buffer to contain the result of the ioctl
};

struct view_args {
	int version;
	long long offset;
};

struct delete_args {
	int version;
};

struct restore_args {
	int version;
};

/*
 * The name of the device file 
 */

#define DEVICE_FILE_NAME "bkpfs"

#endif

