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

#include "tarentry.hh"

#include <cassert>
#include <cstring>
#include <cerrno>
#include <algorithm>

#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>

tarentry::tarentry(const std::string fn, const size_t off) : offset(off),
                   filename(fn)
{
  int ierr = lstat(filename.c_str(), &statbuf);
  if(ierr) {
    fprintf(stderr, "Could not stat '%s': %s\n", filename.c_str(),
            strerror(errno));
    exit(1);
  }

  if(S_ISLNK(statbuf.st_mode))
  {
    char buf[PATH_MAX];
    size_t sz_read =
      readlink(filename.c_str(), buf, sizeof(buf));
    if(sz_read == (size_t)-1)
    {
      fprintf(stderr, "Could not read link %s: %s\n", filename.c_str(),
              strerror(errno));
      exit(1);
    }
    buf[statbuf.st_size] = '\0';
    linkname = buf;
  }

  if(S_ISDIR(statbuf.st_mode) && *filename.rbegin() != '/') {
    filename += "/";
  }
}

size_t tarentry::deserialize(const char *buf)
{
  size_t sz;
  const char *p = buf;
  memcpy(&sz, p, sizeof(sz)); p += sizeof(sz);
  memcpy(&statbuf, p, sizeof(statbuf)); p += sizeof(statbuf);
  memcpy(&offset, p, sizeof(offset)); p += sizeof(offset);
  size_t filenamelen;
  memcpy(&filenamelen, p, sizeof(filenamelen)); p += sizeof(filenamelen);
  filename = std::string(p, filenamelen); p += filenamelen;
  size_t linknamelen;
  memcpy(&linknamelen, p, sizeof(linknamelen)); p += sizeof(linknamelen);
  if(linknamelen > 0) { // two statements!
    linkname = std::string(p, linknamelen); p += linknamelen;
  }
  assert(sz == size_t(p-buf));
  return sz;
}

std::string tarentry::serialize() const
{
  std::string buf;
  size_t sz = sizeof(size_t) + sizeof(statbuf) + sizeof(size_t) +
              sizeof(size_t) + filename.size() + sizeof(size_t) +
              linkname.size();
  size_t fnsz = filename.size();
  size_t lnsz = linkname.size();
  buf = std::string(reinterpret_cast<const char*>(&sz), sizeof(sz)) +
        std::string(reinterpret_cast<const char*>(&statbuf), sizeof(statbuf)) +
        std::string(reinterpret_cast<const char*>(&offset), sizeof(offset)) +
        std::string(reinterpret_cast<const char*>(&fnsz), sizeof(fnsz)) +
        filename +
        std::string(reinterpret_cast<const char*>(&lnsz), sizeof(lnsz)) +
        linkname;
  return buf;
}

std::vector<char> tarentry::make_tar_header() const
{
  size_t pax_sz = get_paxsize();
  size_t pax_hdr_sz = pax_sz > 0 ? round_to_block(BLOCKSIZE + pax_sz) : 0;
  size_t full_hdr_sz = BLOCKSIZE + pax_hdr_sz;
  std::vector<char> full_hdr(full_hdr_sz);

  ustar_hdr &pax_hdr = *reinterpret_cast<ustar_hdr*>(&full_hdr[0]);
  ustar_hdr &hdr = *reinterpret_cast<ustar_hdr*>(&full_hdr[pax_hdr_sz]);
  if(pax_sz > 0) {
    struct stat pax_statbuf = statbuf;
    char *dirpart = strdup(filename.c_str());
    char *filepart = strdup(filename.c_str());
    // anything, really
    std::string pax_filename(std::string(dirname(dirpart))+"/"+
                             std::string(basename(filepart))+".paxhdr");
    free(filepart);
    free(dirpart);
    pax_statbuf.st_mode = 0644 | S_IFREG;
    pax_statbuf.st_size = off_t(get_paxsize());
    make_ustar_header_block(pax_hdr, XHDTYPE, pax_statbuf,
                            pax_filename.c_str(), "");

    char *p = &full_hdr[BLOCKSIZE];
    // end pointer of pax header space plus 1
    // this claims one more byte so that snprintf can write its NUL terminator,
    // it writes into the ustar header which is NUL initailized anyway
    char *q = &full_hdr[pax_hdr_sz]+1;
    if(filename.size() > sizeof(((ustar_hdr*)0)->name)) {
      int sz = record_length("path", filename.c_str());
      p += snprintf(p, q-p, "%d path=%s\n", sz, filename.c_str());
    }
    assert(p < q);
    if(linkname.size() > sizeof(((ustar_hdr*)0)->linkname)) {
      int sz = record_length("linkpath", linkname.c_str());
      p += snprintf(p, q-p, "%d linkpath=%s\n", sz, linkname.c_str());
    }
    assert(p < q);
    if(S_ISREG(statbuf.st_mode) && statbuf.st_size > MAX_FILE_SIZE) {
      char buf[128];
      sprintf(buf, "%zu", size_t(statbuf.st_size));
      int sz = record_length("size", buf);
      p += snprintf(p, q-p, "%d size=%s\n", sz, buf);
    }
    assert(p < q);
  }
  make_ustar_header_block(hdr, 0, statbuf, filename.c_str(), linkname.c_str());

  return full_hdr;
}

