#ifndef ECO_FFT_DSP_H
#define ECO_FFT_DSP_H

#include "IEcoBase1.h"
#include "IEcoMemoryAllocator1.h"

/* All interfaces follow the official Eco ACOM ABI. No C library is used here.
 * Each successful creation/QueryInterface returns one owned reference.
 * Objects are single-threaded; independent instances may be used concurrently.
 * FFT: negative exponent; IFFT: positive exponent, scaled by 1/N.
 * Supported N: 1..65536, including non-powers of two (Bluestein).
 * Input components must be finite and have absolute value <= 1e100.
 * Exact in-place input/output is supported; partial overlap is not supported.
 */
#define ECO_OK 0
#define ECO_E_ARGUMENT (-1)
#define ECO_E_MEMORY (-2)
#define ECO_E_RANGE (-3)
#define ECO_E_STATE (-4)
#define ECO_E_INTERFACE (-5)
#define ECO_FFT_MAX_N 65536U
#define ECO_FFT_DIT 0U
#define ECO_FFT_DIF 1U

typedef struct EcoComplexF { float re; float im; } EcoComplexF;
typedef struct EcoComplexD { double re; double im; } EcoComplexD;
typedef struct EcoComplexL { long double re; long double im; } EcoComplexL;

#define ECO_FFT_ID(name, last) \
    static const UGUID name = {0x01, 0x10, {0xC3,0x45,0xFB,0x78,0x70,0x71,0x44,0x10,0x98,0x21,0xA6,0xEF,0x25,0x31,0x00,last}}
ECO_FFT_ID(IID_IEcoDSP, 0x01);
ECO_FFT_ID(IID_IEcoVector, 0x02);
ECO_FFT_ID(IID_IEcoMatrix, 0x03);
ECO_FFT_ID(IID_IEcoFFTPlan, 0x04);
ECO_FFT_ID(IID_IEcoDigitalFilter, 0x05);
ECO_FFT_ID(CID_EcoFFT_DIT, 0x70);
ECO_FFT_ID(CID_EcoFFT_DIF, 0x71);
#undef ECO_FFT_ID

typedef struct IEcoVector IEcoVector;
typedef struct IEcoMatrix IEcoMatrix;
typedef struct IEcoFFTPlan IEcoFFTPlan;
typedef struct IEcoDigitalFilter IEcoDigitalFilter;
typedef struct IEcoDSP IEcoDSP;

#define ECO_UNKNOWN_METHODS(T) \
    int16_t (ECOCALLMETHOD *QueryInterface)(/* in */ T* me, /* in */ const UGUID* iid, /* out */ void** out); \
    uint32_t (ECOCALLMETHOD *AddRef)(/* in */ T* me); \
    uint32_t (ECOCALLMETHOD *Release)(/* in */ T* me)

typedef struct IEcoVectorVTbl {
    ECO_UNKNOWN_METHODS(IEcoVector);
    uint32_t (ECOCALLMETHOD *Size)(/* in */ IEcoVector* me);
    int16_t (ECOCALLMETHOD *Get)(/* in */ IEcoVector* me, /* in */ uint32_t i, /* out */ EcoComplexL* value);
    int16_t (ECOCALLMETHOD *Set)(/* in */ IEcoVector* me, /* in */ uint32_t i, /* in */ EcoComplexL value);
    uint32_t (ECOCALLMETHOD *MemoryBytes)(/* in */ IEcoVector* me);
} IEcoVectorVTbl;
struct IEcoVector { IEcoVectorVTbl* pVTbl; };

typedef struct IEcoMatrixVTbl {
    ECO_UNKNOWN_METHODS(IEcoMatrix);
    uint32_t (ECOCALLMETHOD *Rows)(/* in */ IEcoMatrix* me);
    uint32_t (ECOCALLMETHOD *Cols)(/* in */ IEcoMatrix* me);
    int16_t (ECOCALLMETHOD *Get)(/* in */ IEcoMatrix* me, /* in */ uint32_t row, /* in */ uint32_t col, /* out */ EcoComplexL* value);
    int16_t (ECOCALLMETHOD *Set)(/* in */ IEcoMatrix* me, /* in */ uint32_t row, /* in */ uint32_t col, /* in */ EcoComplexL value);
    uint32_t (ECOCALLMETHOD *MemoryBytes)(/* in */ IEcoMatrix* me);
} IEcoMatrixVTbl;
struct IEcoMatrix { IEcoMatrixVTbl* pVTbl; };

