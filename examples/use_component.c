/* A complete Eco application: the official Eco.System1 library supplies main().
 * Build: cmake --build build/release --target eco_fft_example
 * Run from the directory containing the CID-named .so files:
 *        (cd build/release && ./eco_fft_example)
 * Change only the CID to select DIF. No FFT implementation is linked. */
#include "IEcoDSP.h"
#include "IEcoSystem1.h"
#include "IEcoInterfaceBus1.h"
#include <stdio.h>

int16_t EcoMain(IEcoUnknown* unknown) {
    IEcoSystem1* system = 0;
    IEcoInterfaceBus1* bus = 0;
    IEcoDSP* dsp = 0;
    EcoComplexD input[4] = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};
    EcoComplexD spectrum[4], restored[4];
    const EcoComplexD expected[4] = {{10, 0}, {-2, 2}, {-2, 0}, {-2, -2}};
    int16_t rc = 1;
    if (!unknown || unknown->pVTbl->QueryInterface(unknown, &GID_IEcoSystem, (void**)&system)) goto done;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus)) goto done;
    /* The bus discovers the module and invokes its standard Eco factory. */
    if (bus->pVTbl->QueryComponent(bus, &CID_EcoFFT_DIT, 0, &IID_IEcoDSP, (void**)&dsp)) goto done;
    if (dsp->pVTbl->fft(dsp, 4, input, spectrum)) goto done;
    if (dsp->pVTbl->ifft(dsp, 4, spectrum, restored)) goto done;
    puts("k,fft_real,fft_imag,restored_real,restored_imag");
    for (uint32_t k = 0; k < 4; ++k) {
        double dr = spectrum[k].re - expected[k].re;
        double di = spectrum[k].im - expected[k].im;
        double rr = restored[k].re - input[k].re;
        double ri = restored[k].im;
        if (dr * dr + di * di > 1e-24 || rr * rr + ri * ri > 1e-24) goto done;
        printf("%u,%.12g,%.12g,%.12g,%.12g\n", k, spectrum[k].re, spectrum[k].im, restored[k].re, restored[k].im);
    }
    rc = 0;
done:
    if (dsp) dsp->pVTbl->Release(dsp);
    if (bus) bus->pVTbl->Release(bus);
    if (system) system->pVTbl->Release(system);
    return rc;
}
