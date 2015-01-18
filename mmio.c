#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <inttypes.h>
#include <getopt.h>

#include "arm-mmio.h"

#define LOP 4

static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-h] [-d <uio-device-file>] [-w <width>] reg-no [val]\n", nm);
	fprintf(stderr,"       NOTE: reg-no is 32-bit register #, NOT byte offset\n", nm);
	fprintf(stderr,"    HOWEVER: if '-w <width>' is given (1,2 or 4) then the\n");
	fprintf(stderr,"             offset IS a byte offset and thus '-w' allows\n");
	fprintf(stderr,"             for arbitrary, unaligned access\n");

}

int
main(int argc, char **argv)
{
const char *fnam = "/dev/uio0";
int  rval = 1;
int  i = 0, j = 0,o;
Arm_MMIO mio = 0;
int  opt;
long long v;
int  drain = 0;
int  wid   = 0;

	while ( (opt = getopt(argc, argv, "hd:Dw:")) > 0 ) {
		switch ( opt ) {
			case 'h': rval = 0;
			default: 
				usage(argv[0]);
				return rval;

			case 'd': fnam = optarg;
				break;

			case 'w': 
				if ( 1 != sscanf(optarg, "%lli", &v) || ( 1 != v && 2 != v && 4 != v)  ) {
					fprintf(stderr,"Invalid -w arg: must be 1,2 or 4\n");
					return 1;
				}
				wid = (int) v;
				break;

			case 'D': drain = 1;
				break;
		}
	}


	if ( argc - optind < 1 || 1 != sscanf(argv[optind],"%i", &o) ) {
		fprintf(stderr,"Need 1 or 2 args (argc: %i, optind %i)\n", argc, optind);
		return 1;
	}
	if ( argc - optind > 1 ) {
		if ( 1 != sscanf(argv[optind+1], "%lli", &v) ) {
			fprintf(stderr,"Unable to scan 2nd arg\n");
			return 1;
		} else {
			i = 1;
		}
	}

	if ( ! (mio = arm_mmio_init( fnam )) ) {
		goto bail;
	}

	printf("bar @0x%p\n", mio->bar);

	if (0 == j) {
		if ( drain ) {
			while ( (v=ioread32( mio, 9 )) ) {
				printf("** %08x\n", v);
				for ( i = (v&0x1ffff) >>2 ; i; i-- )  {
					printf("%08x\n", ioread32(mio, 8));
				}
			}
		} else {
			volatile uint8_t *a = (volatile uint8_t*)mio->bar;
			a += o;
			if ( i ) {
				switch ( wid ) {
					default: iowrite32( mio, o, v ); break;
					case 1:  *a = (uint8_t)v; break;
					case 2:  *(volatile uint16_t*)a = (uint16_t)v; break;
					case 4:  *(volatile uint32_t*)a = (uint32_t)v; break;
				}
			} else {
				uint32_t rv;
				const char *pre = "byte";
				switch ( wid ) {
					default: rv = ioread32(mio, o); pre = "reg"; break;
					case 1:  rv = (uint32_t)*a; break;
					case 2:  rv = (uint32_t)*(volatile uint16_t*)a; break;
					case 4:  rv = *(volatile uint32_t*)a; break;
				}
				printf("%s offset 0x%08x: 0x%08x\n", pre, o, rv);
			}
		}
	} else {

		for ( i=0; i<LOP; i++ ) {
			printf("%08x\n", ioread32(mio, i));
		}

		o &= 0xf;

		for ( i=0; i<LOP; i++ ) {
			uint32_t v = 0;
			for ( j = 0; j<8; j++ )
				v = (v<<4) | (o+i);
			iowrite32( mio, i, (uint32_t)v);
		}

		for ( i=0; i<LOP; i++ ) {
			printf("%08x\n", ioread32(mio, i));
		}

	}
	rval = 0;

bail:
	if ( mio )
		arm_mmio_exit( mio );
	return rval;
}
