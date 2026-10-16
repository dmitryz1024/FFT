#include "eco_internal.h"
#include "IdEcoFFT.h"
#include "IEcoSystem1.h"
#include "IEcoInterfaceBus1.h"
#include "IEcoInterfaceBus1MemExt.h"
#include "IdEcoMemoryManager1.h"

#ifndef ECO_COMPONENT_ALGORITHM
#error Build one factory translation unit per algorithm.
#endif
typedef struct EcoFactory { IEcoComponentFactory base; uint32_t refs; } EcoFactory;
static uint32_t ECOCALLMETHOD factory_ref(IEcoComponentFactory* me) {
    EcoFactory* self = (EcoFactory*)me;
    if (!self || self->refs == 0xFFFFFFFFU) return 0;
    return ++self->refs;
}
static uint32_t ECOCALLMETHOD factory_release(IEcoComponentFactory* me) {
    EcoFactory* self = (EcoFactory*)me;
    if (!self) return 0;
    /* The factory has static storage; references still balance to zero. */
    if (self->refs) --self->refs;
    return self->refs;
}
static int16_t ECOCALLMETHOD factory_query(IEcoComponentFactory* me, const UGUID* iid, void** out) {
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!me || !iid) return ECO_E_ARGUMENT;
    if (!eco_guid_equal(iid, &IID_IEcoUnknown) && !eco_guid_equal(iid, &IID_IEcoComponentFactory)) return ECO_E_INTERFACE;
    factory_ref(me);
    *out = me;
    return ECO_OK;
}
static int16_t ECOCALLMETHOD factory_alloc(IEcoComponentFactory* me, IEcoUnknown* system,
                                           IEcoUnknown* outer, const UGUID* iid, void** out) {
    IEcoSystem1* sys = 0;
    IEcoInterfaceBus1* bus = 0;
    IEcoInterfaceBus1MemExt* extension = 0;
    IEcoMemoryAllocator1* mem = 0;
    IEcoDSP* dsp = 0;
    const UGUID* memory_cid = &CID_EcoMemoryManager1;
    int16_t rc;
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!me || !system || !iid) return ECO_E_ARGUMENT;
    /* Aggregation is unsupported; reject a non-null controlling unknown. */
    if (outer) return ERR_ECO_NOAGGREGATION;
    rc = system->pVTbl->QueryInterface(system, &GID_IEcoSystem, (void**)&sys);
    if (rc || !sys) { rc = ERR_ECO_NOSYSTEM; goto done; }
    rc = sys->pVTbl->QueryInterface(sys, &IID_IEcoInterfaceBus1, (void**)&bus);
    if (rc || !bus) { rc = ERR_ECO_NOBUS; goto done; }
    /* Honor the environment's allocator selection, including replacement
     * allocators. Never register a dependency from inside the component. */
    rc = bus->pVTbl->QueryInterface(bus, &IID_IEcoInterfaceBus1MemExt, (void**)&extension);
    if (!rc && extension) memory_cid = extension->pVTbl->get_Manager(extension);
    if (!memory_cid) { rc = ERR_ECO_GET_MEMORY_ALLOCATOR; goto done; }
    rc = bus->pVTbl->QueryComponent(bus, memory_cid, 0, &IID_IEcoMemoryAllocator1, (void**)&mem);
    if (rc || !mem) { rc = ERR_ECO_GET_MEMORY_ALLOCATOR; goto done; }
    rc = eco_dsp_create(mem, ECO_COMPONENT_ALGORITHM, &dsp);
    if (!rc) rc = dsp->pVTbl->QueryInterface(dsp, iid, out);
done:
    if (dsp) dsp->pVTbl->Release(dsp);
    if (mem) mem->pVTbl->Release(mem);
    if (extension) extension->pVTbl->Release(extension);
    if (bus) bus->pVTbl->Release(bus);
    if (sys) sys->pVTbl->Release(sys);
    return rc;
}
static int16_t ECOCALLMETHOD factory_init(IEcoComponentFactory* me, IEcoUnknown* system, void* instance) {
    /* Alloc returns a fully initialized stateless service. No second phase
     * or extra dependency acquisition is needed for this component. */
    return me && system && instance ? ECO_OK : ECO_E_ARGUMENT;
}
static char_t* ECOCALLMETHOD factory_name(IEcoComponentFactory* me) {
    if (!me) return 0;
#if ECO_COMPONENT_ALGORITHM == 0
    return "EcoFFT DIT + Bluestein";
#else
    return "EcoFFT DIF + Bluestein";
#endif
}
static char_t* ECOCALLMETHOD factory_version(IEcoComponentFactory* me) { return me ? "1.0.0" : 0; }
static char_t* ECOCALLMETHOD factory_author(IEcoComponentFactory* me) { return me ? "EcoFFT" : 0; }
static IEcoComponentFactoryVTbl factory_vtbl = {
    factory_query, factory_ref, factory_release, factory_alloc, factory_init,
    factory_name, factory_version, factory_author
};
static EcoFactory factory = {{&factory_vtbl}, 0};
#ifdef ECO_DLL
/* The upstream Linux ECO_EXPORT is empty; expose only the standard entry. */
__attribute__((visibility("default")))
ECO_EXPORT IEcoComponentFactory* ECOCALLMETHOD GetIEcoComponentFactoryPtr(void) { return &factory.base; }
#elif defined(ECO_LIB)
#if ECO_COMPONENT_ALGORITHM == 0
IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310070 = &factory.base;
#else
IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310071 = &factory.base;
#endif
#else
#error Select ECO_DLL or ECO_LIB for the factory build.
#endif
