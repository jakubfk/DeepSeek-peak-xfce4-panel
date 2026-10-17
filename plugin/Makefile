CC         ?= cc
PKG_CONFIG ?= pkg-config
MODULE      = libdeepseek-indicator.so
SOURCE      = deepseek-indicator.c
PKGS        = gtk+-3.0 libxfce4panel-2.0

CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -fPIC $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDLIBS  += $(shell $(PKG_CONFIG) --libs $(PKGS))

all: $(MODULE)

$(MODULE): $(SOURCE)
	$(CC) $(CFLAGS) -shared -o $@ $(SOURCE) $(LDLIBS)

test: $(SOURCE)
	$(CC) $(CFLAGS) -DDEEPSEEK_TEST -o test-peak $(SOURCE) $(LDLIBS)
	./test-peak

clean:
	rm -f $(MODULE) test-peak

.PHONY: all test clean
