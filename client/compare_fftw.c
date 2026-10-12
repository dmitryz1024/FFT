/* Optional measurement client. FFTW is never linked to the component or eco_fft.
 * Both implementations receive identical long-double complex samples.
 * FFTW_BACKWARD is explicitly normalized by N, including in timed operations. */
#include "IEcoDSP.h"
#include "IEcoSystem1.h"
#include "IEcoInterfaceBus1.h"
#include "IEcoCommandArguments1.h"
#include "IdEcoMemoryManager1.h"
#include <fftw3.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

enum { SOA, FFTW_FULL, OOA, FFTW_REUSE, ECO_SETUP, FFTW_SETUP, METRICS };
static const char* const metric_names[METRICS] = {
    "eco_soa", "fftw_full", "eco_ooa", "fftw_reuse", "eco_setup", "fftw_setup"
};
static const uint32_t sizes[] = {
    1, 2, 3, 5, 7, 8, 16, 31, 64, 127, 256, 257, 1023, 1024, 4096, 16384, 65535, 65536
};
static unsigned comparisons, failures;
static volatile long double observed;

typedef struct {
    uint32_t n;
    IEcoDSP* dsp;
    IEcoFFTPlan* plan;
    IEcoVector *input_vector, *output_vector;
    EcoComplexL *input, *eco_output, *reference;
    fftwl_complex *fftw_input, *fftw_output;
    fftwl_plan forward, backward;
} Comparison;

static double seconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(2); }
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void destroy(Comparison* c) {
    if (c->forward) fftwl_destroy_plan(c->forward);
    if (c->backward) fftwl_destroy_plan(c->backward);
    fftwl_free(c->fftw_input); fftwl_free(c->fftw_output);
    free(c->input); free(c->eco_output); free(c->reference);
    if (c->plan) c->plan->pVTbl->Release(c->plan);
    if (c->input_vector) c->input_vector->pVTbl->Release(c->input_vector);
    if (c->output_vector) c->output_vector->pVTbl->Release(c->output_vector);
}

static int create(Comparison* c, IEcoDSP* dsp, uint32_t n) {
    *c = (Comparison){0};
    c->dsp = dsp; c->n = n;
    c->input = malloc(n * sizeof(*c->input));
    c->eco_output = malloc(n * sizeof(*c->eco_output));
    c->reference = malloc(n * sizeof(*c->reference));
    c->fftw_input = fftwl_alloc_complex(n);
    c->fftw_output = fftwl_alloc_complex(n);
    if (!c->input || !c->eco_output || !c->reference || !c->fftw_input || !c->fftw_output) return 1;
    if (dsp->pVTbl->CreateVector(dsp, n, &c->input_vector) ||
        dsp->pVTbl->CreateVector(dsp, n, &c->output_vector) ||
        dsp->pVTbl->CreatePlan(dsp, n, &c->plan)) return 1;
    c->forward = fftwl_plan_dft_1d((int)n, c->fftw_input, c->fftw_output, FFTW_FORWARD, FFTW_ESTIMATE);
    c->backward = fftwl_plan_dft_1d((int)n, c->fftw_input, c->fftw_output, FFTW_BACKWARD, FFTW_ESTIMATE);
    return !c->forward || !c->backward;
}

static int signal_input(Comparison* c, unsigned kind) {
    uint32_t seed = 0x705171U;
    const long double tau = 6.283185307179586476925286766559005768L;
    for (uint32_t i = 0; i < c->n; ++i) {
        EcoComplexL x = {0, 0};
        if (kind == 0) x.re = i == 0 ? 1 : 0;
        else if (kind == 1) x.re = 2;
        else if (kind == 2) x.re = cosl(tau * i / c->n);
        else if (kind == 3) { x.re = cosl(tau * i / c->n); x.im = sinl(tau * i / c->n); }
        else {
            seed = 1664525U * seed + 1013904223U;
            x.re = (long double)(seed >> 8) / 16777216.0L - 0.5L;
            seed = 1664525U * seed + 1013904223U;
            x.im = (long double)(seed >> 8) / 16777216.0L - 0.5L;
        }
        c->input[i] = x;
        if (c->input_vector->pVTbl->Set(c->input_vector, i, x)) return 1;
    }
    return 0;
}

