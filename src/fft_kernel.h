#ifndef ECO_FFT_KERNEL_H
#define ECO_FFT_KERNEL_H

#include "IEcoBase1.h"
#include "IEcoMemoryAllocator1.h"
#include "IEcoDSP.h"

/* Private numerical plan. A plan owns its allocator reference and all tables.
 * Algorithm 0 is radix-2 DIT; algorithm 1 is radix-2 DIF. Non-power-of-two
 * lengths use Bluestein convolution with the selected radix-2 algorithm.
 * Supported lengths: 1..65536. Forward sign is negative; inverse divides by N.
 * Input/output may alias, including partially overlapping ranges. Every real
 * and imaginary input component must lie in [-1e100, 1e100]; NaN and infinity
 * are rejected. This conservative bound also prevents intermediate overflow
 * on targets where long double has only the range of double.
 * Plans are mutable scratch owners: externally synchronize shared-plan use.
 * Status: 0 success, -1 bad argument, -2 allocation failure, -3 range error,
 * -4 incomplete/busy plan. Output is untouched when argument checks fail.
 */
typedef struct EcoFFTKernel EcoFFTKernel;

int16_t eco_fft_kernel_create(IEcoMemoryAllocator1* mem, uint32_t n,
                             uint32_t algo, EcoFFTKernel** out);
int16_t eco_fft_kernel_apply(EcoFFTKernel* plan, const EcoComplexL* input,
                            EcoComplexL* output, int inverse);
void eco_fft_kernel_destroy(EcoFFTKernel* plan);
/* Requested allocation bytes, including the plan, excluding allocator metadata. */
uint32_t eco_fft_kernel_bytes(const EcoFFTKernel* plan);

#endif
