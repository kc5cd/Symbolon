#include "ring.h"

void sym_ring_init(sym_ring_t* ring, float* buffer, size_t capacity)
{
    ring->buffer = buffer;
    ring->capacity = capacity;
    atomic_init(&ring->write_pos, 0);
    atomic_init(&ring->read_pos, 0);
}

size_t sym_ring_write(sym_ring_t* ring, const float* samples, size_t count)
{
    size_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_relaxed);
    size_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
    size_t used = write_pos - read_pos;
    size_t free_space = ring->capacity - used;
    size_t to_write = (count < free_space) ? count : free_space;

    for (size_t i = 0; i < to_write; ++i) {
        ring->buffer[(write_pos + i) % ring->capacity] = samples[i];
    }

    /* Release: the sample writes above must be visible to the consumer before it can observe
       this updated write_pos and read them. */
    atomic_store_explicit(&ring->write_pos, write_pos + to_write, memory_order_release);
    return to_write;
}

size_t sym_ring_read(sym_ring_t* ring, float* out, size_t count)
{
    size_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_relaxed);
    size_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
    size_t available = write_pos - read_pos;
    size_t to_read = (count < available) ? count : available;

    for (size_t i = 0; i < to_read; ++i) {
        out[i] = ring->buffer[(read_pos + i) % ring->capacity];
    }

    /* Release: so the producer's next free-space check (acquire on read_pos) sees this slot
       as free only after these reads have actually completed. */
    atomic_store_explicit(&ring->read_pos, read_pos + to_read, memory_order_release);
    return to_read;
}

size_t sym_ring_available(const sym_ring_t* ring)
{
    size_t write_pos = atomic_load_explicit(&ring->write_pos, memory_order_acquire);
    size_t read_pos = atomic_load_explicit(&ring->read_pos, memory_order_acquire);
    return write_pos - read_pos;
}
