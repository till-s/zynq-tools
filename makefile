GNU_DIR=/opt/Xilinx/SDK/2013.4/gnu/arm/lin/
GNU_BIN=$(GNU_DIR)/bin/
CROSS=arm-xilinx-linux-gnueabi-
CC=$(GNU_BIN)$(CROSS)gcc
AR=$(GNU_BIN)$(CROSS)ar
RANLIB=$(GNU_BIN)$(CROSS)ranlib

DSTDIR=/remote

APPS=snd-test mmio i2cm ldfilt

ldfilt_LIBS=-lmmio-util
snd-test_LIBS=-lmmio-util -lm
mmio_LIBS=-lmmio-util
i2cm_LIBS=-lmmio-util

all: $(APPS:%=$(DSTDIR)/%)

%.o: %.c
	$(CC) -O2 -I. -c $^

libmmio-util.a: mmio-util.o
	$(AR) cr $@ $^	
	$(RANLIB) $@

$(DSTDIR)/%: %.o libmmio-util.a
	$(CC) -o $@ $< -L. $($(@:$(DSTDIR)/%=%)_LIBS)


clean:
	$(RM) libmmio-util.a $(patsubst %.c,%.o,$(wildcard *.c))

# remove 'installed' binaries, too.
purge: clean
	$(RM) $(patsubst %.c,$(DSTDIR)/%,$(wildcard *.c))
	