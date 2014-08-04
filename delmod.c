#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <fcntl.h>

int
main(int argc, char **argv)
{
	if ( argc < 2 ) {
		fprintf(stderr,"Usage: %s <module_name>\n", argv[0]);
		return 1;
	}
	if ( syscall(SYS_delete_module, argv[1], O_NONBLOCK) ) {
		perror("delete_module");
		return 1;
	}
	return 0;
}
