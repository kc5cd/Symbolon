#ifndef ARGUS_H
#define ARGUS_H

/* Argus: the hundred-eyed giant of Greek myth, ever-watchful, never fully asleep -- matches
   this module's job of continuous spectral watch across the whole slot. */

#include <ft8/decode.h>
#include <ft8/message.h>
#include <fft/kiss_fftr.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float f_min_hz;
    float f_max_hz;
    int sample_rate_hz;
    int time_osr; /* number of time subdivisions per symbol -- see the kickoff's waterfall-params note */
    int freq_osr; /* number of frequency subdivisions per symbol */
} argus_config_t;

typedef struct {
    char text[FTX_MAX_MESSAGE_LENGTH];
    float freq_hz;
    float time_s;
    int score;
} argus_decode_t;

typedef struct {
    argus_config_t config;
    float symbol_period_s;
    int min_bin;
    int block_size;    /* samples per FT8 symbol at this sample rate -- see argus_block_size() */
    int subblock_size; /* STFT hop size = block_size / time_osr */
    int nfft;           /* STFT window size = block_size * freq_osr */
    float fft_norm;
    float* window;     /* nfft samples, owned */
    float* last_frame; /* nfft samples, owned; sliding STFT analysis buffer */
    void* fft_work;
    kiss_fftr_cfg fft_cfg;
    ftx_waterfall_t wf; /* wf.mag owned */
} argus_t;

/* Reimplements ft8_lib's own common/monitor.c STFT algorithm rather than linking it -- see
   .claude/state/context.md for why (monitor.c unconditionally enables real fprintf-based
   logging, which core/ must never call). Same math, first-party code. Allocates its working
   buffers with malloc/free, same as monitor_init/monitor_free do -- unlike core/ring.c,
   which is deliberately caller-buffer-owned; malloc isn't in the kickoff's "no OS calls"
   list (file I/O, sockets, threads, clock reads, printf), and the eventual ARMv7 target is a
   full Linux/glibc system with a real heap, not a bare-metal target. */
void argus_init(argus_t* argus, const argus_config_t* config);
void argus_free(argus_t* argus);

/* Call at the start of each new 15 s slot -- resets accumulated waterfall data, not the
   FFT/window setup (that's fixed for the lifetime of the argus_t). */
void argus_reset(argus_t* argus);

int argus_block_size(const argus_t* argus); /* samples argus_process_block() expects */

/* samples must be argus_block_size() long. A no-op once the slot's max_blocks capacity is
   already full (matches monitor_process()'s own bounds behavior). */
void argus_process_block(argus_t* argus, const float* samples);

/* Runs candidate search + decode over everything accumulated since the last argus_reset().
   Returns the number of decodes written to out (up to max_out). Does not deduplicate --
   the same message can appear more than once if found via multiple candidates; callers that
   care about a unique set (see tests/corpus/test_corpus.cpp) dedupe on the text field
   themselves. Phase 1 scope note: unlike ft8_lib's own demo, this does not maintain a
   persistent callsign hash table across argus_t instances -- see argus.c's static hash
   table, sized for a single slot's worth of hashed-callsign references. */
int argus_decode_slot(argus_t* argus, argus_decode_t* out, int max_out);

#ifdef __cplusplus
}
#endif

#endif /* ARGUS_H */
