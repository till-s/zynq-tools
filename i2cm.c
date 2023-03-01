#include <arm-mmio.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <linux/i2c-dev.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <gpiolib.h>

#define CSR 0

#define CSR_CLR 0

#define MAP_LEN 0x1000

#define CMD_START (1<<(8+0))
#define CMD_STOP  (1<<(8+1))
#define CMD_READ  (1<<(8+2))
#define CMD_WRITE (1<<(8+3))
#define CMD_NACK  (1<<(8+4))

#define ST_DON (1<<(16+0))
#define ST_ERR (1<<(16+1))
#define ST_ALO (1<<(16+2))
#define ST_BBL (1<<(16+3))
#define ST_ACK (1<<(16+4))

#define IS_ERR(status) ((status) & ST_ERR)

#define FLAG_POLL (1<<0)

#define MAXBUF 1024

typedef struct CDevDat  {
    uint8_t  buf[MAXBUF];
    unsigned len;
    int      fd;
} CDevDat;

typedef struct BBDat {
    uint8_t     buf[MAXBUF];
    unsigned    len;
    gpio_handle sda;
    gpio_handle scl;
} BBDat;

typedef struct i2c_io_ {
	union {
		Arm_MMIO mio;
		CDevDat  dat;
		BBDat    bbd;
	} handle;
	uint32_t (*sync_cmd)(struct i2c_io_ *io, uint32_t cmd);
	void     (*cleanup) (struct i2c_io_ *io);
	int      flags;
} i2c_io;

static void slp(unsigned us)
{
struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = us * 1000;
	nanosleep(&t, 0);
}

static void mmio_cleanup(struct i2c_io_ *io)
{
	if ( io->handle.mio )
		arm_mmio_exit(io->handle.mio);
}

static uint32_t mmio_sync_cmd(i2c_io *io, uint32_t cmd)
{
uint32_t irq_ena = 1;
uint32_t status;
Arm_MMIO mio = io->handle.mio;

	if ( ! mio ) {
		return ST_ERR;
	}

	/* clear status */
	iowrite32(mio, CSR, CSR_CLR);
	/* enable IRQ   */
	write( mio->fd, &irq_ena, sizeof(irq_ena) );
	/* enable IRQ   */
	/* issue command */
	iowrite32(mio, CSR, cmd);
	/* enable IRQ   */
	/* block for completion */
	if ( (io->flags & FLAG_POLL) ) {
		while ( ! ((status = ioread32(mio, CSR)) & ST_DON) ) {
			slp(100);
		}
	} else {
		if ( sizeof(status) != read( mio->fd, &status, sizeof(status) ) ) {
			fprintf(stderr,"Blocking for IRQ -- read error\n");
		}
		/* enable IRQ   */
		status = ioread32(mio, CSR);
		/* enable IRQ   */
	}

	if ( IS_ERR(status) ) {
		fprintf(stderr,"Error (status 0x%08"PRIx32") ", status);
		if ( status & ST_ALO )
			fprintf(stderr," [arbitration lost]");
		if ( status & ST_BBL )
			fprintf(stderr," [bus busy]");
	}

	return status;
}

static void cdev_cleanup(struct i2c_io_ *io)
{
	if ( io->handle.dat.fd >= 0 )
		close( io->handle.dat.fd );
}

static int cdev_flush(CDevDat *dat)
{
unsigned len = dat->len;
	if ( len > 0 ) {
		dat->len = 0;
		if ( len != write( dat->fd, dat->buf, len ) ) {
			perror("cdev_sync_cmd() writing:");
			return -1;
		}
	}
	return 0;
}

static int cdev_cache(CDevDat *dat, uint8_t b)
{
	if ( dat->len >= sizeof(dat->buf) ) {
		fprintf(stderr,"ERROR: cdev write buffer exhausted\n");
		return -1;
	}
	dat->buf[dat->len] = b;
	dat->len++;
	return 0;
}

