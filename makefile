DESTDIR=/usr/local
CFLAGS= -g -O2 -Wall
CFLAGS+= `pkg-config --cflags gtk+-3.0` -Wl,--export-dynamic
LIBS= `pkg-config --libs gtk+-3.0`

# Optional libiio support
ifdef USE_LIBIIO
CFLAGS+= -DUSE_LIBIIO `pkg-config --cflags libiio`
LIBS+= `pkg-config --libs libiio`
endif
src = $(wildcard *.c)
obj = $(src:.c=.o)


all: jesd_status jesd_eye_scan

jesd_status: jesd_status.o jesd_common.o
	$(CC) -o $@ $^ -lncurses $(if $(USE_LIBIIO),`pkg-config --libs libiio`)

jesd_eye_scan: jesd_eye_scan.o jesd_common.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS) -lm

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/jesd/
	install ./jesd_status $(DESTDIR)/bin/
	install ./jesd_eye_scan $(DESTDIR)/bin/
	install ./jesd_eye_scan_autostart.sh $(DESTDIR)/bin/
	install ./jesd.glade $(DESTDIR)/share/jesd/
	install ./icons/ADIlogo.png $(DESTDIR)/share/jesd/
	mkdir -p ${HOME}/.config/autostart
	install jesd_eye_scan.desktop $(HOME)/.config/autostart/jesd_eye_scan.desktop

clean:
	rm -f $(obj) jesd_status jesd_eye_scan
	rm -rf *.png *.eye
