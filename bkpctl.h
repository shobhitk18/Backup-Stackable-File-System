#ifndef BKPCTL_H
#define BKPCTL_H

#include <linux/bkp_shared.h>


#define bool 						short
#define FALSE						0
#define TRUE						1
#define STATUS_OK					0
#define STATUS_ERR					-1
#define MAX_FILE_PATH_LEN_BYTES 	(4096-1) 	//-1 is to account for the null character
#define MAX_FILENAME_LEN_BYTES		255 
#define MAX_ARG_LEN					10
#define PAGE_SIZE 					4096

#define DEBUG 				printf("Line = %d, Function :%s, File: %s\n",__LINE__,__FUNCTION__, __FILE__ );
#define ASSERT(cond)											\
		if(!(cond)) 											\
		{														\
			printf("ASSERT for condition %s failed\n", #cond);	\
			return STATUS_ERR;									\
		}														\
							
/*
 * Data Structure for storing the user input as read from
 * the command line. This will be used for validating and 
 * then it will be used to populate the user_data structure
 * which is finally passed to the ioctl system call.
 */

#define DELETE_FLAG 		0x1
#define VIEW_FLAG			0x2
#define RESTORE_FLAG		0x4
#define LIST_FLAG			0x8

typedef struct user_ip_t
{
	char* filename;
	char* delete_arg;
	char* view_arg;
	char* restore_arg;
	unsigned int op_flags;
}user_inp;

/*
 * Below are the list of Functions implemented in xpcenc.c
 * Add an entry here for any function implemented there
 */
int do_ioctl_call(int fd, int opcode, struct ioctl_args *msg);
void display_help(void);
int validate_user_input(user_inp *input);

#endif

