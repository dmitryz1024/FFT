#include "fft_kernel.h"

#define ECO_FFT_MAX_BYTES 0xFFFFFFFFU
#define ECO_FFT_PI 3.141592653589793238462643383279502884L
#define ECO_FFT_INPUT_LIMIT 1.0e100L

struct EcoFFTKernel {
    IEcoMemoryAllocator1* mem;
    uint32_t n;
    uint32_t m;
    uint32_t algo;
    uint32_t bytes;
    int16_t ready;
    int16_t busy;
    EcoComplexL* twiddle;
    EcoComplexL* chirp;
    EcoComplexL* convolution;
    EcoComplexL* scratch;
};

/* Angles are constructed in [0, 2*pi), never from a growing floating-point
 * recurrence. Reduce to [-pi/4, pi/4] before independently evaluating each
 * sine and cosine with 12 Taylor updates. The truncation error is below
 * 3e-29; roundoff in the target long-double format dominates that bound.
 */
static void eco_fft_sincos(long double angle, long double* sine, long double* cosine) {
    long double x;
    long double square;
    long double s;
    long double c;
    long double ts;
    long double tc;
    uint32_t quadrant;
    uint32_t j;

    if (angle <= ECO_FFT_PI * 0.25L) {
        quadrant = 0U;
        x = angle;
    }
    else if (angle <= ECO_FFT_PI * 0.75L) {
        quadrant = 1U;
        x = angle - ECO_FFT_PI * 0.5L;
    }
    else if (angle <= ECO_FFT_PI * 1.25L) {
        quadrant = 2U;
        x = angle - ECO_FFT_PI;
    }
    else if (angle <= ECO_FFT_PI * 1.75L) {
        quadrant = 3U;
        x = angle - ECO_FFT_PI * 1.5L;
    }
    else {
        quadrant = 0U;
        x = angle - ECO_FFT_PI * 2.0L;
    }
    square = x * x;
    s = x;
    c = 1.0L;
    ts = x;
    tc = 1.0L;
    for (j = 1U; j <= 12U; ++j) {
        ts *= -square / (long double)((2U * j) * (2U * j + 1U));
        tc *= -square / (long double)((2U * j - 1U) * (2U * j));
        s += ts;
        c += tc;
    }
    if (quadrant == 0U) {
        *sine = s;
        *cosine = c;
    }
    else if (quadrant == 1U) {
        *sine = c;
        *cosine = -s;
    }
    else if (quadrant == 2U) {
        *sine = -s;
        *cosine = -c;
    }
    else {
        *sine = -c;
        *cosine = s;
    }
}

static EcoComplexL eco_fft_product(EcoComplexL a, EcoComplexL b) {
    EcoComplexL result;
    result.re = a.re * b.re - a.im * b.im;
    result.im = a.re * b.im + a.im * b.re;
    return result;
}

static void eco_fft_reverse(EcoComplexL* data, uint32_t n) {
    uint32_t i;
    uint32_t reversed;
    uint32_t bit;
    EcoComplexL tmp;

    reversed = 0U;
    for (i = 1U; i < n; ++i) {
        bit = n >> 1U;
        while ((reversed & bit) != 0U) {
            reversed ^= bit;
            bit >>= 1U;
        }
        reversed ^= bit;
        if (i < reversed) {
            tmp = data[i];
            data[i] = data[reversed];
            data[reversed] = tmp;
        }
    }
}

/* In-place radix-2 transforms. DIT permutes the input before ascending
 * butterfly stages; DIF performs descending stages and permutes the output.
 * Both expose natural-order input/output and use the same precomputed roots.
 */
