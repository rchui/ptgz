#!/usr/bin/env perl

use strict;
use warnings;

# this file takes a index file like the one written by mpitar, possible with
# some lines removed and produces a chopped tarfile that only contains the
# listed files seeking to the correct spots in the original file to get to the
# data

# TODO: take a list of files on the command line and get the index file from
# the tarfile itself to find the offsets

if(scalar @ARGV != 2) {
  print STDERR "usage: chop.idx data.tar\n";
  exit 1;
}

my %files;
open(my $files_fh, "<", $ARGV[0]) or die $!;
while(<$files_fh>) {
  chomp;
  m/([0-9A-Fa-f]*) (.*)/;
  $files{int($1)} = $2;
}
close($files_fh);

open(my $tar_fh, "<", $ARGV[1]) or die $!;
foreach my $off (sort {$a <=> $b} keys %files) {
  my $head;
  sysseek($tar_fh, $off, 0) or die $!;
  sysread($tar_fh, $head, 512) or die $!;
  my $sz = oct(substr($head, 124, 12));
  my $typeflag = substr($head, 156, 1);
  if($typeflag eq "x") {
    # found extended header, which may contain a file size
    my $exthead;
    sysseek($tar_fh, $off+512, 0) or die $!;
    sysread($tar_fh, $exthead, $sz) or die $!;
    my $pax_sz = ($sz + 511) & ~511;
    $sz = undef;
    # check if the extentded records contain a size and use that iffound
    foreach my $record (split '\n', $exthead) {
      $record =~ m/\s*(\d+)\s+(\w+)=(.*)/ or
        die "$record does not look like a extended header record.";
      if($2 eq "size") {
        $sz = int($3);
      }
    }
    unless(defined($sz)) {
      # no size record, read file size from actual (next) header
      sysseek($tar_fh, $off+512+$pax_sz, 0) or die $!;
      sysread($tar_fh, $head, 512) or die $!;
      $sz = oct(substr($head, 124, 12));
    }
    $sz += 512 + $pax_sz;
  }
  my $rnd_sz = ($sz + 511) & ~511;
  my $data;
  sysseek($tar_fh, $off, 0) or die $!;
  sysread($tar_fh, $data, 512+$rnd_sz) or die $!;
  syswrite(STDOUT, $data);
}
close($tar_fh);
my $term = "\0" x 1024;
syswrite(STDOUT, $term);
