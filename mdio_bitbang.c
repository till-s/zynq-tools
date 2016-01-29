#include <arm-mmio.h>
#include <stdio.h>
#include <getopt.h>
#include <time.h>
#include <stdlib.h>
#include <gpiolib.h>

#define REG_CMD 1
#define REG_STA 5
#define  CMD_VAL (1<<5)
#define  CMD_CLK (1<<4)
#define  STA_VAL 0x80000000

#define GPIO_PIN_CLK 4
#define GPIO_PIN_OUT 5
#define GPIO_PIN_INP 11

#define GPIO_PIN_TYPE EMIO_PIN

typedef struct gpio_io_ {
	gpio_handle clk, out, inp;
} *gpio_io;

typedef struct io_ops_ {
	int (*send_bit) (void *ioc, int val);
	void (*clk_lo)  (void *ioc);
	void *ioc;
} *io_ops;

static void
clk_lo_gpio(void *arg)
{
gpio_io iop = (gpio_io)arg;
	gpio_clr(iop->clk);
}

static void
clk_lo_mmio(void *arg)
{
Arm_MMIO iop = (Arm_MMIO)arg;
uint32_t v = ioread32(iop, REG_CMD);
	v &= ~CMD_CLK;
	iowrite32(iop, REG_CMD, v);
}

static void
clk_lo(io_ops ops)
{
	ops->clk_lo(ops->ioc);
}

static int
send_bit(io_ops ops, int val)
{
	return ops->send_bit(ops->ioc, val);
}

static void nsdly(unsigned ns)
{
struct timespec dly;
	dly.tv_sec  = 0;
	dly.tv_nsec = ns;
	nanosleep( &dly, 0 );
}

static int
send_bit_gpio(void * arg, int bitval)
{
gpio_io iop = (gpio_io)arg;
struct timespec dly;
int    rv;

	
	if ( bitval )
		gpio_set(iop->out);
	else
		gpio_clr(iop->out);

	nsdly( 1000 );

	rv = gpio_get(iop->inp);

	if ( rv < 0 ) {
		perror("INTERNAL ERROR: Unable to read GPIO");
		exit(1);
	}
	
	gpio_set(iop->clk);	
	nsdly( 1000 );
	
	gpio_clr(iop->clk);

	return rv;
}

static int
send_bit_mmio(void *arg, int bitval)
{
Arm_MMIO iop = (Arm_MMIO)arg;
struct timespec dly;

uint32_t v = ioread32(iop, REG_CMD);
uint32_t rv;

	if ( bitval )
		v |= CMD_VAL;
	else
		v &= ~CMD_VAL;
	iowrite32(iop, REG_CMD, v);
	nsdly( 1000 );

	v |= CMD_CLK;
	rv = ioread32(iop, REG_STA);
	iowrite32(iop, REG_CMD, v);
	nsdly( 1000 );
	v &= ~CMD_CLK;
	iowrite32(iop, REG_CMD, v);

	return !!(rv & STA_VAL);
}

static struct io_ops_ mmio_ops = {
	send_bit: send_bit_mmio,
	clk_lo:   clk_lo_mmio,
};

static struct gpio_io_ gpio_ctxt = {
	0
};

static struct io_ops_ gpio_ops = {
	send_bit: send_bit_gpio,
	clk_lo:   clk_lo_gpio,
	ioc:      &gpio_ctxt
};

static uint16_t
send_word(io_ops iop, uint32_t tx)
{
int i;
uint32_t rx = 0;
printf("send word 0x%08"PRIx32"\n", tx);
	for ( i=0; i<32; i++) {
		rx = (rx<<1) | send_bit(iop, !! (tx & (1<<31)));
		tx <<= 1;
	}
	return (rx & 0xffff);
}

static uint16_t
mmio_xact(io_ops iop, int phy, int reg, int val)
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
	fprintf(stderr,"Usage: %s [-p phy] [-r reg] [-v val] [-hm]\n", nm);
}

int
main(int argc, char **argv)
{
io_ops   iop = &gpio_ops;
int      phy =  4;
int      reg =  0;
int      val = -1;
int      opt;
int     *i_p;
int      rval = 1;
uint16_t got;
int      use_mmio = 0;
	
	while ( ( opt = getopt(argc, argv, "p:r:v:hm")) > 0 ) {
		i_p = 0;
		switch (opt) {
			case 'p': i_p = &phy; break;
			case 'r': i_p = &reg; break;
			case 'v': i_p = &val; break;
			case 'm': use_mmio = 1; break;
			case 'h':
				rval = 0;
			default:
				usage(argv[0]); return rval;
		}
		if ( i_p && 1 != sscanf(optarg,"%i",i_p) ) {
			fprintf(stderr,"Unable to scan option %c\n", opt);
			return 1;
		}
	}

	if ( use_mmio ) {
		iop = &mmio_ops;
		if ( ! (iop->ioc = arm_mmio_init("/dev/uio2") ) ) {
			fprintf(stderr,"Unable to open MMIO/UIO device\n");
			return 1;
		}
	} else {
		if ( ! (gpio_ctxt.clk = gpio_open( GPIO_PIN_CLK, GPIO_PIN_TYPE )) ) {
			fprintf(stderr,"Unable to open GPIO CLK pin\n");
			return 1;
		}
		if ( ! (gpio_ctxt.out = gpio_open( GPIO_PIN_OUT, GPIO_PIN_TYPE )) ) {
			fprintf(stderr,"Unable to open GPIO OUT pin\n");
			gpio_close(gpio_ctxt.clk);
			return 1;
		}
		if ( ! (gpio_ctxt.inp = gpio_open( GPIO_PIN_INP, GPIO_PIN_TYPE )) ) {
			fprintf(stderr,"Unable to open GPIO INP pin\n");
			gpio_close(gpio_ctxt.clk);
			gpio_close(gpio_ctxt.out);
			return 1;
		}
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