static uint32_t cdev_sync_cmd(i2c_io *io, uint32_t cmd)
{
uint32_t status = ST_ACK;
int      fd = io->handle.dat.fd;
uint8_t  byte = (uint8_t)(cmd & 0xff);
unsigned len;

		if ( fd < 0 )
			return ST_ERR;

		if ( (cmd & CMD_START) ) {
			if ( ioctl(fd, I2C_SLAVE, (cmd>>1) & 0x7f) ) {
				perror("cdev_sync_cmd(START) via ioctl(SLAVE_ADDR):");
				return ST_ERR;
			}
		} else if ( (cmd & CMD_STOP) ) {
			if ( cdev_flush( &io->handle.dat ) ) {
				return ST_ERR;
			}
		} else if ( (cmd & CMD_WRITE) ) {
			if ( cdev_cache( &io->handle.dat, byte ) ) {
				return ST_ERR;
			}
		} else if ( (cmd & CMD_READ) ) {
            if ( cdev_flush( &io->handle.dat ) ) {
				return ST_ERR;
			}
			if ( 1 != read(fd, &byte, 1) ) {
				perror("cdev_sync_cmd() reading:");
				return ST_ERR;
			}
			status |= byte;
		} else {
			fprintf(stderr,"Warning: cdev_sync_cmd() ignoring command 0x%08"PRIx32"\n", cmd);
			return ST_ERR;
		}

		return status;
}

static void bb_close(BBDat *dat)
{
	if ( dat->scl ) {
		gpio_inp( dat->scl );
		gpio_close( dat->scl );
		dat->scl = 0;
	}
	if ( dat->sda ) {
		gpio_inp( dat->sda );
		gpio_close( dat->sda );
		dat->sda = 0;
	}
}

static void bb_dly(BBDat *dat)
{
struct timespec v;
	v.tv_sec  = 0;
	v.tv_nsec = 500000;
	nanosleep( &v, 0 );
}

static void bb_scl_hi(BBDat *dat)
{
int attempt, val;

	if ( gpio_inp( dat->scl ) ) {
		fprintf(stderr, "bb_scl_hi failed\n");
		bb_close( dat );
		exit( 1 );
	}
	for ( attempt = 0; attempt < 1000; attempt++ ) {
		val = gpio_get( dat->scl );
		if ( val < 0 ) {
			fprintf(stderr, "gpio_get(SCL) failed\n");
			bb_close( dat );
			exit( 1 );
		}
		if ( val ) {
			return;
		}
		bb_dly( dat );
	}
	fprintf(stderr, "bb_scl_hi -- clock stretching timeout\n");
	bb_close( dat );
	exit( 1 );
}

static void bb_scl_lo(BBDat *dat)
{
	if ( gpio_out( dat->scl ) ) {
		fprintf(stderr, "bb_scl_lo failed\n");
		bb_close( dat );
		exit( 1 );
	}
}

static void bb_sda_hi(BBDat *dat)
{
	if ( gpio_inp( dat->sda ) ) {
		fprintf(stderr, "bb_sda_hi failed\n");
		bb_close( dat );
		exit( 1 );
	}
}

static void bb_sda_lo(BBDat *dat)
{
	if ( gpio_out( dat->sda ) ) {
		fprintf(stderr, "bb_sda_lo failed\n");
		bb_close( dat );
		exit( 1 );
	}
}

static void bb_cleanup(struct i2c_io_ *io)
{
BBDat *dat = &io->handle.bbd;
	bb_close( dat );
}

static void bb_start(BBDat *dat)
{
	bb_sda_hi( dat );
    bb_scl_hi( dat );
	bb_dly   ( dat );
	bb_sda_lo( dat );
	bb_dly   ( dat );
    bb_scl_lo( dat );
}

static void bb_stop(BBDat *dat)
{
	bb_scl_lo( dat );
	bb_sda_lo( dat );
	bb_dly   ( dat );
    bb_scl_hi( dat );
	bb_dly   ( dat );
    bb_sda_hi( dat );
}

/* write byte, return ACK */
static int bb_write_byte(BBDat *dat, uint8_t byte)
{
int bit;
int val;
printf("bb writing 0x%08x\n", byte);
	for ( bit=0; bit<8; bit++ ) {
		if ( (byte & 0x80) ) {
			bb_sda_hi( dat );
		} else {
			bb_sda_lo( dat );
		}
		byte <<= 1;
		bb_dly( dat );
		bb_scl_hi( dat );
		bb_dly( dat );
		bb_scl_lo( dat );
	}
	bb_dly( dat );
	bb_scl_hi( dat );
	bb_dly( dat );

	val = gpio_get( dat->sda );
	if ( val < 0 ) {
		fprintf(stderr, "gpio_get(SDA) failed\n");
		bb_close( dat );
		exit( 1 );
	}

	bb_scl_lo( dat );

	return !val;
}

