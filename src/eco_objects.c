#include "eco_internal.h"

/* The interface is the first member, so the standard C initial-member pointer
 * conversion obtains the owning object without offsets or pointer arithmetic.
 * Every object owns one allocator reference; each numerical kernel owns its
 * own reference. MemoryBytes includes requested bytes, never allocator metadata.
 */
typedef struct EcoVectorObject {
    IEcoVector iface;
    IEcoMemoryAllocator1* mem;
    uint32_t refs;
    uint32_t n;
    EcoComplexL* data;
} EcoVectorObject;

typedef struct EcoMatrixObject {
    IEcoMatrix iface;
    IEcoMemoryAllocator1* mem;
    uint32_t refs;
    uint32_t rows;
    uint32_t cols;
    EcoComplexL* data;
} EcoMatrixObject;

typedef struct EcoPlanObject {
    IEcoFFTPlan iface;
    IEcoMemoryAllocator1* mem;
    uint32_t refs;
    uint32_t n;
    int16_t loaded;
    EcoFFTKernel* kernel;
    EcoComplexL* signal;
    EcoComplexL* staging;
} EcoPlanObject;

typedef struct EcoFilterObject {
    IEcoDigitalFilter iface;
    IEcoMemoryAllocator1* mem;
    uint32_t refs;
    uint32_t n;
    EcoFFTKernel* kernel;
    EcoComplexL* spectrum;
    EcoComplexL* scratch;
} EcoFilterObject;

static int allocator_valid(IEcoMemoryAllocator1* mem) {
    return mem != 0 && mem->pVTbl != 0 &&
           mem->pVTbl->Alloc != 0 && mem->pVTbl->Free != 0 &&
           mem->pVTbl->AddRef != 0 && mem->pVTbl->Release != 0;
}

static int vector_readable(IEcoVector* vector) {
    return vector != 0 && vector->pVTbl != 0 &&
           vector->pVTbl->Size != 0 && vector->pVTbl->Get != 0;
}

static int vector_writable(IEcoVector* vector) {
    return vector != 0 && vector->pVTbl != 0 &&
           vector->pVTbl->Size != 0 && vector->pVTbl->Set != 0;
}

static void zero_array(EcoComplexL* values, uint32_t n) {
    uint32_t i;
    for (i = 0U; i < n; ++i) {
        values[i].re = 0.0L;
        values[i].im = 0.0L;
    }
}

static int16_t vector_read(IEcoVector* vector, uint32_t n,
                           EcoComplexL* destination) {
    uint32_t i;
    int16_t status;
    EcoComplexL value;
    if (!vector_readable(vector) || destination == 0) return ECO_E_ARGUMENT;
    if (vector->pVTbl->Size(vector) != n) return ECO_E_RANGE;
    for (i = 0U; i < n; ++i) {
        status = vector->pVTbl->Get(vector, i, &value);
        if (status != ECO_OK) return status;
        if (!eco_value_valid(value)) return ECO_E_RANGE;
        destination[i].re = value.re;
        destination[i].im = value.im;
    }
    return ECO_OK;
}

static int16_t vector_write(IEcoVector* vector, uint32_t n,
                            const EcoComplexL* source) {
    uint32_t i;
    int16_t status;
    if (!vector_writable(vector) || source == 0) return ECO_E_ARGUMENT;
    if (vector->pVTbl->Size(vector) != n) return ECO_E_RANGE;
    /* Range failures are detected before the first Set. An external vector's
     * own Set implementation may still fail; propagate its status faithfully.
     */
    for (i = 0U; i < n; ++i) {
        if (!eco_value_valid(source[i])) return ECO_E_RANGE;
    }
    for (i = 0U; i < n; ++i) {
        status = vector->pVTbl->Set(vector, i, source[i]);
        if (status != ECO_OK) return status;
    }
    return ECO_OK;
}

static void vector_destroy(EcoVectorObject* object) {
    IEcoMemoryAllocator1* mem = object->mem;
    if (object->data != 0) mem->pVTbl->Free(mem, object->data);
    mem->pVTbl->Free(mem, object);
    mem->pVTbl->Release(mem);
}

static void matrix_destroy(EcoMatrixObject* object) {
    IEcoMemoryAllocator1* mem = object->mem;
    if (object->data != 0) mem->pVTbl->Free(mem, object->data);
    mem->pVTbl->Free(mem, object);
    mem->pVTbl->Release(mem);
}

