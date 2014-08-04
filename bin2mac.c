/* convert 6 bytes to MAC address */
#include <stdio.h>
#include <getopt.h>
#include <inttypes.h>

static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-n] <filename>\n", nm);
	fprintf(stderr,"       convert first 6 bytes in <filename>\n");
	fprintf(stderr,"       to ASCII representation, separated\n");
	fprintf(stderr,"       by colons (ethernet address)\n");
	fprintf(stderr,"       Read from stdin if no filename is given\n");
	fprintf(stderr,"  [-n] Do not print trailing newline\n");
	fprintf(stderr,"  [-h] Print this message\n");
}

#define SIZ 6

int
main(int argc, char **argv)
{
FILE *f;
uint8_t buf[SIZ];
int   rval = 1;
int   i;
int   nl   = 1;

	while ( ( i = getopt(argc, argv, "hn") ) > 0 ) {
		switch ( i ) {
			case 'h':
				rval = 0;
			default:
				usage(argv[0]);
				goto bail;

			case 'n':
				nl = 0;
			break;
		}
	}

	if ( argc > optind ) {
		if ( ! (f = fopen(argv[optind],"r")) ) {
			perror("Opening file");
			goto bail;
		}
	} else {
		f = stdin;
	}

	if ( SIZ != fread(buf, 1, sizeof(buf), f) ) {
		perror("Reading file");
		goto bail;
	}

	for (i=0; i < SIZ-1; i++ )
		printf("%02"PRIX8":", buf[i]);
	printf("%02"PRIX8, buf[i]);
	if ( nl )
		printf("\n");

	rval = 0;

bail:
	if ( f && argc > 1 )
		fclose(f);
	return rval;
}
