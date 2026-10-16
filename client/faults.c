#include "fft_client.h"
#include "eco_internal.h"
#include "IdEcoFFT.h"

/* This allocator records the public allocation boundary, without depending
 * on any private object layout. Storage still comes from the actual Eco
 * allocator supplied by the running Eco application. */
#define TRACK_CAPACITY 256U
typedef struct TrackingAllocator {
    IEcoMemoryAllocator1 iface;
    IEcoMemoryAllocator1* backing;
    uint32_t refs;
    uint32_t calls;
    uint32_t fail_at;
    uint32_t live;
    uint32_t bytes;
    void* pointers[TRACK_CAPACITY];
    uint32_t sizes[TRACK_CAPACITY];
} TrackingAllocator;

static uint32_t track_find(TrackingAllocator* self, void* pointer) {
    uint32_t i;
    for (i = 0; i < self->live; ++i)
        if (self->pointers[i] == pointer) return i;
    return self->live;
}
static uint32_t ECOCALLMETHOD track_addref(IEcoMemoryAllocator1* me) {
    return ++((TrackingAllocator*)me)->refs;
}
static uint32_t ECOCALLMETHOD track_release(IEcoMemoryAllocator1* me) {
    TrackingAllocator* self = (TrackingAllocator*)me;
    CHECK(self->refs > 1U); /* The test owns the initial, non-heap reference. */
    if (self->refs > 1U) --self->refs;
    return self->refs;
}
static int16_t ECOCALLMETHOD track_query(IEcoMemoryAllocator1* me,
                                         const UGUID* iid, void** out) {
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!me || !iid) return ECO_E_ARGUMENT;
    if (!eco_guid_equal(iid, &IID_IEcoUnknown) &&
        !eco_guid_equal(iid, &IID_IEcoMemoryAllocator1)) return ECO_E_INTERFACE;
    track_addref(me);
    *out = me;
    return ECO_OK;
}
static void* ECOCALLMETHOD track_alloc(IEcoMemoryAllocator1* me, uint32_t size) {
    TrackingAllocator* self = (TrackingAllocator*)me;
    void* pointer;
    ++self->calls;
    if (self->calls == self->fail_at) return 0;
    CHECK(self->live < TRACK_CAPACITY);
    if (self->live >= TRACK_CAPACITY) return 0;
    pointer = self->backing->pVTbl->Alloc(self->backing, size);
    if (!pointer) return 0;
    self->pointers[self->live] = pointer;
    self->sizes[self->live++] = size;
    self->bytes += size;
    return pointer;
}
static void ECOCALLMETHOD track_free(IEcoMemoryAllocator1* me, void* pointer) {
    TrackingAllocator* self = (TrackingAllocator*)me;
    uint32_t i;
    if (!pointer) return;
    i = track_find(self, pointer);
    CHECK(i < self->live); /* Detect foreign frees and double releases. */
    if (i == self->live) return;
    self->bytes -= self->sizes[i];
    --self->live;
    self->pointers[i] = self->pointers[self->live];
    self->sizes[i] = self->sizes[self->live];
    self->backing->pVTbl->Free(self->backing, pointer);
}
static void* ECOCALLMETHOD track_realloc(IEcoMemoryAllocator1* me, void* pointer,
                                         uint32_t size) {
    TrackingAllocator* self = (TrackingAllocator*)me;
    uint32_t i;
    uint32_t copy_size;
    void* replacement;
    if (!pointer) return track_alloc(me, size);
    if (!size) { track_free(me, pointer); return 0; }
    i = track_find(self, pointer);
    CHECK(i < self->live);
    if (i == self->live) return 0;
    copy_size = size < self->sizes[i] ? size : self->sizes[i];
    replacement = track_alloc(me, size);
    if (!replacement) return 0;
    memcpy(replacement, pointer, copy_size);
    track_free(me, pointer);
    return replacement;
}
static void* ECOCALLMETHOD track_copy(IEcoMemoryAllocator1* me, void* destination,
                                      void* source, uint32_t size) {
    (void)me;
    return memmove(destination, source, size);
}
static void* ECOCALLMETHOD track_fill(IEcoMemoryAllocator1* me, void* destination,
                                      char_t value, uint32_t size) {
    (void)me;
    return memset(destination, (unsigned char)value, size);
}
static int16_t ECOCALLMETHOD track_compare(IEcoMemoryAllocator1* me, void* a,
                                          void* b, uint32_t size) {
    int comparison;
    (void)me;
    comparison = memcmp(a, b, size);
    return (int16_t)((comparison > 0) - (comparison < 0));
}
static uint32_t ECOCALLMETHOD track_size(IEcoMemoryAllocator1* me, void* pointer) {
    TrackingAllocator* self = (TrackingAllocator*)me;
    uint32_t i = track_find(self, pointer);
    return i < self->live ? self->sizes[i] : 0;
}
static IEcoMemoryAllocator1VTbl track_vtable = {
    track_query, track_addref, track_release, track_alloc, track_free,
    track_realloc, track_copy, track_fill, track_compare, track_size
};

