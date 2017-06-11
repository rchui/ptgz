/* Copyright (c) 2017 The Board of Trustees of the University of Illinois
 * All rights reserved.
 *
 * Developed by: National Center for Supercomputing Applications
 *               University of Illinois at Urbana-Champaign
 *               http://www.ncsa.illinois.edu/
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimers.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimers in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the names of the National Center for Supercomputing Applications,
 * University of Illinois at Urbana-Champaign, nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * Software without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.  */

#include "cmdline.hh"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
void usage(const char *cmd) {
  fprintf(stdout, "%s: -c -f FILE [-T FILE] [FILE]...\n", cmd);
}
}

cmdline::cmdline(const int argc, char * const argv[], bool mute) :
  action(ACTION_INVALID), entries(), tarfilename()
{
  int opt;
  opterr = 0; // we handle our own errors
  while((opt = getopt(argc, argv, "-cf:T:h")) != -1) {
    switch(opt) {
      case 'c':
        if(action && action != ACTION_CREATE) {
          if(!mute)
            fprintf(stderr,
                    "Only one of [xct] is allowed, but both 'c' and '%c' were used\n",
                    action);
          action = ACTION_ERROR;
          goto quit;
        }
        action = ACTION_CREATE;
        break;
      case 'f':
        if(!tarfilename.empty()) {
          if(!mute)
            fprintf(stderr,
                    "Multiple output file names '%s' and '%s' specified\n",
                    tarfilename.c_str(), optarg);
          action = ACTION_ERROR;
          goto quit;
        }
        tarfilename = optarg;
        break;
      case 'T':
        entries.add_entry(new filelist(optarg));
        break;
      case 'h':
        usage(argv[0]);
        action = ACTION_EXIT;
        goto quit;
        break;
      case 1:
        entries.add_entry(new filearg(optarg));
        break;
      case '?':
        if(!mute)
          fprintf(stderr, "Unknown option '%c'\n", optopt);
        action = ACTION_ERROR;
        goto quit;
        break;
      default:
        if(!mute)
          fprintf(stderr, "Unknown option '%c'\n", opt);
        action = ACTION_ERROR;
        goto quit;
        break;
    }
  }

  // check that we have all required options
  if(!action) {
    if(!mute)
      fprintf(stderr, "No action specified.\n");
    action = ACTION_ERROR;
  }
  if(tarfilename.empty()) {
    if(!mute)
      fprintf(stderr, "No output file name specified\n");
    action = ACTION_ERROR;
  }
quit:
  return;
}