static void plan_destroy(EcoPlanObject* object) {
    IEcoMemoryAllocator1* mem = object->mem;
    eco_fft_kernel_destroy(object->kernel);
    if (object->signal != 0) mem->pVTbl->Free(mem, object->signal);
    if (object->staging != 0) mem->pVTbl->Free(mem, object->staging);
    mem->pVTbl->Free(mem, object);
    mem->pVTbl->Release(mem);
}

static void filter_destroy(EcoFilterObject* object) {
    IEcoMemoryAllocator1* mem = object->mem;
    eco_fft_kernel_destroy(object->kernel);
    if (object->spectrum != 0) mem->pVTbl->Free(mem, object->spectrum);
    if (object->scratch != 0) mem->pVTbl->Free(mem, object->scratch);
    mem->pVTbl->Free(mem, object);
    mem->pVTbl->Release(mem);
}

/* All four objects expose exactly their own interface and IEcoUnknown.
 * Their identical pointer is the canonical unknown identity. Each successful
 * query adds one reference, and unsuccessful queries clear the output.
 */
#define ECO_OBJECT_UNKNOWN(prefix, Interface, Object, interface_id) \
    static uint32_t ECOCALLMETHOD prefix##_addref(Interface* me) { \
        Object* object = (Object*)me; \
        if (object == 0) return 0U; \
        if (object->refs != 0xFFFFFFFFU) ++object->refs; \
        return object->refs; \
    } \
    static uint32_t ECOCALLMETHOD prefix##_release(Interface* me) { \
        Object* object = (Object*)me; \
        uint32_t remaining; \
        if (object == 0) return 0U; \
        remaining = --object->refs; \
        if (remaining == 0U) prefix##_destroy(object); \
        return remaining; \
    } \
    static int16_t ECOCALLMETHOD prefix##_query(Interface* me, \
                                               const UGUID* iid, void** out) { \
        if (out == 0) return ECO_E_ARGUMENT; \
        *out = 0; \
        if (me == 0 || iid == 0) return ECO_E_ARGUMENT; \
        if (!eco_guid_equal(iid, &(interface_id)) && \
            !eco_guid_equal(iid, &IID_IEcoUnknown)) return ECO_E_INTERFACE; \
        prefix##_addref(me); \
        *out = me; \
        return ECO_OK; \
    }

ECO_OBJECT_UNKNOWN(vector, IEcoVector, EcoVectorObject, IID_IEcoVector)
ECO_OBJECT_UNKNOWN(matrix, IEcoMatrix, EcoMatrixObject, IID_IEcoMatrix)
ECO_OBJECT_UNKNOWN(plan, IEcoFFTPlan, EcoPlanObject, IID_IEcoFFTPlan)
ECO_OBJECT_UNKNOWN(filter, IEcoDigitalFilter, EcoFilterObject, IID_IEcoDigitalFilter)
#undef ECO_OBJECT_UNKNOWN

static uint32_t ECOCALLMETHOD vector_size(IEcoVector* me) {
    return me == 0 ? 0U : ((EcoVectorObject*)me)->n;
}

static int16_t ECOCALLMETHOD vector_get(IEcoVector* me, uint32_t i,
                                       EcoComplexL* value) {
    EcoVectorObject* object = (EcoVectorObject*)me;
    if (object == 0 || value == 0) return ECO_E_ARGUMENT;
    if (i >= object->n) return ECO_E_RANGE;
    value->re = object->data[i].re;
    value->im = object->data[i].im;
    return ECO_OK;
}

static int16_t ECOCALLMETHOD vector_set(IEcoVector* me, uint32_t i,
                                       EcoComplexL value) {
    EcoVectorObject* object = (EcoVectorObject*)me;
    if (object == 0) return ECO_E_ARGUMENT;
    if (i >= object->n || !eco_value_valid(value)) return ECO_E_RANGE;
    object->data[i].re = value.re;
    object->data[i].im = value.im;
    return ECO_OK;
}

static uint32_t ECOCALLMETHOD vector_bytes(IEcoVector* me) {
    EcoVectorObject* object = (EcoVectorObject*)me;
    return object == 0 ? 0U : (uint32_t)sizeof(*object) +
           object->n * (uint32_t)sizeof(EcoComplexL);
}

static IEcoVectorVTbl vector_vtable = {
    vector_query, vector_addref, vector_release,
    vector_size, vector_get, vector_set, vector_bytes
};

