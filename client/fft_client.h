#ifndef ECO_FFT_CLIENT_H
#define ECO_FFT_CLIENT_H
#include "IEcoDSP.h"
#include "IEcoInterfaceBus1.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <float.h>

extern unsigned client_checks, client_failures;
#define CHECK(expression) do { ++client_checks; if (!(expression)) { \
  ++client_failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); } } while (0)
void client_reference_dft(uint32_t n, const EcoComplexL* x, EcoComplexL* y, int inverse);
long double client_error(uint32_t n, const EcoComplexL* a, const EcoComplexL* b);
void client_signal(uint32_t n, EcoComplexL* x, unsigned kind);
void client_tests(IEcoDSP* dsp);
void client_faults(IEcoMemoryAllocator1* allocator);
void client_benchmark(IEcoDSP* dsp);
void client_demo(IEcoDSP* dsp);
int client_transform(IEcoDSP* dsp, const char* input, const char* output, int inverse, int object_mode);
#endif
