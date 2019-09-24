INC=/lib/modules/$(shell uname -r)/build/arch/x86/include
INC1=/lib/modules/$(shell uname -r)/build/include

all: bkpctl writer

bkpctl: bkpctl.c
	gcc -Wall -Werror -I$(INC1) -I$(INC)/generated -I$(INC)/uapi bkpctl.c -o bkpctl

writer: writer.c
	gcc -Wall -Werror writer.c -o writer

clean:
	rm -f bkpctl writer *.o