int16_t eco_vector_create(IEcoMemoryAllocator1* mem, uint32_t n,
                          IEcoVector** out) {
    EcoVectorObject* object;
    if (out == 0) return ECO_E_ARGUMENT;
    *out = 0;
    if (!allocator_valid(mem)) return ECO_E_ARGUMENT;
    if (n == 0U || n > ECO_FFT_MAX_N) return ECO_E_RANGE;
    object = (EcoVectorObject*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*object));
    if (object == 0) return ECO_E_MEMORY;
    object->iface.pVTbl = &vector_vtable;
    object->mem = mem;
    object->refs = 1U;
    object->n = n;
    object->data = 0;
    mem->pVTbl->AddRef(mem);
    object->data = (EcoComplexL*)mem->pVTbl->Alloc(
        mem, n * (uint32_t)sizeof(EcoComplexL));
    if (object->data == 0) {
        vector_destroy(object);
        return ECO_E_MEMORY;
    }
    zero_array(object->data, n);
    *out = &object->iface;
    return ECO_OK;
}

static uint32_t ECOCALLMETHOD matrix_rows(IEcoMatrix* me) {
    return me == 0 ? 0U : ((EcoMatrixObject*)me)->rows;
}

static uint32_t ECOCALLMETHOD matrix_cols(IEcoMatrix* me) {
    return me == 0 ? 0U : ((EcoMatrixObject*)me)->cols;
}

static int16_t ECOCALLMETHOD matrix_get(IEcoMatrix* me, uint32_t row,
                                       uint32_t col, EcoComplexL* value) {
    EcoMatrixObject* object = (EcoMatrixObject*)me;
    uint32_t index;
    if (object == 0 || value == 0) return ECO_E_ARGUMENT;
    if (row >= object->rows || col >= object->cols) return ECO_E_RANGE;
    index = row * object->cols + col;
    value->re = object->data[index].re;
    value->im = object->data[index].im;
    return ECO_OK;
}

static int16_t ECOCALLMETHOD matrix_set(IEcoMatrix* me, uint32_t row,
                                       uint32_t col, EcoComplexL value) {
    EcoMatrixObject* object = (EcoMatrixObject*)me;
    uint32_t index;
    if (object == 0) return ECO_E_ARGUMENT;
    if (row >= object->rows || col >= object->cols || !eco_value_valid(value))
        return ECO_E_RANGE;
    index = row * object->cols + col;
    object->data[index].re = value.re;
    object->data[index].im = value.im;
    return ECO_OK;
}

static uint32_t ECOCALLMETHOD matrix_bytes(IEcoMatrix* me) {
    EcoMatrixObject* object = (EcoMatrixObject*)me;
    return object == 0 ? 0U : (uint32_t)sizeof(*object) +
           object->rows * object->cols * (uint32_t)sizeof(EcoComplexL);
}

static IEcoMatrixVTbl matrix_vtable = {
    matrix_query, matrix_addref, matrix_release,
    matrix_rows, matrix_cols, matrix_get, matrix_set, matrix_bytes
};

int16_t eco_matrix_create(IEcoMemoryAllocator1* mem, uint32_t rows,
                          uint32_t cols, IEcoMatrix** out) {
    EcoMatrixObject* object;
    uint32_t n;
    if (out == 0) return ECO_E_ARGUMENT;
    *out = 0;
    if (!allocator_valid(mem)) return ECO_E_ARGUMENT;
    /* Division before multiplication avoids overflow even for UINT32_MAX. */
    if (rows == 0U || cols == 0U || rows > ECO_FFT_MAX_N / cols)
        return ECO_E_RANGE;
    n = rows * cols;
    object = (EcoMatrixObject*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*object));
    if (object == 0) return ECO_E_MEMORY;
    object->iface.pVTbl = &matrix_vtable;
    object->mem = mem;
    object->refs = 1U;
    object->rows = rows;
    object->cols = cols;
    object->data = 0;
    mem->pVTbl->AddRef(mem);
    object->data = (EcoComplexL*)mem->pVTbl->Alloc(
        mem, n * (uint32_t)sizeof(EcoComplexL));
    if (object->data == 0) {
        matrix_destroy(object);
        return ECO_E_MEMORY;
    }
    zero_array(object->data, n);
    *out = &object->iface;
    return ECO_OK;
}

