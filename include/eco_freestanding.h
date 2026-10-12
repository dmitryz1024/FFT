#ifndef ECO_FFT_FREESTANDING_H
#define ECO_FFT_FREESTANDING_H
/* Build-time platform adapter, ONLY for the authored component translation
 * units. The upstream Linux depend.h eagerly includes stdio/stdlib/math/string
 * headers. Keep the original SDK untouched, suppress that platform shim, and
 * provide its Linux x86_64 ABI types using compiler primitive types instead.
 * IEcoBase1/UGUID/vtables/allocator remain the original upstream definitions.
 * The client uses the original depend.h; tests check ABI sizes independently.
 */
#if !defined(__linux__) || !defined(__x86_64__)
#error This freestanding adapter targets Linux x86_64; use the supplied Docker image.
#endif
#define __ECO_DEPEND_H__
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned long uint64_t;
typedef unsigned char bool_t;
typedef unsigned char byte_t;
typedef char char_t;
typedef unsigned char uchar_t;
typedef float float_t;
typedef double double_t;
typedef long long_t;
typedef unsigned long ulong_t;
typedef void* voidptr_t;
typedef void* descriptor_t;
#define ECO_EXPORT __attribute__((visibility("default")))
#define ECOCALLMETHOD
#define ECOCDECLMETHOD
#define ECOSTDCALLMETHOD
#define ECOFASTCALLMETHOD
#define ECOPASCALMETHOD
#define ECOSAFECALLMETHOD
#define ECOTHISCALLMETHOD
#define ECOVECTORCALLMETHOD
_Static_assert(sizeof(uint32_t) == 4 && sizeof(void*) == 8, "Eco Linux x64 ABI");
#endif