/* QI is checked through each declared interface type, including ownership of
 * the returned reference and identity across IEcoUnknown and the own IID. */
#define CHECK_OBJECT_QUERY(object, own_iid) do { \
    void* queried = (object); \
    int16_t query_rc = (object)->pVTbl->QueryInterface((object), &(own_iid), &queried); \
    CHECK(query_rc == ECO_OK && queried == (void*)(object)); \
    if (query_rc == ECO_OK && queried) CHECK((object)->pVTbl->Release(object) == 1U); \
    queried = 0; \
    query_rc = (object)->pVTbl->QueryInterface((object), &IID_IEcoUnknown, &queried); \
    CHECK(query_rc == ECO_OK && queried == (void*)(object)); \
    if (query_rc == ECO_OK && queried) CHECK((object)->pVTbl->Release(object) == 1U); \
    queried = (object); \
    CHECK((object)->pVTbl->QueryInterface((object), &CID_EcoFFT_DIT, &queried) == ECO_E_INTERFACE); \
    CHECK(queried == 0); \
    queried = (object); \
    CHECK((object)->pVTbl->QueryInterface((object), 0, &queried) == ECO_E_ARGUMENT); \
    CHECK(queried == 0); \
    CHECK((object)->pVTbl->QueryInterface((object), &(own_iid), 0) == ECO_E_ARGUMENT); \
    queried = (object); \
    CHECK((object)->pVTbl->QueryInterface(0, &(own_iid), &queried) == ECO_E_ARGUMENT); \
    CHECK(queried == 0); \
    CHECK((object)->pVTbl->AddRef(object) == 2U); \
    CHECK((object)->pVTbl->Release(object) == 1U); \
} while (0)

static void check_baseline(TrackingAllocator* mem, uint32_t live,
                            uint32_t bytes, uint32_t refs) {
    CHECK(mem->live == live);
    CHECK(mem->bytes == bytes);
    CHECK(mem->refs == refs);
}

/* Increase the injection index until construction succeeds. This discovers
 * every actual allocation boundary instead of encoding implementation counts.
 */
