#ifndef ENGINE_SOURCE_BASE_PARALLELFOR_H
#define ENGINE_SOURCE_BASE_PARALLELFOR_H

// MODIFIED: performance, multithreaded scene. A fixed pool of worker threads that splits a frame's independent work
// (model animation; culling tests later) across cores. The 2008 client runs the scene on one thread; its only helper
// is CM2Cache's "Model2" thread, which animates every other model. The calling thread takes part in each job, so a pool
// of N workers runs a job on N + 1 threads. Each worker runs with the caller's Prop context and SSE control word.
//
// Jobs come from one thread at a time. A job started while another is running, or from inside a job, runs serially on
// the calling thread, so nesting is safe.

#include <cstdint>

// func handles indices [begin, end); it may run on any thread, in any order, many times per job
typedef void (*ParallelForFunc)(void* context, uint32_t begin, uint32_t end);

// threads a job may use, the caller included: 0 = one per logical processor (at most 16), 1 = no workers (serial).
// Takes effect at the next job; workers are created on first use and kept
void ParallelSetThreads(uint32_t threads);

// threads the next job would run on, the caller included (1 when there is no pool)
uint32_t ParallelThreadCount();

// runs func over [0, count) in pieces of at least grain indices (0 = choose one), returns when all of it is done
void ParallelFor(uint32_t count, uint32_t grain, ParallelForFunc func, void* context);

#endif
