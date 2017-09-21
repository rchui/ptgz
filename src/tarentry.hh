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

#ifndef TAR_ENTRY_HH_
#define TAR_ENTRY_HH_

#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>

#include <string>
#include <vector>

struct ustar_hdr {
  char name[100];      /*   0 */
  char mode[8];        /* 100 */
  char uid[8];         /* 108 */
  char gid[8];         /* 116 */
  char size[12];       /* 124 */
  char mtime[12];      /* 136 */
  char chksum[8];      /* 148 */
  char typeflag;       /* 156 */
  char linkname[100];  /* 157 */
  char magic[6];       /* 257 */
  char version[2];     /* 263 */
  char uname[32];      /* 265 */
  char gname[32];      /* 297 */
  char devmajor[8];    /* 329 */
  char devminor[8];    /* 337 */
  char prefix[155];    /* 345 */ /* not used in pax format */
  char pad[12];        /* 500 */
};
#define BLOCKSIZE 512
namespace { typedef int ustar_hdr_size_assert[(sizeof(ustar_hdr) == BLOCKSIZE) ? 1 : -1]; }
#define REGTYPE '0'
#define SYMTYPE '2'
#define DIRTYPE '5'
#define XHDTYPE 'x'
#define TVERSION "00"
#define TMAGIC "ustar\0"
#define MODE_MASK 07777

#define MAX_FILE_SIZE 077777777777

class tarentry
{
  public:
  tarentry(const std::string fn, const size_t off);
  tarentry() {};
  ~tarentry() {};

  // construct a tar header
  std::vector<char> make_tar_header() const;
  // size of tar entry in the file
  size_t size() const;

  // serialize and de-serialize data for MPI transmission
  size_t deserialize(const char *buf);
  std::string serialize() const;

  // accessors
  const std::string &get_filename() const { return filename; }
  size_t get_filesize() const { return is_reg() ? size_t(statbuf.st_size) : 0; }
  bool is_reg() const { return S_ISREG(statbuf.st_mode); }
  size_t get_offset() const { return offset; }

  private:
  size_t offset;
  struct stat statbuf;
  std::string filename;
  std::string linkname;

  // size of pax extended header
  size_t get_paxsize() const;
  static void make_ustar_header_block(ustar_hdr &hdr, const int xtype,
                                      const struct stat &statbuf,
                                      const char *fn, const char *ln);
  static size_t round_to_block(size_t sz) {
    return (sz + BLOCKSIZE-1) & ~(BLOCKSIZE-1);
  }
  static size_t record_length(const char *keyword, const char *value);
};

#endif // TAR_ENTRY_HH_
