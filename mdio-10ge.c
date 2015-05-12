/* Access MDIO registers on xilinx 10G-Ethernet */

#include <stdio.h>
#include <arm-mmio.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>

#define REG_C0 0x140 /* register, not byte-offset (0x500) */
#define REG_C1 0x141 /* register, not byte-offset (0x504) */
#define REG_TD 0x142 /* register, not byte-offset (0x508) */
#define REG_RD 0x143 /* register, not byte-offset (0x50c) */

#define DIV    62    /* 156.25MHz / 2.5MHz                */

#define OP_ADDR (0<<14)
#define OP_WRTE (1<<14)
#define OP_READ (3<<14)

#define CM_GO   0x800
#define ST_DONE 0x080

#define CMD(p,d,o) (((p)<<24) | ((d)<<16) | (o) | CM_GO)

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s -d <uio-device> [-P phy_portddr] [-D phy_devaddr] phy_reg [value]\n", nm);
	fprintf(stderr,"       phy_portaddr defaults to 0\n");
	fprintf(stderr,"       phy_devaddr  defaults to 1\n");
}

static void
exec_cmd(Arm_MMIO m, uint32_t cmd)
{
struct timespec t;
uint32_t st;
	iowrite32(m, REG_C1, cmd  );
	do {
		t.tv_sec  = 0;
		t.tv_nsec = 1000000;
		nanosleep(&t, 0);
		st = ioread32(m, REG_C1);
	} while ( ! (st & ST_DONE) );
}

int
main(int argc, char **argv)
{
int ch;
int rval = 1;
const char *devn = 0;
long long ll;
int p_prt = 0;
int p_dev = 1;
int *i_p;
int reg;
uint32_t v,x,cmd;
int have_v = 0;
Arm_MMIO m = 0;
	while ( (ch = getopt(argc, argv, "hd:P:D:")) > 0 ) {
		i_p = 0;
		switch ( ch ) {
			case 'h': rval = 0; /* fall thru */
			default: 
				usage(argv[0]);
				return rval;

			case 'd': devn = optarg; break;
			case 'P': i_p = &p_prt;  break;
			case 'D': i_p = &p_dev;  break;
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

	if ( argc <= optind || 1 != sscanf(argv[optind],"%i",&reg) ) {
		fprintf(stderr, "Need register arg\n");
		return rval;
	}

	if ( argc > optind+1 ) {
		if ( 1 != sscanf(argv[optind+1],"%lli",&ll) ) {
			fprintf(stderr,"Unable to parse 'value'\n");
			return rval;
		}
		v = (uint32_t)ll;
		have_v = 1;
	}

	m = arm_mmio_init(devn);
	if ( ! m ) {
		fprintf(stderr,"Unable to open device\n");
		return rval;
	}

	/* Make sure divider is initialized */
	x = ioread32(m, REG_C0);
	if ( x == 0 ) {
		fprintf(stderr,"Info: Setting divider to %i and enabling MDIO interface\n", DIV);
		iowrite32(m, REG_C0, (1<<6) | DIV);
	}

	cmd = CMD(p_prt, p_dev, 0);

	/* Address */
	iowrite32(m, REG_TD, reg);
	exec_cmd(m, cmd | OP_ADDR);

	if ( have_v ) {
		iowrite32(m, REG_TD, v);
		exec_cmd(m, cmd | OP_WRTE);
	} else {
		exec_cmd(m, cmd | OP_READ);
		printf("%d.%d: %08"PRIx32"\n", p_dev, reg, ioread32(m, REG_RD));
	}

	rval = 0;

bail:
	arm_mmio_exit( m );
	return rval;
}