static void eco_fft_radix(EcoFFTKernel* plan, EcoComplexL* data,
                          int16_t inverse) {
    uint32_t length;
    uint32_t half;
    uint32_t stride;
    uint32_t start;
    uint32_t j;
    uint32_t k;
    EcoComplexL a;
    EcoComplexL b;
    EcoComplexL w;
    EcoComplexL value;
    long double scale;

    if (plan->algo == 0U) {
        eco_fft_reverse(data, plan->m);
        for (length = 2U; length <= plan->m; length <<= 1U) {
            half = length >> 1U;
            stride = plan->m / length;
            for (start = 0U; start < plan->m; start += length) {
                for (j = 0U; j < half; ++j) {
                    w = plan->twiddle[j * stride];
                    if (inverse != 0) {
                        w.im = -w.im;
                    }
                    a = data[start + j];
                    b = eco_fft_product(data[start + j + half], w);
                    data[start + j].re = a.re + b.re;
                    data[start + j].im = a.im + b.im;
                    data[start + j + half].re = a.re - b.re;
                    data[start + j + half].im = a.im - b.im;
                }
            }
        }
    }
    else {
        for (length = plan->m; length >= 2U; length >>= 1U) {
            half = length >> 1U;
            stride = plan->m / length;
            for (start = 0U; start < plan->m; start += length) {
                for (j = 0U; j < half; ++j) {
                    w = plan->twiddle[j * stride];
                    if (inverse != 0) {
                        w.im = -w.im;
                    }
                    a = data[start + j];
                    b = data[start + j + half];
                    data[start + j].re = a.re + b.re;
                    data[start + j].im = a.im + b.im;
                    value.re = a.re - b.re;
                    value.im = a.im - b.im;
                    data[start + j + half] = eco_fft_product(value, w);
                }
            }
        }
        eco_fft_reverse(data, plan->m);
    }
    if (inverse != 0) {
        scale = 1.0L / (long double)plan->m;
        for (k = 0U; k < plan->m; ++k) {
            data[k].re *= scale;
            data[k].im *= scale;
        }
    }
}

static int16_t eco_fft_array(EcoFFTKernel* plan, uint32_t count,
                             EcoComplexL** output) {
    uint32_t bytes;

    if (count > ECO_FFT_MAX_BYTES / (uint32_t)sizeof(EcoComplexL)) {
        return -3;
    }
    bytes = count * (uint32_t)sizeof(EcoComplexL);
    if (plan->bytes > ECO_FFT_MAX_BYTES - bytes) {
        return -3;
    }
    *output = (EcoComplexL*)plan->mem->pVTbl->Alloc(plan->mem, bytes);
    if (*output == 0) {
        return -2;
    }
    plan->bytes += bytes;
    return 0;
}

int16_t eco_fft_kernel_create(IEcoMemoryAllocator1* mem, uint32_t n,
                             uint32_t algo, EcoFFTKernel** output) {
    EcoFFTKernel* plan;
    uint32_t m;
    uint32_t i;
    uint32_t twiddle_count;
    uint32_t phase;
    int16_t status;
    int16_t bluestein;
    long double s;
    long double c;
    long double angle;

    if (output == 0) {
        return -1;
    }
    *output = 0;
    if (mem == 0 || mem->pVTbl == 0 || mem->pVTbl->Alloc == 0 ||
        mem->pVTbl->Free == 0 || mem->pVTbl->AddRef == 0 ||
        mem->pVTbl->Release == 0 || algo > 1U) {
        return -1;
    }
    if (n == 0U || n > ECO_FFT_MAX_N) {
        return -3;
    }
    bluestein = (int16_t)((n & (n - 1U)) != 0U);
    m = n;
    if (bluestein != 0) {
        m = 1U;
        while (m < 2U * n - 1U) {
            m <<= 1U;
        }
    }
    plan = (EcoFFTKernel*)mem->pVTbl->Alloc(mem, (uint32_t)sizeof(*plan));
    if (plan == 0) {
        return -2;
    }
    plan->mem = mem;
    plan->n = n;
    plan->m = m;
    plan->algo = algo;
    plan->bytes = (uint32_t)sizeof(*plan);
    plan->ready = 0;
    plan->busy = 0;
    plan->twiddle = 0;
    plan->chirp = 0;
    plan->convolution = 0;
    plan->scratch = 0;
    mem->pVTbl->AddRef(mem);
    twiddle_count = m > 1U ? m / 2U : 1U;
    status = eco_fft_array(plan, twiddle_count, &plan->twiddle);
    if (status == 0) {
        status = eco_fft_array(plan, m, &plan->scratch);
    }
    if (status == 0 && bluestein != 0) {
        status = eco_fft_array(plan, n, &plan->chirp);
    }
    if (status == 0 && bluestein != 0) {
        status = eco_fft_array(plan, m, &plan->convolution);
    }
    if (status != 0) {
        eco_fft_kernel_destroy(plan);
        return status;
    }
    for (i = 0U; i < twiddle_count; ++i) {
        angle = (2.0L * ECO_FFT_PI * (long double)i) / (long double)m;
        eco_fft_sincos(angle, &s, &c);
        plan->twiddle[i].re = c;
        plan->twiddle[i].im = -s;
    }
    if (bluestein != 0) {
        for (i = 0U; i < m; ++i) {
            plan->convolution[i].re = 0.0L;
            plan->convolution[i].im = 0.0L;
        }
        for (i = 0U; i < n; ++i) {
            /* i <= 65535, so i*i fits in uint32_t. Reducing the integer
             * phase modulo 2*N avoids large-angle precision loss.
             */
            phase = (i * i) % (2U * n);
            angle = ECO_FFT_PI * (long double)phase / (long double)n;
            eco_fft_sincos(angle, &s, &c);
            plan->chirp[i].re = c;
            plan->chirp[i].im = -s;
            plan->convolution[i].re = c;
            plan->convolution[i].im = s;
            if (i != 0U) {
                plan->convolution[m - i] = plan->convolution[i];
            }
        }
        eco_fft_radix(plan, plan->convolution, 0);
    }
    plan->ready = 1;
    *output = plan;
    return 0;
}

