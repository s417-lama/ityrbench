/*
 *         ---- The Unbalanced Tree Search (UTS) Benchmark ----
 *  
 *  Copyright (c) 2010 See AUTHORS file for copyright holders
 *
 *  This file is part of the unbalanced tree search benchmark.  This
 *  project is licensed under the MIT Open Source license.  See the LICENSE
 *  file for copyright and licensing information.
 *
 *  UTS is a collaborative project between researchers at the University of
 *  Maryland, the University of North Carolina at Chapel Hill, and the Ohio
 *  State University.  See AUTHORS file for more information.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ityr/ityr.hpp"

using my_ityr = ityr::ityr_if<ityr::ityr_policy>;
template <typename T>
using global_ptr = my_ityr::iro::global_ptr<T>;

#include "uts.h"

#ifndef UTS_RUN_SEQ
#define UTS_RUN_SEQ 0
#endif

#ifndef UTS_RECURSIVE_FOR
#define UTS_RECURSIVE_FOR 0
#endif

/***********************************************************
 *  UTS Implementation Hooks                               *
 ***********************************************************/

// The name of this implementation
const char * impl_getName(void) {
  return "Itoyori Parallel Search";
}

int impl_paramsToStr(char *strBuf, int ind) {
  ind += sprintf(strBuf + ind, "Execution strategy:  %s\n", impl_getName());
  return ind;
}

// Not using UTS command line params, return non-success
int impl_parseParam(char *param, char *value) {
  return 1;
}

void impl_helpMessage(void) {
  printf("   none.\n");
}

void impl_abort(int err) {
  exit(err);
}

/***********************************************************
 * Recursive depth-first implementation                    *
 ***********************************************************/

typedef struct {
  counter_t maxdepth, size, leaves;
} Result;

Result mergeResult(Result r0, Result r1) {
  Result r = {
    (r0.maxdepth > r1.maxdepth) ? r0.maxdepth : r1.maxdepth,
    r0.size + r1.size,
    r0.leaves + r1.leaves
  };
  return r;
}

Node makeChild(const Node *parent, int childType, int computeGranuarity, counter_t idx) {
  int j;

  Node c = { childType, (int)parent->height + 1, -1, {{0}} };

  for (j = 0; j < computeGranularity; j++) {
    rng_spawn(parent->state.state, c.state.state, (int)idx);
  }

  return c;
}

struct dynamic_node {
  int n_children;
  global_ptr<dynamic_node> children[1];
};

global_ptr<dynamic_node> new_dynamic_node(int n_children) {
  std::size_t size = sizeof(dynamic_node) + (n_children - 1) * sizeof(global_ptr<dynamic_node>);
  auto gptr = static_cast<global_ptr<dynamic_node>>(
      my_ityr::iro::malloc_local<std::byte>(size));
  gptr->*(&dynamic_node::n_children) = n_children;
  return gptr;
}

global_ptr<dynamic_node> build_tree(Node parent) {
  counter_t numChildren = uts_numChildren(&parent);
  int childType = uts_childType(&parent);

  global_ptr<dynamic_node> this_node = new_dynamic_node(numChildren);
  global_ptr<global_ptr<dynamic_node>> children = &(this_node->*(&dynamic_node::children));

  if (numChildren > 0) {
    my_ityr::parallel_transform(
        ityr::count_iterator<counter_t>(0),
        ityr::count_iterator<counter_t>(numChildren),
        children,
        [=](counter_t i) {
          Node child = makeChild(&parent, childType,
                                 computeGranularity, i);
          return build_tree(child);
        });
  }

  return this_node;
}

Result traverse_tree(counter_t depth, global_ptr<dynamic_node> this_node) {
  counter_t numChildren = this_node->*(&dynamic_node::n_children);
  global_ptr<global_ptr<dynamic_node>> children = &(this_node->*(&dynamic_node::children));

  if (numChildren == 0) {
    return { depth, 1, 1 };
  } else {
    Result init { 0, 0, 0 };
    Result result = my_ityr::parallel_reduce(
        children,
        children + numChildren,
        init,
        mergeResult,
        [=](global_ptr<dynamic_node> child_node) {
          return traverse_tree(depth + 1, child_node);
        });
    result.size += 1;
    return result;
  }
}

