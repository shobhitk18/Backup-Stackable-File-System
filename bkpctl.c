#include <asm/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "bkpctl.h"

#ifdef MYDEBUG
static void print_input(user_inp *inp)
{
	printf("------- struct user_inp begin ------\n"); 
	
	printf("Input File = %s\n", inp->filename);
	if(inp->op_flags & LIST_FLAG)
		printf("list versions = True\n");
	if(inp->op_flags & DELETE_FLAG)
		printf("delete version args = %s\n",inp->delete_arg);
	if(inp->op_flags & VIEW_FLAG)
		printf("view version args = %s\n",inp->view_arg);
	if(inp->op_flags & RESTORE_FLAG)
		printf("restore version args = %s\n",inp->restore_arg);

	printf("-------  struct user_inp end  -------\n"); 
	return;
}
#endif

/*	 @brief : displays help/usage
 */
void display_help(void)
{
	printf("<USAGE>\n\t\t ./bkcpts -l -d <newest,oldest,all> -v <newest,oldest,N> -r <newest,N> \"FILE\" \n\n");
	printf("eg ./bkpctl -l <file>\n");
	printf("=>Only combination supported is with <any option> -l\n");
	printf("Options:\n");
	printf("-l : list available backup versions\n");
	printf("-v [N] : view backup versions\n");
	printf("-d [newest,oldest,all] : delete backup versions\n");
	printf("-r [newest,N] : restore backup version\n");
	printf("=> Combination of multiple options not supported except for -l option\n");
	printf("=> Options that take N as argument implies N = version number\n");
	printf("=> File is MANDATORY ARGUMENT\n");
	return;
}

/*
 * 	This API is a wrapper to make the ioctl syscall on 
 * 	behalf of the user program. 
 * 	Input : ioctl opcode, ioclt_args
 * 	Return 0 on SUCCESS
 * 	Return errno for ERROR
 */
int do_ioctl_call(int fd, int cmd, struct ioctl_args *args)
{
	int status;
	unsigned long msg = (unsigned long)args; //Typecasting is dangerous as pointer maybe 64bit for some arch
	
	status = ioctl(fd ,cmd, msg);
	if (status < 0)
		status = STATUS_ERR;
	else
		status = STATUS_OK;
#ifdef MYDEBUG
	printf("ioctl returned %d (errno=%d)\n", status, errno);
#endif
	return status;	
}

/* @brief: get number of backup versions currently
 * present for the user file
 */
int get_num_versions(int fd)
{
	int rc = STATUS_OK;
	unsigned int count;

	rc = ioctl(fd, IOCTL_GET_NUM_VERS, &count);
	if(rc < 0)
		return STATUS_ERR;	

	return count;
}
/* @brief:  get max backup version supported by the fs
 * retention policy requires older version to be deleted
 * when max threshold is reached.
 */

int get_max_versions(int fd)
{
	int rc = STATUS_OK;
	unsigned int max_vers = 0;

	rc = ioctl(fd, IOCTL_GET_MAX_VERS, &max_vers);
	if(rc < 0)
		return STATUS_ERR;	

	return max_vers;
}

/* @brief: get file size for the backup with input 
 * version number
 */
long long get_bkp_file_size(int fd, int ver)
{
	int rc = STATUS_OK;
	struct ioctl_args *msg;
	long long size;

	msg = (struct ioctl_args *) malloc(sizeof(struct ioctl_args));
	msg->in_arg_size = sizeof(ver);
	msg->in_arg = &ver;
	msg->buff_size = sizeof(size);
	msg->buff = &size;

	rc = do_ioctl_call(fd, IOCTL_GET_FILE_SIZE, msg);
	if(rc < 0){
		free(msg);
		if(errno == 2)
			printf("ERROR::backup version doesn't exist, try listing versions\n");
		return STATUS_ERR;
	}

#ifdef MYDEBUG
	printf("Size of backup ver %d = %llu\n", ver, size);
#endif
	free(msg);
	return size;
}

/* @brief: This API will perform the restore operation on the backups.
 * sends  0 for newest version
 *		  N>0 for any valid version
 */