size_t tarentry::size() const
{
  size_t pax_sz = get_paxsize();
  size_t pax_hdr_sz = pax_sz > 0 ? round_to_block(BLOCKSIZE + pax_sz) : 0;
  size_t full_hdr_sz = BLOCKSIZE + pax_hdr_sz;
  return round_to_block(full_hdr_sz + get_filesize());
}

size_t tarentry::get_paxsize() const
{
  size_t pax_sz = 0;

  // format of a pax extended record:
  // "%d %s=%s\n", <length>, <keyword>, <value>
  // where length is the length of the record including the newline and %d may
  // be space padded
  if(filename.size() > sizeof(((ustar_hdr*)0)->name)) {
    pax_sz += record_length("path", filename.c_str());
  }
  if(linkname.size() > sizeof(((ustar_hdr*)0)->linkname)) {
    pax_sz += record_length("linkpath", linkname.c_str());
  }
  if(S_ISREG(statbuf.st_mode) && statbuf.st_size > MAX_FILE_SIZE) {
    char buf[128];
    sprintf(buf, "%zu", size_t(statbuf.st_size));
    pax_sz += record_length("size", buf);
  }

  return pax_sz;
}

void tarentry::make_ustar_header_block(ustar_hdr &hdr, const int xtype,
                                       const struct stat &statbuf,
                                       const char *filename, const char *ln)
{
  struct group *grp;
  struct passwd *pwd;

  if(!(S_ISLNK(statbuf.st_mode) || S_ISREG(statbuf.st_mode) ||
       S_ISDIR(statbuf.st_mode))) {
    fprintf(stderr, "Only symbolic links, regular files and directories are supported. '%s' is neither one.\n",
            filename);
    exit(1);
  }

  errno = 0;
  grp = getgrgid(statbuf.st_gid);
  if(!grp) {
    fprintf(stderr, "Could not get group name for group '%d': %s\n",
            int(statbuf.st_gid), strerror(errno));
    exit(1);
  }
  errno = 0;
  pwd = getpwuid(statbuf.st_uid);
  if(!pwd) {
    fprintf(stderr, "Could not get user name for user '%d': %s\n",
            int(statbuf.st_gid), strerror(errno));
    exit(1);
  }

  memset(&hdr, 0, BLOCKSIZE);
  if(S_ISLNK(statbuf.st_mode))
  {
    strncpy(hdr.linkname, ln, sizeof(hdr.linkname));
    hdr.linkname[statbuf.st_size] = '\0';
  }
  else
  {
    strcpy(hdr.linkname, "");
  }

  // name is set at the end due to funny handling of long file names
  snprintf(hdr.mode, sizeof(hdr.mode), "%0*o",
           (int)sizeof(hdr.mode)-1, statbuf.st_mode & MODE_MASK);
  snprintf(hdr.uid, sizeof(hdr.uid), "%0*o",
           (int)sizeof(hdr.uid)-1, xtype ? 0 : statbuf.st_uid);
  snprintf(hdr.gid, sizeof(hdr.gid), "%0*o",
           (int)sizeof(hdr.gid)-1, xtype ? 0 : statbuf.st_gid);
  // tar requires zero size for links and allows it for dirs
  off_t size = S_ISREG(statbuf.st_mode) ? statbuf.st_size : 0;
  snprintf(hdr.size, sizeof(hdr.size), "%0*lo",
           (int)sizeof(hdr.size)-1, size < MAX_FILE_SIZE ? size : 0);
  snprintf(hdr.mtime, sizeof(hdr.mtime), "%0*lo",
           (int)sizeof(hdr.mtime)-1, statbuf.st_mtime);
  memset(hdr.chksum, ' ', sizeof(hdr.chksum));
  if(xtype)
    hdr.typeflag = char(xtype);
  else if(S_ISLNK(statbuf.st_mode))
    hdr.typeflag = SYMTYPE;
  else if(S_ISDIR(statbuf.st_mode))
    hdr.typeflag = DIRTYPE;
  else if(S_ISREG(statbuf.st_mode))
    hdr.typeflag = REGTYPE;
  else
    assert(0);

  // link name already set
  strncpy(hdr.magic, TMAGIC, sizeof(hdr.magic));
  strncpy(hdr.version, TVERSION, sizeof(hdr.version));
  if(!xtype) {
    snprintf(hdr.uname, sizeof(hdr.uname), "%s", pwd->pw_name);
    snprintf(hdr.gname, sizeof(hdr.gname), "%s", grp->gr_name);
    snprintf(hdr.devmajor, sizeof(hdr.devmajor), "%0*o",
             (int)sizeof(hdr.devmajor)-1, 0);
    snprintf(hdr.devminor, sizeof(hdr.devminor), "%0*o",
             (int)sizeof(hdr.devminor)-1, 0);
  }
  strncpy(hdr.name, filename, sizeof(hdr.name));

  unsigned long int checksum = 0;
  for(size_t j = 0 ; j < sizeof(hdr) ; j++)
    checksum += ((unsigned char*)&hdr)[j];
  snprintf(hdr.chksum, sizeof(hdr.chksum), "0%-lo", checksum);
}

size_t tarentry::record_length(const char *keyword, const char *value)
{
  int oldlen = 0, newlen = strlen(keyword)+strlen(value)+5; // would be 2 digits
  int count = 0;
  do {
    oldlen = newlen;
    newlen = snprintf(NULL, 0, "%d %s=%s\n", oldlen, keyword, value);
    assert(count++ < 3);
  } while(oldlen != newlen);

  return size_t(newlen);
}
