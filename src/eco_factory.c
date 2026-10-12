#include "eco_internal.h"
#include "IEcoInterfaceBus1.h"
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
    /* One permanent reference belongs to the module's static factory. */
    if (self->refs > 1) --self->refs;
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
    IEcoInterfaceBus1* bus = 0;
    IEcoMemoryAllocator1* mem = 0;
    IEcoDSP* dsp = 0;
    int16_t rc;
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!me || !system || !iid) return ECO_E_ARGUMENT;
    /* Aggregation is unsupported; reject a non-null controlling unknown. */
    if (outer) return ECO_E_ARGUMENT;
    rc = system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus);
    if (rc || !bus) return ECO_E_INTERFACE;
    rc = bus->pVTbl->QueryComponent(bus, &CID_EcoMemoryManager1, 0, &IID_IEcoMemoryAllocator1, (void**)&mem);
    bus->pVTbl->Release(bus);
    if (rc || !mem) return ECO_E_MEMORY;
    rc = eco_dsp_create(mem, ECO_COMPONENT_ALGORITHM, &dsp);
    mem->pVTbl->Release(mem);
    if (rc) return rc;
    rc = dsp->pVTbl->QueryInterface(dsp, iid, out);
    dsp->pVTbl->Release(dsp);
    return rc;
}
static int16_t ECOCALLMETHOD factory_init(IEcoComponentFactory* me, IEcoUnknown* system, void* instance) {
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
static EcoFactory factory = {{&factory_vtbl}, 1};
#if ECO_COMPONENT_ALGORITHM == 0
ECO_EXPORT IEcoComponentFactory* ECOCALLMETHOD GetEcoFFTDITFactory(void) { return &factory.base; }
ECO_EXPORT IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310070 = &factory.base;
#else
ECO_EXPORT IEcoComponentFactory* ECOCALLMETHOD GetEcoFFTDIFactory(void) { return &factory.base; }
ECO_EXPORT IEcoComponentFactory* GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310071 = &factory.base;
#endif
#ifdef ECO_DYNAMIC_COMPONENT
ECO_EXPORT IEcoComponentFactory* ECOCALLMETHOD GetIEcoComponentFactoryPtr(void) { return &factory.base; }
#endif
