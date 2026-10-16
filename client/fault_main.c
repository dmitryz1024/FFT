/* White-box allocation-failure checks. This executable deliberately links the
 * implementation. It is separate from all public-ABI integration clients. */
#include "fft_client.h"
#include "IEcoSystem1.h"
#include "IdEcoMemoryManager1.h"

unsigned client_checks = 0, client_failures = 0;

int16_t EcoMain(IEcoUnknown* unknown) {
    IEcoSystem1* system = 0;
    IEcoInterfaceBus1* bus = 0;
    IEcoMemoryAllocator1* memory = 0;
    int16_t rc = 1;
    if (!unknown || unknown->pVTbl->QueryInterface(unknown, &GID_IEcoSystem, (void**)&system)) goto done;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus)) goto done;
    if (bus->pVTbl->QueryComponent(bus, &CID_EcoMemoryManager1, 0, &IID_IEcoMemoryAllocator1, (void**)&memory)) goto done;
    client_faults(memory);
    rc = client_failures ? 1 : 0;
done:
    if (memory) memory->pVTbl->Release(memory);
    if (bus) bus->pVTbl->Release(bus);
    if (system) system->pVTbl->Release(system);
    return rc;
}
