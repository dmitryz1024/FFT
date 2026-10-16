#include "eco_internal.h"
#include "IEcoCalculatorX.h"

/* A service owns only immutable configuration and the allocator reference.
 * No signal, tables or history survive a method call. */
typedef struct EcoDSP {
    IEcoDSP base;
    IEcoCalculatorX calculator;
    uint32_t refs;
    IEcoMemoryAllocator1* mem;
    uint32_t algorithm;
} EcoDSP;

static uint32_t ECOCALLMETHOD dsp_addref(IEcoDSP* me) {
    EcoDSP* self = (EcoDSP*)me;
    if (!self || self->refs == 0xFFFFFFFFU) return 0;
    return ++self->refs;
}
static uint32_t ECOCALLMETHOD dsp_release(IEcoDSP* me) {
    EcoDSP* self = (EcoDSP*)me;
    IEcoMemoryAllocator1* mem;
    uint32_t refs;
    if (!self) return 0;
    refs = --self->refs;
    if (!refs) {
        mem = self->mem;
        mem->pVTbl->Free(mem, self);
        mem->pVTbl->Release(mem);
    }
    return refs;
}
static int16_t ECOCALLMETHOD dsp_query(IEcoDSP* me, const UGUID* iid, void** out) {
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!me || !iid) return ECO_E_ARGUMENT;
    if (eco_guid_equal(iid, &IID_IEcoDSP) || eco_guid_equal(iid, &IID_IEcoUnknown))
        *out = me;
    else if (eco_guid_equal(iid, &IID_IEcoCalculatorX))
        *out = &((EcoDSP*)me)->calculator;
    else return ECO_E_INTERFACE;
    dsp_addref(me);
    return ECO_OK;
}

/* Both interfaces share ownership and the canonical IEcoUnknown. An interface
 * at a nonzero offset must recover its owner before accessing instance data. */
static EcoDSP* calculator_owner(IEcoCalculatorX* me) {
    return me ? (EcoDSP*)((char_t*)me - __builtin_offsetof(EcoDSP, calculator)) : 0;
}
static int16_t ECOCALLMETHOD calculator_query(IEcoCalculatorX* me, const UGUID* iid, void** out) {
    EcoDSP* self = calculator_owner(me);
    return dsp_query(self ? &self->base : 0, iid, out);
}
static uint32_t ECOCALLMETHOD calculator_ref(IEcoCalculatorX* me) {
    EcoDSP* self = calculator_owner(me);
    return dsp_addref(self ? &self->base : 0);
}
static uint32_t ECOCALLMETHOD calculator_release(IEcoCalculatorX* me) {
    EcoDSP* self = calculator_owner(me);
    return dsp_release(self ? &self->base : 0);
}
static int32_t ECOCALLMETHOD calculator_add(IEcoCalculatorX* me, int16_t a, int16_t b) {
    return me ? (int32_t)a + b : 0;
}
static int16_t ECOCALLMETHOD calculator_subtract(IEcoCalculatorX* me, int16_t a, int16_t b) {
    int32_t value = (int32_t)a - b;
    if (!me) return 0;
    /* The existing interface returns 16 bits. Define wraparound explicitly. */
    if (value > 32767) value -= 65536;
    if (value < -32768) value += 65536;
    return (int16_t)value;
}
static IEcoCalculatorXVTbl calculator_vtbl = {
    calculator_query, calculator_ref, calculator_release, calculator_add, calculator_subtract
};

/* Explicit widening before FFT and narrowing after all checks succeed.
 * The float bound ensures N*max(input) still fits in float for N<=65536.
 * Double/long-double limits deliberately leave ample overflow headroom. */
static int16_t dsp_transform(IEcoDSP* me, uint32_t n, const void* input,
                             void* output, uint32_t type, int inverse) {
    EcoDSP* self = (EcoDSP*)me;
    EcoFFTKernel* kernel = 0;
    EcoComplexL* values;
    uint32_t i;
    int16_t rc;
    if (!me || !input || !output) return ECO_E_ARGUMENT;
    if (!n || n > ECO_FFT_MAX_N) return ECO_E_RANGE;
    values = (EcoComplexL*)self->mem->pVTbl->Alloc(self->mem, n * (uint32_t)sizeof(*values));
    if (!values) return ECO_E_MEMORY;
    for (i = 0; i < n; ++i) {
        if (type == 0) {
            values[i].re = ((const EcoComplexD*)input)[i].re;
            values[i].im = ((const EcoComplexD*)input)[i].im;
        } else if (type == 1) {
            values[i].re = ((const EcoComplexF*)input)[i].re;
            values[i].im = ((const EcoComplexF*)input)[i].im;
        } else if (type == 2) {
            values[i].re = ((const EcoComplexL*)input)[i].re;
            values[i].im = ((const EcoComplexL*)input)[i].im;
        } else {
            values[i].re = type == 3 ? (long double)((const int*)input)[i] : (long double)((const long*)input)[i];
            values[i].im = 0;
        }
        if (!eco_value_valid(values[i]) ||
            (type == 1 && (values[i].re > 1e30L || values[i].re < -1e30L ||
                           values[i].im > 1e30L || values[i].im < -1e30L))) {
            self->mem->pVTbl->Free(self->mem, values);
            return ECO_E_RANGE;
        }
    }
    rc = eco_fft_kernel_create(self->mem, n, self->algorithm, &kernel);
    if (!rc) rc = eco_fft_kernel_apply(kernel, values, values, inverse);
    if (!rc) for (i = 0; i < n; ++i) {
        if (type == 1) {
            ((EcoComplexF*)output)[i].re = (float)values[i].re;
            ((EcoComplexF*)output)[i].im = (float)values[i].im;
        } else if (type == 2) {
            ((EcoComplexL*)output)[i].re = values[i].re;
            ((EcoComplexL*)output)[i].im = values[i].im;
        } else {
            ((EcoComplexD*)output)[i].re = (double)values[i].re;
            ((EcoComplexD*)output)[i].im = (double)values[i].im;
        }
    }
    eco_fft_kernel_destroy(kernel);
    self->mem->pVTbl->Free(self->mem, values);
    return rc;
}
#define COMPLEX_METHOD(name, type, tag, direction) \
    static int16_t ECOCALLMETHOD name(IEcoDSP* me, uint32_t n, const type* in, type* out) { \
        return dsp_transform(me, n, in, out, tag, direction); \
    }
