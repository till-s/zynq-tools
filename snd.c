/* Send ethernet frame */

#include <stdio.h>
#include <arm-mmio.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>

#define REG_TFIFO 4
#define REG_TLAST 5

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s -d <uio-device> [-s]\n",nm);
	fprintf(stderr,"       [-s] issue TLAST - otherwise don't\n");
}

uint32_t pkt[128];

int
main(int argc, char **argv)
{
int ch;
int *i_p;
long long ll;
int rval = 1;
const char *devn = 0;
int do_lst = 0;
Arm_MMIO m = 0;
int i;

	while ( (ch = getopt(argc, argv, "hd:s")) > 0 ) {
		i_p = 0;
		switch ( ch ) {
			case 'h': rval = 0; /* fall thru */
			default: 
				usage(argv[0]);
				return rval;

			case 'd': devn = optarg; break;
			case 's': do_lst = 1;    break;
		}

		if ( i_p ) {
			if ( 1 != sscanf(optarg,"%lli",&ll) ) {
				fprintf(stderr,"Unable to parse (integer) arg to -%c: %s\n", optarg, ch);
				return rval;
			}
			*i_p = (int) ll;
		}
	}
	if ( !devn ) {
		fprintf(stderr,"Need -d <uio_dev> argument\n");
		return rval;
	}

	m = arm_mmio_init(devn);
	if ( ! m ) {
		fprintf(stderr,"Unable to open device\n");
		return rval;
	}

	/* Addresses (LSB goes out first) */
	i = 0;
	pkt[i++] = 0x02350a00;
	pkt[i++] = 0x54529de6;
	pkt[i++] = 0xec853b00;
	pkt[i++] = 0x00000008;
	while ( i < sizeof(pkt)/sizeof(pkt[0]) ) {
		pkt[i++] = i;
	}

	for ( i = 0; i<sizeof(pkt)/sizeof(pkt[0]); i++ ) {
		iowrite32(m, REG_TFIFO, pkt[i]);
	}

	if ( do_lst )
		iowrite32(m, REG_TLAST, 4);

	rval = 0;

bail:
	arm_mmio_exit( m );
	return rval;
}