static void creation_failures(TrackingAllocator* mem, IEcoDSP* dsp,
                               IEcoVector* coefficients, uint32_t n,
                               uint32_t operation) {
    uint32_t nth;
    uint32_t live = mem->live;
    uint32_t bytes = mem->bytes;
    uint32_t refs = mem->refs;
    int success = 0;
    for (nth = 1U; nth <= 64U; ++nth) {
        int16_t rc;
        mem->fail_at = mem->calls + nth;
        if (operation == 0U) {
            IEcoDSP* value = dsp;
            rc = eco_dsp_create(&mem->iface, dsp->pVTbl->Algorithm(dsp), &value);
            if (rc == ECO_OK && value) {
                CHECK(value->pVTbl->MemoryBytes(value) == mem->bytes - bytes);
                CHECK_OBJECT_QUERY(value, IID_IEcoDSP);
                CHECK(value->pVTbl->Release(value) == 0U);
            } else CHECK(value == 0);
        } else if (operation == 1U) {
            IEcoVector* value = coefficients;
            rc = dsp->pVTbl->CreateVector(dsp, n, &value);
            if (rc == ECO_OK && value) {
                CHECK(value->pVTbl->MemoryBytes(value) == mem->bytes - bytes);
                CHECK(value->pVTbl->Size(value) == n);
                CHECK_OBJECT_QUERY(value, IID_IEcoVector);
                CHECK(value->pVTbl->Release(value) == 0U);
            } else CHECK(value == 0);
        } else if (operation == 2U) {
            IEcoMatrix* value = (IEcoMatrix*)dsp;
            rc = dsp->pVTbl->CreateMatrix(dsp, 2U, n, &value);
            if (rc == ECO_OK && value) {
                CHECK(value->pVTbl->MemoryBytes(value) == mem->bytes - bytes);
                CHECK(value->pVTbl->Rows(value) == 2U && value->pVTbl->Cols(value) == n);
                CHECK_OBJECT_QUERY(value, IID_IEcoMatrix);
                CHECK(value->pVTbl->Release(value) == 0U);
            } else CHECK(value == 0);
        } else if (operation == 3U) {
            IEcoFFTPlan* value = (IEcoFFTPlan*)dsp;
            rc = dsp->pVTbl->CreatePlan(dsp, n, &value);
            if (rc == ECO_OK && value) {
                CHECK(value->pVTbl->MemoryBytes(value) == mem->bytes - bytes);
                CHECK(value->pVTbl->Size(value) == n);
                CHECK(value->pVTbl->FFT(value) == ECO_E_STATE);
                CHECK(value->pVTbl->IFFT(value) == ECO_E_STATE);
                CHECK_OBJECT_QUERY(value, IID_IEcoFFTPlan);
                CHECK(value->pVTbl->Release(value) == 0U);
            } else CHECK(value == 0);
        } else {
            IEcoDigitalFilter* value = (IEcoDigitalFilter*)dsp;
            rc = dsp->pVTbl->CreateFilter(dsp, coefficients, &value);
            if (rc == ECO_OK && value) {
                CHECK(value->pVTbl->MemoryBytes(value) == mem->bytes - bytes);
                CHECK(value->pVTbl->Size(value) == n);
                CHECK_OBJECT_QUERY(value, IID_IEcoDigitalFilter);
                CHECK(value->pVTbl->Release(value) == 0U);
            } else CHECK(value == 0);
        }
        mem->fail_at = 0;
        check_baseline(mem, live, bytes, refs);
        if (rc == ECO_OK) { success = 1; break; }
        CHECK(rc == ECO_E_MEMORY);
    }
    CHECK(success && nth > 1U);
}

static void set_values(IEcoVector* vector, uint32_t n, const EcoComplexL* values) {
    uint32_t i;
    for (i = 0; i < n; ++i) CHECK(vector->pVTbl->Set(vector, i, values[i]) == ECO_OK);
}
static void check_values(IEcoVector* vector, uint32_t n,
                          const EcoComplexL* expected, long double tolerance) {
    uint32_t i;
    for (i = 0; i < n; ++i) {
        EcoComplexL value = {0, 0};
        CHECK(vector->pVTbl->Get(vector, i, &value) == ECO_OK);
        CHECK(fabsl(value.re - expected[i].re) <= tolerance);
        CHECK(fabsl(value.im - expected[i].im) <= tolerance);
    }
}

