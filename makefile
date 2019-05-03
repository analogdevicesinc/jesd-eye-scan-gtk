DESTDIR=/usr/local
CFLAGS= -g -O2 -Wall
CFLAGS+= `pkg-config --cflags gtk+-3.0` -Wl,--export-dynamic
LIBS= `pkg-config --libs gtk+-3.0`

ALL:
	$(CC)  $(CFLAGS) jesd_eye_scan.c jesd_common.c $(LIBS) -o jesd_eye_scan -lm

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/jesd/
	install ./jesd_eye_scan $(DESTDIR)/bin/
	install ./jesd_eye_scan_autostart.sh $(DESTDIR)/bin/
	install ./jesd.glade $(DESTDIR)/share/jesd/
	install ./icons/ADIlogo.png $(DESTDIR)/share/jesd/
	install jesd_eye_scan.desktop $(HOME)/.config/autostart/jesd_eye_scan.desktop

clean:
	rm -rf jesd_eye_scan
	rm -f jesd_eye_scan.o
	rm -rf *.png *.txt *.eye *~