static void bb_read_byte(BBDat *dat, uint8_t *byte_p, int do_ack)
{
uint8_t byte = 0;
int     bit, val;

	for ( bit = 0; bit < 8; bit ++ ) {
		byte <<= 1;
		bb_dly   ( dat );
		bb_scl_hi( dat );
		bb_dly   ( dat );
		val = gpio_get( dat->sda );
		if ( val < 0 ) {
			fprintf(stderr, "bb_read_byte: gpio_get(SDA) failed\n");
			bb_close( dat );
			exit( 1 );
		}
		if ( val ) {
			byte |= 1;
		}
		bb_scl_lo( dat );
	}
	/* send ACK */
	if ( do_ack ) {
		bb_sda_lo( dat );
	} else {
		bb_sda_hi( dat );
	}
	bb_dly   ( dat );
	bb_scl_hi( dat );
	bb_dly   ( dat );
	bb_scl_lo( dat );
	*byte_p = byte;
}

static int bb_flush(BBDat *dat)
{
unsigned i;
	for ( i = 0; i < dat->len; i++ ) {
		if ( ! bb_write_byte( dat, dat->buf[i] ) ) {
			fprintf(stderr, "bb_flush: no ACK (byte %d)\n", i);
			bb_close( dat );
			return -1;
		}
	}
	dat->len = 0;
	return 0;
}

static int bb_cache(BBDat *dat, uint8_t b)
{
	if ( dat->len >= sizeof(dat->buf) ) {
		fprintf(stderr,"ERROR: bb write buffer exhausted\n");
		return -1;
	}
	dat->buf[dat->len] = b;
	dat->len++;
	return 0;
}

static uint32_t bb_sync_cmd(i2c_io *io, uint32_t cmd)
{
uint32_t status = ST_ACK;
BBDat   *dat    = &io->handle.bbd;
uint8_t  byte   = (uint8_t)(cmd & 0xff);
unsigned len;

		if ( ! dat->scl || ! dat->sda )
			return ST_ERR;

		if ( (cmd & CMD_START) ) {
			bb_start( dat );
			if ( ! bb_write_byte( dat, cmd & 0xff ) ) {
				/* Not ACK */
				return 0;
			}
		} else if ( (cmd & CMD_STOP) ) {
			if ( bb_flush( dat ) ) {
				return ST_ERR;
			}
			bb_stop( dat );
		} else if ( (cmd & CMD_WRITE) ) {
			if ( bb_cache( dat, byte ) ) {
				return ST_ERR;
			}
		} else if ( (cmd & CMD_READ) ) {
            if ( bb_flush( dat ) ) {
				return ST_ERR;
			}
			bb_read_byte( dat, &byte, ! (cmd & CMD_NACK) );
			status |= byte;
		} else {
			fprintf(stderr,"Warning: cdev_sync_cmd() ignoring command 0x%08"PRIx32"\n", cmd);
			return ST_ERR;
		}

		return status;
}


#define I2C_RD 1

#define CHECK(status, io, cmd) \
	do { \
		status=(io)->sync_cmd(io, cmd); \
		if ( IS_ERR(status) ) \
			goto bail; \
	} while (0)

#define CHECK_ACK(status, io, cmd, msg) \
	do { \
		CHECK(status, io, cmd); \
		if ( ! (status & ST_ACK) ) { \
			fprintf(stderr,"Error: Missing ACK (%s): 0x%08"PRIx32"\n", msg, status); \
			goto bail; \
		} \
	} while (0)

static void
usage(const char *nm)
{
	fprintf(stderr,"Usage: %s -d device [-hp] [-b base_off]  [-o offset] [-a i2c_addr] [-l len] {value}\n", nm);
	fprintf(stderr,"          -p polled operation\n");
	fprintf(stderr,"          -d /dev/uio<X>         : i2c master in fabric/PL\n");
	fprintf(stderr,"          -d /dev/i2c-<X>        : PS i2c master X\n");
	fprintf(stderr,"          -b base_offset         : offset of device registers in UIO device\n");
	fprintf(stderr,"          -d [e]mio<X>/[e]mio<Y> : bit-bang via gpio SCL pin X, SDA pin Y\n");
}

