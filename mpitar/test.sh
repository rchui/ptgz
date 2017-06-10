#!/bin/bash

set -e

LOREM="Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."

mkdir test
cd test
exec &>test.log

set -v

# compare two tar files
function cmptar() { (
  # at least tar 1.26 produces different files from tar 1.29 so I cannot check
  # for bitwise identity unless the exact version is known
  if tar --version | grep --silent -F 'tar (GNU tar) 1.29' ; then
    CHECK_BITWISE=yes
  fi
  if [ -n $CHECK_BITWISE ] ; then
    cmp $1 $2
  else
    a=${1%.tar}
    b=${2%.tar}
    rm -rf cmptar
    mkdir -p cmptar/$a cmptar/$b
    tar -C cmptar/$a -xf $1
    tar -C cmptar/$b -xf $2
    diff -r --brief --binary cmptar/$a cmptar/$b
  fi
)}

TAR='tar --no-recursion --format=pax --record-size=512 --pax-option delete=?time --pax-option exthdr.name=%d/%f.paxhdr'
LONGNAME=fileABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ
LONGDIR=dirABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ

echo "file1 $LOREM" >file1
echo "file2 $LOREM$LOREM" >file2
ln -s file1`seq -s'' 1 70` file3
mkdir dir1 dir2 dir1/dir11 dir2/$LONGDIR
echo "file3 $LOREM$LOREM$LOREM" >dir1/file3
echo "file4 $LOREM$LOREM$LOREM$LOREM" >dir1/dir11/file3
echo "Longfile $LOREM$LOREM" >dir1/$LONGNAME

find file2 dir2 -type f -or -type l -or -type d >files.txt
cat files.txt | mpirun -n 2 ../mpitar -f mpitar.tar -c file1 dir1 -T -
$TAR --recursion file1 dir1 --no-recursion -T files.txt -c -f tar.tar mpitar.tar.idx
cmptar tar.tar mpitar.tar

grep -v file2 <mpitar.tar.idx >nofile2.idx
../choptar.pl nofile2.idx mpitar.tar >chopped.tar
awk '{print $2}' nofile2.idx | $TAR -T - -c -f nofile2.tar
cmptar nofile2.tar chopped.tar

../extractindex.pl mpitar.tar >extracted_mpitar.tar.idx
cmp mpitar.tar.idx extracted_mpitar.tar.idx
