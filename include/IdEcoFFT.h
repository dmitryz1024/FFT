#ifndef ECO_FFT_COMPONENT_IDS_H
#define ECO_FFT_COMPONENT_IDS_H

#include "IEcoDSP.h"

/* CIDs remain in IEcoDSP.h for source compatibility. Factory exports follow
 * the Eco SDK convention: one function per DLL, one CID-qualified pointer
 * per static component. Application code obtains objects through the bus. */
#ifdef ECO_DLL
ECO_EXPORT IEcoComponentFactory* ECOCALLMETHOD GetIEcoComponentFactoryPtr(void);
#elif defined(ECO_LIB)
extern IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310070;
extern IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310071;
#endif

#endif
