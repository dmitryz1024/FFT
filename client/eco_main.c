/* The application entry point is supplied by the official Eco.System1 runtime.
 * libc/libm are used only by this client, never by the FFT component. */
#include "fft_client.h"
#include "IEcoSystem1.h"
#include "IEcoCommandArguments1.h"
#include "eco_runtime.h"
#ifdef ECO_STATIC_CLIENT
#include "IdEcoFFT.h"
#endif
#include <errno.h>
#include <sys/stat.h>

unsigned client_checks = 0, client_failures = 0;

static void usage(void) {
    puts("EcoFFT - complex FFT and IFFT using Eco components\n"
         "Usage: eco_fft --fft|--ifft --input=FILE [--output=FILE]\n"
         "               [--algorithm=dit|dif] [--architecture=service|object]\n"
         "               [--dynamic=DIRECTORY]\n"
         "       eco_fft --test|--demo|--benchmark [--algorithm=dit|dif|both]\n"
         "               [--dynamic=DIRECTORY]\n"
         "       eco_fft --help\n\n"
         "CSV: optional real,imag header; one complex sample per row.\n"
         "Use '-' for stdin/stdout. Output defaults to stdout.\n"
#ifdef ECO_STATIC_CLIENT
         "Transform defaults: DIT, service, static components (--dynamic is unsupported).\n"
#else
         "Transform defaults: DIT, service, Eco bus loading beside the executable.\n"
#endif
         "N: 1..65536. Finite real/imaginary parts with abs(value) <= 1e100.\n"
         "FFT uses a negative exponent; IFFT includes division by N.\n"
         "Test, demo and benchmark results are written to build/results/.");
}

static int results_directory(void) {
    if (mkdir("build", 0777) && errno != EEXIST) return 1;
    if (mkdir("build/results", 0777) && errno != EEXIST) return 1;
    return 0;
}