static int16_t ECOCALLMETHOD plan_load(IEcoFFTPlan* me, IEcoVector* signal) {
    EcoPlanObject* object = (EcoPlanObject*)me;
    uint32_t i;
    int16_t status;
    if (object == 0) return ECO_E_ARGUMENT;
    /* Read and validate into staging first: invalid size/value/Get failures
     * leave the old signal and the loaded flag unchanged.
     */
    status = vector_read(signal, object->n, object->staging);
    if (status != ECO_OK) return status;
    for (i = 0U; i < object->n; ++i) {
        object->signal[i].re = object->staging[i].re;
        object->signal[i].im = object->staging[i].im;
    }
    object->loaded = 1;
    return ECO_OK;
}

static int16_t plan_transform(EcoPlanObject* object, int inverse) {
    uint32_t i;
    int16_t status;
    if (object == 0) return ECO_E_ARGUMENT;
    if (object->loaded == 0) return ECO_E_STATE;
    status = eco_fft_kernel_apply(object->kernel, object->signal,
                                  object->staging, inverse);
    if (status != ECO_OK) return status;
    /* Maintain the vector-value invariant for subsequent Store/FFT/IFFT.
     * A spectrum exceeding the documented representable object range fails
     * without replacing the old signal.
     */
    for (i = 0U; i < object->n; ++i) {
        if (!eco_value_valid(object->staging[i])) return ECO_E_RANGE;
    }
    for (i = 0U; i < object->n; ++i) {
        object->signal[i].re = object->staging[i].re;
        object->signal[i].im = object->staging[i].im;
    }
    return ECO_OK;
}

static int16_t ECOCALLMETHOD plan_fft(IEcoFFTPlan* me) {
    return plan_transform((EcoPlanObject*)me, 0);
}

static int16_t ECOCALLMETHOD plan_ifft(IEcoFFTPlan* me) {
    return plan_transform((EcoPlanObject*)me, 1);
}

static int16_t ECOCALLMETHOD plan_store(IEcoFFTPlan* me, IEcoVector* signal) {
    EcoPlanObject* object = (EcoPlanObject*)me;
    if (object == 0 || !vector_writable(signal)) return ECO_E_ARGUMENT;
    if (object->loaded == 0) return ECO_E_STATE;
    return vector_write(signal, object->n, object->signal);
}

static uint32_t ECOCALLMETHOD plan_size(IEcoFFTPlan* me) {
    return me == 0 ? 0U : ((EcoPlanObject*)me)->n;
}

static uint32_t ECOCALLMETHOD plan_bytes(IEcoFFTPlan* me) {
    EcoPlanObject* object = (EcoPlanObject*)me;
    return object == 0 ? 0U : (uint32_t)sizeof(*object) +
           2U * object->n * (uint32_t)sizeof(EcoComplexL) +
           eco_fft_kernel_bytes(object->kernel);
}

static IEcoFFTPlanVTbl plan_vtable = {
    plan_query, plan_addref, plan_release,
    plan_load, plan_fft, plan_ifft, plan_store, plan_size, plan_bytes
};

int16_t eco_plan_create(IEcoMemoryAllocator1* mem, uint32_t n,
                        uint32_t algo, IEcoFFTPlan** out) {
    EcoPlanObject* object;
    int16_t status;
    if (out == 0) return ECO_E_ARGUMENT;
    *out = 0;
    if (!allocator_valid(mem) || algo > ECO_FFT_DIF) return ECO_E_ARGUMENT;
    if (n == 0U || n > ECO_FFT_MAX_N) return ECO_E_RANGE;
    object = (EcoPlanObject*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*object));
    if (object == 0) return ECO_E_MEMORY;
    object->iface.pVTbl = &plan_vtable;
    object->mem = mem;
    object->refs = 1U;
    object->n = n;
    object->loaded = 0;
    object->kernel = 0;
    object->signal = 0;
    object->staging = 0;
    mem->pVTbl->AddRef(mem);
    status = eco_fft_kernel_create(mem, n, algo, &object->kernel);
    if (status == ECO_OK) {
        object->signal = (EcoComplexL*)mem->pVTbl->Alloc(
            mem, n * (uint32_t)sizeof(EcoComplexL));
        if (object->signal == 0) status = ECO_E_MEMORY;
    }
    if (status == ECO_OK) {
        object->staging = (EcoComplexL*)mem->pVTbl->Alloc(
            mem, n * (uint32_t)sizeof(EcoComplexL));
        if (object->staging == 0) status = ECO_E_MEMORY;
    }
    if (status != ECO_OK) {
        plan_destroy(object);
        return status;
    }
    zero_array(object->signal, n);
    zero_array(object->staging, n);
    *out = &object->iface;
    return ECO_OK;
}