int
main(int argc, char **argv)
{
i2c_io    io  = {0};
int rval      = 1;
int ch;

int       len  = 256;
int    romaddr = -1;
int    rdoff   = 0;
int   slv_addr = 0x50;
int      *i_p;
int       i,val;
const char   *devnam = 0;
unsigned      basoff = 0;

uint32_t sta, cmd;

uint32_t cmd_addr;


	while ( (ch = getopt(argc, argv, "ho:l:a:d:b:p")) >= 0 ) {
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
			case 'b': i_p = &basoff;   break;

			case 'd': devnam = optarg; break;

			case 'p': io.flags |= FLAG_POLL; break;
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

	if ( ! devnam ) {
		fprintf(stderr,"No device name -- use -d option\n");
		return rval;
	}

	if ( romaddr != -1 ) {
		if ( romaddr & ~0xff ) {
			fprintf(stderr,"Invalid offset %d (> 256)\n", len);
			return rval;
		}
		rdoff = romaddr;
	}

	if ( strstr(devnam, "uio") || strstr(devnam, "mem") ) {
		if ( ! (io.handle.mio = arm_mmio_init_2( devnam, MAP_LEN, basoff )) ) {
			return rval;
		}
		io.sync_cmd = mmio_sync_cmd;
		io.cleanup  = mmio_cleanup;
	} else if ( strstr(devnam, "i2c-") ) {
		if ( (io.handle.dat.fd = open(devnam, O_RDWR)) < 0 ) {
			fprintf(stderr,"Error opening device %s: %s\n", devnam, strerror(errno));
			return rval;
		}
		io.handle.dat.len = 0;
		io.sync_cmd = cdev_sync_cmd;
		io.cleanup  = cdev_cleanup;
	} else if ( strstr(devnam, "mio") ) {
		int sda_pin, scl_pin, sda_emio = 0, scl_emio = 0;
		if        ( 2 == sscanf(devnam, "mio%d/mio%d",   &scl_pin, &sda_pin ) ) {
		} else if ( 2 == sscanf(devnam, "emio%d/mio%d",  &scl_pin, &sda_pin ) ) {
			scl_emio = 1;
		} else if ( 2 == sscanf(devnam, "mio%d/emio%d",  &scl_pin, &sda_pin ) ) {
			sda_emio = 1;
		} else if ( 2 == sscanf(devnam, "emio%d/emio%d", &scl_pin, &sda_pin ) ) {
			scl_emio = 1;
			sda_emio = 1;
		} else {
			fprintf(stderr, "Invalid bit-bang configuration, must be [e]mio[0-9]+[/][e]mio[0-9]+\n");
			return rval;
		}
		if ( ! ( io.handle.bbd.scl = gpio_open( scl_pin, scl_emio ) ) ) {
			fprintf(stderr, "Unable to open GPIO for SCL pin %d\n", scl_pin);
			return rval;
		}
		if ( ! ( io.handle.bbd.sda = gpio_open( sda_pin, sda_emio ) ) ) {
			fprintf(stderr, "Unable to open GPIO for SDA pin %d\n", sda_pin);
			gpio_close( io.handle.bbd.scl );
			return rval;
		}

		bb_sda_hi( &io.handle.bbd );
		if ( gpio_inp( io.handle.bbd.scl ) ) {
			fprintf(stderr, "gpio_inp(SCL) failed\n");
			bb_close( &io.handle.bbd );
			exit( 1 );
		}
		/* Ignore return value of gpio_clr; it signals failure but
		 * the output is cleared anyways
		 */
		gpio_clr( io.handle.bbd.scl );
		gpio_clr( io.handle.bbd.sda );
		io.handle.bbd.len = 0;
		io.sync_cmd = bb_sync_cmd;
		io.cleanup  = bb_cleanup;
	} else {
		fprintf(stderr,"Don't know how to handle this device: %s\n", devnam);
		return rval;
	}

	cmd_addr = CMD_START | CMD_WRITE | (slv_addr<<1) ;
	CHECK_ACK( sta, &io, cmd_addr, "Addressing slave (for WR)" );

	if ( -1 != romaddr ) {
		/*
		   cmd = CMD_WRITE | ((romaddr>>8) & 0xff);
		   CHECK_ACK( sta, &io, cmd, "Sending ROMaddr (HI)" );
		 */

		cmd = CMD_WRITE | (romaddr & 0xff);
		CHECK_ACK( sta, &io, cmd, "Sending ROMaddr (LO)" );
	}

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
			CHECK_ACK( sta, &io, cmd, "Sending values" );
		}
	} else {
		/* read  */
		CHECK_ACK( sta, &io, cmd_addr | I2C_RD, "Addressing slave (for RD)" );

		cmd = CMD_READ;

		for ( i = 0; i < len; i++ ) {
			if ( ! (i&0xf) )
				printf("\n%04x: ", rdoff + i);
			if ( i == len - 1 )
				cmd |= CMD_NACK;
			CHECK( sta, &io, cmd );
			printf(" %02"PRIX32, (sta & 0xff));
		}
		printf("\n");
	}

	rval = 0;

bail:
	io.sync_cmd( &io, CMD_STOP );
	io.cleanup( &io );
	return rval;
}
