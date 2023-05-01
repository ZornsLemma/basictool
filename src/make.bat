@REM Build basictool with Visual C++

cl /std:c11 /Fe:bintoinc.exe bintoinc.c
bintoinc ../roms/EDITORA100 > zz-editor-a.c
@IF ERRORLEVEL 1 EXIT /B 1

bintoinc ../roms/EDITORB100 > zz-editor-b.c
@IF ERRORLEVEL 1 EXIT /B 1

bintoinc ../roms/Basic2 > zz-basic-2.c
@IF ERRORLEVEL 1 EXIT /B 1

bintoinc ../roms/Basic432 > zz-basic-4.c
@IF ERRORLEVEL 1 EXIT /B 1

cl /MP /MT /Zi /O2 /D_CRT_DECLARE_NONSTDC_NAMES=0 /std:c11 /Fd:../basictool.pdb /Fe:../basictool.exe main.c config.c emulation.c driver.c roms.c utils.c lib6502.c cargs.c
@IF ERRORLEVEL 1 EXIT /B 1