int16_t eco_fft_kernel_apply(EcoFFTKernel* plan, const EcoComplexL* input,
                            EcoComplexL* output, int inverse) {
    uint32_t i;
    EcoComplexL value;
    long double scale;

    if (plan == 0 || input == 0 || output == 0 ||
        (inverse != 0 && inverse != 1)) {
        return -1;
    }
    if (plan->ready == 0 || plan->busy != 0) {
        return -4;
    }
    /* Ordered comparisons reject NaN, as well as infinity and excess range.
     * Finish validation before any output write to make errors transactional.
     */
    for (i = 0U; i < plan->n; ++i) {
        if (!(input[i].re >= -ECO_FFT_INPUT_LIMIT &&
              input[i].re <= ECO_FFT_INPUT_LIMIT &&
              input[i].im >= -ECO_FFT_INPUT_LIMIT &&
              input[i].im <= ECO_FFT_INPUT_LIMIT)) {
            return -3;
        }
    }
    plan->busy = 1;
    if (plan->chirp == 0) {
        for (i = 0U; i < plan->n; ++i) {
            plan->scratch[i] = input[i];
        }
        eco_fft_radix(plan, plan->scratch, (int16_t)inverse);
        for (i = 0U; i < plan->n; ++i) {
            output[i] = plan->scratch[i];
        }
    }
    else {
        /* Bluestein: X[k] = chirp[k] * convolution(x*chirp, conj(chirp)).
         * For inverse transforms use conj(FFT(conj(x)))/N. The convolution
         * inverse independently divides by M, the padded convolution size.
         */
        for (i = 0U; i < plan->n; ++i) {
            value = input[i];
            if (inverse != 0) {
                value.im = -value.im;
            }
            plan->scratch[i] = eco_fft_product(value, plan->chirp[i]);
        }
        for (i = plan->n; i < plan->m; ++i) {
            plan->scratch[i].re = 0.0L;
            plan->scratch[i].im = 0.0L;
        }
        eco_fft_radix(plan, plan->scratch, 0);
        for (i = 0U; i < plan->m; ++i) {
            plan->scratch[i] = eco_fft_product(plan->scratch[i],
                                               plan->convolution[i]);
        }
        eco_fft_radix(plan, plan->scratch, 1);
        scale = inverse != 0 ? 1.0L / (long double)plan->n : 1.0L;
        for (i = 0U; i < plan->n; ++i) {
            value = eco_fft_product(plan->scratch[i], plan->chirp[i]);
            output[i].re = value.re * scale;
            output[i].im = inverse != 0 ? -value.im * scale : value.im;
        }
    }
    plan->busy = 0;
    return 0;
}

void eco_fft_kernel_destroy(EcoFFTKernel* plan) {
    IEcoMemoryAllocator1* mem;

    if (plan == 0) {
        return;
    }
    mem = plan->mem;
    if (plan->twiddle != 0) {
        mem->pVTbl->Free(mem, plan->twiddle);
    }
    if (plan->chirp != 0) {
        mem->pVTbl->Free(mem, plan->chirp);
    }
    if (plan->convolution != 0) {
        mem->pVTbl->Free(mem, plan->convolution);
    }
    if (plan->scratch != 0) {
        mem->pVTbl->Free(mem, plan->scratch);
    }
    mem->pVTbl->Free(mem, plan);
    mem->pVTbl->Release(mem);
}

uint32_t eco_fft_kernel_bytes(const EcoFFTKernel* plan) {
    return plan == 0 ? 0U : plan->bytes;
}
