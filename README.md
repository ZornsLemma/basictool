TODO: Actually write something!

# basictool

## Overview

basictool is a command-line utility which can tokenise, de-tokenise, pack and analyse BBC BASIC programs for use with Acorn BBC BASIC on the BBC Micro and Acorn Electron.

BBC BASIC programs are tokenised by the interpreter when entered, and are usually saved and loaded in this tokenised form. This makes them hard to view or edit on a modern PC. basictool can tokenise and de-tokenise BBC BASIC programs:
```
$ # Start off with an ASCII text BASIC program, editable in any text editor.
$ cat test.bas
10PRINT "Hello, world!"
$ # Now tokenise it.
$ basictool -t test.bas test.tok
$ xxd test.tok
00000000: 0d00 0a15 f120 2248 656c 6c6f 2c20 776f  ..... "Hello, wo
00000010: 726c 6421 220d ff                        rld!"..
$ # Note that test.tok is tokenised - there's no "PRINT" in xxd's output.
$ # If you transferred test.tok to a real BBC, you could LOAD it in the usual way.
$ # Now we have a tokenised BASIC program, we can demonstrate de-tokenising it.
$ basictool test.tok
   10PRINT "Hello, world!"
$ # The previous command gave no output filename, so the output went to the terminal.
$ # But we can also de-tokenise to a file.
$ basictool test.tok test2.bas
$ # test2.bas is ASCII text BASIC, editable in any text editor.
$ cat test2.bas
   10PRINT "Hello, world!"
```

(basictool doesn't care about file extensions; I'm using .bas and .tok here for text BASIC and tokenised BASIC respectively, but you can use any conventions you wish.)

You can also "pack" a program, making it significantly less readable but taking up less memory:
```
$ cat test3.bas
square_root_of_49 = 7
my_variable = 42 + square_root_of_49
PRINT my_variable
$ basictool -p test3.bas
    1s=7:m=42+s:PRINTm
$ # basictool is happy to output the packed program as ASCII text, but it would
$ # usually make more sense to save it in tokenised form suitable for use with
$ # BBC BASIC.
$ basictool -p -t test3.bas test3.tok
$ xxd test3.tok
00000000: 0d00 0111 733d 373a 6d3d 3432 2b73 3af1  ...s=7:m=42+s:.
00000010: 6d0d ff                                  m..
```



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
