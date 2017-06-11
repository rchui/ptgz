#!/usr/bin/env perl

use strict;
use warnings;

# this script extracts the index file from the end of the tar file

if(scalar @ARGV != 1) {
  print STDERR "usage: data.tar\n";
  exit 1;
}

my $SIZE_FIELD = 124;

open(my $tar_fh, "<", $ARGV[0]) or die $!;
# 1024 bytes of zeros at end of file
# possibly rounded to 512 byte boundary
# hopefully at most 22 bytes for the offset string
# 2 bytes for space and newline
my $sz = length($ARGV[0])+24+512+1024;
sysseek($tar_fh, -$sz, 2) or dir $!;
my $buf;
sysread($tar_fh, $buf, $sz) or die $!;
$buf =~ m/([0-9]*) ([^\n]*)\n\0*$/s;
my $off = $1;
my $idx_fn = $2;
sysseek($tar_fh, $off, 0) or die $!;
my $head;
sysread($tar_fh, $head, 512) or die $!;
my $idx_sz = oct(substr($head, $SIZE_FIELD, 12));
sysseek($tar_fh, $off+512, 0) or die $!;
my $data;
sysread($tar_fh, $data, $idx_sz) or die $!;
close($tar_fh);

syswrite(STDOUT, $data) or die $!;