int16_t EcoMain(IEcoUnknown* unknown) {
    IEcoSystem1* system = 0;
    IEcoInterfaceBus1* bus = 0;
    IEcoCommandArguments1* args = 0;
    IEcoDSP* dsp = 0;
    const char* mode = 0;
    const char* selected = 0;
    const char* dynamic = 0;
    const char* input = 0;
    const char* output = 0;
    const char* architecture = 0;
    int transform = 0;
    unsigned first = 0, last = 1, algorithm;
    int16_t rc = 1;
    if (!unknown || unknown->pVTbl->QueryInterface(unknown, &GID_IEcoSystem, (void**)&system)) goto done;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoCommandArguments1, (void**)&args) || !args) goto done;
    {
        int16_t count = args->pVTbl->get_Count(args);
        if (count < 2) { usage(); rc = 0; goto done; }
        for (int16_t i = 1; i < count; ++i) {
            const char* arg = args->pVTbl->get_Arg(args, i);
            if (!strcmp(arg, "--help")) { usage(); rc = 0; goto done; }
            if (!strcmp(arg, "--test") || !strcmp(arg, "--demo") || !strcmp(arg, "--benchmark") ||
                !strcmp(arg, "--fft") || !strcmp(arg, "--ifft")) {
                if (mode) { fprintf(stderr, "Select exactly one operation\n"); goto done; }
                mode = arg + 2;
            }
            else if (!strncmp(arg, "--algorithm=", 12)) selected = arg + 12;
            else if (!strncmp(arg, "--architecture=", 15)) architecture = arg + 15;
            else if (!strncmp(arg, "--dynamic=", 10)) dynamic = arg + 10;
            else if (!strncmp(arg, "--input=", 8)) input = arg + 8;
            else if (!strncmp(arg, "--output=", 9)) output = arg + 9;
            else { fprintf(stderr, "Unknown option: %s\n", arg); goto done; }
        }
    }
    if (selected && strcmp(selected, "dit") && strcmp(selected, "dif") && strcmp(selected, "both")) {
        fprintf(stderr, "Algorithm must be dit, dif or both\n"); goto done;
    }
    if (!mode) { fprintf(stderr, "Select an operation; see --help\n"); goto done; }
    transform = !strcmp(mode, "fft") || !strcmp(mode, "ifft");
    if (transform) {
        if (!input || !*input) { fprintf(stderr, "--input=FILE is required (use - for stdin)\n"); goto done; }
        if (!selected) selected = "dit";
        if (!strcmp(selected, "both")) { fprintf(stderr, "A transform requires one algorithm: dit or dif\n"); goto done; }
        if (architecture && strcmp(architecture, "service") && strcmp(architecture, "object")) {
            fprintf(stderr, "Architecture must be service or object\n"); goto done;
        }
        if (output && !*output) { fprintf(stderr, "Output path must not be empty\n"); goto done; }
    } else {
        if (input || output || architecture) {
            fprintf(stderr, "Input, output and architecture options require --fft or --ifft\n"); goto done;
        }
        if (results_directory()) { perror("Cannot create build/results"); goto done; }
    }
    if (dynamic && !*dynamic) { fprintf(stderr, "Dynamic library directory must not be empty\n"); goto done; }
    if (selected && !strcmp(selected, "dit")) last = 0;
    else if (selected && !strcmp(selected, "dif")) first = 1;
    if (system->pVTbl->QueryInterface(system, &IID_IEcoInterfaceBus1, (void**)&bus)) goto done;
    if (!transform) {
        printf("EcoFFT | %s | Eco QueryComponent | Linux x86_64\n", mode);
        printf("ABI: UGUID=%zu, pointer=%zu, complexL=%zu, LDBL_MANT_DIG=%d\n",
               sizeof(UGUID), sizeof(void*), sizeof(EcoComplexL), LDBL_MANT_DIG);
    }
    for (algorithm = first; algorithm <= last; ++algorithm) {
        const UGUID* cid = algorithm ? &CID_EcoFFT_DIF : &CID_EcoFFT_DIT;
#ifdef ECO_STATIC_CLIENT
        if (dynamic) { fprintf(stderr, "The static example does not accept --dynamic\n"); goto done; }
        {
            IEcoComponentFactory* factory = algorithm
                ? GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310071
                : GetIEcoComponentFactoryPtr_C345FB78707144109821A6EF25310070;
            if (bus->pVTbl->RegisterComponent(bus, cid, (IEcoUnknown*)factory)) goto done;
        }
        if (bus->pVTbl->QueryComponent(bus, cid, 0, &IID_IEcoDSP, (void**)&dsp)) goto done;
#else
        if (client_query_component(bus, cid, dynamic, &IID_IEcoDSP, (void**)&dsp)) goto done;
#endif
        if (transform) {
            if (client_transform(dsp, input, output ? output : "-", !strcmp(mode, "ifft"),
                                 architecture && !strcmp(architecture, "object"))) goto done;
        } else {
            printf("Component: EcoFFT %s + Bluestein\n", dsp->pVTbl->Algorithm(dsp) ? "DIF" : "DIT");
            if (!strcmp(mode, "test")) client_tests(dsp);
            else if (!strcmp(mode, "demo")) client_demo(dsp);
            else client_benchmark(dsp);
        }
        dsp->pVTbl->Release(dsp); dsp = 0;
    }
    if (!strcmp(mode, "test")) {
        printf("TEST_RESULT checks=%u failures=%u status=%s\n",
               client_checks, client_failures, client_failures ? "FAIL" : "PASS");
    }
    rc = client_failures ? 1 : 0;
done:
    if (dsp) dsp->pVTbl->Release(dsp);
    if (args) args->pVTbl->Release(args);
    if (bus) bus->pVTbl->Release(bus);
    if (system) system->pVTbl->Release(system);
    /* The runtime owns the registered factories and loaded modules. */
    if (rc) fprintf(stderr, "EcoFFT failed (status=%d)\n", rc);
    return rc;
}