typedef struct IEcoFFTPlanVTbl {
    ECO_UNKNOWN_METHODS(IEcoFFTPlan);
    /* Load copies the signal; later changes to the vector cannot change it. */
    int16_t (ECOCALLMETHOD *Load)(/* in */ IEcoFFTPlan* me, /* in */ IEcoVector* signal);
    int16_t (ECOCALLMETHOD *FFT)(/* in,out */ IEcoFFTPlan* me);
    int16_t (ECOCALLMETHOD *IFFT)(/* in,out */ IEcoFFTPlan* me);
    int16_t (ECOCALLMETHOD *Store)(/* in */ IEcoFFTPlan* me, /* out */ IEcoVector* signal);
    uint32_t (ECOCALLMETHOD *Size)(/* in */ IEcoFFTPlan* me);
    uint32_t (ECOCALLMETHOD *MemoryBytes)(/* in */ IEcoFFTPlan* me);
} IEcoFFTPlanVTbl;
struct IEcoFFTPlan { IEcoFFTPlanVTbl* pVTbl; };

typedef struct IEcoDigitalFilterVTbl {
    ECO_UNKNOWN_METHODS(IEcoDigitalFilter);
    /* N-point circular convolution; immutable cached kernel spectrum.
     * This is a block FIR demonstrator, not an IIR filter with delay history. */
    int16_t (ECOCALLMETHOD *Apply)(/* in,out */ IEcoDigitalFilter* me, /* in */ IEcoVector* signal, /* out */ IEcoVector* result);
    uint32_t (ECOCALLMETHOD *Size)(/* in */ IEcoDigitalFilter* me);
    uint32_t (ECOCALLMETHOD *MemoryBytes)(/* in */ IEcoDigitalFilter* me);
} IEcoDigitalFilterVTbl;
struct IEcoDigitalFilter { IEcoDigitalFilterVTbl* pVTbl; };

typedef struct IEcoDSPVTbl {
    ECO_UNKNOWN_METHODS(IEcoDSP);
    /* C overloads use explicit suffixes. Integer input is promoted to double
     * output because a Fourier spectrum is generally not integer-valued. */
    int16_t (ECOCALLMETHOD *fft)(IEcoDSP* me, uint32_t n, const EcoComplexD* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *ifft)(IEcoDSP* me, uint32_t n, const EcoComplexD* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *fftf)(IEcoDSP* me, uint32_t n, const EcoComplexF* input, EcoComplexF* output);
    int16_t (ECOCALLMETHOD *ifftf)(IEcoDSP* me, uint32_t n, const EcoComplexF* input, EcoComplexF* output);
    int16_t (ECOCALLMETHOD *fftl)(IEcoDSP* me, uint32_t n, const EcoComplexL* input, EcoComplexL* output);
    int16_t (ECOCALLMETHOD *ifftl)(IEcoDSP* me, uint32_t n, const EcoComplexL* input, EcoComplexL* output);
    int16_t (ECOCALLMETHOD *ffti)(IEcoDSP* me, uint32_t n, const int* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *iffti)(IEcoDSP* me, uint32_t n, const int* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *fftlong)(IEcoDSP* me, uint32_t n, const long* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *ifftlong)(IEcoDSP* me, uint32_t n, const long* input, EcoComplexD* output);
    int16_t (ECOCALLMETHOD *CreateVector)(IEcoDSP* me, uint32_t n, IEcoVector** out);
    int16_t (ECOCALLMETHOD *CreateMatrix)(IEcoDSP* me, uint32_t rows, uint32_t cols, IEcoMatrix** out);
    int16_t (ECOCALLMETHOD *CreatePlan)(IEcoDSP* me, uint32_t n, IEcoFFTPlan** out);
    int16_t (ECOCALLMETHOD *CreateFilter)(IEcoDSP* me, IEcoVector* kernel, IEcoDigitalFilter** out);
    int16_t (ECOCALLMETHOD *Filter)(IEcoDSP* me, IEcoVector* signal, IEcoVector* kernel, IEcoVector* output);
    long double (ECOCALLMETHOD *Add)(IEcoDSP* me, long double a, long double b);
    long double (ECOCALLMETHOD *Subtract)(IEcoDSP* me, long double a, long double b);
    uint32_t (ECOCALLMETHOD *Algorithm)(IEcoDSP* me);
    uint32_t (ECOCALLMETHOD *MemoryBytes)(IEcoDSP* me);
} IEcoDSPVTbl;
struct IEcoDSP { IEcoDSPVTbl* pVTbl; };
#undef ECO_UNKNOWN_METHODS

/* Named factories for static linking; dynamic modules also export the official
 * Eco entry point GetIEcoComponentFactoryPtr. */
IEcoComponentFactory* ECOCALLMETHOD GetEcoFFTDITFactory(void);
IEcoComponentFactory* ECOCALLMETHOD GetEcoFFTDIFactory(void);

#endif
