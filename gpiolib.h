#ifndef GPIOLIB_H
#define GPIOLIB_H

typedef void *gpio_handle;

/* returns handle on success, NULL on error */
gpio_handle gpio_open(unsigned pin, int is_emio);

void        gpio_close(gpio_handle);

/* set, clr, out, inp return 0 on success, -1 (with errno set) on error */
int gpio_set(gpio_handle);
int gpio_clr(gpio_handle);
int gpio_out(gpio_handle);
int gpio_inp(gpio_handle);
/* get returns  1 or 0 on success and -1 on error (with errno set)      */
int gpio_get(gpio_handle);

#endif
