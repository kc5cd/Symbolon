/* Horae -- see horae.h for the myth note. */
#include "horae.h"

horae_slot_t horae_slot_at(uint64_t utc_us)
{
    horae_slot_t slot;
    slot.slot_epoch_us = (utc_us / HORAE_SLOT_US) * HORAE_SLOT_US;
    slot.offset_us = utc_us - slot.slot_epoch_us;
    return slot;
}
