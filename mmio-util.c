#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>

#include "arm-mmio.h"

#define MAP_LEN 0x1000

static int verbose = 0;

Arm_MMIO
arm_mmio_init(const char *fnam)
{
	return arm_mmio_init_1(fnam, MAP_LEN);
}

Arm_MMIO
arm_mmio_init_1(const char *fnam, size_t len)
{
Arm_MMIO           rval = 0;
int                fd;
volatile uint32_t *bar = MAP_FAILED;
int  i = 0, j = 0,o,v;


	if ( (fd = open(fnam, O_RDWR)) < 0 ) {
		perror("opening");
		return 0;
	}

	bar = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if ( MAP_FAILED == bar ) {
		perror("mmap failed");
		goto bail;
	}

	if ( verbose )
		fprintf(stderr, "bar found @0x%p\n", bar);

	rval = malloc(sizeof(*rval));

	if ( rval ) {
		rval->fd  = fd;  fd = -1;
		rval->bar = bar; bar = MAP_FAILED;
		rval->lim = len;
	}

bail:
	if ( MAP_FAILED != bar )
		munmap( (void*)bar, len );
	if ( fd >= 0 )
		close( fd );
	return rval;
}

void
arm_mmio_exit(Arm_MMIO mio)
{
	if ( mio ) {
		munmap( (void*)mio->bar, mio->lim );
		close( mio->fd );
		free( mio );
	}
}