int restore_backup_version(int fd, user_inp *input)
{
	int rc = STATUS_OK;
	struct ioctl_args *msg;
	struct restore_args *in_arg;
	int version;

	msg = (struct ioctl_args *) malloc(sizeof(struct ioctl_args));
	in_arg = (struct restore_args *) malloc(sizeof(struct restore_args));

	if(strcmp(input->restore_arg, "newest") == 0)
		version = 0;
	else 
		version = atoi(input->restore_arg);  // For "N", already validated before

	in_arg->version = version;
#ifdef MYDEBUG	
	printf("version to restore = %d\n",version);
#endif
	msg->in_arg_size = sizeof(struct restore_args);
	msg->in_arg = in_arg;
	msg->buff_size = 0;
	msg->buff = NULL;

	rc = do_ioctl_call(fd, IOCTL_RESTORE_VERS, msg);
	if(rc < 0)
		rc = STATUS_ERR;	

	free(in_arg);
	free(msg);
	return rc;
}

/* @brief: This API will perform the view operation on the backups.
 * sends -1 to kernel module for oldest version
 *		  0 for newest version
 *		  N>0 for any valid version
 *	view data in chunks of PAGE_SIZE and dispays the data on stdout.
 */

int view_backup_version(int fd, user_inp *input)
{
	int rc = STATUS_OK;
	struct ioctl_args *msg;
	struct view_args *in_arg;
	void *buff;
	int version;
	long long size;
	unsigned long bytes_to_copy, offset = 0;
	unsigned int total_bytes_left;

	msg = (struct ioctl_args *) malloc(sizeof(struct ioctl_args));
	in_arg = (struct view_args *) malloc(sizeof(struct view_args));
	buff = malloc(PAGE_SIZE);
	
	if(strcmp(input->view_arg, "newest") == 0)
		version = 0;
	else if(strcmp(input->view_arg, "oldest") == 0)
		version = -1;
	else 
		version = atoi(input->view_arg);  // For "N", already validated before

	in_arg->version = version;
	in_arg->offset = 0;
#ifdef MYDEBUG	
	printf("version to view = %d\n",version);
#endif
	msg->in_arg_size = sizeof(struct view_args);
	msg->in_arg = in_arg;
	msg->buff = buff;

	size = get_bkp_file_size(fd, version);
	if(size < 0) {
		printf("ERROR::failed retrieving size for backup file.\n");
		rc = STATUS_ERR;
		goto out;
	}

	total_bytes_left = size;
	while(total_bytes_left > 0) {

		memset(buff, '\0', PAGE_SIZE);
		bytes_to_copy = total_bytes_left;
		if(bytes_to_copy > PAGE_SIZE)
			bytes_to_copy =  PAGE_SIZE;

		msg->buff_size = bytes_to_copy;
		rc = do_ioctl_call(fd, IOCTL_VIEW_VERS, msg);
		if(rc < 0) {
			printf("ERROR::failed viewing at offset %ld\n", offset);
			break;
		}
		/* Increment offset for next view operation */
		offset += bytes_to_copy;
		in_arg->offset = offset;
		
		total_bytes_left -= bytes_to_copy;
		printf("%s", (char*)msg->buff);	
	}

out:
	free(in_arg);
	free(buff);
	free(msg);
	return rc;
}

/* @brief: This API will perform the delete operation on the backups.
 * sends -1 to kernel module for oldest version
 *		  0 for newest version
 *		  1 for all versions
 */
int delete_backup_version(int fd, user_inp *input)
{
	int rc = STATUS_OK;
	int version;
	struct ioctl_args *msg;
	struct delete_args *in_arg;

	msg = (struct ioctl_args *) malloc(sizeof(struct ioctl_args));
	in_arg = malloc(sizeof(struct delete_args));
	
	if(strcmp(input->delete_arg, "newest") == 0)
		version = 0;
	else if(strcmp(input->delete_arg, "oldest") == 0)
		version = -1;
	else
		version = 1;  // For "all"
	
	in_arg->version  = version;

	msg->in_arg = in_arg;
	msg->in_arg_size = sizeof(struct delete_args);
	msg->buff_size = 0;
	msg->buff = NULL;

	rc = do_ioctl_call(fd, IOCTL_DELETE_VERS, msg);
	if(rc < 0) {
		goto out;
	}
#ifdef MYDEBUG
	printf("Deleted [ %s ] backup\n", input->delete_arg);
#endif
out:
	free(msg);
	free(in_arg);
	return rc;

}

