#ifndef ARM_MMIO_H
#define ARM_MMIO_H

#include <inttypes.h>
#include <sys/types.h>

typedef struct arm_mmio_ {
	volatile uint32_t *bar;
	size_t             lim;
	int                fd; /* for interrupts */
} *Arm_MMIO;


static inline uint32_t __raw_readl(Arm_MMIO mio, unsigned regno)
{
volatile uint32_t *addr;
uint32_t           val;
	addr = mio->bar + regno;
	asm volatile("ldr %1, %0"
		     : "+Qo" (*addr),
		       "=r" (val));
	return val;
}

static inline void __raw_writel(Arm_MMIO mio, unsigned regno, uint32_t val)
{
volatile uint32_t *addr;
	addr = mio->bar + regno;
	asm volatile("str %1, %0"
		     : "+Qo" (*addr)
		     : "r" (val));
}

static inline uint32_t __bad_readl(Arm_MMIO mio, unsigned regno)
{
	return *(mio->bar + regno);
}

static inline void __bad_writel(Arm_MMIO mio, unsigned regno, uint32_t val)
{
	*(mio->bar + regno) = val;
}


#define iowrite32(m, r, v) __raw_writel(m, r, v)
#define ioread32(m, r)    __raw_readl(m, r)

Arm_MMIO
arm_mmio_init(const char *fnam);

Arm_MMIO
arm_mmio_init_1(const char *fnam, size_t len);

Arm_MMIO
arm_mmio_init_2(const char *fnam, size_t len, size_t off);

void
arm_mmio_exit(Arm_MMIO mio);

#endif
