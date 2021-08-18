#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>

static void usage(const char *nm)
{
	printf("Usage: %s -d /dev/uioX\n", nm);
	printf("       enable UIO IRQ and block for event\n");
}

int
main(int argc, char **argv)
{
int         opt;
int         fd   = -1;
int         rval = 1;
const char *dev  = 0;
uint32_t    val;

	while ( (opt = getopt(argc, argv, "hd:")) > 0 ) {
		switch ( opt ) {
			case 'h':
				rval = 0;
			default:
				usage( argv[0] );
				return rval;
			case 'd':
				dev = optarg;
				break;
		}
	}

	if ( ! dev ) {
		fprintf(stderr, "Need UIO device file (-d option)\n");
		return 1;
	}

	if ( (fd = open( dev, O_RDWR )) < 0 ) {
		perror("Opening device file");
		goto bail;
	}

	val = 1;
	if ( sizeof(val) != write( fd, &val, sizeof(val) ) ) {
		perror("Writing to enable IRQ");
		goto bail;
	}

	if ( sizeof(val) != read( fd, &val, sizeof(val) ) ) {
		perror("Reading/blocking for IRQ");
		goto bail;
	}

	printf("Interrupts: %" PRIu32 "\n", val);


	rval = 0;

bail:
	if ( fd >= 0 )
		close( fd );
	
	return rval;
}
