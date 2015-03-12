#include <arm-mmio.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <linux/i2c-dev.h>
#include <sys/fcntl.h>
#include <errno.h>

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

typedef struct i2c_io_ {
	union {
		Arm_MMIO mio;
		int      fd;
	} handle;
	uint32_t (*sync_cmd)(struct i2c_io_ *io, uint32_t cmd);
	void     (*cleanup) (struct i2c_io_ *io);
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
	if ( sizeof(status) != read( mio->fd, &status, sizeof(status) ) ) {
		fprintf(stderr,"Blocking for IRQ -- read error\n");
	}
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

static void cdev_cleanup(struct i2c_io_ *io)
{
	if ( io->handle.fd >= 0 )
		close( io->handle.fd );
}

static uint32_t cdev_sync_cmd(i2c_io *io, uint32_t cmd)
{
uint32_t status = ST_ACK;
int      fd = io->handle.fd;
uint8_t  byte = (uint8_t)(cmd & 0xff);

		if ( fd < 0 )
			return ST_ERR;

		if ( (cmd & CMD_START) ) {
			if ( ioctl(fd, I2C_SLAVE, (cmd>>1) & 0x7f) ) {
				perror("cdev_sync_cmd(START) via ioctl(SLAVE_ADDR):");
				return ST_ERR;
			}
		} else if ( (cmd & CMD_STOP) ) {
			/* ignore */
		} else if ( (cmd & CMD_WRITE) ) {
			if ( 1 != write(fd, &byte, 1) ) {
				perror("cdev_sync_cmd() writing:");
				return ST_ERR;
			}
		} else if ( (cmd & CMD_READ) ) {
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
	fprintf(stderr,"Usage: %s -d device [-h] [-o offset] [-a i2c_addr] [-l len] {value}\n", nm); 
}

int
main(int argc, char **argv)
{
i2c_io    io;
int rval      = 1;
int ch;

int       len  = 256;
int    romaddr = -1;
int    rdoff   = 0;
int   slv_addr = 0x50;
int      *i_p;
int       i,val;
const char *devnam = 0;

uint32_t sta, cmd;

uint32_t cmd_addr;


	while ( (ch = getopt(argc, argv, "ho:l:a:d:")) >= 0 ) {
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

			case 'd': devnam = optarg; break;
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

	if ( strstr(devnam, "uio") ) {
		if ( ! (io.handle.mio = arm_mmio_init( devnam )) ) {
			return rval;
		}
		io.sync_cmd = mmio_sync_cmd;
		io.cleanup  = mmio_cleanup;
	} else if ( strstr(devnam, "i2c-") ) {
		if ( (io.handle.fd = open(devnam, O_RDWR)) < 0 ) {
			fprintf(stderr,"Error opening device %s: %s\n", devnam, strerror(errno));
			return rval;
		}
		io.sync_cmd = cdev_sync_cmd;
		io.cleanup  = cdev_cleanup;
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
