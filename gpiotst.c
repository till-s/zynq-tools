#include <gpiolib.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
gpio_handle gpio = gpio_open(0, EMIO_PIN);
int         got;
int         i;

	if ( ! gpio )
		return 1;

	if ( gpio_out(gpio) ) {
		perror("gpio_out");
		return 1;
	}

	for ( i=0; i<5; i++ ) {
		if ( gpio_clr(gpio) ) {
			perror("gpio_clr");
			return 1;
		}
		sleep(1);
		if ( gpio_set(gpio) ) {
			perror("gpio_set");
			return 1;
		}
		sleep(1);
	}
	return 0;
}
