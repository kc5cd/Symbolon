#include "tx_playback.h"

void TxPlayback::arm(const float* samples, size_t count)
{
    buffer_.assign(samples, samples + count);
    played_frames_.store(0, std::memory_order_relaxed);
    total_frames_.store(count, std::memory_order_release); // publishes buffer_ + the reset above
}

uint32_t TxPlayback::drain(float* out, uint32_t frame_count)
{
    size_t total = total_frames_.load(std::memory_order_acquire);
    size_t played = played_frames_.load(std::memory_order_relaxed);

    uint32_t i = 0;
    for (; i < frame_count && played < total; ++i, ++played) {
        out[i] = buffer_[played];
    }
    for (; i < frame_count; ++i) {
        out[i] = 0.0f;
    }

    played_frames_.store(played, std::memory_order_release);
    return frame_count;
}

bool TxPlayback::is_done() const
{
    return played_frames_.load(std::memory_order_acquire) >= total_frames_.load(std::memory_order_acquire);
}

uint32_t tx_playback_callback(float* out, uint32_t frame_count, void* user)
{
    return static_cast<TxPlayback*>(user)->drain(out, frame_count);
}
