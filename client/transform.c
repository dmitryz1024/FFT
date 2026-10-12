/* CSV parsing and filesystem I/O belong to the application, not the component. */
#include "fft_client.h"
#include <ctype.h>
#include <errno.h>

static char* skip_space(char* text) {
    while (isspace((unsigned char)*text)) ++text;
    return text;
}

static int read_signal(const char* path, EcoComplexL** signal, uint32_t* size) {
    FILE* file = !strcmp(path, "-") ? stdin : fopen(path, "rb");
    EcoComplexL* values = 0;
    char line[1024];
    unsigned line_number = 0;
    uint32_t count = 0, capacity = 0;
    int first_record = 1, rc = 1;
    if (!file) { perror(path); return 1; }
    while (fgets(line, sizeof(line), file)) {
        char* p = line;
        char* end;
        EcoComplexL value;
        ++line_number;
        if (!strchr(line, '\n') && !feof(file)) {
            fprintf(stderr, "%s:%u: CSV row is too long (maximum 1022 bytes)\n", path, line_number); goto done;
        }
        if (line_number == 1 && strlen(p) >= 3 &&
            (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        p = skip_space(p);
        if (!*p || *p == '#') continue;
        end = p + strlen(p);
        while (end > p && isspace((unsigned char)end[-1])) *--end = '\0';
        if (first_record && !strcmp(p, "real,imag")) { first_record = 0; continue; }
        first_record = 0;
        errno = 0;
        value.re = strtold(p, &end);
        if (end == p || errno == ERANGE) goto invalid;
        p = skip_space(end);
        if (*p++ != ',') goto invalid;
        p = skip_space(p);
        errno = 0;
        value.im = strtold(p, &end);
        if (end == p || errno == ERANGE || *skip_space(end)) goto invalid;
        if (!isfinite(value.re) || !isfinite(value.im) || fabsl(value.re) > 1e100L || fabsl(value.im) > 1e100L) {
            fprintf(stderr, "%s:%u: samples must be finite with abs(value) <= 1e100\n", path, line_number); goto done;
        }
        if (count == ECO_FFT_MAX_N) {
            fprintf(stderr, "%s: more than %u samples\n", path, ECO_FFT_MAX_N); goto done;
        }
        if (count == capacity) {
            uint32_t next = capacity ? capacity * 2 : 256;
            EcoComplexL* grown = realloc(values, (size_t)next * sizeof(*values));
            if (!grown) { fprintf(stderr, "Cannot allocate input buffer\n"); goto done; }
            values = grown;
            capacity = next;
        }
        values[count++] = value;
        continue;
invalid:
        fprintf(stderr, "%s:%u: expected two numbers separated by a comma\n", path, line_number);
        goto done;
    }
    if (ferror(file)) { fprintf(stderr, "Cannot read %s\n", path); goto done; }
    if (!count) { fprintf(stderr, "%s: signal is empty\n", path); goto done; }
    *signal = values;
    *size = count;
    values = 0;
    rc = 0;
done:
    free(values);
    if (file != stdin) fclose(file);
    return rc;
}

static int16_t object_transform(IEcoDSP* dsp, uint32_t n, const EcoComplexL* input,
                                EcoComplexL* output, int inverse) {
    IEcoVector* source = 0;
    IEcoVector* result = 0;
    IEcoFFTPlan* plan = 0;
    int16_t rc = dsp->pVTbl->CreateVector(dsp, n, &source);
    if (rc) goto done;
    rc = dsp->pVTbl->CreateVector(dsp, n, &result);
    if (rc) goto done;
    rc = dsp->pVTbl->CreatePlan(dsp, n, &plan);
    if (rc) goto done;
    for (uint32_t i = 0; i < n; ++i) {
        rc = source->pVTbl->Set(source, i, input[i]);
        if (rc) goto done;
    }
    rc = plan->pVTbl->Load(plan, source);
    if (rc) goto done;
    rc = inverse ? plan->pVTbl->IFFT(plan) : plan->pVTbl->FFT(plan);
    if (rc) goto done;
    rc = plan->pVTbl->Store(plan, result);
    if (rc) goto done;
    for (uint32_t i = 0; i < n; ++i) {
        rc = result->pVTbl->Get(result, i, &output[i]);
        if (rc) goto done;
    }
done:
    if (plan) plan->pVTbl->Release(plan);
    if (result) result->pVTbl->Release(result);
    if (source) source->pVTbl->Release(source);
    return rc;
}

int client_transform(IEcoDSP* dsp, const char* input, const char* output, int inverse, int object_mode) {
    EcoComplexL* signal = 0;
    EcoComplexL* result = 0;
    FILE* file = 0;
    uint32_t n = 0;
    int rc = 1;
    int16_t status;
    if (read_signal(input, &signal, &n)) goto done;
    result = malloc((size_t)n * sizeof(*result));
    if (!result) { fprintf(stderr, "Cannot allocate output buffer\n"); goto done; }
    status = object_mode ? object_transform(dsp, n, signal, result, inverse) :
             inverse ? dsp->pVTbl->ifftl(dsp, n, signal, result) : dsp->pVTbl->fftl(dsp, n, signal, result);
    if (status) { fprintf(stderr, "Transform returned Eco error %d\n", status); goto done; }
    /* Read and compute first: a bad input never creates or truncates output. */
    file = !strcmp(output, "-") ? stdout : fopen(output, "wb");
    if (!file) { perror(output); goto done; }
    if (fprintf(file, "real,imag\n") < 0) goto write_error;
    for (uint32_t i = 0; i < n; ++i)
        if (fprintf(file, "%.21Lg,%.21Lg\n", result[i].re, result[i].im) < 0) goto write_error;
    if (fflush(file)) goto write_error;
    rc = 0;
    goto done;
write_error:
    fprintf(stderr, "Cannot write %s\n", output);
done:
    if (file && file != stdout && fclose(file)) { fprintf(stderr, "Cannot close %s\n", output); rc = 1; }
    free(result);
    free(signal);
    return rc;
}
