#include "eco_runtime.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int16_t client_query_component(IEcoInterfaceBus1* bus, const UGUID* cid,
                               const char* directory, const UGUID* iid, void** out) {
    char executable[4096], filename[36];
    static const char hex[] = "0123456789ABCDEF";
    int saved_directory;
    int16_t rc = -1;
    if (!out) return rc;
    *out = 0;
    if (!bus || !cid || cid->Preamble != 1 || cid->Length != 16 || !iid) return rc;
    if (!directory) {
        ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        char* slash;
        if (size < 0 || (size_t)size == sizeof(executable) - 1) return rc;
        executable[size] = 0;
        slash = strrchr(executable, '/');
        if (!slash) return rc;
        if (slash == executable) slash[1] = 0;
        else *slash = 0;
        directory = executable;
    }
    for (unsigned i = 0; i < 16; ++i) {
        filename[2*i] = hex[cid->Data[i] >> 4];
        filename[2*i+1] = hex[cid->Data[i] & 15];
    }
    memcpy(filename + 32, ".so", 4);
    saved_directory = open(".", O_RDONLY | O_DIRECTORY);
    if (saved_directory < 0) return rc;
    if (chdir(directory)) {
        fprintf(stderr, "Cannot open Eco component directory: %s\n", directory);
    } else if (access(filename, R_OK)) {
        fprintf(stderr, "Eco component file is missing: %s/%s\n", directory, filename);
    } else {
        /* The official Eco.InterfaceBus1 resolves CID -> .so -> factory ->
         * interface. There is no client-side dlopen/dlsym or static fallback. */
        rc = bus->pVTbl->QueryComponent(bus, cid, 0, iid, out);
        if (rc || !*out) {
            fprintf(stderr, "Eco QueryComponent failed: CID=%.*s status=%d\n", 32, filename, rc);
            if (!rc) rc = -1;
        }
    }
    if (fchdir(saved_directory)) {
        if (*out) {
            IEcoUnknown* object = (IEcoUnknown*)*out;
            object->pVTbl->Release(object);
            *out = 0;
        }
        rc = -1;
    }
    close(saved_directory);
    return rc;
}
