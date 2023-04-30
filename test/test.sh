#!/bin/bash

# This is a really crude test harness. It probably won't work properly on
# non-Unix systems, because it doesn't allow for different text file
# conventions.

set -e

VALGRIND=""
if [ "$1" = "-v" ]; then
	VALGRIND="valgrind --leak-check=yes"
fi

mkdir -p tmp
mkdir -p out
/bin/rm -f tmp/zz-test-*.bas out/*.out

# In order to avoid any misguided attempts by git to standardise line endings,
# we generate some simple tests automatically here.
cd tmp
echo -en "A=3\nB=4\nC=5" > zz-test-lf-ut.bas
echo -en "A=3\rB=4\rC=5" > zz-test-cr-ut.bas
echo -en "A=3\r\nB=4\r\nC=5" > zz-test-crlf-ut.bas
echo -en "A=3\n\rB=4\n\rC=5" > zz-test-lfcr-ut.bas
echo -en "A=3\nB=4\nC=5\n" > zz-test-lf.bas
echo -en "A=3\rB=4\rC=5\r" > zz-test-cr.bas
echo -en "A=3\r\nB=4\r\nC=5\r\n" > zz-test-crlf.bas
echo -en "A=3\n\rB=4\n\rC=5\n\r" > zz-test-lfcr.bas
echo -en "A=3\n   B=4\nC=5   \n" >> zz-test-spaces.bas
cd ..

BASICTOOL="$VALGRIND ../basictool"
TESTS="hello.bas loader.tok loader-packed.tok embedded-lf.tok embedded-nul-and-trailing-data.tok tmp/zz-test-*.bas"

# TODO: We could also test stderr (especially with -vv) but let's not get too
# complex for now.
for TEST in $TESTS; do
	echo Running $TEST...
	BASENAME=$(basename $TEST)
	# Before v0.06, --strip-spaces was the default behaviour, so we
	# deliberately emulated that behaviour here. (Some tests don't use
	# $STRIP and therefore test the default behaviour of keeping spaces.)
	STRIP=""
	if [ "$(basename $BASENAME .tok)" == "$BASENAME" ]; then
		STRIP=-s
	fi
	$BASICTOOL -a $STRIP $TEST > out/$BASENAME-a.out
	$BASICTOOL -t $STRIP $TEST > out/$BASENAME-t.out
	$BASICTOOL -t $STRIP --pack $TEST > out/$BASENAME-t-pack.out
	$BASICTOOL -f $STRIP $TEST > out/$BASENAME-f.out
	$BASICTOOL -u $STRIP --renumber-step 100 $TEST > out/$BASENAME-u.out
	$BASICTOOL --line-ref $STRIP $TEST > out/$BASENAME-line-ref.out
	$BASICTOOL --variable-xref $STRIP $TEST > out/$BASENAME-variable-xref.out
	if [ "$TEST" == "tmp/zz-test-spaces.bas" ]; then
		$BASICTOOL -2t $TEST out/$BASENAME-2kt.out
		# The next line generates a warning. The fiddly redirection and
		# grep use is to stop valgrind's stderr output causing a test
		# failure.
		$BASICTOOL -4t $TEST 2>&1 > out/$BASENAME-4kt.out | grep "^warning:" > out/$BASENAME-4kt-err.out
	fi
done

for RESULT in out/*.out; do
	cmp -s $RESULT mst/$(basename $RESULT .out).mst || echo TEST FAILED: $RESULT
done
echo Finished