static void operation_failures(TrackingAllocator* mem, IEcoDSP* dsp,
                                IEcoVector* input_vector, IEcoVector* coefficients,
                                IEcoVector* output_vector, uint32_t n) {
    EcoComplexL input[8];
    EcoComplexL output[8];
    EcoComplexL sentinel[8];
    uint32_t kind, i, nth;
    uint32_t live = mem->live;
    uint32_t bytes = mem->bytes;
    uint32_t refs = mem->refs;
    for (i = 0; i < n; ++i) {
        input[i].re = (long double)i + 1;
        input[i].im = 0;
        sentinel[i].re = -777;
        sentinel[i].im = 999;
    }
    set_values(input_vector, n, input);
    for (kind = 0; kind < 2U; ++kind) {
        int success = 0;
        for (nth = 1; nth <= 64U; ++nth) {
            int16_t rc;
            for (i = 0; i < n; ++i) output[i] = sentinel[i];
            set_values(output_vector, n, sentinel);
            mem->fail_at = mem->calls + nth;
            rc = kind == 0U ? dsp->pVTbl->fftl(dsp, n, input, output) :
                 dsp->pVTbl->Filter(dsp, input_vector, coefficients, output_vector);
            mem->fail_at = 0;
            check_baseline(mem, live, bytes, refs);
            if (rc == ECO_OK) {
                success = 1;
                if (kind != 0U) check_values(output_vector, n, input, 1e-14L);
                break;
            }
            CHECK(rc == ECO_E_MEMORY);
            if (kind == 0U) {
                for (i = 0; i < n; ++i)
                    CHECK(output[i].re == sentinel[i].re && output[i].im == sentinel[i].im);
            } else check_values(output_vector, n, sentinel, 0);
        }
        CHECK(success && nth > 1U);
    }
}

/* A separate implementation of IEcoVector verifies polymorphic reads and
 * caller-error handling. Get can fail after returning a valid prefix, or
 * return a non-finite value. It never allocates and is borrowed by the caller.
 */
typedef struct AdversarialVector {
    IEcoVector iface;
    uint32_t n;
    uint32_t fail_index;
    uint32_t invalid_index;
    const EcoComplexL* values;
} AdversarialVector;
static uint32_t ECOCALLMETHOD adversarial_size(IEcoVector* me) {
    return ((AdversarialVector*)me)->n;
}
static int16_t ECOCALLMETHOD adversarial_get(IEcoVector* me, uint32_t i,
                                             EcoComplexL* out) {
    AdversarialVector* self = (AdversarialVector*)me;
    if (!out) return ECO_E_ARGUMENT;
    if (i >= self->n) return ECO_E_RANGE;
    if (i == self->fail_index) return ECO_E_STATE;
    *out = self->values[i];
    if (i == self->invalid_index) out->im = NAN;
    return ECO_OK;
}
static IEcoVectorVTbl adversarial_vtable = {
    0, 0, 0, adversarial_size, adversarial_get, 0, 0
};

