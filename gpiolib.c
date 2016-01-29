#include <gpiolib.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/fcntl.h>
#include <string.h>
#include <stdlib.h>

#define EMIO_OFFSET 54
#define ZYNQ_GPIO   "zynq_gpio"
#define CLASS_GPIO  "/sys/class/gpio/"

typedef struct h_impl_ {
	int val_fd, dir_fd;
} *h_impl;

static int base = -1;

static int fillb(char *buf, size_t bufsz, const char *fmt, ...)
{
va_list ap;
int     put;
	va_start(ap, fmt);
	put = vsnprintf(buf, bufsz, fmt, ap);
	va_end(ap);
	if ( put >= bufsz ) {
		fprintf(stderr,"gpiolib: internal error -- buffer too small\n");
		return -1;
	}
	return 0;
}

gpio_handle
gpio_open(unsigned pin, int is_emio)
{
DIR           *dir;
struct dirent dent, *dentp;
int           st;
char          buf[256];
h_impl        rval = 0;
int           fd   = -1;
int           got;
int           val_fd = -1;
int           dir_fd = -1;
int           retry;
FILE         *f = 0;

	if ( pin > 53 ) {
		fprintf(stderr,"gpiolib: Invalid pin number (must be < 54)\n");
		return 0;
	}
	if ( ! (dir = opendir( CLASS_GPIO )) ) {
		fprintf(stderr,"gpiolib: error reading '%s'\n", CLASS_GPIO);
		return 0;
	}

	if ( is_emio )
		pin += EMIO_OFFSET;


	while ( base < 0 ) {

		if ( readdir_r( dir, &dent, &dentp ) ) {
			fprintf(stderr,"gpiolib: readdir_r failed: %s\n", strerror(errno));
			goto bail;
		}

		if ( !dentp )
			goto bail; /* end reached */

		if ( strncmp("gpiochip", dent.d_name, 8) || DT_DIR != dent.d_type )
			continue;
		if ( fillb(buf, sizeof(buf), "%s%s/label", CLASS_GPIO, dent.d_name) ) {
			goto bail;
		}
		fd = open(buf, O_RDONLY);
		if ( fd < 0 ) {
			fprintf(stderr,"gpiolib: WARNING: unable to open '%s' (%s)\n", buf, strerror(errno));
			continue;
		}
		got = read(fd, buf, sizeof(buf) - 1);
		close(fd); fd = -1;

		if ( got < 0 ) {
			fprintf(stderr,"gpiolib: WARNING: unable to read 'label' (%s)\n", strerror(errno));
			continue;
		}
		buf[got] = 0;
		if ( 0 == strncmp(buf, ZYNQ_GPIO, sizeof(buf)) ) {
			/* Found it */
			if ( (base = get_base( dent.d_name, pin )) < 0 )
				goto bail;
		}
	}

	retry = 0;

	do {
		if ( fillb(buf, sizeof(buf), "%sgpio%d/value", CLASS_GPIO, base + pin) )
			goto bail;

		val_fd = open(buf, O_RDWR);

		if ( val_fd < 0 ) {
			if ( ! retry++ && ENOENT == errno ) {
				if ( fillb(buf, sizeof(buf), "%sexport", CLASS_GPIO) )
					goto bail;

				if ( (f = fopen(buf, "w")) < 0 ) {
					fprintf(stderr,"gpiolib: unable to open '%s' (%s)\n", buf, strerror(errno));
					goto bail;
				}

				if ( fprintf(f, "%d", base + pin) < 0 ) {
					fprintf(stderr,"gpiolib: unable to write to '%s' (%s)\n", buf, strerror(errno));
					goto bail;
				}

				fclose(f); f = 0;
			} else {
				fprintf(stderr,"gpiolib: unable to open 'value' file for pin %d: %s\n", pin, strerror(errno));
				goto bail;
			}
		}
	} while ( val_fd < 0 );

	if ( fillb(buf, sizeof(buf), "%sgpio%d/dir", CLASS_GPIO, base + pin) )
		goto bail;

	dir_fd = open(buf, O_RDWR);

	if ( dir_fd < 0 ) {
		fprintf(stderr,"gpiolib: unable to open 'dir' file for pin %d: %s\n", pin, strerror(errno));
		goto bail;
	}

	if ( ! (rval = malloc(sizeof(*rval))) ) {
		fprintf(stderr,"gpiolib: no memory\n");
		goto bail;
	}

	rval->dir_fd = dir_fd; dir_fd = -1;
	rval->val_fd = val_fd; val_fd = -1;

bail:
	if ( val_fd >= 0 )
		close(val_fd);
	if ( dir_fd >= 0 )
		close(dir_fd);
	if ( fd >= 0 )
		close(fd);
	closedir( dir );
	if ( f )
		fclose(f);
	return rval;
}

void gpio_close(gpio_handle p)
{
h_impl h = (h_impl)p;
	close(h->val_fd);
	close(h->dir_fd);
	free(h);
}

int gpio_set(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = write(h->val_fd,"1",1);
	return rval < 0 ? rval : 0;
}

int gpio_clr(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = write(h->val_fd,"0",1);
	return rval < 0 ? rval : 0;
}

int gpio_out(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = write(h->dir_fd,"out",3);
	return rval < 0 ? rval : 0;
}

int gpio_inp(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = write(h->dir_fd,"out",3);
	return rval < 0 ? rval : 0;
}

int  gpio_get(gpio_handle p)
{
h_impl        h = (h_impl)p;
unsigned char v;
int           rval = read(h->val_fd, &v, 1);
		return rval < 0 ? rval : ( v - '0' );
}
