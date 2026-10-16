#ifndef ECO_FFT_CLIENT_RUNTIME_H
#define ECO_FFT_CLIENT_RUNTIME_H
#include "IEcoInterfaceBus1.h"

/* Linux client glue for the SDK's current-directory CID lookup. This does not
 * load a library or call a factory. NULL directory selects the executable's
 * directory. The caller's working directory is restored before returning. */
int16_t client_query_component(IEcoInterfaceBus1* bus, const UGUID* cid,
                               const char* directory, const UGUID* iid, void** out);
#endif