static void cached_state(TrackingAllocator* mem, IEcoDSP* dsp,
                          IEcoVector* signal, IEcoVector* coefficients,
                          IEcoVector* result, uint32_t n) {
    IEcoFFTPlan* a = 0;
    IEcoFFTPlan* b = 0;
    IEcoDigitalFilter* identity = 0;
    IEcoDigitalFilter* doubled = 0;
    EcoComplexL original[8];
    EcoComplexL changed[8];
    EcoComplexL scaled[8];
    EcoComplexL impulse = {2, 0};
    AdversarialVector bad = {{&adversarial_vtable}, n, n / 2U, n, changed};
    uint32_t live = mem->live;
    uint32_t bytes = mem->bytes;
    uint32_t refs = mem->refs;
    uint32_t calls;
    uint32_t i;
    int16_t rc;

    for (i = 0; i < n; ++i) {
        original[i].re = (long double)i + 1;
        original[i].im = -(long double)i / 4;
        changed[i].re = 30 - (long double)i;
        changed[i].im = 2;
        scaled[i].re = 2 * changed[i].re;
        scaled[i].im = 2 * changed[i].im;
    }
    rc = dsp->pVTbl->CreatePlan(dsp, n, &a);
    CHECK(rc == ECO_OK && a != 0);
    if (!a) goto cleanup;
    rc = dsp->pVTbl->CreatePlan(dsp, n, &b);
    CHECK(rc == ECO_OK && b != 0);
    if (!b) goto cleanup;
    rc = dsp->pVTbl->CreateFilter(dsp, coefficients, &identity);
    CHECK(rc == ECO_OK && identity != 0);
    if (!identity) goto cleanup;
    CHECK(coefficients->pVTbl->Set(coefficients, 0, impulse) == ECO_OK);
    rc = dsp->pVTbl->CreateFilter(dsp, coefficients, &doubled);
    CHECK(rc == ECO_OK && doubled != 0);
    if (!doubled) goto cleanup;

    calls = mem->calls;
    mem->fail_at = calls + 1U; /* Every following operation must reuse storage. */
    CHECK(a->pVTbl->Store(a, result) == ECO_E_STATE);
    CHECK(b->pVTbl->Load(b, &bad.iface) == ECO_E_STATE);
    CHECK(b->pVTbl->FFT(b) == ECO_E_STATE);
    set_values(signal, n, original);
    CHECK(a->pVTbl->Load(a, signal) == ECO_OK);
    set_values(signal, n, changed);
    CHECK(b->pVTbl->Load(b, signal) == ECO_OK);
    CHECK(a->pVTbl->Store(a, result) == ECO_OK);
    check_values(result, n, original, 0); /* Load owns a snapshot. */

    CHECK(a->pVTbl->Load(a, &bad.iface) == ECO_E_STATE);
    CHECK(a->pVTbl->Store(a, result) == ECO_OK);
    check_values(result, n, original, 0);
    bad.fail_index = n;
    bad.invalid_index = n / 2U;
    CHECK(a->pVTbl->Load(a, &bad.iface) == ECO_E_RANGE);
    CHECK(a->pVTbl->Store(a, result) == ECO_OK);
    check_values(result, n, original, 0);
    bad.invalid_index = n;
    bad.n = n - 1U;
    CHECK(a->pVTbl->Load(a, &bad.iface) == ECO_E_RANGE);
    CHECK(a->pVTbl->Store(a, result) == ECO_OK);
    check_values(result, n, original, 0);
    bad.n = n;
    CHECK(a->pVTbl->Load(a, 0) == ECO_E_ARGUMENT);
    CHECK(a->pVTbl->FFT(a) == ECO_OK);
    CHECK(a->pVTbl->IFFT(a) == ECO_OK);
    CHECK(a->pVTbl->Store(a, result) == ECO_OK);
    check_values(result, n, original, 1e-14L);
    CHECK(b->pVTbl->Store(b, result) == ECO_OK);
    check_values(result, n, changed, 0); /* Independent plan state. */

    CHECK(identity->pVTbl->Apply(identity, signal, result) == ECO_OK);
    check_values(result, n, changed, 1e-14L); /* Coefficients were changed to 2. */
    CHECK(doubled->pVTbl->Apply(doubled, signal, result) == ECO_OK);
    check_values(result, n, scaled, 1e-14L);
    bad.fail_index = n / 2U;
    CHECK(identity->pVTbl->Apply(identity, &bad.iface, result) == ECO_E_STATE);
    check_values(result, n, scaled, 1e-14L);
    CHECK(identity->pVTbl->Apply(identity, signal, result) == ECO_OK);
    check_values(result, n, changed, 1e-14L);
    CHECK(identity->pVTbl->Apply(identity, signal, signal) == ECO_OK);
    check_values(signal, n, changed, 1e-14L);
    CHECK(mem->calls == calls);
    mem->fail_at = 0;

    /* A polymorphic coefficient read failure unwinds the already allocated
     * filter and kernel and clears the output ownership slot. */
    {
        uint32_t old_live = mem->live, old_bytes = mem->bytes, old_refs = mem->refs;
        IEcoDigitalFilter* failed = identity;
        CHECK(dsp->pVTbl->CreateFilter(dsp, &bad.iface, &failed) == ECO_E_STATE);
        CHECK(failed == 0);
        check_baseline(mem, old_live, old_bytes, old_refs);
    }

cleanup:
    mem->fail_at = 0;
    if (a) a->pVTbl->Release(a);
    if (b) b->pVTbl->Release(b);
    if (identity) identity->pVTbl->Release(identity);
    if (doubled) doubled->pVTbl->Release(doubled);
    impulse.re = 1;
    CHECK(coefficients->pVTbl->Set(coefficients, 0, impulse) == ECO_OK);
    check_baseline(mem, live, bytes, refs);
}

