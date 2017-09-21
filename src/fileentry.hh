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

#ifndef FILEENTRY_HH_
#define FILEENTRY_HH_

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>

#include <list>
#include <stack>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

class fileentry
{
  public:
  fileentry() {};
  virtual ~fileentry() {};
  virtual std::string nextfile() = 0;
};

class fileentries
{
  public:
  fileentries() : curr(entries.begin()) {};
  ~fileentries() {};

  void add_entry(fileentry *entry) {
    entries.push_back(entry);
    curr = entries.begin();
  }

  std::string nextfile() {
    if(curr == entries.end()) {
      return "";
    }

    std::string fn = (*curr)->nextfile();
    while(fn.empty()) {
      if(++curr == entries.end())
        break;
      fn = (*curr)->nextfile();
    };

    return fn;
  }

  private:
  std::list<fileentry*> entries;
  std::list<fileentry*>::iterator curr;
};

// a source of files representing a list of files
class filelist : public fileentry
{
  public:
  filelist(const std::string fl_) : fl(fl_), fh(NULL), done(false) {};
  virtual std::string nextfile() {
    if(done) {
      assert(fh == NULL);
      return "";
    }

    if(fh == NULL) {
      if(fl == "-") {
        fh = fopen("/dev/stdin", "r");
      } else {
        fh = fopen(fl.c_str(), "r");
      }
      if(fh == NULL) {
        fprintf(stderr, "Could not open '%s' for reading: %s\n", fl.c_str(),
                strerror(errno));
        exit(1);
      }
    }

    char buf[4096];
    char *line = fgets(buf, sizeof(buf), fh);
    if(line == NULL && !feof(fh)) {
      fprintf(stderr, "Could not read from '%s': %s", fl.c_str(),
              strerror(errno));
      exit(1);
    }

    // remove newline at end of input (if present)
    int len;;
    if(line && (len = strlen(line)) >= 1 && line[len-1] == '\n') {
      line[len-1] = '\0';
    }

    if(feof(fh)) {
      fclose(fh);
      fh = NULL;
      done = true;
    }

    return line ? line : "";
  }

  private:
  const std::string fl;
  
  FILE *fh;
  bool done;
};

// a source of files representing command line arguments
class filearg : public fileentry
{
  public:
  filearg(const std::string file_) : file(file_), done(false) {};
  virtual std::string nextfile() {
    if(done) {
      assert(dirstack.empty());
      return "";
    }

    std::string fn;
    bool is_dir = false;
    if(dirstack.empty()) {
      assert(!done);

      fn = file;
      struct stat statbuf;
      int ierr = lstat(fn.c_str(), &statbuf);
      if(ierr != 0) {
        fprintf(stderr, "Could not stat '%s': %s", fn.c_str(), strerror(errno));
        exit(1);
      }
      is_dir = S_ISDIR(statbuf.st_mode);
      done = !is_dir;
    } else {
      struct dirent *ent = NULL;
      do {
        errno = 0;
        ent = readdir(dirstack.top());
        if(ent == NULL && errno) {
          fprintf(stderr, "Could not list directory '%s': %s",
                  dirs.top().c_str(), strerror(errno));
          exit(1);
        }
        if(ent == NULL) { // done with this directory, continue with parent
          closedir(dirstack.top());
          dirs.pop();
          dirstack.pop();
        } else if(strcmp(ent->d_name, ".") == 0 ||
                  strcmp(ent->d_name, "..") == 0) { // skips "." and ".."
          ent = NULL;
        }
      } while(ent == NULL && !dirstack.empty());
      if(ent != NULL) {
        fn = dirs.top() + ent->d_name;
        is_dir = ent->d_type == DT_DIR; // Linux and BSD specific
      } else { // all done
        assert(dirstack.empty());
        fn = "";
        done = true;
      }
    }

    if(is_dir) {
      assert(!done);
      DIR *dh = opendir(fn.c_str());
      if(dh == NULL) {
          fprintf(stderr, "Could not open directory '%s': %s\n", fn.c_str(),
                  strerror(errno));
          exit(1);
      }
      dirs.push(fn+"/");
      dirstack.push(dh);
    }

    return fn;
  }

  private:
  const std::string file;

  std::stack<DIR*> dirstack;
  std::stack<std::string> dirs;
  
  bool done;
};

#endif //FILEENTRY_HH_