void destroy_tree(global_ptr<dynamic_node> this_node) {
  counter_t numChildren = this_node->*(&dynamic_node::n_children);
  global_ptr<global_ptr<dynamic_node>> children = &(this_node->*(&dynamic_node::children));

  if (numChildren > 0) {
    my_ityr::parallel_for<my_ityr::iro::access_mode::read>(
        children,
        children + numChildren,
        [=](global_ptr<dynamic_node> child_node) {
          destroy_tree(child_node);
        });
  }

  my_ityr::iro::free(this_node);
}

//-- main ---------------------------------------------------------------------

Result uts_run(uint64_t *walltime) {
  Result r;
  int my_rank = my_ityr::rank();

  if (my_rank == 0) {
    Node root;
    uts_initRoot(&root, type);

    global_ptr<dynamic_node> root_node = my_ityr::root_spawn([=]() { return build_tree(root); });

    uint64_t t1 = uts_wctime();

    r = my_ityr::root_spawn([=]() { return traverse_tree(0, root_node); });

    uint64_t t2 = uts_wctime();
    *walltime = t2 - t1;

    my_ityr::root_spawn([=]() { return destroy_tree(root_node); });
  }

  my_ityr::barrier();

  return r;
}

void real_main(int argc, char *argv[]) {
  counter_t nNodes = 0;
  counter_t nLeaves = 0;
  counter_t maxTreeDepth = 0;
  uint64_t  walltime = 0;

  uts_parseParams(argc, argv);

  int my_rank = my_ityr::rank();
  int n_ranks = my_ityr::n_ranks();

  if (my_rank == 0) {
    setlocale(LC_NUMERIC, "en_US.UTF-8");
    printf("=============================================================\n"
           "[UTS++]\n"
           "# of processes:                %d\n"
           "# of repeats:                  %d\n"
           "PCAS cache size:               %ld MB\n"
           "-------------------------------------------------------------\n",
           n_ranks, numRepeats, cache_size);

    if (type == GEO) {
      printf("t (Tree type):                 Geometric (%d)\n"
             "r (Seed):                      %d\n"
             "b (Branching factor):          %f\n"
             "a (Shape function):            %d\n"
             "d (Depth):                     %d\n"
             "-------------------------------------------------------------\n",
             type, rootId, b_0, shape_fn, gen_mx);
    } else if (type == BIN) {
      printf("t (Tree type):                 Binomial (%d)\n"
             "r (Seed):                      %d\n"
             "b (# of children at root):     %f\n"
             "m (# of children at non-root): %d\n"
             "q (Prob for having children):  %f\n"
             "-------------------------------------------------------------\n",
             type, rootId, b_0, nonLeafBF, nonLeafProb);
    } else {
      assert(0); // TODO:
    }
    printf("uth options:\n");
    madm::uth::print_options(stdout);
    printf("=============================================================\n\n");
    printf("PID of the main worker: %d\n", getpid());
    fflush(stdout);
  }

  my_ityr::iro::init(cache_size * 1024 * 1024);

  for (int i = 0; i < numRepeats; i++) {
    Result r = uts_run(&walltime);

    if (my_rank == 0) {
      maxTreeDepth = r.maxdepth;
      nNodes = r.size;
      nLeaves = r.leaves;

      double perf = (double)nNodes / walltime;

      printf("[%d] %ld ns %.6g Gnodes/s ( nodes: %llu depth: %llu leaves: %llu )\n",
             i, walltime, perf, nNodes, maxTreeDepth, nLeaves);
      fflush(stdout);
    }
  }

  my_ityr::iro::fini();
}

int main(int argc, char **argv) {
  my_ityr::main(real_main, argc, argv);
  return 0;
}
