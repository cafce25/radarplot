# $Id: Makefile,v 1.48 2009-07-25 10:40:54 ecd Exp $

prefix=/usr/local
locale=/usr/local/share/locale


CC = gcc
CFLAGS = -O2 -g -Wall -Werror \
	$(shell pkg-config gtk+-2.0 --cflags)
LDFLAGS = -g

CFLAGS += $(shell if `pkg-config --exists 'gtk+-2.0 >= 2.6.0'` ; then echo -DHAVE_RENDER; fi)
CFLAGS += $(shell if `pkg-config --exists 'gtk+-2.0 >= 2.8.0'` ; then echo -DUSE_GDK_DRAW_TRAPEZOIDS_FIXUP; fi)

# CFLAGS += $(shell if [ "`uname -m`" = "sparc64" ]; then echo "-mcpu=v9"; fi)
# CFLAGS += $(shell if [ "`uname -m`" = "sun4u" ]; then echo "-mcpu=v9"; fi)

OS = $(shell uname -s | sed -e 's/-.*//')

ifeq ($(OS),CYGWIN_NT)
OS = MINGW32_NT
ISCC = /cygdrive/c/Program\ Files/Inno\ Setup\ 5/ISCC.exe
endif

CFLAGS += -DOS_$(OS)

LDLIBS = $(shell pkg-config gtk+-2.0 --libs) \
	 -lcrypto -lm

ifeq ($(OS),MINGW32_NT)
LDFLAGS += -mwindows
LDLIBS += -lwsock32
endif

ifeq ($(OS),Darwin)
CFLAGS += $(shell pkg-config ige-mac-integration --cflags)
LDLIBS += $(shell pkg-config ige-mac-integration --libs)
endif

ICONS = radar16x16.png radar32x32.png radar48x48.png radar64x64.png radar128x128.png

ICONS_16 = $(patsubst %, %.16, $(ICONS))
ICONS_256 = $(patsubst %, %.256, $(ICONS))
ICONS_H = $(patsubst %.png, %.h, $(ICONS))
ICONS_SETUP = radar55x55.bmp

RADAR_MAJOR = 2
RADAR_MINOR = 0
RADAR_PATCHLEVEL = 0

CFLAGS += -DRADAR_MAJOR=$(RADAR_MAJOR) \
	  -DRADAR_MINOR=$(RADAR_MINOR) \
	  -DRADAR_PATCHLEVEL=$(RADAR_PATCHLEVEL) \
	  -DPREFIX=\"$(prefix)\"

RELEASE = radarplot-$(RADAR_MAJOR).$(RADAR_MINOR).$(RADAR_PATCHLEVEL)

OBJS = radar.o print.o afm.o encoding.o license.o public.o

SRCS = $(patsubst %.o,%.c,$(OBJS)) icongen.c

ifeq ($(OS),MINGW32_NT)
OBJS += icon.o
endif

ifeq (.depend,$(wildcard .depend))
all: do-it-all
include .depend
else
all: depend-and-build
endif


depend-and-build: $(ICONS_H) $(SRCS)
	$(CC) $(CFLAGS) -MM $(SRCS) >.depend
	$(MAKE) $(MAKEARGS) all


do-it-all: po $(ICONS) radarplot

bundle/radarplot.app: radarplot
	mkdir -p bundle/radarplot.app/Contents/MacOS
	cp radarplot bundle/radarplot.app/Contents/MacOS
	mkdir -p bundle/radarplot.app/Contents/Resources
	cp radarplot.icns bundle/radarplot.app/Contents/Resources
	mkdir -p bundle/radarplot.app/Contents/Resources/share/locale
	$(MAKE) locale=$(PWD)/bundle/radarplot.app/Contents/Resources/share/locale -C po install
	cp Info.plist bundle/radarplot.app/Contents

radarplot: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

.PHONY: po
po:
	$(MAKE) -C $@ all

radar.ico: $(ICONS_256) $(ICONS_16)
	png2ico radar.ico $(ICONS_256) --colors 16 $(ICONS_16)

icon.o: radar.ico $(ICONS_SETUP)
	echo "101 ICON $<" >rc.rc
	windres -i rc.rc -o $@
	rm -f rc.rc

radar.o: radar.c $(ICONS_H)

radar%.h: radar%.png
	gdk-pixbuf-csource --static --name=radar$* $< >$@

radar%.png: icongen
	./icongen $* radar$*.png

%.png.256: %.png
	pngquant -force 256 $<
	mv `basename $< .png`-fs8.png $@

%.png.16: %.png
	pngquant -force 16 $<
	mv `basename $< .png`-fs8.png $@

%.bmp: %.png
	pngtopnm -mix -background 1,1,1 $< >`basename $< .png`.pnm
	pnmcolormap 256 `basename $< .png`.pnm >`basename $< .png`.map
	pnmremap -map `basename $< .png`.map `basename $< .png`.pnm | ppmtobmp >$@
	rm -f `basename $< .png`.pnm `basename $< .png`.map

radarplot-setup.exe: all setup.iss $(ICONS_SETUP)
	$(MAKE) locale=$(PWD)/po/locale -C po install
	strip radarplot.exe
	$(ISCC) /Q setup.iss

icongen: icongen.o
	$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS)

ifeq ($(OS),Darwin)
install: bundle/radarplot.app
else
ifeq ($(OS),MINGW32_NT)
install: radarplot-setup.exe
else
install: all
	install -d $(prefix)/bin
	install -s -m 755 radarplot $(prefix)/bin
	install -d $(prefix)/share/radarplot
	install -m 644 Helvetica.afm $(prefix)/share/radarplot
	$(MAKE) -C po locale=$(locale) install
endif
endif

clean:
	rm -f icongen *.o radar??x??.h radar55x55.png \
		.depend *.png.* *.pgm *.pbm *.ico *.bmp core

realclean: clean
	rm -f radarplot *.png
	rm -rf radarplot.app

release:
	rm -rf tmp/$(RELEASE)
	mkdir -p tmp/$(RELEASE)
	cp radar.h radar.c print.c afm.h afm.c encoding.h encoding.c \
		translation.h translation.c \
		license.h license.c public.h public.c \
		icongen.c COPYING ChangeLog Makefile \
		Helvetica.afm tmp/$(RELEASE)
	mkdir -p tmp/$(RELEASE)/po
	cp po/*.po po/*.pot po/Makefile tmp/$(RELEASE)/po
	mkdir -p tmp/$(RELEASE)/Plots
	cp Plots/*.rpt tmp/$(RELEASE)/Plots
	(cd tmp; tar cvf - $(RELEASE) | gzip -9c >$(RELEASE).tar.gz)
	mv tmp/$(RELEASE).tar.gz ftp

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
