/* Integration test: only public interfaces and the unmodified Eco runtime.
 * Run in the directory containing the components. No factory registration,
 * implementation symbols or custom loader are used by this executable. */
#include "IEcoDSP.h"
#include "IEcoCalculatorX.h"
#include "IEcoSystem1.h"
#include "IEcoInterfaceBus1.h"
#include "IEcoInterfaceBus1MemExt.h"
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "ACOM FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void query_laws(IEcoDSP* dsp) {
    IEcoUnknown* interfaces[3] = {0, 0, 0};
    const UGUID* ids[3] = {&IID_IEcoDSP, &IID_IEcoCalculatorX, &IID_IEcoUnknown};
    for (unsigned i = 0; i < 3; ++i) {
        int16_t rc = dsp->pVTbl->QueryInterface(dsp, ids[i], (void**)&interfaces[i]);
        CHECK(rc == 0 && interfaces[i]);
    }
    /* Reflexivity, symmetry, transitivity, identity and a static interface set.
     * The calculator is deliberately a different address within the object. */
    CHECK((void*)interfaces[0] != (void*)interfaces[1]);
    for (unsigned i = 0; i < 3; ++i) if (interfaces[i]) {
        for (unsigned repetition = 0; repetition < 2; ++repetition) {
            for (unsigned j = 0; j < 3; ++j) {
                IEcoUnknown* queried = 0;
                int16_t rc = interfaces[i]->pVTbl->QueryInterface(interfaces[i], ids[j], (void**)&queried);
                CHECK(rc == 0 && queried == interfaces[j]);
                if (queried) queried->pVTbl->Release(queried);
            }
            void* absent = interfaces[i];
            CHECK(interfaces[i]->pVTbl->QueryInterface(interfaces[i], &CID_EcoFFT_DIT, &absent) != 0);
            CHECK(absent == 0);
        }
    }
    if (interfaces[1]) {
        IEcoCalculatorX* calc = (IEcoCalculatorX*)interfaces[1];
        CHECK(calc->pVTbl->Addition(calc, 32767, 32767) == 65534);
        CHECK(calc->pVTbl->Subtraction(calc, 12, 7) == 5);
        CHECK(calc->pVTbl->Subtraction(calc, -32768, 1) == 32767);
    }
    for (unsigned i = 0; i < 3; ++i) if (interfaces[i]) interfaces[i]->pVTbl->Release(interfaces[i]);
    CHECK(dsp->pVTbl->AddRef(dsp) == 2);
    CHECK(dsp->pVTbl->Release(dsp) == 1);
}

static void component(IEcoInterfaceBus1* bus, const UGUID* cid, unsigned algorithm) {
    IEcoDSP* dsp = 0;
    IEcoDSP* second = 0;
    IEcoCalculatorX* calc = 0;
    IEcoVector* vector = 0;
    IEcoFFTPlan* plan = 0;
    EcoComplexL value = {7, -2};
    int16_t rc = bus->pVTbl->QueryComponent(bus, cid, 0, &IID_IEcoDSP, (void**)&dsp);
    CHECK(rc == 0 && dsp);
    if (!dsp) return;
    CHECK(dsp->pVTbl->Algorithm(dsp) == algorithm);
    query_laws(dsp);
    rc = bus->pVTbl->QueryComponent(bus, cid, 0, &IID_IEcoDSP, (void**)&second);
    CHECK(rc == 0 && second && second != dsp);
    if (second) CHECK(second->pVTbl->Release(second) == 0);
    rc = bus->pVTbl->QueryComponent(bus, cid, 0, &IID_IEcoCalculatorX, (void**)&calc);
    CHECK(rc == 0 && calc);
    if (calc) {
        CHECK(calc->pVTbl->Addition(calc, 3, 4) == 7);
        CHECK(calc->pVTbl->Release(calc) == 0);
    }
    calc = 0;
    CHECK(dsp->pVTbl->QueryInterface(dsp, &IID_IEcoCalculatorX, (void**)&calc) == 0);
    CHECK(dsp->pVTbl->CreateVector(dsp, 1, &vector) == 0);
    CHECK(dsp->pVTbl->CreatePlan(dsp, 1, &plan) == 0);
    if (vector && plan) {
        CHECK(vector->pVTbl->Set(vector, 0, value) == 0);
        CHECK(plan->pVTbl->Load(plan, vector) == 0);
    }
    /* Another interface owns the same instance after the first is released. */
    CHECK(dsp->pVTbl->Release(dsp) == 1);
    if (calc) {
        CHECK(calc->pVTbl->Subtraction(calc, 19, 5) == 14);
        CHECK(calc->pVTbl->Release(calc) == 0);
    }
    /* Child objects own their allocator references and outlive the service. */
    if (vector && plan) {
        CHECK(plan->pVTbl->FFT(plan) == 0);
        CHECK(plan->pVTbl->IFFT(plan) == 0);
        CHECK(plan->pVTbl->Store(plan, vector) == 0);
        CHECK(vector->pVTbl->Get(vector, 0, &value) == 0);
        CHECK(value.re == 7 && value.im == -2);
    }
    if (plan) CHECK(plan->pVTbl->Release(plan) == 0);
    if (vector) CHECK(vector->pVTbl->Release(vector) == 0);
}

