#ifndef ECO_FFT_CALCULATOR_X_H
#define ECO_FFT_CALCULATOR_X_H

#include "IEcoBase1.h"

/* Published Eco calculator contract, retained without changing its IID,
 * method order or integer signatures when adding the DSP interface.
 * IEcoCalculatorX IID = {93221116-2248-4742-AE06-82819447843D} */
static const UGUID IID_IEcoCalculatorX = {0x01, 0x10,
    {0x93,0x22,0x11,0x16,0x22,0x48,0x47,0x42,0xAE,0x06,0x82,0x81,0x94,0x47,0x84,0x3D}};

typedef struct IEcoCalculatorX IEcoCalculatorX;
typedef struct IEcoCalculatorXVTbl {
    int16_t (ECOCALLMETHOD *QueryInterface)(IEcoCalculatorX* me, const UGUID* iid, void** out);
    uint32_t (ECOCALLMETHOD *AddRef)(IEcoCalculatorX* me);
    uint32_t (ECOCALLMETHOD *Release)(IEcoCalculatorX* me);
    int32_t (ECOCALLMETHOD *Addition)(IEcoCalculatorX* me, int16_t a, int16_t b);
    int16_t (ECOCALLMETHOD *Subtraction)(IEcoCalculatorX* me, int16_t a, int16_t b);
} IEcoCalculatorXVTbl;
struct IEcoCalculatorX { IEcoCalculatorXVTbl* pVTbl; };

#endif