static void import_fftw(uint32_t n, const EcoComplexL* input, fftwl_complex* out) {
    for (uint32_t i = 0; i < n; ++i) { out[i][0] = input[i].re; out[i][1] = input[i].im; }
}

static void export_fftw(uint32_t n, fftwl_complex* input, EcoComplexL* out, int inverse) {
    const long double divisor = inverse ? n : 1;
    for (uint32_t i = 0; i < n; ++i) { out[i].re = input[i][0] / divisor; out[i].im = input[i][1] / divisor; }
}

static int operation(Comparison* c, unsigned metric, int inverse) {
    IEcoDSP* dsp = c->dsp;
    if (metric == SOA) return inverse ? dsp->pVTbl->ifftl(dsp, c->n, c->input, c->eco_output)
                                     : dsp->pVTbl->fftl(dsp, c->n, c->input, c->eco_output);
    if (metric == OOA) {
        if (c->plan->pVTbl->Load(c->plan, c->input_vector)) return 1;
        if (inverse ? c->plan->pVTbl->IFFT(c->plan) : c->plan->pVTbl->FFT(c->plan)) return 1;
        return c->plan->pVTbl->Store(c->plan, c->output_vector);
    }
    if (metric == ECO_SETUP) {
        IEcoFFTPlan* plan = 0;
        int rc = dsp->pVTbl->CreatePlan(dsp, c->n, &plan);
        if (plan) plan->pVTbl->Release(plan);
        return rc;
    }
    if (metric == FFTW_REUSE) {
        import_fftw(c->n, c->input, c->fftw_input);
        fftwl_execute(inverse ? c->backward : c->forward);
        export_fftw(c->n, c->fftw_output, c->reference, inverse);
        return 0;
    }
    if (metric == FFTW_SETUP) {
        fftwl_plan plan = fftwl_plan_dft_1d((int)c->n, c->fftw_input, c->fftw_output,
                                          inverse ? FFTW_BACKWARD : FFTW_FORWARD, FFTW_ESTIMATE);
        if (!plan) return 1;
        fftwl_destroy_plan(plan);
        return 0;
    }
    if (metric == FFTW_FULL) {
        fftwl_complex *input = fftwl_alloc_complex(c->n), *output = fftwl_alloc_complex(c->n);
        fftwl_plan plan = 0;
        int failed;
        if (input && output) plan = fftwl_plan_dft_1d((int)c->n, input, output,
                                                    inverse ? FFTW_BACKWARD : FFTW_FORWARD, FFTW_ESTIMATE);
        failed = plan == 0;
        if (plan) {
            import_fftw(c->n, c->input, input);
            fftwl_execute(plan);
            export_fftw(c->n, output, c->reference, inverse);
            fftwl_destroy_plan(plan);
        }
        fftwl_free(input); fftwl_free(output);
        return failed;
    }
    return 1;
}

static int compare_result(Comparison* c, FILE* csv, unsigned kind, int inverse, unsigned metric) {
    long double absolute = 0, scaled = 0;
    if (operation(c, metric, inverse)) return 1;
    for (uint32_t i = 0; i < c->n; ++i) {
        EcoComplexL x = c->eco_output[i];
        if (metric == OOA && c->output_vector->pVTbl->Get(c->output_vector, i, &x)) return 1;
        long double error = fmaxl(fabsl(x.re - c->reference[i].re), fabsl(x.im - c->reference[i].im));
        long double scale = fmaxl(1, fmaxl(fabsl(c->reference[i].re), fabsl(c->reference[i].im)));
        if (!isfinite(x.re) || !isfinite(x.im) || !isfinite(c->reference[i].re) || !isfinite(c->reference[i].im)) return 1;
        absolute = fmaxl(absolute, error); scaled = fmaxl(scaled, error / scale);
    }
    const int passed = scaled < 2e-14L;
    ++comparisons;
    if (!passed) ++failures;
    if (fprintf(csv, "%s,%u,%u,%s,%s,%.18Le,%.18Le,%s\n",
                c->dsp->pVTbl->Algorithm(c->dsp) ? "DIF" : "DIT", c->n, kind,
                inverse ? "IFFT" : "FFT", metric == SOA ? "SOA" : "OOA", absolute, scaled,
                passed ? "PASS" : "FAIL") < 0) return 1;
    return passed ? 0 : 1;
}

