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

#ifndef TIMER_HH_
#define TIMER_HH_

#include <mpi.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>

#ifdef DO_TIMING
class timer
{
  public:
  timer(std::string const name_) : acc(0), start_time(-1), name(name_) {
    all_timers.push_back(this);
  };
  ~timer() {
    std::vector<timer*>::iterator it =
      std::find(all_timers.begin(), all_timers.end(), this);
    all_timers.erase(it);
  };

  void start(int line) {
    if(start_time != -1) {
      fprintf(stderr, "Incorrect nesting for %s at line %d\n", name.c_str(), line);
      assert(0);
    }
    start_time = MPI_Wtime();
  }
  void stop(const int line) {
    if(start_time == -1) {
      fprintf(stderr, "Incorrect nesting for %s at line %d\n", name.c_str(), line);
      assert(0);
    }
    acc += MPI_Wtime() - start_time;
    start_time = -1;
  }
  static void print_timers() {
    std::vector<double> timers(all_timers.size());
    for(size_t i = 0 ; i < all_timers.size() ; ++i) {
      timers[i] = all_timers[i]->get(__LINE__);
    }
    std::vector<double> sum_timers(all_timers.size());
    MPI_Reduce(&timers[0], &sum_timers[0], int(timers.size()), MPI_DOUBLE,
               MPI_SUM, 0, MPI_COMM_WORLD);
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if(rank == 0) {
      for(size_t i = 0 ; i < all_timers.size() ; ++i) {
        fprintf(stdout, "%s%s: %g", i>0?" ":"", all_timers[i]->name.c_str(), sum_timers[i]);
      }
      fprintf(stdout, "\n");
    }
  }

  private:
  static std::vector<timer*> all_timers;

  double acc, start_time;
  const std::string name;

  double get(const int line) {
    if(start_time != -1) {
      fprintf(stderr, "Incorrect nesting for %s at line %d\n", name.c_str(), line);
      assert(0);
    }
    return acc;
  }

};
#else // dummy class
class timer
{
  public:
  timer(std::string const) {};
  ~timer() {};
  void start(int) {};
  void stop(const int) {};
  static void print_timers() {};
  private:
  static std::vector<timer*> all_timers;
};
#endif

#endif // TIMER_HH_