COMPLEX_METHOD(dsp_fft, EcoComplexD, 0, 0)
COMPLEX_METHOD(dsp_ifft, EcoComplexD, 0, 1)
COMPLEX_METHOD(dsp_fftf, EcoComplexF, 1, 0)
COMPLEX_METHOD(dsp_ifftf, EcoComplexF, 1, 1)
COMPLEX_METHOD(dsp_fftl, EcoComplexL, 2, 0)
COMPLEX_METHOD(dsp_ifftl, EcoComplexL, 2, 1)
#undef COMPLEX_METHOD
#define INTEGER_METHOD(name, type, tag, direction) \
    static int16_t ECOCALLMETHOD name(IEcoDSP* me, uint32_t n, const type* in, EcoComplexD* out) { \
        return dsp_transform(me, n, in, out, tag, direction); \
    }
INTEGER_METHOD(dsp_ffti, int, 3, 0)
INTEGER_METHOD(dsp_iffti, int, 3, 1)
INTEGER_METHOD(dsp_fftlong, long, 4, 0)
INTEGER_METHOD(dsp_ifftlong, long, 4, 1)
#undef INTEGER_METHOD
static int16_t ECOCALLMETHOD dsp_vector(IEcoDSP* me, uint32_t n, IEcoVector** out) {
    if (out) *out = 0;
    if (!me) return ECO_E_ARGUMENT;
    return eco_vector_create(((EcoDSP*)me)->mem, n, out);
}
static int16_t ECOCALLMETHOD dsp_matrix(IEcoDSP* me, uint32_t r, uint32_t c, IEcoMatrix** out) {
    if (out) *out = 0;
    if (!me) return ECO_E_ARGUMENT;
    return eco_matrix_create(((EcoDSP*)me)->mem, r, c, out);
}
static int16_t ECOCALLMETHOD dsp_plan(IEcoDSP* me, uint32_t n, IEcoFFTPlan** out) {
    if (out) *out = 0;
    if (!me) return ECO_E_ARGUMENT;
    return eco_plan_create(((EcoDSP*)me)->mem, n, ((EcoDSP*)me)->algorithm, out);
}
static int16_t ECOCALLMETHOD dsp_filter_create(IEcoDSP* me, IEcoVector* kernel, IEcoDigitalFilter** out) {
    if (out) *out = 0;
    if (!me) return ECO_E_ARGUMENT;
    return eco_filter_create(((EcoDSP*)me)->mem, ((EcoDSP*)me)->algorithm, kernel, out);
}
static int16_t ECOCALLMETHOD dsp_filter(IEcoDSP* me, IEcoVector* signal, IEcoVector* kernel, IEcoVector* out) {
    IEcoDigitalFilter* filter = 0;
    int16_t rc;
    if (!me || !signal || !kernel || !out) return ECO_E_ARGUMENT;
    rc = dsp_filter_create(me, kernel, &filter);
    if (!rc) {
        rc = filter->pVTbl->Apply(filter, signal, out);
        filter->pVTbl->Release(filter);
    }
    return rc;
}
static long double ECOCALLMETHOD dsp_add(IEcoDSP* me, long double a, long double b) {
    return me ? a + b : 0;
}
static long double ECOCALLMETHOD dsp_subtract(IEcoDSP* me, long double a, long double b) {
    return me ? a - b : 0;
}
static uint32_t ECOCALLMETHOD dsp_algorithm(IEcoDSP* me) { return me ? ((EcoDSP*)me)->algorithm : 0xFFFFFFFFU; }
static uint32_t ECOCALLMETHOD dsp_bytes(IEcoDSP* me) { return me ? (uint32_t)sizeof(EcoDSP) : 0; }
static IEcoDSPVTbl dsp_vtbl = {
    dsp_query, dsp_addref, dsp_release,
    dsp_fft, dsp_ifft, dsp_fftf, dsp_ifftf, dsp_fftl, dsp_ifftl,
    dsp_ffti, dsp_iffti, dsp_fftlong, dsp_ifftlong,
    dsp_vector, dsp_matrix, dsp_plan, dsp_filter_create, dsp_filter,
    dsp_add, dsp_subtract, dsp_algorithm, dsp_bytes
};
int16_t eco_dsp_create(IEcoMemoryAllocator1* mem, uint32_t algorithm, IEcoDSP** out) {
    EcoDSP* self;
    if (!out) return ECO_E_ARGUMENT;
    *out = 0;
    if (!mem || !mem->pVTbl || !mem->pVTbl->Alloc || !mem->pVTbl->Free ||
        !mem->pVTbl->AddRef || !mem->pVTbl->Release || algorithm > ECO_FFT_DIF) return ECO_E_ARGUMENT;
    self = (EcoDSP*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*self));
    if (!self) return ECO_E_MEMORY;
    self->base.pVTbl = &dsp_vtbl;
    self->calculator.pVTbl = &calculator_vtbl;
    self->refs = 1;
    self->mem = mem;
    self->algorithm = algorithm;
    mem->pVTbl->AddRef(mem);
    *out = &self->base;
    return ECO_OK;
}