/* Consume results outside the timed interval, for both service and object paths. */
static int consume(Comparison* c, unsigned metric) {
    EcoComplexL value = {0, 0};
    if (metric == OOA) {
        if (c->output_vector->pVTbl->Get(c->output_vector, c->n / 2, &value)) return 1;
    } else if (metric == SOA) value = c->eco_output[c->n / 2];
    else if (metric == FFTW_REUSE || metric == FFTW_FULL) value = c->reference[c->n / 2];
    if (!isfinite(value.re) || !isfinite(value.im)) return 1;
    observed += value.re + value.im;
    return 0;
}

static int batch(Comparison* c, unsigned metric, int inverse, uint32_t repeats, double* elapsed) {
    int rc = 0;
    double start = seconds();
    for (uint32_t i = 0; i < repeats; ++i) rc |= operation(c, metric, inverse);
    *elapsed = seconds() - start;
    return rc || consume(c, metric);
}

static int measure(Comparison* c, unsigned metric, int inverse, double* result, uint32_t* repeats) {
    double elapsed, samples[7];
    *repeats = 1;
    /* Warm up and calibrate batches to >=10 ms, capped at 65536 operations. */
    for (;;) {
        if (batch(c, metric, inverse, *repeats, &elapsed)) return 1;
        if (elapsed >= 0.01 || *repeats == 65536) break;
        *repeats *= 2;
    }
    for (unsigned i = 0; i < 7; ++i) {
        if (batch(c, metric, inverse, *repeats, &elapsed)) return 1;
        samples[i] = elapsed * 1e6 / *repeats;
    }
    for (unsigned i = 1; i < 7; ++i) {
        double x = samples[i]; unsigned j = i;
        while (j && samples[j - 1] > x) { samples[j] = samples[j - 1]; --j; }
        samples[j] = x;
    }
    *result = samples[3];
    return !isfinite(*result) || *result <= 0;
}

static int benchmark_size(uint32_t n) {
    return n == 16 || n == 64 || n == 127 || n == 256 || n == 1023 || n == 1024 ||
           n == 4096 || n == 16384 || n == 65535 || n == 65536;
}

static int compare_component(IEcoDSP* dsp, FILE* accuracy, FILE* timing) {
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        Comparison c;
        int rc = create(&c, dsp, sizes[s]);
        if (rc) { destroy(&c); return 1; }
        for (unsigned kind = 0; kind < 5 && !rc; ++kind) {
            rc = signal_input(&c, kind);
            for (int inverse = 0; inverse < 2 && !rc; ++inverse) {
                rc = operation(&c, FFTW_REUSE, inverse);
                if (!rc) rc = compare_result(&c, accuracy, kind, inverse, SOA);
                /* Exercise the allocation/planning path in --check as well. */
                if (!rc) rc = operation(&c, FFTW_FULL, inverse);
                if (!rc) rc = compare_result(&c, accuracy, kind, inverse, OOA);
            }
        }
        if (timing && benchmark_size(c.n) && !rc) {
            rc = signal_input(&c, 4);
            for (int inverse = 0; inverse < 2 && !rc; ++inverse) {
                double us[METRICS]; uint32_t repeats[METRICS];
                for (unsigned m = 0; m < METRICS && !rc; ++m) rc = measure(&c, m, inverse, &us[m], &repeats[m]);
                if (rc) break;
                if (fprintf(timing, "%s,%u,%s", dsp->pVTbl->Algorithm(dsp) ? "DIF" : "DIT", c.n,
                            inverse ? "IFFT" : "FFT") < 0) rc = 1;
                for (unsigned m = 0; m < METRICS; ++m) if (fprintf(timing, ",%.6f", us[m]) < 0) rc = 1;
                for (unsigned m = 0; m < METRICS; ++m) if (fprintf(timing, ",%u", repeats[m]) < 0) rc = 1;
                if (fputc('\n', timing) == EOF) rc = 1;
                printf("FFTW_BENCH %s N=%u %s SOA=%.3fus FFTW_FULL=%.3fus OOA=%.3fus FFTW_REUSE=%.3fus\n",
                       dsp->pVTbl->Algorithm(dsp) ? "DIF" : "DIT", c.n, inverse ? "IFFT" : "FFT",
                       us[SOA], us[FFTW_FULL], us[OOA], us[FFTW_REUSE]);
                fflush(stdout);
            }
        }
        destroy(&c);
        if (rc) return 1;
    }
    return 0;
}

