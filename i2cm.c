#include <arm-mmio.h>
#include <stdio.h>
#include <time.h>
#include <getopt.h>

#define CSR 0

#define CSR_CLR 0

#define CMD_START (1<<(8+0))
#define CMD_STOP  (1<<(8+1))
#define CMD_READ  (1<<(8+2))
#define CMD_WRITE (1<<(8+3))
#define CMD_NACK  (1<<(8+4))

#define ST_ERR (1<<(16+1))
#define ST_ALO (1<<(16+2))	
#define ST_BBL (1<<(16+3))
#define ST_ACK (1<<(16+4))

#define IS_ERR(status) ((status) & ST_ERR)

static void slp(unsigned us)
{
struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = us * 1000;
	nanosleep(&t, 0);
}

static uint32_t sync_cmd(Arm_MMIO mio, uint32_t cmd)
{
uint32_t irq_ena = 1;
uint32_t status;
	/* clear status */
	iowrite32(mio, CSR, CSR_CLR);
	/* enable IRQ   */
	write( mio->fd, &irq_ena, sizeof(irq_ena) );
	/* enable IRQ   */
	/* issue command */
	iowrite32(mio, CSR, cmd);
	/* enable IRQ   */
	/* block for completion */
	read( mio->fd );
	/* enable IRQ   */
	status = ioread32(mio, CSR);
	/* enable IRQ   */

	if ( IS_ERR(status) ) {
		fprintf(stderr,"Error (status 0x%08"PRIx32") ", status);
		if ( status & ST_ALO )
			fprintf(stderr," [arbitration lost]");
		if ( status & ST_BBL )
			fprintf(stderr," [bus busy]");
	}

	return status;
}

#define I2C_RD 1

#define CHECK(status, mio, cmd) \
	do { \
		status=sync_cmd(mio, cmd); \
		if ( IS_ERR(status) ) \
			goto bail; \
	} while (0)

#define CHECK_ACK(status, mio, cmd, msg) \
	do { \
		CHECK(status, mio, cmd); \
		if ( ! (status & ST_ACK) ) { \
			fprintf(stderr,"Error: Missing ACK (%s): 0x%08"PRIx32"\n", msg, status); \
			goto bail; \
		} \
	} while (0)

static void
usage(const char *nm) 
{
	fprintf(stderr,"Usage: %s [-h] [-o offset] [-a i2c_addr] [-l len] {value}\n", nm); 
}

int
main(int argc, char **argv)
{
Arm_MMIO mio  = 0;
int rval      = 1;
int ch;

int       len  = 256;
int    romaddr = 0;
int   slv_addr = 0x50;
int      *i_p;
int       i,val;

uint32_t sta, cmd;

uint32_t cmd_addr;


	while ( (ch = getopt(argc, argv, "ho:l:a:")) >= 0 ) {
		i_p = 0;
		switch (ch) {
			case 'h':
				rval = 0;
			default: 
				usage(argv[0]);
				return rval;

			case 'o': i_p = &romaddr;  break;
			case 'l': i_p = &len;      break;
			case 'a': i_p = &slv_addr; break;
		}
		if ( i_p ) {
			if ( 1 != sscanf(optarg, "%i", i_p) ) {
				fprintf(stderr,"Unable to parse integer arg to option '%c'\n", ch);
				return rval;
			}
		}
	}

	if ( slv_addr & ~0x7f ) {
		fprintf(stderr,"Invalid slave address 0x%x (> 0x7f)\n", slv_addr);
		return rval;
	}

	if ( len < 0 || len > 256 ) {
		fprintf(stderr,"Invalid length %d (> 256)\n", len);
		return rval;
	}
	
	if ( romaddr & ~0xff ) {
		fprintf(stderr,"Invalid offset %d (> 256)\n", len);
		return rval;
	}
	
	if ( ! (mio = arm_mmio_init( "/dev/uio0" )) ) {
		return rval;
	}
	cmd_addr = CMD_START | CMD_WRITE | (slv_addr<<1) ;
	CHECK_ACK( sta, mio, cmd_addr, "Addressing slave (for WR)" );

/*
	cmd = CMD_WRITE | ((romaddr>>8) & 0xff);
	CHECK_ACK( sta, mio, cmd, "Sending ROMaddr (HI)" );
*/

	cmd = CMD_WRITE | (romaddr & 0xff);
	CHECK_ACK( sta, mio, cmd, "Sending ROMaddr (LO)" );

	if ( argc > optind ) {
		/* write */
		for ( i=optind; i<argc; i++ ) {
			if ( 1 != sscanf(argv[i],"%i", &val) ) {
				fprintf(stderr,"Unable to parse value %i\n", i-optind+1);
				goto bail;
			}
			if ( (val & ~0xff) ) {
				fprintf(stderr,"Warning: Value %i out of range (0..255) -- truncating\n", i-optind+1);
			}
			cmd = CMD_WRITE | (val & 0xff);
			CHECK_ACK( sta, mio, cmd, "Sending values" );
		}
	} else {
		/* read  */
		CHECK_ACK( sta, mio, cmd_addr | I2C_RD, "Addressing slave (for RD)" );

		cmd = CMD_READ;

		for ( i = 0; i < len; i++ ) {
			if ( ! (i&0xf) )
				printf("\n%04x: ", romaddr + i);
			if ( i == len - 1 )
				cmd |= CMD_NACK;
			CHECK( sta, mio, cmd );
			printf(" %02"PRIX32, (sta & 0xff));
		}
		printf("\n");
	}

	rval = 0;

bail:
	if ( mio ) {
		sync_cmd(mio, CMD_STOP);
	}
	arm_mmio_exit( mio );
	return rval;
}
