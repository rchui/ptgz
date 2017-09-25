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

// a very simple proof-of-concept MPI parallelized tar file writer
// The algorithm is fairly straigtforward.
// * the master reads filenames one at a time from stdin then obtains the file
//   size for each file and computes the offset that this file should appear in
//   the tar file based on the previous files
// * it assigns the file to one of the workers who read the full file and
//   places it at the given offset
// * the master bunches up files in lots of 100 or 1e6 bytes of data (whichever
//   is reached first) in the jobs
// * op to 3 jobs are send to a given client at once, clients ack jobs when
//   they are done with it and this triggers a new job to be send to them
// * currently it supports regular files, directories and symbolic links and
//   the output is identical to a regular tar as long as the same file list is
//   passed to tar's -T option

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include<mpi.h>

#include<vector>
#include<queue>
#include<string>

//#define DO_TIMING
#include "cmdline.hh"
#include "fileentry.hh"
#include "timer.hh"
#include "tarentry.hh"

#define MAX_JOBS_IN_FLIGHT 3
#define MAX_FILES_IN_JOB 100
#define TARGET_JOB_SIZE (1024ul*1024ul*1024ul)
#define COPY_BLOCK_SIZE (1024*1024*512)
#define STREAM_BUFFER_SIZE (COPY_BLOCK_SIZE)


std::vector<timer*> timer::all_timers;
timer timer_all("all");
timer timer_stat("stat"), timer_open("open");
timer timer_read("read"), timer_write("write"), timer_seek("seek");
timer timer_worker_wait("worker_wait"), timer_master_wait("master_wait");

#define DIM(v) (sizeof(v)/sizeof(v[0]))

static void copy_file_content(FILE *out_fh, const char *out_fn,
                              const tarentry &ent);

static int find_unused_request(int count, MPI_Request *request);
size_t show_progress(size_t total, size_t chunksize, int show_percent);

void master(const char *out_fn, fileentries& entries);
void worker(const char *out_fn);

int mpitar(int argc, char **argv)
{
  int rc = -1;
  timer_all.start(__LINE__);

  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  cmdline args(argc, argv, rank!=0);
  const char action = args.get_action();
  switch(action) {
    case cmdline::ACTION_ERROR:
      rc = 1;
      break;
    case cmdline::ACTION_EXIT:
      rc = 0;
      break;
    case cmdline::ACTION_CREATE:
      if(size < 2) {
        if(rank == 0) {
          fprintf(stderr, "Needs at least 2 mpi ranks.\n");
        }
        rc = 1;
      } else {
        if(rank) {
          worker(args.get_tarfilename().c_str());
        } else {
          master(args.get_tarfilename().c_str(), args.get_fileentries());
        }
        rc = 0;
      }
      break;
    default:
      fprintf(stderr, "Unknown action '%c'\n", action);
      rc = 1;
      break;
  }

  timer_all.stop(__LINE__);

  if(rc == 0)
    timer::print_timers();

  MPI_Barrier(MPI_COMM_WORLD);
  return rc;
}

