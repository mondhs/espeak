# remove -fno-exceptions  if it causes problems with stdio library on some platforms (ARM)

DATADIR=/usr/share/espeak-data

PREFIX=/usr
BINDIR=$(PREFIX)/bin
INCDIR=$(PREFIX)/include/espeak
LIBDIR=$(PREFIX)/lib

RELEASE = 1.46
LIBESPEAK = libespeak.so
STATIC_LIBESPEAK = libespeak.a
LIB_VERSION = 1
LIBTAG = $(LIB_VERSION).$(RELEASE)

# Use SONAME_OPT=-Wl,h, on Solaris
SONAME_OPT=-Wl,-soname,

# Use EXTRA_LIBS=-lm on Solaris
EXTRA_LIBS =

INSTALL = install
LN_SF = /bin/ln -sf
MKDIR = mkdir -p

COMMITID = $(shell git rev-parse HEAD)

CXXFLAGS = -O2 -Wall -c
CPPFLAGS = -pedantic -fno-exceptions -D PATH_ESPEAK_DATA=\"$(DATADIR)\" -D COMMITID=\"$(COMMITID)\"

.cpp.o:
	# Compiling $@
	$(CXX) $(CXXFLAGS) $(USE_AUDIO) -I. $(CPPFLAGS) $<

x_%.o: %.cpp
	# Compiling $@
	$(CXX) $(CXXFLAGS) $(USE_AUDIO) -fpic -fvisibility=hidden \
	-I. -D LIBRARY $(CPPFLAGS) $< -o x_$*.o

all: $(LIBESPEAK) $(STATIC_LIBESPEAK) $(ESPEAKEDIT)

common_SOURCES = compiledict.cpp dictionary.cpp intonation.cpp \
	klatt.cpp mbrowrap.cpp numbers.cpp readclause.cpp \
	phonemelist.cpp setlengths.cpp sonic.cpp \
	synthdata.cpp synthesize.cpp synth_mbrola.cpp \
	translate.cpp tr_languages.cpp voices.cpp wavegen.cpp 

libespeak_SOURCES = speak_lib.cpp $(common_SOURCES)
libespeak_OBJS = $(patsubst %.cpp,x_%.o,$(libespeak_SOURCES))
libespeak_LIBS = -lstdc++ -lpthread
$(LIBESPEAK): $(libespeak_OBJS)
	# Linking $@
	$(CXX) $(LDFLAGS) -shared $(SONAME_OPT)$(LIBESPEAK).$(LIB_VERSION) -o $@ \
	$(libespeak_OBJS) $(libespeak_LIBS)
	mv $(LIBESPEAK) $(LIBESPEAK).$(LIBTAG)
	$(LN_SF) $(LIBESPEAK).$(LIBTAG) $(LIBESPEAK).$(LIB_VERSION)
	$(LN_SF) $(LIBESPEAK).$(LIB_VERSION) $(LIBESPEAK)

$(STATIC_LIBESPEAK): $(libespeak_OBJS)
	# Linking $@
	$(AR) cqs $(STATIC_LIBESPEAK) $(libespeak_OBJS)


clean:
	rm -f *.o *.a *~

distclean: clean
	rm -f $(LIBESPEAK)*
	rm -f $(STATIC_LIBESPEAK)

install: all
	# Create directories
	$(MKDIR) $(DESTDIR)$(LIBDIR)
	$(MKDIR) $(DESTDIR)$(INCDIR)
	# Install shared library
	$(INSTALL) -m 755 $(LIBESPEAK).$(LIBTAG) $(DESTDIR)$(LIBDIR)
	# Install static library
	$(INSTALL) -m 755 $(STATIC_LIBESPEAK) $(DESTDIR)$(LIBDIR)
	$(LN_SF) $(LIBESPEAK).$(LIBTAG) $(DESTDIR)$(LIBDIR)/$(LIBESPEAK).$(LIB_VERSION)
	$(LN_SF) $(LIBESPEAK).$(LIB_VERSION) $(DESTDIR)$(LIBDIR)/$(LIBESPEAK)
	# Install development headers
	$(INSTALL) -pm 644 speak_lib.h $(DESTDIR)$(INCDIR)
