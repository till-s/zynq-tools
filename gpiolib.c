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

static int base  = -1;
static int ngpio = 0;

static int
fillb(char *buf, size_t bufsz, const char *fmt, ...)
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

static int
get_param(const char *ctl_name, const char *param)
{
char buf[256];
FILE *f = 0;
int  got;
int  rval = -1;
	if ( fillb(buf, sizeof(buf), "%s%s/%s", CLASS_GPIO, ctl_name, param) )
		return -1;
	if ( ! (f = fopen(buf,"r")) ) {
		fprintf(stderr,"Unable to open '%s' file: %s\n", param, strerror(errno));
		return -1;
	}
	if ( 1 != (got = fscanf(f,"%d", &rval)) ) {
		fprintf(stderr,"Unable to open 'read' file");
		if ( got < 0 )
			fprintf(stderr,": %s", strerror(errno));
		fprintf(stderr,"\n");
		rval = -1;
	}
	fclose(f);
	return rval;
}

static int
get_base(const char *ctl_name, int *b_p, int *n_p)
{
int b, n;
	if ( (b = get_param(ctl_name, "base")) < 0 )
		return -1;
	if ( (n = get_param(ctl_name, "ngpio")) < 0 )
		return -1;
	*b_p = b;
	*n_p = n;
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
int           got,min;
int           val_fd = -1;
int           dir_fd = -1;
int           retry;
FILE         *f = 0;


	if ( pin >= EMIO_OFFSET ) {
		fprintf(stderr,"gpiolib: Invalid pin number (must be < %d)\n", EMIO_OFFSET);
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

		if ( !dentp ) {
			printf("DIR END\n");
			goto bail; /* end reached */
		}

		if ( (got = strncmp("gpiochip", dent.d_name, 8)) ) {
			continue;
		}

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
		min = strlen(ZYNQ_GPIO);
		if ( sizeof(buf) < min )
			min = sizeof(buf);
		if ( 0 == strncmp(buf, ZYNQ_GPIO, min) ) {
			/* Found it */
			if ( (get_base( dent.d_name, &base, &ngpio)) < 0 )
				goto bail;
		}
	}

	if ( pin >= ngpio ) {
		fprintf(stderr,"gpiolib: invalid pin # %d (max: %d)\n", pin, ngpio - 1);
		goto bail;
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

	if ( fillb(buf, sizeof(buf), "%sgpio%d/direction", CLASS_GPIO, base + pin) )
		goto bail;

	dir_fd = open(buf, O_RDWR);

	if ( dir_fd < 0 ) {
		fprintf(stderr,"gpiolib: unable to open 'direction' file for pin %d: %s\n", pin, strerror(errno));
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
int rval = pwrite(h->val_fd,"1",1,0);
	return rval < 0 ? rval : 0;
}

int gpio_clr(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = pwrite(h->val_fd,"0",1,0);
	return rval < 0 ? rval : 0;
}

int gpio_out(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = pwrite(h->dir_fd,"out",3,0);
	return rval < 0 ? rval : 0;
}

int gpio_inp(gpio_handle p)
{
h_impl h = (h_impl)p;
int rval = pwrite(h->dir_fd,"in",2,0);
	return rval < 0 ? rval : 0;
}

int  gpio_get(gpio_handle p)
{
h_impl        h = (h_impl)p;
unsigned char v;
int           rval = pread(h->val_fd, &v, 1, 0);
		return rval < 0 ? rval : ( v - '0' );
}
