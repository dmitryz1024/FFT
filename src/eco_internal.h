#ifndef ECO_FFT_INTERNAL_H
#define ECO_FFT_INTERNAL_H
#include "IEcoDSP.h"
#include "fft_kernel.h"

static inline int eco_guid_equal(const UGUID* a, const UGUID* b) {
    uint32_t i;
    if (!a || !b || a->Preamble != b->Preamble || a->Length != b->Length) return 0;
    for (i = 0; i < 16U; ++i) if (a->Data[i] != b->Data[i]) return 0;
    return 1;
}
static inline int eco_value_valid(EcoComplexL z) {
    return z.re == z.re && z.im == z.im &&
           z.re <= 1e100L && z.re >= -1e100L && z.im <= 1e100L && z.im >= -1e100L;
}
int16_t eco_vector_create(IEcoMemoryAllocator1*, uint32_t, IEcoVector**);
int16_t eco_matrix_create(IEcoMemoryAllocator1*, uint32_t, uint32_t, IEcoMatrix**);
int16_t eco_plan_create(IEcoMemoryAllocator1*, uint32_t, uint32_t, IEcoFFTPlan**);
int16_t eco_filter_create(IEcoMemoryAllocator1*, uint32_t, IEcoVector*, IEcoDigitalFilter**);
int16_t eco_dsp_create(IEcoMemoryAllocator1*, uint32_t, IEcoDSP**);
#endif
