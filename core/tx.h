#ifndef SYM_TX_H
#define SYM_TX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYM_TX_OK = 0,
    SYM_TX_ERROR_ENCODE, /* text couldn't be packed into a valid FT8 payload -- see
                             ftx_message_encode()'s own rules (bad callsign, grid, etc.) */
} sym_tx_rc_t;

/* Total samples sym_tx_synthesize() writes to out_signal at a given sample rate -- the FT8
   tone burst only (79 symbols * 0.16 s), no slot-alignment silence padding. Callers wanting
   a full 15 s slot buffer (e.g. to feed straight into core/argus.c for a round-trip test)
   pad before/after themselves; a real on-air caller keys PTT for exactly this many samples
   and no more. */
int sym_tx_signal_samples(int sample_rate_hz);

/* Encodes `text` (free-form FT8 message text, the same shape core/argus.c decodes back out
   via ftx_message_decode()) and synthesizes it as a GFSK-shaped audio signal: `freq_hz` is
   the tone-0 base frequency, matching the "AUDIO FREQ" column callers already see out of
   core/argus.c's own decode output. out_signal must have room for
   sym_tx_signal_samples(sample_rate_hz) samples; nothing is written on
   SYM_TX_ERROR_ENCODE. */
sym_tx_rc_t sym_tx_synthesize(const char* text, float freq_hz, int sample_rate_hz, float* out_signal);

#ifdef __cplusplus
}
#endif

#endif /* SYM_TX_H */
