/*
 * arch/arm/mach-omap2/include/mach/gpio.h
 */

#include <plat/gpio.h>

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio)	(32 * (bank) + (gpio))
