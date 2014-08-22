#include <stdio.h>
#include <inttypes.h>
#include "arm-mmio.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

/* period */
#define NP 109
#define AMP 10000.0

#define FIFO_SZ 512
#define VAC_FUL 450
#define VAC_EMP  50

#define P_DFLT 2000

#define ST_RX_EMPTY (1<<19)
#define ST_RX_FULL  (1<<20)
#define ST_RX_RST_DON (1<<23)

#define ST_TX_EMPTY (1<<21)
#define ST_TX_FULL  (1<<22)
#define ST_TX_RST_DON (1<<24)

/* Reg. indices */
#define REG_ST		0
#define REG_IEN		1    /* IRQ enable     */
#define REG_TX_RST  2    /* TX fifo reset  */
#define    VAC_MSK     ((1<<12)-1)
#define REG_TX_VAC  3    /* TX vacancy     */
#define    RST_KEY     0x000000A5
#define REG_TX      4    /* TX fifo        */
#define REG_RX_RST  6    /* RX fifo reset  */
#define REG_RX_OCC  7    /* RX occupancy   */
#define REG_RX      8    /* RX fifo        */
#define REG_TX_DEST 0xb  /* TX destination */

static void
ack_irq(Arm_MMIO mmio, uint32_t msk)
{
	iowrite32( mmio, REG_ST, msk );
}

static int
set_irq(Arm_MMIO mmio, int32_t on)
{
	if ( sizeof(on) != write(mmio->fd, &on, sizeof(on)) )
		return -1;
}

static int
enb_irq(Arm_MMIO mmio)
{
	return set_irq(mmio, 1);
}

static int
dis_irq(Arm_MMIO mmio)
{
	return set_irq(mmio, 1);
}

static int
block_irq(Arm_MMIO mmio, uint32_t msk)
{
uint32_t got;
	if ( sizeof(got) != read(mmio->fd, &got, sizeof(got)) ) {
		return -1;
	}
}

uint32_t
fill_fifo(Arm_MMIO mmio, uint32_t *tab, unsigned sz, int irq)
{
int i;
uint32_t sta, vac;
uint32_t msk = ST_TX_EMPTY;
//uint32_t ste, sto, vaco, vace;

//ste = ioread32(mmio,REG_ST);
//vace = ioread32(mmio,REG_TX_VAC);

	while ( sz ) {

		while ( 0 == (vac = (ioread32(mmio, REG_TX_VAC) & VAC_MSK) ) ) {

			if ( irq ) {
				block_irq(mmio, msk);
				ack_irq( mmio, msk );
				enb_irq(mmio);
			} else {
				while ( ! ((sta = ioread32(mmio, REG_ST)) & msk) )
//printf("lop - vac %u, sta %08x\n", ioread32(mmio, REG_TX_VAC) & VAC_MSK, sta)
					;
				ack_irq( mmio, ST_TX_EMPTY|ST_TX_FULL );
			}

		}
		

		if ( vac > sz )
			vac = sz;

		for ( i=0; i<vac; i++ )
			iowrite32(mmio, REG_TX, tab[i]);

		tab += vac;
		sz  -= vac;

	}
//sto = ioread32(mmio, REG_ST);
//vaco = ioread32(mmio,REG_TX_VAC) & VAC_MSK;
//printf("entry %08x, now %08x, vac %u, now %u\n", ste, sto, vace, vaco);

	return 0;
}


static void usage(const char *nm)
{
	fprintf(stderr,"Usage: %s [-h] [-r <nsamples>] [-P period] [-p <nsamples>] [-D <dest>] [-a <a>] [-lr] [i] [-s] [-b <size>]\n", nm);
	fprintf(stderr,"          Fill fifo with sine wave or stdin\n");
	fprintf(stderr,"  -r <n>  Read fifo to stdout (n samples)\n");
	fprintf(stderr,"      -L  Fill left (default)\n");
	fprintf(stderr,"      -R  Fill right\n");
	fprintf(stderr,"      -i  Run interrupt driven\n");
	fprintf(stderr,"  -p <n>  Discard first 'n' samples (default: %i)\n", P_DFLT);
	fprintf(stderr,"  -s      Stream stdin\n");
	fprintf(stderr,"  -b <n>  Stream buffer size\n");
	fprintf(stderr,"  -D <d>  Stream destination address\n");
	fprintf(stderr,"  -a <a>  Set amplitude of sine wave\n");
	fprintf(stderr,"  -P <p>  Set period (#samples) of sine wave\n");
}

static void
read_init(Arm_MMIO mmio)
{
uint32_t mske = ST_RX_FULL;
uint32_t msk  = mske | ST_RX_RST_DON;

	iowrite32(mmio, REG_ST, msk);

	/* Reset FIFO */
	iowrite32(mmio, REG_RX_RST, RST_KEY);
	while ( ! (ioread32(mmio, REG_ST) & ST_RX_RST_DON) ) 
		;
	/* Clear irqs */
	iowrite32(mmio, REG_ST, msk);

	/* Enable irqs */
	iowrite32(mmio, REG_IEN, mske);
}

