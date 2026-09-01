#ifndef SYM_RING_H
#define SYM_RING_H

#include <stddef.h>

/* <stdatomic.h> is C11-only -- not valid to include from a C++ translation unit (app/
   main.cpp includes this header). <atomic>'s std::atomic_size_t is layout-compatible with
   C11's atomic_size_t per both standards, so this alias lets sym_ring_t's definition below
   compile unchanged in either language; only ring.c (always compiled as C) actually calls
   the C11 atomic_load_explicit/atomic_store_explicit/atomic_init functions on it. */
#ifdef __cplusplus
#include <atomic>
using atomic_size_t = std::atomic_size_t;
#else
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Lock-free single-producer/single-consumer float sample ring: the realtime audio capture
   callback (hal_audio_capture_cb, see hal/hal_audio.h) is the producer, the decode loop is
   the consumer. Caller owns the backing buffer -- core/ never calls malloc, so sizing (how
   many seconds of audio to buffer) is a host/app decision, not baked into core/. */
typedef struct {
    float* buffer;
    size_t capacity;
    atomic_size_t write_pos; /* producer-owned; monotonically increasing, indexed mod capacity */
    atomic_size_t read_pos;  /* consumer-owned; monotonically increasing, indexed mod capacity */
} sym_ring_t;

void sym_ring_init(sym_ring_t* ring, float* buffer, size_t capacity);

/* Producer only (one thread). Returns the number of samples actually written -- fewer than
   count if the ring is full. Never blocks, never allocates. */
size_t sym_ring_write(sym_ring_t* ring, const float* samples, size_t count);

/* Consumer only (one thread). Returns the number of samples actually read. */
size_t sym_ring_read(sym_ring_t* ring, float* out, size_t count);

/* Consumer-side: how many samples are currently available to read. */
size_t sym_ring_available(const sym_ring_t* ring);

#ifdef __cplusplus
}
#endif

#endif /* SYM_RING_H */
