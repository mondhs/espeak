Changes to eSpeak to support building on a POSIX system by running make.

This does the following things:
   1/  compile the libespeak library;
   2/  compile the speak and espeak command-line applications;
   3/  compile the espeakedit application;
   4/  compile the voice data, creating an espeak-data directory.

This allows you to run:

	make && sudo make install

and end up with a working espeak application.


This branch also contains some bug fixes and improvements that get
sent back upstream.


Espeak-lt
   1/  add lithuanian language
   2/  changed to wx automatic library and cxx resolution 

More Info: https://github.com/mondhs/espeak/wiki

in ubuntu 14.04 espeak-data path is /usr/lib/x86_64-linux-gnu/espeak-data
In order to mbrola work copy to /usr/share/mbrola/lt1/lt1 (directory with content most importat is lt1 file in the dir) 