static void environment_contract(IEcoInterfaceBus1* bus) {
    IEcoInterfaceBus1MemExt* extension = 0;
    IEcoDSP* dsp = 0;
    void* rejected = 0;
    const UGUID* previous;
    /* The CID below intentionally has no provider in the test environment. */
    const UGUID absent = {0x01, 0x10, {0xAB,0x11,0x80,0xD3,0x9C,0x2E,0x49,0x51,
                                     0x81,0xB8,0xE5,0x26,0x89,0x44,0xEA,0x01}};
    CHECK(bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIT, 0, &IID_IEcoDSP, (void**)&dsp) == 0);
    if (!dsp) return;
    CHECK(bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIT, (IEcoUnknown*)dsp,
                                    &IID_IEcoUnknown, &rejected) != 0);
    CHECK(rejected == 0);
    CHECK(bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIT, 0, &absent, &rejected) != 0);
    CHECK(rejected == 0);
    dsp->pVTbl->Release(dsp);
    CHECK(bus->pVTbl->QueryInterface(bus, &IID_IEcoInterfaceBus1MemExt, (void**)&extension) == 0);
    if (!extension) return;
    previous = extension->pVTbl->get_Manager(extension);
    CHECK(extension->pVTbl->set_Manager(extension, &absent) == 0);
    CHECK(bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIF, 0, &IID_IEcoDSP, &rejected) != 0);
    CHECK(rejected == 0); /* No hard-coded fallback to the default allocator. */
    CHECK(extension->pVTbl->set_Manager(extension, previous) == 0);
    dsp = 0;
    CHECK(bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIF, 0, &IID_IEcoDSP, (void**)&dsp) == 0);
    if (dsp) CHECK(dsp->pVTbl->Release(dsp) == 0);
    extension->pVTbl->Release(extension);
}

int16_t EcoMain(IEcoUnknown* unknown) {
    IEcoSystem1* system = 0;
    IEcoInterfaceBus1* bus = 0;
    int16_t rc = 1;
    if (!unknown || unknown->pVTbl->QueryInterface(unknown, &GID_IEcoSystem, (void**)&system)) goto done;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus)) goto done;
    component(bus, &CID_EcoFFT_DIT, ECO_FFT_DIT);
    component(bus, &CID_EcoFFT_DIF, ECO_FFT_DIF);
    environment_contract(bus);
    rc = failures ? 1 : 0;
done:
    if (bus) bus->pVTbl->Release(bus);
    if (system) system->pVTbl->Release(system);
    printf("ACOM_RESULT checks=%u failures=%u status=%s\n", checks, failures, rc ? "FAIL" : "PASS");
    return rc;
}