static void
fill_init(Arm_MMIO mmio, uint32_t dest)
{
uint32_t mske = ST_TX_EMPTY;
uint32_t msk  = mske | ST_TX_RST_DON;

	iowrite32(mmio, REG_ST, msk );

	/* Reset FIFO */
	iowrite32(mmio, REG_TX_RST, RST_KEY);
	while ( ! (ioread32(mmio, REG_ST) & ST_TX_RST_DON) ) 
		;

	iowrite32(mmio, REG_TX_DEST, dest);

	/* Enable irqs */
	iowrite32(mmio, REG_IEN, mske);
}

uint32_t
read_fifo(Arm_MMIO mmio, uint32_t *buf, unsigned sz, unsigned pre, int irq)
{
uint32_t sta;
uint32_t occ;
int      i,n;
uint32_t msk = ST_RX_FULL;

	while ( sz > 0 )  {
		// wait until filled 
		if ( irq ) {
			block_irq(mmio, msk);
		} else {
			while ( ! (ioread32(mmio, REG_ST) & msk) )
				;
		}
		occ = ioread32(mmio, REG_RX_OCC) & VAC_MSK;
		n = pre > occ  ? occ : pre;
		for ( i=0; i<n; i++ ) {
			ioread32(mmio,REG_RX); /* toss */
		}
		pre -= n;
		n    = occ - n;
		if ( n > sz )
			n = sz;
		for ( i=0; i<n; i++ ) {
			*(buf++) = ioread32(mmio, REG_RX);
		}

		ack_irq(mmio, msk);
		if ( irq ) {
			enb_irq(mmio);
		}
		sz -= n;
	}
	return 0;
}

int
main(int argc, char **argv)
{
const char *fnam = "/dev/uio2";
Arm_MMIO    mmio;

int         i;
int         ch;
int         rval     = 1;
int         shft     = 0;
unsigned    nsamples = 0;
unsigned    period   = NP;
unsigned    pre      = P_DFLT;
unsigned long long p;
float       amp      = AMP;
unsigned    au       = -1;

uint32_t   *dat      = 0;
uint32_t    msk      = 0;
unsigned    dest     = 0;
unsigned   *u_p;
int         use_irq  = 0;
unsigned    bufsz    = 0;
int         strm     = 0;
int         do_rd    = 0;
int         got;

	while ( (ch = getopt(argc, argv, "D:r:hLRp:isb:a:P:")) > 0 ) {
		u_p      = 0;
		switch (ch) {
			case 'h': rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'a':
				u_p   = &au;
			break;

			case 'r':
				u_p   = &nsamples;
				do_rd = 1;
			break;

			case 'D':
				u_p   = &dest;
			break;

			case 'p':
				u_p  = &pre;
			break;

			case 'P':
				u_p = &period;
			break;

			case 'L':
				shft = 0;
			break;

			case 'R':
				shft = 16;
			break;

			case 'i':
				use_irq = 1;
			break;

			case 'b':
				u_p = &bufsz;
			break;

			case 's':
				strm = 1;
			break;
		}
		if ( u_p ) {
			if ( 1 != sscanf(optarg,"%lli", &p) ) {
				fprintf(stderr,"Unable to scan option (-%c) arg\n", ch);
				return 1;
			}
			*u_p = p;
		}
	}

	if ( au < 32768 )
		amp = (float)au;
	
	if ( 0 == nsamples )
		nsamples = 10*period;

	if ( bufsz == 0 )
		bufsz    = 900;

	if ( !strm )
		bufsz = do_rd ? nsamples : period;

	if ( ! (dat = calloc(bufsz, sizeof(*dat))) ) {
		fprintf(stderr,"Unable to allocate memory\n");
		return 1;
	}

	
	if ( ! do_rd && !strm ) {
		for (i=0; i<bufsz; i++) {
			dat[i] = (uint32_t)( ((uint16_t)(int16_t)rintf(amp * sinf(2.*3.141592654/(float)period*(float)i))) << shft );
		}
	}

	if ( ! (mmio = arm_mmio_init(fnam)) ) {
		fprintf(stderr, "Unable to open MMIO (%s)\n", fnam);
		return(1);
	}

	if ( do_rd ) {
		read_init(mmio);
	} else {
		fill_init(mmio, (uint32_t) dest);
	}

	if ( use_irq )
		enb_irq(mmio);

	if ( do_rd ) {
		rval = !! read_fifo(mmio, dat, bufsz, pre, use_irq);
	} else {
		if ( strm ) {
			while ( (got = read(0, dat, bufsz*sizeof(*dat))) > 0 && 0 == fill_fifo(mmio, dat, got/sizeof(*dat), use_irq) )
				;
			/* if got > 0 then fill_fifo failed; if got < 0 then read failed */
			rval = got != 0;
		} else {
			do {
				rval = !! fill_fifo(mmio, dat, bufsz, use_irq);
			} while ( 0 == rval );
		}
	}

	if ( use_irq )
		dis_irq(mmio);

	arm_mmio_exit(mmio);

	if ( do_rd && !rval ) {
		for ( i=0; i<nsamples; i++ ) {
			printf("%08x\n", dat[i]);
		}
	}
	free( dat );
	return rval;
}
