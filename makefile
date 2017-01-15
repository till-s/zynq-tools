GNU_DIR=/opt/Xilinx/SDK/2013.4/gnu/arm/lin/
GNU_BIN=$(GNU_DIR)/bin/
CROSS=arm-xilinx-linux-gnueabi-
GNU_BIN=/media/till/1e668486-a93f-4895-9460-bd877790aca5/buildroot/buildroot-2016.11/host/linux-x86_64/arm/usr/bin/
CROSS=arm-linux-
CC=$(GNU_BIN)$(CROSS)gcc
AR=$(GNU_BIN)$(CROSS)ar
RANLIB=$(GNU_BIN)$(CROSS)ranlib

DSTDIR=/remote

APPS=snd-test mmio i2cm ldfilt mdio-10ge snd mdio_bitbang dump-fifo gpiotst

LIBS=-lmmio-util

gpiotst_LIBS=-lgpio
mdio_bitbang_LIBS=-lgpio
ldfilt_LIBS=-lm
snd-test_LIBS=-lm
mmio_LIBS=
i2cm_LIBS=
mdio-10ge_LIBS=
snd_LIBS=

all: $(APPS:%=$(DSTDIR)/%) libgpio.a libmmio-util.a

%.o: %.c
	$(CC) -O2 -I. -c $^

libmmio-util.a: mmio-util.o
	$(AR) cr $@ $^	
	$(RANLIB) $@

libgpio.a: gpiolib.o
	$(AR) cr $@ $^	
	$(RANLIB) $@

$(DSTDIR)/%: %.o libmmio-util.a libgpio.a
	$(CC) -o $@ $< -L. $($(@:$(DSTDIR)/%=%)_LIBS) $(LIBS)


clean:
	$(RM) libmmio-util.a $(patsubst %.c,%.o,$(wildcard *.c))

# remove 'installed' binaries, too.
purge: clean
	$(RM) $(patsubst %.c,$(DSTDIR)/%,$(wildcard *.c))
	