static void invalid_creation(TrackingAllocator* mem, IEcoDSP* dsp,
                              IEcoVector* coefficients) {
    IEcoVector* vector = coefficients;
    IEcoMatrix* matrix = (IEcoMatrix*)dsp;
    IEcoFFTPlan* plan = (IEcoFFTPlan*)dsp;
    IEcoDigitalFilter* filter = (IEcoDigitalFilter*)dsp;
    uint32_t calls = mem->calls;
    CHECK(dsp->pVTbl->CreateVector(dsp, 0, &vector) == ECO_E_RANGE && vector == 0);
    vector = coefficients;
    CHECK(dsp->pVTbl->CreateVector(dsp, 65537U, &vector) == ECO_E_RANGE && vector == 0);
    CHECK(dsp->pVTbl->CreateVector(dsp, 7, 0) == ECO_E_ARGUMENT);
    CHECK(dsp->pVTbl->CreateMatrix(dsp, 0xFFFFFFFFU, 2, &matrix) == ECO_E_RANGE && matrix == 0);
    matrix = (IEcoMatrix*)dsp;
    CHECK(dsp->pVTbl->CreateMatrix(dsp, 2, 0, &matrix) == ECO_E_RANGE && matrix == 0);
    CHECK(dsp->pVTbl->CreateMatrix(dsp, 2, 3, 0) == ECO_E_ARGUMENT);
    CHECK(dsp->pVTbl->CreatePlan(dsp, 0, &plan) == ECO_E_RANGE && plan == 0);
    CHECK(dsp->pVTbl->CreatePlan(dsp, 7, 0) == ECO_E_ARGUMENT);
    CHECK(dsp->pVTbl->CreateFilter(dsp, 0, &filter) == ECO_E_ARGUMENT && filter == 0);
    CHECK(dsp->pVTbl->CreateFilter(dsp, coefficients, 0) == ECO_E_ARGUMENT);
    CHECK(mem->calls == calls);
}

static void factory_contracts(void) {
    IEcoComponentFactory* factories[2] = {
        GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310070,
        GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310071};
    uint32_t i;
    for (i = 0; i < 2U; ++i) {
        IEcoComponentFactory* factory = factories[i];
        void* queried;
        uint32_t baseline;
        int16_t rc;
        CHECK(factory != 0);
        if (!factory) continue;
        baseline = factory->pVTbl->AddRef(factory) - 1U;
        CHECK(factory->pVTbl->Release(factory) == baseline);
        queried = 0;
        rc = factory->pVTbl->QueryInterface(factory, &IID_IEcoUnknown, &queried);
        CHECK(rc == ECO_OK && queried == factory);
        if (rc == ECO_OK && queried) CHECK(factory->pVTbl->Release(factory) == baseline);
        queried = 0;
        rc = factory->pVTbl->QueryInterface(factory, &IID_IEcoComponentFactory, &queried);
        CHECK(rc == ECO_OK && queried == factory);
        if (rc == ECO_OK && queried) CHECK(factory->pVTbl->Release(factory) == baseline);
        queried = factory;
        CHECK(factory->pVTbl->QueryInterface(factory, &IID_IEcoDSP, &queried) == ECO_E_INTERFACE);
        CHECK(queried == 0);
        queried = factory;
        CHECK(factory->pVTbl->QueryInterface(factory, 0, &queried) == ECO_E_ARGUMENT);
        CHECK(queried == 0);
        CHECK(factory->pVTbl->QueryInterface(factory, &IID_IEcoUnknown, 0) == ECO_E_ARGUMENT);
        queried = factory;
        CHECK(factory->pVTbl->Alloc(factory, 0, 0, &IID_IEcoDSP, &queried) == ECO_E_ARGUMENT);
        CHECK(queried == 0);
        CHECK(factory->pVTbl->Alloc(factory, 0, 0, &IID_IEcoDSP, 0) == ECO_E_ARGUMENT);
    }
}