int16_t EcoMain(IEcoUnknown* unknown) {
    IEcoSystem1* system = 0;
    IEcoInterfaceBus1* bus = 0;
    IEcoCommandArguments1* args = 0;
    IEcoDSP* dsp = 0;
    FILE *accuracy = 0, *timing = 0;
    const UGUID* registered = 0;
    int check_only = 0;
    int16_t rc = 1;
    if (!unknown || unknown->pVTbl->QueryInterface(unknown, &GID_IEcoSystem, (void**)&system)) goto done;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoCommandArguments1, (void**)&args)) goto done;
    if (args->pVTbl->get_Count(args) == 2 && !strcmp(args->pVTbl->get_Arg(args, 1), "--check")) check_only = 1;
    else if (args->pVTbl->get_Count(args) != 1) { fprintf(stderr, "Usage: eco_fft_reference [--check]\n"); goto done; }
    if ((mkdir("build", 0777) && errno != EEXIST) ||
        (mkdir("build/results", 0777) && errno != EEXIST)) { perror("build/results"); goto done; }
    printf("FFTW_REFERENCE version=%s precision=long-double mantissa_bits=%d planner=ESTIMATE threads=1\n",
           fftwl_version, LDBL_MANT_DIG);
    accuracy = fopen("build/results/fftw-accuracy.csv", "w");
    if (!accuracy || fprintf(accuracy, "algorithm,n,signal,direction,architecture,max_abs_error,max_scaled_error,status\n") < 0) goto done;
    if (!check_only) {
        timing = fopen("build/results/fftw-benchmark.csv", "w");
        if (!timing || fprintf(timing, "algorithm,n,direction") < 0) goto done;
        for (unsigned m = 0; m < METRICS; ++m) if (fprintf(timing, ",%s_us", metric_names[m]) < 0) goto done;
        for (unsigned m = 0; m < METRICS; ++m) if (fprintf(timing, ",%s_repeats", metric_names[m]) < 0) goto done;
        if (fputc('\n', timing) == EOF) goto done;
    }
    if (system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus)) goto done;
    if (bus->pVTbl->RegisterComponent(bus, &CID_EcoMemoryManager1,
        (IEcoUnknown*)GetIEcoComponentFactoryPtr_0000000000000000000000004D656D31)) goto done;
    for (unsigned algorithm = 0; algorithm < 2; ++algorithm) {
        const UGUID* cid = algorithm ? &CID_EcoFFT_DIF : &CID_EcoFFT_DIT;
        IEcoComponentFactory* factory = algorithm ? GetEcoFFTDIFactory() : GetEcoFFTDITFactory();
        if (bus->pVTbl->RegisterComponent(bus, cid, (IEcoUnknown*)factory)) goto done;
        registered = cid;
        if (bus->pVTbl->QueryComponent(bus, cid, 0, &IID_IEcoDSP, (void**)&dsp)) goto done;
        if (compare_component(dsp, accuracy, timing)) goto done;
        dsp->pVTbl->Release(dsp); dsp = 0;
        if (bus->pVTbl->UnRegisterComponent(bus, cid)) goto done;
        registered = 0;
    }
    rc = failures || !isfinite(observed) ? 1 : 0;
done:
    if (dsp) dsp->pVTbl->Release(dsp);
    if (registered && bus && bus->pVTbl->UnRegisterComponent(bus, registered)) rc = 1;
    if (args) args->pVTbl->Release(args);
    if (bus) bus->pVTbl->Release(bus);
    if (system) system->pVTbl->Release(system);
    if (accuracy && fclose(accuracy)) rc = 1;
    if (timing && fclose(timing)) rc = 1;
    fftwl_cleanup();
    printf("FFTW_RESULT comparisons=%u failures=%u status=%s\n", comparisons, failures, rc ? "FAIL" : "PASS");
    return rc;
}
