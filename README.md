TODO: Actually write something!

## Building

### Getting the ROM images

You can download EDITORA100 and EDITORB100 from https://stardot.org.uk/forums/viewtopic.php?p=81362#p81362. Rename them to have a ".rom" extension and copy them into the roms directory.

I recommend using BBC BASIC 4r32, simply because that's what I've done most testing with. You can download it from http://mdfs.net/Software/BBCBasic/BBC/. Other versions of 6502 BASIC will probably work as well; the BASIC ROM is only used for tokenised, de-tokenising and renumbering BASIC programs, so most of the changes between different BASIC versions aren't relevant. Rename the file "basic4.rom" and copy it into the roms directory.

TODO: This seems a little fiddly, if those are going to be my recommended download options. Maybe I should just use the original names with no extensions.

The ROMs are compiled into the executable, so these files are only need when building - the resulting executable is self-contained and can be copied to wherever you want to install it.

## Credits

6502 emulation is performed using lib6502. This was originally written by Ian Piumarta, but the versions of lib6502.[ch] included here are taken from PiTubeDirect (https://github.com/hoglet67/PiTubeClient).

Cross-platform command line parsing is performed using cargs (https://github.com/likle/cargs).

TODO BASIC ROM

TODO: INCLUDE THE TEXT (OR A MILDLY MORE READABLE VARIANT) FROM THE "--roms" HELP ABOUT BASIC EDITOR/UTILITIES (ALTRA, BAILDON)
