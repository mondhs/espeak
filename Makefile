REFIX=/usr
BINDIR=$(PREFIX)/bin
DATADIR=$(PREFIX)/share/espeak-data

.PHONY: all clean distclean

##### standard build actions:

all: src/libespeak.so src/libespeak.a src/espeakedit docs/speak_lib.h

install: all
	cd src && make DESTDIR=$(DESTDIR) PREFIX=$(PREFIX) install && cd ..
	install -m 755 src/espeakedit $(DESTDIR)$(BINDIR)

clean:
	cd src && rm -f *.o *~ && cd ..

distclean: clean
	cd src && rm -f libespeak.a libespeak.so.* speak espeak espeakedit && cd ..

##### build targets:

common_SOURCES = \
	src/compiledict.cpp \
	src/dictionary.cpp \
	src/intonation.cpp \
	src/klatt.cpp \
	src/mbrowrap.cpp \
	src/numbers.cpp \
	src/readclause.cpp \
	src/phonemelist.cpp \
	src/setlengths.cpp \
	src/sonic.cpp \
	src/synthdata.cpp \
	src/synthesize.cpp \
	src/synth_mbrola.cpp \
	src/translate.cpp \
	src/tr_languages.cpp \
	src/voices.cpp \
	src/wavegen.cpp 

libespeak_SOURCES = \
	src/speak_lib.cpp \
	src/espeak_command.cpp \
	src/event.cpp \
	src/fifo.cpp \
	src/wave.cpp \
	src/wave_pulse.cpp \
	src/wave_sada.cpp \
	src/debug.cpp

espeakedit_SOURCES = \
	src/compiledata.cpp \
	src/espeakedit.cpp \
	src/extras.cpp \
	src/formantdlg.cpp \
	src/menus.cpp \
	src/options.cpp \
	src/prosodydisplay.cpp \
	src/spect.cpp \
	src/spectdisplay.cpp \
	src/spectseq.cpp \
	src/transldlg.cpp \
	src/voicedlg.cpp \
	src/vowelchart.cpp

docs/speak_lib.h: src/speak_lib.h
	cp $< $@

src/libespeak.a: $(common_SOURCES) $(libespeak_SOURCES)
	cd src && make libespeak.a PREFIX=$(PREFIX) && cd ..

src/libespeak.so: $(common_SOURCES) $(libespeak_SOURCES)
	cd src && make libespeak.so PREFIX=$(PREFIX) && cd ..

src/espeakedit: $(common_SOURCES) $(libespeak_SOURCES) $(espeakedit_SOURCES)
	cd src && make espeakedit PREFIX=$(PREFIX) && cd ..
