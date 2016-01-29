#ifndef GPIOLIB_H
#define GPIOLIB_H

typedef void *gpio_handle;

/* returns handle on success, NULL on error */
#define EMIO_PIN 1
#define MIO_PIN  0
gpio_handle gpio_open(unsigned pin, int pin_type);

void        gpio_close(gpio_handle);

/* set, clr, out, inp return 0 on success, -1 (with errno set) on error */
int gpio_set(gpio_handle);
int gpio_clr(gpio_handle);
int gpio_out(gpio_handle);
int gpio_inp(gpio_handle);
/* get returns  1 or 0 on success and -1 on error (with errno set)      */
int gpio_get(gpio_handle);

#endif