void master(const char *out_fn, fileentries& entries)
{
  timer_open.start(__LINE__);
  // I use this open/fdopen combo to be able to use fread but also have a
  // WRONLY which avoid fseek() pre-reading into the buffer
  int out_fd = open(out_fn, O_WRONLY | O_TRUNC | O_CREAT, 0666);
  if(out_fd == -1) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  FILE *out_fh = fdopen(out_fd, "wb");
  if(out_fh == NULL) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  timer_open.stop(__LINE__);
  // I need this barrier so that all ranks wait until the last one has opened
  // and truncated the file
  // I cannot use r+ since this seems to make fseek in in data into the buffer
  MPI_Barrier(MPI_COMM_WORLD);

  char idx_fn[1024];
  size_t idx_fn_size = snprintf(idx_fn, sizeof(idx_fn), "%s.idx", out_fn);
  assert(idx_fn_size < sizeof(idx_fn));
  timer_open.start(__LINE__);
  FILE *idx_fh = fopen(idx_fn, "w");
  if(idx_fh == NULL) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", idx_fn,
            strerror(errno));
    exit(1);
  }
  timer_open.stop(__LINE__);

  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  std::vector<int> jobs_in_flight((size_t)size);
  std::vector<MPI_Request> recv_requests(MAX_JOBS_IN_FLIGHT*size_t(size), MPI_REQUEST_NULL);
  std::vector<int> recv_completed(recv_requests.size());
  std::vector<unsigned long long int> recv_buffers(recv_requests.size());
  std::vector<MPI_Request> send_requests(recv_requests.size(), MPI_REQUEST_NULL);
  std::vector<std::string> send_buffers(recv_requests.size());

  size_t off = 0;
  int done = 0;
  do {
    for(int current_worker = 1 ; current_worker < size && !done ; current_worker++) {
      /* process some MPI requests every once in a while to keep things going */

      /* kick the send requests every once in a while in case MPI does not
       * process them otherwise */
      int dummy1, dummy2;
      timer_master_wait.start(__LINE__);
      MPI_Testany((int)send_requests.size(), &send_requests[0], &dummy1, &dummy2,
                  MPI_STATUSES_IGNORE);

      /* check for ack's of workers */
      int count;
      std::vector<MPI_Status> status(recv_requests.size());
      MPI_Testsome((int)recv_requests.size(), &recv_requests[0], &count,
                   &recv_completed[0], &status[0]);
      timer_master_wait.stop(__LINE__);
      for(int r = 0 ; count != MPI_UNDEFINED && r < count ; r++) {
        int idx = recv_completed[r];
        show_progress(off, size_t(recv_buffers[idx]), 0);
        int w = status[r].MPI_SOURCE;
        jobs_in_flight[w] -= 1;
      }

      /* if the curent worker is underworked, give it something to do */
      if(jobs_in_flight[current_worker] < MAX_JOBS_IN_FLIGHT) {
        const int buf0num = current_worker*MAX_JOBS_IN_FLIGHT;
        int recv_id = find_unused_request(MAX_JOBS_IN_FLIGHT, &recv_requests[buf0num]);
        assert(recv_id >= 0);
        int send_id = find_unused_request(MAX_JOBS_IN_FLIGHT,
                                          &send_requests[buf0num]);
        assert(send_id >= 0);
        send_buffers[buf0num+send_id].clear();
        assert(send_buffers[buf0num+send_id].size() == 0);
        /* make a job package for a worker containing up to MAX_FILES_IN_JOB
         * files and aiming to be at least TARGET_JOB_SIZE bytes worth of files
         * */
        for(size_t n = 0, job_sz = 0 ;
            n < MAX_FILES_IN_JOB &&
              job_sz < TARGET_JOB_SIZE &&
              !done ;
            n++) {
          const std::string fn(entries.nextfile());
          if(fn.empty()) {
            done = 1;
            break;
          }
          timer_stat.start(__LINE__);
          tarentry ent(fn, off);
          timer_stat.stop(__LINE__);
          const size_t sz = ent.size();
          job_sz += sz;
          //printf("%s (%zu bytes)\n", fn, sz);
          /* enough room for
           * filename, \0, decimal file size, \0, decimal offset into output, \0
           * using 20 decimal digits is enough for 64 bit numbers */
          send_buffers.at(buf0num+send_id) += ent.serialize();
          timer_write.start(__LINE__);
          fprintf(idx_fh, "%zu %s\n", off, fn.c_str());
          timer_write.stop(__LINE__);
          off += sz;
        }
        if(!send_buffers[buf0num+send_id].empty()) {
          /* prepare for "done" message from worker */
          timer_master_wait.start(__LINE__);
          MPI_Irecv(&recv_buffers[buf0num+recv_id], 1, MPI_UNSIGNED_LONG_LONG, current_worker,
                    send_id, MPI_COMM_WORLD, &recv_requests[buf0num+recv_id]);
          MPI_Isend(send_buffers.at(buf0num+send_id).data(),
                    (int)send_buffers.at(buf0num+send_id).size(), MPI_BYTE,
                    current_worker, send_id, MPI_COMM_WORLD,
                    &send_requests[buf0num+send_id]);
          timer_master_wait.stop(__LINE__);
          jobs_in_flight[current_worker] += 1;
        }
      }
    }
  } while(!done);
  printf("\rAll done in master, waiting for workers\n");

  /* done, wait for everything to settle down */
  timer_master_wait.start(__LINE__);
  MPI_Waitall((int)send_requests.size(), &send_requests[0], MPI_STATUSES_IGNORE);
  MPI_Waitall((int)recv_requests.size(), &recv_requests[0], MPI_STATUSES_IGNORE);

  printf("All communication finished in master\n");
  timer_master_wait.stop(__LINE__);

  /* tell all workers to quit */
  for(int current_worker = 1 ; current_worker < size ; current_worker++) {
    /* the empty file name is magic and tells the worker to quit */
    std::string terminate = tarentry().serialize();
    timer_master_wait.start(__LINE__);
    MPI_Send(terminate.c_str(), (int)terminate.size(), MPI_BYTE, current_worker,
             0, MPI_COMM_WORLD);
    timer_master_wait.stop(__LINE__);
  }

  printf("Closing file.\n");

  timer_master_wait.start(__LINE__);
  MPI_Barrier(MPI_COMM_WORLD);
  timer_master_wait.stop(__LINE__);
  size_t current = show_progress(off, size_t(0), 0);
  show_progress(off, off-current, 0); /* show 100% written */
  printf("\n");

  /* add index file to tar */
  timer_write.start(__LINE__);
  fprintf(idx_fh, "%zu %s\n", off, idx_fn);
  int ierr_fclose = fclose(idx_fh);
  if(ierr_fclose != 0) {
    fprintf(stderr, "Could not write to '%s': %s\n", idx_fn,
            strerror(errno));
    exit(1);
  }
  timer_write.stop(__LINE__);
  timer_stat.start(__LINE__);
  tarentry idx_ent(idx_fn, off);
  timer_stat.stop(__LINE__);
  copy_file_content(out_fh, out_fn, idx_ent);
  off += idx_ent.size();

  /* terminate tar file */
  static char buffer[2*BLOCKSIZE];
  off_t ierr_seek = fseek(out_fh, (long)off, SEEK_SET);
  if(ierr_seek == -1) {
    fprintf(stderr, "Could not seek '%s' to %zu: %s\n", out_fn,
            size_t(off), strerror(errno));
    exit(1);
  }
  timer_write.start(__LINE__);
  size_t written = fwrite(buffer, 1, 2*BLOCKSIZE, out_fh);
  timer_write.stop(__LINE__);
  if(written != 2*BLOCKSIZE) {
    fprintf(stderr, "Could not write %zu bytes to '%s': %s\n",
            size_t(2*BLOCKSIZE), out_fn, strerror(errno));
    exit(1);
  }
  timer_write.start(__LINE__);
  int ierr_close = fclose(out_fh);
  timer_write.stop(__LINE__);
  if(ierr_close != 0) {
    fprintf(stderr, "Could not write to '%s': %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  printf("Done.\n");

  timer_master_wait.start(__LINE__);
  MPI_Barrier(MPI_COMM_WORLD);
  timer_master_wait.stop(__LINE__);
}

/* make this a non-local type to make the compiler happy */
struct filedesc_t  {
  tarentry ent;
  int ask_for_work;
  int tag;
  filedesc_t(tarentry ent_, int ask_for_work_, int tag_) :
    ent(ent_), ask_for_work(ask_for_work_), tag(tag_) {};
};
void worker(const char *out_fn)
{
  std::queue<filedesc_t> files;
  int file_count = 0;

  timer_open.start(__LINE__);
  // I need this barrier so that all ranks wait until the last one has opened
  // and truncated the file
  // I cannot use r+ since this seems to make fseek in in data into the buffer
  // this barrier is after/before the open call in master/worker
  MPI_Barrier(MPI_COMM_WORLD);
  // I use this open/fdopen combo to be able to use fread but also have a
  // WRONLY which avoid fseek() pre-reading into the buffer
  int out_fd = open(out_fn, O_WRONLY, 0666);
  if(out_fd == -1) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  FILE *out_fh = fdopen(out_fd, "wb");
  if(out_fh == NULL) {
    fprintf(stderr, "Could not open '%s' for writing: %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  timer_open.stop(__LINE__);
  static char buffer[STREAM_BUFFER_SIZE];
  setbuffer(out_fh, buffer, sizeof(buffer));

  int done = 0;
  do {
    MPI_Status status;
    int count, flag;
    if(files.empty()) {
      timer_worker_wait.start(__LINE__);
      MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
      timer_worker_wait.stop(__LINE__);
      flag = 1;
    } else {
      timer_worker_wait.start(__LINE__);
      MPI_Iprobe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
      timer_worker_wait.stop(__LINE__);
    }
    if(flag) {
      timer_worker_wait.start(__LINE__);
      MPI_Get_count(&status, MPI_BYTE, &count);
      std::vector<char> recv_buffer(count);
      int tag = status.MPI_TAG;
      MPI_Recv(&recv_buffer[0], count, MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD,
               MPI_STATUS_IGNORE);
      timer_worker_wait.stop(__LINE__);
      int first = 1;
      for(char *p = &recv_buffer[0], *s = p ; p - s < (ptrdiff_t)recv_buffer.size() ; ) {
        tarentry ent;
        p += ent.deserialize(p);
        /* magic empty file name for end of work? */
        if(ent.get_filename().empty()) {
          done = 1;
          break;
        }
        files.push(filedesc_t(ent, first, tag));
        first = 0;
      }
    }

    /* only do one file, then look for more work from master, this assumes that
     * MPI is much faster than IO */
    if(!files.empty()) {
      assert(sizeof(size_t) <= sizeof(unsigned long long int));
      static unsigned long long int chunk_written = 0;
      const filedesc_t& file = files.front();
      copy_file_content(out_fh, out_fn, file.ent);
      file_count += 1;
      chunk_written += static_cast<unsigned long long int>(file.ent.size());
      if(file.ask_for_work) {
        timer_worker_wait.start(__LINE__);
        MPI_Send(&chunk_written, 1, MPI_UNSIGNED_LONG_LONG, 0, file.tag, MPI_COMM_WORLD);
        timer_worker_wait.stop(__LINE__);
        chunk_written = 0;
      }
      files.pop();
    }

  } while(!done || !files.empty());

  /* this will usually induce a delay while caches are flushed */
  timer_write.start(__LINE__);
  int ierr_close = fclose(out_fh);
  timer_write.stop(__LINE__);
  if(ierr_close != 0) {
    fprintf(stderr, "Could not write to '%s': %s\n", out_fn,
            strerror(errno));
    exit(1);
  }
  // the barrier before master reports 100% done
  timer_worker_wait.start(__LINE__);
  MPI_Barrier(MPI_COMM_WORLD);
  timer_worker_wait.stop(__LINE__);
  // the barrier at the end of master
  timer_worker_wait.start(__LINE__);
  MPI_Barrier(MPI_COMM_WORLD);
  timer_worker_wait.stop(__LINE__);
}

static int find_unused_request(int count, MPI_Request *request)
{
  int flag, idx;
  
  for(int r = 0 ; r < count ; r++) {
    if(request[r] == MPI_REQUEST_NULL) {
      return r;
    }
  }

  timer_master_wait.start(__LINE__);
  MPI_Testany(count, request, &idx, &flag, MPI_STATUSES_IGNORE);
  timer_master_wait.stop(__LINE__);
  return flag ? idx : -1;
}

static void copy_file_content(FILE *out_fh, const char *out_fn,
                              const tarentry &ent)
{
  const size_t off = ent.get_offset();
  const char *in_fn = ent.get_filename().c_str();
  const std::vector<char> hdr(ent.make_tar_header());

  // seek only when required to avoid flushes
  // ftell however seems to call fflush() so we keep track of the file pointer
  // ourselves
  static size_t file_off = 0;
  timer_seek.start(__LINE__);
  if(file_off != off) {
    int ierr_seek = fseek(out_fh, (long)off, SEEK_SET);
    if(ierr_seek == -1) {
      fprintf(stderr, "Could not seek '%s' to %zu: %s\n", out_fn,
              size_t(off), strerror(errno));
      exit(1);
    }
    file_off = off;
  }
  timer_seek.stop(__LINE__);

  timer_write.start(__LINE__);
  size_t written = fwrite(hdr.data(), 1, hdr.size(), out_fh);
  timer_write.stop(__LINE__);
  if(written != hdr.size()) {
    fprintf(stderr, "Could not write %zu bytes to '%s': %s\n", hdr.size(),
            out_fn, strerror(errno));
    exit(1);
  }
  file_off += hdr.size();
  if(!ent.is_reg())
    return;

  timer_open.start(__LINE__);
  int in_fd = open(in_fn, O_RDONLY);
  timer_open.stop(__LINE__);
  if(in_fd < 0) {
    fprintf(stderr, "Could not open '%s' for reading: %s\n", in_fn,
            strerror(errno));
    exit(1);
  }
  off_t size = (off_t)ent.get_filesize();
  off_t offset = 0;
  while(offset < size) {
    static char fbuf[COPY_BLOCK_SIZE]; /* TODO: find an optimal number */
    timer_read.start(__LINE__);
    ssize_t read_sz = read(in_fd, fbuf, size_t(size-offset) > sizeof(fbuf) ? sizeof(fbuf) : (size-offset));
    timer_read.stop(__LINE__);
    if(read_sz == -1) {
      fprintf(stderr, "Could not read from '%s': %s\n", in_fn, strerror(errno));
      exit(1);
    }
    offset += read_sz;
    timer_write.start(__LINE__);
    size_t write_sz = fwrite(fbuf, 1, read_sz, out_fh);
    timer_write.stop(__LINE__);
    if(write_sz != size_t(read_sz)) {
      fprintf(stderr, "Could not write %zu bytes to '%s': %s\n",
              size_t(read_sz), out_fn, strerror(errno));
      exit(1);
    }
  }
  assert(offset == size);
  file_off += size;

  static char block[BLOCKSIZE]; /* bunch of zeros for padding to block size */
  if(size % BLOCKSIZE) {
    const size_t padsize = BLOCKSIZE - (size % BLOCKSIZE);
    timer_write.start(__LINE__);
    const size_t ierr_write = fwrite(block, 1, padsize, out_fh);
    timer_write.stop(__LINE__);
    if(ierr_write != padsize) {
      fprintf(stderr, "Could not write %zu bytes to '%s': %s\n", padsize,
              out_fn, strerror(errno));
      exit(1);
    }
    file_off += padsize;
  }
  timer_open.start(__LINE__);
  int ierr_close = close(in_fd);
  assert(ierr_close == 0);
  timer_open.stop(__LINE__);

}

size_t show_progress(size_t total, size_t chunksize, int show_percent)
{
  static size_t written = 0;

  written += chunksize;
  static const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
  int unit = int(written > 0 ? log2(written)/10 : 0);
  if(unit >= int(DIM(units)))
    unit = DIM(units) - 1;
  printf("\r% 6.1f %s done", written/pow(2.,10*unit), units[unit]);
  if(show_percent)
    printf(" (% 4.1f%%)", 100.*((double)written)/total);
  fflush(stdout);

  return written;
}
