#include <arm-mmio.h>
#include <stdio.h>
#include <getopt.h>

#define REG_CMD 1
#define REG_STA 5
#define  CMD_VAL (1<<5)
#define  CMD_CLK (1<<4)
#define  STA_VAL 0x80000000

static void
clk_lo(Arm_MMIO iop)
{
uint32_t v = ioread32(iop, REG_CMD);
	v &= ~CMD_CLK;
	iowrite32(iop, REG_CMD, v);
}

static int
send_bit(Arm_MMIO iop, int bitval)
{
uint32_t v = ioread32(iop, REG_CMD);
	if ( bitval )
		v |= CMD_VAL;
	else
		v &= ~CMD_VAL;
	iowrite32(iop, REG_CMD, v);
	v |= CMD_CLK;
	iowrite32(iop, REG_CMD, v);
	v &= ~CMD_CLK;
	iowrite32(iop, REG_CMD, v);
	v = ioread32(iop, REG_STA);
	return !!(v & STA_VAL);
}

static uint16_t
send_word(Arm_MMIO iop, uint32_t tx)
{
int i;
uint32_t rx = 0;
printf("send word 0x%08"PRIx32"\n", tx);
	for ( i=0; i<32; i++) {
		rx = (rx<<1) | send_bit(iop, !! (tx & (1<<31)));
		tx <<= 1;
	}
	return ((rx>>1) & 0xffff);
}

static uint16_t
mmio_xact(Arm_MMIO iop, int phy, int reg, int val)
{
uint32_t v = 0x40000000;

	v |= ( (phy << 7) | (reg << 2) ) << 16;

	if ( val < 0 ) {
		/* read */
		v |= 0x20000000;
	} else {
		v |= 0x10020000 | (val & 0xffff);
	}
	send_word(iop, 0xffffffff); /* preamble */
	return send_word(iop, v);
}

static void 
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-p phy] [-r reg] [-v val] [-h]\n", nm);
}

int
main(int argc, char **argv)
{
Arm_MMIO iop = arm_mmio_init("/dev/uio2");
int      phy =  4;
int      reg =  0;
int      val = -1;
int      opt;
int     *i_p;
int      rval = 1;
uint16_t got;
	
	while ( ( opt = getopt(argc, argv, "p:r:v:h")) > 0 ) {
		i_p = 0;
		switch (opt) {
			case 'p': i_p = &phy; break;
			case 'r': i_p = &reg; break;
			case 'v': i_p = &val; break;
			case 'h':
				rval = 0;
			default:
				usage(argv[0]); return rval;
		}
		if ( 1 != sscanf(optarg,"%i",i_p) ) {
			fprintf(stderr,"Unable to scan option %c\n", opt);
			return 1;
		}
	}

	if ( ! iop ) {
		fprintf(stderr,"Unable to open MMIO/UIO device\n");
		return 1;
	}

	if ( phy < 0 || (phy == 0 && val < 0 ) || phy > 31 ) {
		fprintf(stderr,"Invalid phy #%i\n", reg);
		return 1;
	}
	if ( reg < 0 || reg > 31 ) {
		fprintf(stderr,"Invalid reg #%i\n", reg);
		return 1;
	}

	if ( val > 0xffff ) {
		fprintf(stderr,"Value out of range\n");
		return 1;
	}

	clk_lo( iop );
	
	got = mmio_xact(iop, phy, reg, val);
	if ( val < 0 ) {
		printf("MMIO (phy %d) @reg %d: 0x%04"PRIx16"\n",
				phy,
				reg,
				got);
	}

	clk_lo( iop );
	return 0;
}