static int16_t ECOCALLMETHOD filter_apply(IEcoDigitalFilter* me,
                                          IEcoVector* signal,
                                          IEcoVector* result) {
    EcoFilterObject* object = (EcoFilterObject*)me;
    uint32_t i;
    int16_t status;
    EcoComplexL value;
    if (object == 0 || !vector_writable(result)) return ECO_E_ARGUMENT;
    if (result->pVTbl->Size(result) != object->n) return ECO_E_RANGE;
    status = vector_read(signal, object->n, object->scratch);
    if (status != ECO_OK) return status;
    status = eco_fft_kernel_apply(object->kernel, object->scratch,
                                  object->scratch, 0);
    if (status != ECO_OK) return status;
    for (i = 0U; i < object->n; ++i) {
        value.re = object->scratch[i].re * object->spectrum[i].re -
                   object->scratch[i].im * object->spectrum[i].im;
        value.im = object->scratch[i].re * object->spectrum[i].im +
                   object->scratch[i].im * object->spectrum[i].re;
        /* Each input component was <= 1e100 and N <= 65536. Spectral
         * multiplication cannot overflow a double-range long double, but
         * may exceed the numerical kernel's conservative input limit.
         * Reject that case before invoking IFFT or writing to the caller.
         */
        if (!eco_value_valid(value)) return ECO_E_RANGE;
        object->scratch[i].re = value.re;
        object->scratch[i].im = value.im;
    }
    status = eco_fft_kernel_apply(object->kernel, object->scratch,
                                  object->scratch, 1);
    if (status != ECO_OK) return status;
    /* All reads precede writes, so Apply(signal, signal) is supported. */
    return vector_write(result, object->n, object->scratch);
}

static uint32_t ECOCALLMETHOD filter_size(IEcoDigitalFilter* me) {
    return me == 0 ? 0U : ((EcoFilterObject*)me)->n;
}

static uint32_t ECOCALLMETHOD filter_bytes(IEcoDigitalFilter* me) {
    EcoFilterObject* object = (EcoFilterObject*)me;
    return object == 0 ? 0U : (uint32_t)sizeof(*object) +
           2U * object->n * (uint32_t)sizeof(EcoComplexL) +
           eco_fft_kernel_bytes(object->kernel);
}

static IEcoDigitalFilterVTbl filter_vtable = {
    filter_query, filter_addref, filter_release,
    filter_apply, filter_size, filter_bytes
};

int16_t eco_filter_create(IEcoMemoryAllocator1* mem, uint32_t algo,
                          IEcoVector* coefficients, IEcoDigitalFilter** out) {
    EcoFilterObject* object;
    uint32_t n;
    int16_t status;
    if (out == 0) return ECO_E_ARGUMENT;
    *out = 0;
    if (!allocator_valid(mem) || algo > ECO_FFT_DIF ||
        !vector_readable(coefficients)) return ECO_E_ARGUMENT;
    n = coefficients->pVTbl->Size(coefficients);
    if (n == 0U || n > ECO_FFT_MAX_N) return ECO_E_RANGE;
    object = (EcoFilterObject*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*object));
    if (object == 0) return ECO_E_MEMORY;
    object->iface.pVTbl = &filter_vtable;
    object->mem = mem;
    object->refs = 1U;
    object->n = n;
    object->kernel = 0;
    object->spectrum = 0;
    object->scratch = 0;
    mem->pVTbl->AddRef(mem);
    status = eco_fft_kernel_create(mem, n, algo, &object->kernel);
    if (status == ECO_OK) {
        object->spectrum = (EcoComplexL*)mem->pVTbl->Alloc(
            mem, n * (uint32_t)sizeof(EcoComplexL));
        if (object->spectrum == 0) status = ECO_E_MEMORY;
    }
    if (status == ECO_OK) {
        object->scratch = (EcoComplexL*)mem->pVTbl->Alloc(
            mem, n * (uint32_t)sizeof(EcoComplexL));
        if (object->scratch == 0) status = ECO_E_MEMORY;
    }
    if (status == ECO_OK) status = vector_read(coefficients, n, object->scratch);
    if (status == ECO_OK) {
        status = eco_fft_kernel_apply(object->kernel, object->scratch,
                                      object->spectrum, 0);
    }
    if (status != ECO_OK) {
        filter_destroy(object);
        return status;
    }
    zero_array(object->scratch, n);
    *out = &object->iface;
    return ECO_OK;
}