/* @brief this API will gets the backup versions present for the 
 * file and prints them to console
 */
int list_backup_version(int fd, user_inp *input)
{
	int num_vers;
	const char *name; 
	int i;

	num_vers = get_num_versions(fd);
	if(num_vers < 0) {
		printf("error::failed to get number of bkp versions of the file\n");
		goto out;
	}
#ifdef MYDEBUG
	printf("number of current backup versions for file = %d\n", num_vers);
#endif
	printf("*********** listing backup versions for file %s ***********\n",input->filename);
	
	if(num_vers == 0) {
		printf("No backups exist\n");
		goto out;
	}
	name = strrchr(input->filename, '/');

	printf("VERSION NO. \t BACKUP NAME\n");
	for(i = 1; i <= num_vers; i++)
		printf("%d. \t\t ./bkp_%s.%d\n", i, name+1, i);

out:
	return num_vers;
}

int handle_input(user_inp *input)
{
	/* Handle various option permutation/combination here */
	int rc = STATUS_OK;
	int fd, max_vers;

	/* Open the file and start doing some actual work */
	fd = open(input->filename, O_RDONLY);
	if (fd < 0) {
		printf("ERROR::can't open file: %s\n", input->filename);
		rc = STATUS_ERR;
		goto out;
	}
	
	max_vers = get_max_versions(fd);
	if(max_vers < 0) {
		printf("ERROR::failed to get max versions supported by bkpfs\n");
		rc = max_vers;
		goto out;
	}
#ifdef MYDEBUG
	printf("Max versions supported by bkpfs fs = %d\n", max_vers);
#endif
	/* We allow user to combine various options together, incase there are 
	 * multiple options passed by the user, Following is the priority
	 * sequence:
	 * 
	 *  view version -> restore version -> delete version -> list version
	 */
	if (input->op_flags & VIEW_FLAG) {
		rc = view_backup_version(fd, input);
		if (rc < 0)
			goto out;
	}
	
	if (input->op_flags & RESTORE_FLAG) {
		rc = restore_backup_version(fd, input);
		if (rc < 0)
			goto out;
	}
	
	if (input->op_flags & DELETE_FLAG) {
		rc = delete_backup_version(fd, input);
		if (rc < 0)
			goto out;
	}

	if(input->op_flags & LIST_FLAG) {
		rc = list_backup_version(fd, input);
		if (rc < 0)
			goto out;
	}

out:
	/* close the file before exiting from this function */
	close(fd);
	return rc;
}

int validate_input_args(user_inp * input)
{
	int status = STATUS_OK;
	const char *arg; 
	int num_opts = 0;
	do
	{
		// Add any user level checks here as and when discovered	
		if(input->op_flags & LIST_FLAG) {
#ifdef MYDEBUG
			printf("List version option passed\n");
#endif
			num_opts++;
		}

		if(input->op_flags & DELETE_FLAG) {
			arg = input->delete_arg;
#ifdef MYDEBUG
			printf("Delete version option passed with arg=%s\n", arg);
#endif
			if(strcmp(arg, "newest") && strcmp(arg, "oldest") && strcmp(arg, "all")) 
			{
				printf("ERROR::Invalid argument for delete operation\n");
				status = STATUS_ERR;	
				break;
			}
			num_opts++;
		}

		if(input->op_flags & VIEW_FLAG) {
			arg = input->view_arg;
#ifdef MYDEBUG
			printf("View version option passed with arg=%s\n", arg);
#endif
			if(strcmp(arg, "newest") && strcmp(arg, "oldest") && atoi(arg) <= 0)
			{
				printf("ERROR::Invalid argument for view operation\n");
				status = STATUS_ERR;	
				break;
			}
			num_opts++;
		}

		if(input->op_flags & RESTORE_FLAG) {
			arg = input->restore_arg;
#ifdef MYDEBUG
			printf("Restore version option passed with arg=%s\n", arg);
#endif
			if(strcmp(arg, "newest") && atoi(arg) <= 0) 
			{
				printf("ERROR::Invalid argument for restore operation\n");
				status = STATUS_ERR;	
				break;
			}
			num_opts++;
		}

		if(num_opts > 2 || (num_opts == 2 && !(input->op_flags & LIST_FLAG))){
			printf("ERROR::Option combination not supported\n");
			status = STATUS_ERR;
			break;
		}

	}while(0);

	return status;
}


