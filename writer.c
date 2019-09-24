#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

void print_help()
{
        printf("Usage: \n");
        printf("./file_writer <filename> <no of iterations> <block size in KB>\n");
        printf("e.g. ./file_writer hello.txt 100 4\n");
}

char *get_buff(int blk_size)
{
        char *ptr = (char *)malloc(blk_size);
		srand(time(0));
        memset(ptr, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[rand() % 26], blk_size);
        return ptr;
}


int main(int argc, char *argv[])
{
        int no_iter, blk_size;
        int fd;
        char *buff = NULL;
		int offset;

        if (argc != 5) {
                print_help();
                return 1;
        } else {
		//Simple function to open a file and write in blk size units. 
		no_iter = atoi(argv[2]);
		blk_size = atoi(argv[3]);
		offset = atoi(argv[4]);

		int iter = 0;
		while (no_iter--) {
			buff = get_buff(blk_size);
			fd = open(argv[1], O_CREAT | O_WRONLY, S_IRWXU);
			offset = lseek(fd, offset, SEEK_SET);
			printf("write at offset = %d\n", offset);
			if ( write(fd, buff, blk_size) == -1)
				perror("write failed\n");
			offset = offset + (iter * blk_size) ;
			iter++;
			close(fd);      
		}
		if (buff)
			free(buff);
	}

	return 0;
}

