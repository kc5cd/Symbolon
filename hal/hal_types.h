#ifndef HAL_TYPES_H
#define HAL_TYPES_H

#include "../core/sym_types.h"

/* HAL functions reuse core's result codes so callers don't juggle two rc enums across the
   core/hal boundary. */
typedef sym_rc_t hal_rc_t;

#define HAL_RC_OK           SYM_RC_OK
#define HAL_RC_ERROR        SYM_RC_ERROR
#define HAL_RC_INVALID_ARG  SYM_RC_INVALID_ARG
#define HAL_RC_UNSUPPORTED  SYM_RC_UNSUPPORTED
#define HAL_RC_TIMEOUT      SYM_RC_TIMEOUT
#define HAL_RC_DENIED       SYM_RC_DENIED

#endif /* HAL_TYPES_H */