int main(int argc, char *argv[])
{
	int ret = STATUS_OK;
	int opt, idx, len;
	char *arg;
	user_inp *input = (user_inp*)calloc(1,sizeof(user_inp));
	/*
	 * References: linux manual - getopt()
	 */
	const char* valid_opt = ":lhd:v:r:";

	while((opt = getopt(argc, argv, valid_opt)) != -1)
	{
		switch(opt)
		{
			/* List versions */
			case 'l':
				input->op_flags |= LIST_FLAG;
				break;

			/* Delete version */
			case 'd': 
				/* Empty Statement to make Compiler happy */
				/* Limiting size of key to 4K for now. This key will be encrypted and passed to the kernel later */
				len = strlen(optarg);
				if(len > MAX_ARG_LEN)
				{
					printf("ERROR::Length of delete argument is greater than expected\n");
					ret = STATUS_ERR;
					goto out;
				}	
				arg = (char*) malloc(sizeof(char) * len+1);
				input->delete_arg = arg;
				strcpy(input->delete_arg, optarg);
				input->op_flags |= DELETE_FLAG;
				break;
	
			/* View version */
			case 'v':
				; 
				/* Empty Statement to make Compiler happy */
				/* Limiting size of key to 4K for now. This key will be encrypted and passed to the kernel later */
				len = strlen(optarg);
				if(len > MAX_ARG_LEN)
				{
					printf("ERROR::Length of view argument is greater than expected\n");
					ret = STATUS_ERR;
					goto out;
				}	
				arg = (char*) malloc(sizeof(char) * len+1);
				input->view_arg = arg;
				strcpy(input->view_arg, optarg);
				input->op_flags |= VIEW_FLAG;
				break;
		
			/* Restore version */
			case 'r':
				/* Empty Statement to make Compiler happy */
				/* Limiting size of key to 4K for now. This key will be encrypted and passed to the kernel later */
				len = strlen(optarg);
				if(len > MAX_ARG_LEN)
				{
					printf("ERROR::Length of restore argument is greater than expected\n");
					ret = STATUS_ERR;
					goto out;
				}	
				arg = (char*) malloc(sizeof(char) * len+1);
				input->restore_arg = arg;
				strcpy(input->restore_arg, optarg);
				input->op_flags |= RESTORE_FLAG;
				break;

			/* Help/Usage Option */	
			case 'h':
				display_help();
				ret = STATUS_OK;
				goto out;
		default:
				if(opt == '?')
					printf("ERROR::Unknown option : %c passed\n",(char)optopt); 
				else if(opt == ':')	
					printf("ERROR::Missing Argument for option: %c\n",(char)optopt);

				ret = STATUS_ERR;
				goto out;
		}
	}
	
	if(argc - optind > 1)
	{
		printf("ERROR::More arguments provided than required. Please see Usage\n");
		display_help();
		ret = STATUS_ERR;
		goto out;
	}

	if(argc - optind < 1)	
	{
		printf("ERROR::File name missing.Please see Usage\n");
		display_help();
		ret = STATUS_ERR;
		goto out;
	}
	
	idx = optind;
	
	len = strlen(argv[idx])+1;	
	input->filename = (char*) malloc(len+1);
	strcpy(input->filename, argv[idx]);	

#ifdef MYDEBUG 	
	print_input(input);
#endif
	if(validate_input_args(input) == STATUS_OK)
		ret = handle_input(input);
	else 
		ret = STATUS_ERR;

	/* Free all buffers allocated before exiting */
	free(input->filename);

out:
	if(input->delete_arg != NULL)
		free(input->delete_arg);
	
	if(input->restore_arg != NULL)
		free(input->restore_arg);

	if(input->view_arg != NULL)
		free(input->view_arg);
	
	
	if(ret < 0)
		printf("failed with err = %d\n", errno);
	return (ret);
}
