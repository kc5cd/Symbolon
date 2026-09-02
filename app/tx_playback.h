#ifndef SYMBOLON_TX_PLAYBACK_H
#define SYMBOLON_TX_PLAYBACK_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// Single-shot producer/consumer buffer feeding a synthesized TX signal (core/tx.c) to
// miniaudio's realtime playback callback (hal/hal_audio.h's hal_audio_playback_cb). Unlike
// core/ring.c's circular SPSC ring (built for an unbounded capture stream), armed/beacon
// mode only ever has one transmission in flight at a time, known in full up front -- arm()
// hands over the whole buffer once, drain() (called from the realtime audio thread) consumes
// it a callback-block at a time and zero-fills once exhausted.
//
// arm() must only be called by the main/decode thread while the previous transmission has
// already finished draining (is_done() == true) -- concurrent arm()/drain() on two different
// buffers is not supported, and armed/beacon mode's own loop never queues a second
// transmission while one is still in flight.
class TxPlayback {
public:
    void arm(const float* samples, size_t count);

    // Realtime audio thread only. Always fills exactly frame_count samples (copies from the
    // armed buffer, zero-fills the rest once exhausted) -- matches hal_audio_playback_cb's
    // contract of returning the frames actually produced, which for this "always fill"
    // policy is always frame_count.
    uint32_t drain(float* out, uint32_t frame_count);

    // Main/decode thread only. True once every armed sample has been drained (or nothing has
    // been armed yet).
    bool is_done() const;

private:
    std::vector<float> buffer_;
    std::atomic<size_t> total_frames_{ 0 };
    std::atomic<size_t> played_frames_{ 0 };
};

// hal_audio_playback_cb-shaped trampoline: user must point at a TxPlayback.
uint32_t tx_playback_callback(float* out, uint32_t frame_count, void* user);

#endif // SYMBOLON_TX_PLAYBACK_H