void client_faults(IEcoMemoryAllocator1* allocator) {
    TrackingAllocator mem = {0};
    uint32_t initial_refs;
    uint32_t final_refs;
    uint32_t algorithm, n, operation;
    unsigned start_checks = client_checks;
    unsigned start_failures = client_failures;
    CHECK(allocator != 0);
    if (!allocator) return;
    initial_refs = allocator->pVTbl->AddRef(allocator);
    allocator->pVTbl->Release(allocator);
    mem.iface.pVTbl = &track_vtable;
    mem.backing = allocator;
    mem.refs = 1;
    allocator->pVTbl->AddRef(allocator);
    factory_contracts();
    for (algorithm = 0; algorithm < 2U; ++algorithm) {
        IEcoDSP* dsp = 0;
        int16_t rc = eco_dsp_create(&mem.iface, algorithm, &dsp);
        CHECK(rc == ECO_OK && dsp != 0);
        if (!dsp) continue;
        CHECK(dsp->pVTbl->MemoryBytes(dsp) == mem.bytes);
        CHECK_OBJECT_QUERY(dsp, IID_IEcoDSP);
        for (n = 7; n <= 8U; ++n) {
            IEcoVector* coefficients = 0;
            IEcoVector* signal = 0;
            IEcoVector* result = 0;
            EcoComplexL impulse = {1, 0};
            rc = dsp->pVTbl->CreateVector(dsp, n, &coefficients);
            CHECK(rc == ECO_OK && coefficients != 0);
            rc = dsp->pVTbl->CreateVector(dsp, n, &signal);
            CHECK(rc == ECO_OK && signal != 0);
            rc = dsp->pVTbl->CreateVector(dsp, n, &result);
            CHECK(rc == ECO_OK && result != 0);
            if (coefficients && signal && result) {
                CHECK(coefficients->pVTbl->Set(coefficients, 0, impulse) == ECO_OK);
                CHECK(mem.bytes == dsp->pVTbl->MemoryBytes(dsp) +
                      coefficients->pVTbl->MemoryBytes(coefficients) +
                      signal->pVTbl->MemoryBytes(signal) + result->pVTbl->MemoryBytes(result));
                for (operation = 0; operation < 5U; ++operation)
                    creation_failures(&mem, dsp, coefficients, n, operation);
                operation_failures(&mem, dsp, signal, coefficients, result, n);
                cached_state(&mem, dsp, signal, coefficients, result, n);
                invalid_creation(&mem, dsp, coefficients);
            }
            if (coefficients) coefficients->pVTbl->Release(coefficients);
            if (signal) signal->pVTbl->Release(signal);
            if (result) result->pVTbl->Release(result);
            CHECK(mem.bytes == dsp->pVTbl->MemoryBytes(dsp));
        }
        dsp->pVTbl->Release(dsp);
        check_baseline(&mem, 0, 0, 1);
    }
    check_baseline(&mem, 0, 0, 1);
    allocator->pVTbl->Release(allocator);
    final_refs = allocator->pVTbl->AddRef(allocator);
    allocator->pVTbl->Release(allocator);
    CHECK(initial_refs == final_refs);
    printf("FAULT/LIFETIME: %u checks, %u failures; injected allocator calls=%u\n",
           client_checks - start_checks, client_failures - start_failures, mem.calls);
}
