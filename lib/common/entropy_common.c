/* ******************************************************************
 * Common functions of New Generation Entropy library
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 *  You can contact the author at :
 *  - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *  - Public forum : https://groups.google.com/forum/#!forum/lz4c
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

/* *************************************
*  Dependencies
***************************************/
#include "mem.h"
#include "error_private.h"       /* ERR_*, ERROR */
#define FSE_STATIC_LINKING_ONLY  /* FSE_MIN_TABLELOG */
#define FSE_BIC
#include "fse.h"
#include "huf.h"
#include "bits.h"                /* ZSDT_highbit32, ZSTD_countTrailingZeros32 */


/*===   Version   ===*/
unsigned FSE_versionNumber(void) { return FSE_VERSION_NUMBER; }


/*===   Error Management   ===*/
unsigned FSE_isError(size_t code) { return ERR_isError(code); }
const char* FSE_getErrorName(size_t code) { return ERR_getErrorName(code); }

unsigned HUF_isError(size_t code) { return ERR_isError(code); }
const char* HUF_getErrorName(size_t code) { return ERR_getErrorName(code); }


/*-**************************************************************
*  FSE NCount encoding-decoding
****************************************************************/
FORCE_INLINE_TEMPLATE
size_t FSE_readNCount_normal(short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                           const void* headerBuffer, size_t hbSize)
{
    const BYTE* const istart = (const BYTE*) headerBuffer;
    const BYTE* const iend = istart + hbSize;
    const BYTE* ip = istart;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    unsigned charnum = 0;
    unsigned const maxSV1 = *maxSVPtr + 1;
    int previous0 = 0;

    if (hbSize < 8) {
        /* This function only works when hbSize >= 8 */
        char buffer[8] = {0};
        ZSTD_memcpy(buffer, headerBuffer, hbSize);
        {   size_t const countSize = FSE_readNCount(normalizedCounter, maxSVPtr, tableLogPtr,
                                                    buffer, sizeof(buffer));
            if (FSE_isError(countSize)) return countSize;
            if (countSize > hbSize) return ERROR(corruption_detected);
            return countSize;
    }   }
    assert(hbSize >= 8);

    /* init */
    ZSTD_memset(normalizedCounter, 0, (*maxSVPtr+1) * sizeof(normalizedCounter[0]));   /* all symbols not present in NCount have a frequency of 0 */
    bitStream = MEM_readLE32(ip);
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   /* extract tableLog */
    if (nbBits > FSE_TABLELOG_ABSOLUTE_MAX) return ERROR(tableLog_tooLarge);
    bitStream >>= 4;
    bitCount = 4;
    *tableLogPtr = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;

    for (;;) {
        if (previous0) {
            /* Count the number of repeats. Each time the
             * 2-bit repeat code is 0b11 there is another
             * repeat.
             * Avoid UB by setting the high bit to 1.
             */
            int repeats = ZSTD_countTrailingZeros32(~bitStream | 0x80000000) >> 1;
            while (repeats >= 12) {
                charnum += 3 * 12;
                if (LIKELY(ip <= iend-7)) {
                    ip += 3;
                } else {
                    bitCount -= (int)(8 * (iend - 7 - ip));
                    bitCount &= 31;
                    ip = iend - 4;
                }
                bitStream = MEM_readLE32(ip) >> bitCount;
                repeats = ZSTD_countTrailingZeros32(~bitStream | 0x80000000) >> 1;
            }
            charnum += 3 * repeats;
            bitStream >>= 2 * repeats;
            bitCount += 2 * repeats;

            /* Add the final repeat which isn't 0b11. */
            assert((bitStream & 3) < 3);
            charnum += bitStream & 3;
            bitCount += 2;

            /* This is an error, but break and return an error
             * at the end, because returning out of a loop makes
             * it harder for the compiler to optimize.
             */
            if (charnum >= maxSV1) break;

            /* We don't need to set the normalized count to 0
             * because we already memset the whole buffer to 0.
             */

            if (LIKELY(ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                assert((bitCount >> 3) <= 3); /* For first condition to work */
                ip += bitCount>>3;
                bitCount &= 7;
            } else {
                bitCount -= (int)(8 * (iend - 4 - ip));
                bitCount &= 31;
                ip = iend - 4;
            }
            bitStream = MEM_readLE32(ip) >> bitCount;
        }
        {
            int const max = (2*threshold-1) - remaining;
            int count;

            if ((bitStream & (threshold-1)) < (U32)max) {
                count = bitStream & (threshold-1);
                bitCount += nbBits-1;
            } else {
                count = bitStream & (2*threshold-1);
                if (count >= threshold) count -= max;
                bitCount += nbBits;
            }

            count--;   /* extra accuracy */
            /* When it matters (small blocks), this is a
             * predictable branch, because we don't use -1.
             */
            if (count >= 0) {
                remaining -= count;
            } else {
                assert(count == -1);
                remaining += count;
            }
            normalizedCounter[charnum++] = (short)count;
            previous0 = !count;

            assert(threshold > 1);
            if (remaining < threshold) {
                /* This branch can be folded into the
                 * threshold update condition because we
                 * know that threshold > 1.
                 */
                if (remaining <= 1) break;
                nbBits = ZSTD_highbit32(remaining) + 1;
                threshold = 1 << (nbBits - 1);
            }
            if (charnum >= maxSV1) break;

            if (LIKELY(ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                ip += bitCount>>3;
                bitCount &= 7;
            } else {
                bitCount -= (int)(8 * (iend - 4 - ip));
                bitCount &= 31;
                ip = iend - 4;
            }
            bitStream = MEM_readLE32(ip) >> bitCount;
    }   }
    if (remaining != 1) return ERROR(corruption_detected);
    /* Only possible when there are too many zeros. */
    if (charnum > maxSV1) return ERROR(maxSymbolValue_tooSmall);
    if (bitCount > 32) return ERROR(corruption_detected);
    *maxSVPtr = charnum-1;

    ip += (bitCount+7)>>3;
    return ip-istart;
}


FORCE_INLINE_TEMPLATE
size_t FSE_readNCount_bic(short* normalizedCounter, const void* headerBuffer, size_t hbSize, 
    unsigned* maxSVPtr, unsigned* tableLogPtr)
{
    static unsigned int bicTable[] = {
        0x80, 0, 0x100, 0xC0, 0x80, 0x100, 0xE0, 0xC0, 0x100,
        0xF0, 0xE0, 0x100, 0xF8, 0xF0, 0x100, 0xFC, 0xF8, 0x100,
        0xFE, 0xFC, 0x100, 0xFF, 0xFE, 0x100, 0xFD, 0xFC, 0xFE,
        0xFA, 0xF8, 0xFC, 0xFB, 0xFA, 0xFC, 0xF9, 0xF8, 0xFA,
        0xF4, 0xF0, 0xF8, 0xF6, 0xF4, 0xF8, 0xF7, 0xF6, 0xF8,
        0xF5, 0xF4, 0xF6, 0xF2, 0xF0, 0xF4, 0xF3, 0xF2, 0xF4,
        0xF1, 0xF0, 0xF2, 0xE8, 0xE0, 0xF0, 0xEC, 0xE8, 0xF0,
        0xEE, 0xEC, 0xF0, 0xEF, 0xEE, 0xF0, 0xED, 0xEC, 0xEE,
        0xEA, 0xE8, 0xEC, 0xEB, 0xEA, 0xEC, 0xE9, 0xE8, 0xEA,
        0xE4, 0xE0, 0xE8, 0xE6, 0xE4, 0xE8, 0xE7, 0xE6, 0xE8,
        0xE5, 0xE4, 0xE6, 0xE2, 0xE0, 0xE4, 0xE3, 0xE2, 0xE4,
        0xE1, 0xE0, 0xE2, 0xD0, 0xC0, 0xE0, 0xD8, 0xD0, 0xE0,
        0xDC, 0xD8, 0xE0, 0xDE, 0xDC, 0xE0, 0xDF, 0xDE, 0xE0,
        0xDD, 0xDC, 0xDE, 0xDA, 0xD8, 0xDC, 0xDB, 0xDA, 0xDC,
        0xD9, 0xD8, 0xDA, 0xD4, 0xD0, 0xD8, 0xD6, 0xD4, 0xD8,
        0xD7, 0xD6, 0xD8, 0xD5, 0xD4, 0xD6, 0xD2, 0xD0, 0xD4,
        0xD3, 0xD2, 0xD4, 0xD1, 0xD0, 0xD2, 0xC8, 0xC0, 0xD0,
        0xCC, 0xC8, 0xD0, 0xCE, 0xCC, 0xD0, 0xCF, 0xCE, 0xD0,
        0xCD, 0xCC, 0xCE, 0xCA, 0xC8, 0xCC, 0xCB, 0xCA, 0xCC,
        0xC9, 0xC8, 0xCA, 0xC4, 0xC0, 0xC8, 0xC6, 0xC4, 0xC8,
        0xC7, 0xC6, 0xC8, 0xC5, 0xC4, 0xC6, 0xC2, 0xC0, 0xC4,
        0xC3, 0xC2, 0xC4, 0xC1, 0xC0, 0xC2, 0xA0, 0x80, 0xC0,
        0xB0, 0xA0, 0xC0, 0xB8, 0xB0, 0xC0, 0xBC, 0xB8, 0xC0,
        0xBE, 0xBC, 0xC0, 0xBF, 0xBE, 0xC0, 0xBD, 0xBC, 0xBE,
        0xBA, 0xB8, 0xBC, 0xBB, 0xBA, 0xBC, 0xB9, 0xB8, 0xBA,
        0xB4, 0xB0, 0xB8, 0xB6, 0xB4, 0xB8, 0xB7, 0xB6, 0xB8,
        0xB5, 0xB4, 0xB6, 0xB2, 0xB0, 0xB4, 0xB3, 0xB2, 0xB4,
        0xB1, 0xB0, 0xB2, 0xA8, 0xA0, 0xB0, 0xAC, 0xA8, 0xB0,
        0xAE, 0xAC, 0xB0, 0xAF, 0xAE, 0xB0, 0xAD, 0xAC, 0xAE,
        0xAA, 0xA8, 0xAC, 0xAB, 0xAA, 0xAC, 0xA9, 0xA8, 0xAA,
        0xA4, 0xA0, 0xA8, 0xA6, 0xA4, 0xA8, 0xA7, 0xA6, 0xA8,
        0xA5, 0xA4, 0xA6, 0xA2, 0xA0, 0xA4, 0xA3, 0xA2, 0xA4,
        0xA1, 0xA0, 0xA2, 0x90, 0x80, 0xA0, 0x98, 0x90, 0xA0,
        0x9C, 0x98, 0xA0, 0x9E, 0x9C, 0xA0, 0x9F, 0x9E, 0xA0,
        0x9D, 0x9C, 0x9E, 0x9A, 0x98, 0x9C, 0x9B, 0x9A, 0x9C,
        0x99, 0x98, 0x9A, 0x94, 0x90, 0x98, 0x96, 0x94, 0x98,
        0x97, 0x96, 0x98, 0x95, 0x94, 0x96, 0x92, 0x90, 0x94,
        0x93, 0x92, 0x94, 0x91, 0x90, 0x92, 0x88, 0x80, 0x90,
        0x8C, 0x88, 0x90, 0x8E, 0x8C, 0x90, 0x8F, 0x8E, 0x90,
        0x8D, 0x8C, 0x8E, 0x8A, 0x88, 0x8C, 0x8B, 0x8A, 0x8C,
        0x89, 0x88, 0x8A, 0x84, 0x80, 0x88, 0x86, 0x84, 0x88,
        0x87, 0x86, 0x88, 0x85, 0x84, 0x86, 0x82, 0x80, 0x84,
        0x83, 0x82, 0x84, 0x81, 0x80, 0x82, 0x40, 0, 0x80,
        0x60, 0x40, 0x80, 0x70, 0x60, 0x80, 0x78, 0x70, 0x80,
        0x7C, 0x78, 0x80, 0x7E, 0x7C, 0x80, 0x7F, 0x7E, 0x80,
        0x7D, 0x7C, 0x7E, 0x7A, 0x78, 0x7C, 0x7B, 0x7A, 0x7C,
        0x79, 0x78, 0x7A, 0x74, 0x70, 0x78, 0x76, 0x74, 0x78,
        0x77, 0x76, 0x78, 0x75, 0x74, 0x76, 0x72, 0x70, 0x74,
        0x73, 0x72, 0x74, 0x71, 0x70, 0x72, 0x68, 0x60, 0x70,
        0x6C, 0x68, 0x70, 0x6E, 0x6C, 0x70, 0x6F, 0x6E, 0x70,
        0x6D, 0x6C, 0x6E, 0x6A, 0x68, 0x6C, 0x6B, 0x6A, 0x6C,
        0x69, 0x68, 0x6A, 0x64, 0x60, 0x68, 0x66, 0x64, 0x68,
        0x67, 0x66, 0x68, 0x65, 0x64, 0x66, 0x62, 0x60, 0x64,
        0x63, 0x62, 0x64, 0x61, 0x60, 0x62, 0x50, 0x40, 0x60,
        0x58, 0x50, 0x60, 0x5C, 0x58, 0x60, 0x5E, 0x5C, 0x60,
        0x5F, 0x5E, 0x60, 0x5D, 0x5C, 0x5E, 0x5A, 0x58, 0x5C,
        0x5B, 0x5A, 0x5C, 0x59, 0x58, 0x5A, 0x54, 0x50, 0x58,
        0x56, 0x54, 0x58, 0x57, 0x56, 0x58, 0x55, 0x54, 0x56,
        0x52, 0x50, 0x54, 0x53, 0x52, 0x54, 0x51, 0x50, 0x52,
        0x48, 0x40, 0x50, 0x4C, 0x48, 0x50, 0x4E, 0x4C, 0x50,
        0x4F, 0x4E, 0x50, 0x4D, 0x4C, 0x4E, 0x4A, 0x48, 0x4C,
        0x4B, 0x4A, 0x4C, 0x49, 0x48, 0x4A, 0x44, 0x40, 0x48,
        0x46, 0x44, 0x48, 0x47, 0x46, 0x48, 0x45, 0x44, 0x46,
        0x42, 0x40, 0x44, 0x43, 0x42, 0x44, 0x41, 0x40, 0x42,
        0x20, 0, 0x40, 0x30, 0x20, 0x40, 0x38, 0x30, 0x40,
        0x3C, 0x38, 0x40, 0x3E, 0x3C, 0x40, 0x3F, 0x3E, 0x40,
        0x3D, 0x3C, 0x3E, 0x3A, 0x38, 0x3C, 0x3B, 0x3A, 0x3C,
        0x39, 0x38, 0x3A, 0x34, 0x30, 0x38, 0x36, 0x34, 0x38,
        0x37, 0x36, 0x38, 0x35, 0x34, 0x36, 0x32, 0x30, 0x34,
        0x33, 0x32, 0x34, 0x31, 0x30, 0x32, 0x28, 0x20, 0x30,
        0x2C, 0x28, 0x30, 0x2E, 0x2C, 0x30, 0x2F, 0x2E, 0x30,
        0x2D, 0x2C, 0x2E, 0x2A, 0x28, 0x2C, 0x2B, 0x2A, 0x2C,
        0x29, 0x28, 0x2A, 0x24, 0x20, 0x28, 0x26, 0x24, 0x28,
        0x27, 0x26, 0x28, 0x25, 0x24, 0x26, 0x22, 0x20, 0x24,
        0x23, 0x22, 0x24, 0x21, 0x20, 0x22, 0x10, 0, 0x20,
        0x18, 0x10, 0x20, 0x1C, 0x18, 0x20, 0x1E, 0x1C, 0x20,
        0x1F, 0x1E, 0x20, 0x1D, 0x1C, 0x1E, 0x1A, 0x18, 0x1C,
        0x1B, 0x1A, 0x1C, 0x19, 0x18, 0x1A, 0x14, 0x10, 0x18,
        0x16, 0x14, 0x18, 0x17, 0x16, 0x18, 0x15, 0x14, 0x16,
        0x12, 0x10, 0x14, 0x13, 0x12, 0x14, 0x11, 0x10, 0x12,
        8, 0, 0x10, 0xC, 8, 0x10, 0xE, 0xC, 0x10, 0xF, 0xE,
        0x10, 0xD, 0xC, 0xE, 0xA, 8, 0xC, 0xB, 0xA, 0xC, 9,
        8, 0xA, 4, 0, 8, 6, 4, 8, 7, 6, 8, 5, 4, 6, 2, 0, 4,
        3, 2, 4, 1, 0, 2, 0, 0, 1, 1, 0, 0, 0, 0, 0,
    };
    
    unsigned int bicCounter[257];
    ZSTD_memset(bicCounter, 0, sizeof(bicCounter));
    if (!hbSize) return ERROR(corruption_detected);
    const BYTE* const ip = (const BYTE*) headerBuffer;
    unsigned int bitStream = *ip;
    unsigned int bitCount = bitStream & 0x7F;
    if (hbSize <= bitCount) return ERROR(corruption_detected);
    unsigned int bitSize = bitCount + 1;
    if (bitSize >= hbSize) return ERROR(corruption_detected);
    
    size_t bitBlockSize = 0;
    size_t bitBlockCount = 0;
    size_t bitBlockRemaining = 0;
    size_t bitBlockEnd = 0;
    size_t bitPos = 0;
    
    if (bitCount) {
        size_t currentBitCount = 0;
        size_t currentBitBlockSize = 0;
        do {
            currentBitCount = bitCount;
            currentBitBlockSize = bitBlockSize;
            --bitCount;
            bitBlockSize = *(ip + currentBitCount) | (bitBlockSize << 8);
        } while (bitCount && !((currentBitBlockSize >> 32) & 0xFFFFFFFF));
        
        bitBlockCount = bitBlockSize / 0x34;
        
        if ((currentBitCount != 1) && ((currentBitBlockSize >> 34) <= 0xC)) {
            do {
                bitPos = *(ip + bitCount--);
                bitBlockRemaining = bitPos | (bitBlockCount << 8);
                if (!bitCount) break;
                bitBlockEnd = ((bitBlockCount >> 32) & 0xFFFFFFFF);
                bitBlockCount = bitBlockRemaining;
            } while (!bitBlockEnd);
            
            bitBlockCount = bitBlockRemaining;
        }
    } else {
        bitBlockCount = 0;
        bitCount = 0;
    }
    
    unsigned int charbase = bitBlockSize % 0x34;
    if (*maxSVPtr <= charbase) return ERROR(corruption_detected);
    
    size_t innerBitBlockRemaining = 0;
    size_t innerBitBlockCount = bitBlockCount >> 3;
    
    if (!bitCount || (bitBlockCount >> 43)) {
        innerBitBlockRemaining = bitBlockCount >> 3;
    } else {
        do {
            bitPos = *(ip + bitCount--);
            innerBitBlockRemaining = bitPos | (innerBitBlockCount << 8);
            if (!bitCount) break;
            bitBlockEnd = ((innerBitBlockCount >> 32) & 0xFFFFFFFF);
            innerBitBlockCount = innerBitBlockRemaining;
        } while (!bitBlockEnd);
    }

    unsigned int nbBits = (bitBlockCount & 7) + 5;
    unsigned int charnum = charbase + 1;
    int remaining = (32 << (bitBlockCount & 7));
    
    size_t outerBitBlockRemaining = 0;
    size_t outerBitBlockCount = innerBitBlockRemaining >> nbBits;
    
    if (!bitCount || (outerBitBlockCount >> 40)) {
        outerBitBlockRemaining = innerBitBlockRemaining >> nbBits;
    } else {
        do {
            bitPos = *(ip + bitCount--);
            outerBitBlockRemaining = bitPos | (outerBitBlockCount << 8);
            if (!bitCount) break;
            bitBlockEnd = ((outerBitBlockCount >> 32) & 0xFFFFFFFF);
            outerBitBlockCount = outerBitBlockRemaining;
        } while (!bitBlockEnd);
    }
    
    unsigned int charoffset = (charnum | (charnum >> 1) | ((charnum | (charnum >> 1)) >> 2));
    unsigned int charidx = charoffset | (charoffset >> 4);
    
    unsigned int countStart = 0;
    if ((bitStream & 0x80u) == 0) {
        countStart = 1;
    } else {
        countStart = charbase + 3;
    }
    
    unsigned int charend = charidx + 1;
    bicCounter[charend] = countStart + (innerBitBlockRemaining & (remaining - 1));
    
    unsigned int bicOffset = 0;
    do {
        unsigned int *bt = &bicTable[3 * (bicOffset - charidx + 255)];
        unsigned int bt1 = bt[1];
        unsigned int bt2 = bt[2];
        unsigned int bcStart = bicCounter[bt1];
        unsigned int bcDist = bicCounter[bt2] - bcStart;
        
        if (bcDist) {
            unsigned int bcSize = bcDist + 1;
            size_t bcRemaining = 0;
            size_t bcCount = 0;
            if (!bitCount || ((outerBitBlockRemaining / bcSize) >> 40)) {
                bcRemaining = outerBitBlockRemaining / bcSize;
            } else {
                bcCount = outerBitBlockRemaining / bcSize;
                do {
                    bitPos = *(ip + bitCount--);
                    bcRemaining = bitPos | (bcCount << 8);
                    if (!bitCount) break;
                    bitBlockEnd = ((bcCount >> 32) & 0xFFFFFFFF);
                    bcCount = bcRemaining;
                } while (!bitBlockEnd);
            }
            
            bicCounter[*bt] = bcStart + outerBitBlockRemaining % bcSize;
            outerBitBlockRemaining = bcRemaining;
        } else {
            unsigned int bcOffset = bt1 + 1;
            if (bcOffset < bt2) {
                for (int i = ~(bt1 - bt2) & 7; i; --i ) {
                    bicCounter[bcOffset++] = bcStart;
                }
            
                if ((bt2 - bt1 - 2) >= 7) {
                    unsigned int bcLeft = bt2 - bcOffset;
                    unsigned int *bc = &bicCounter[bcOffset + 4];
                    do {
                        *(bc - 4) = bcStart;
                        *(bc - 3) = bcStart;
                        bcLeft -= 8;
                        *(bc - 2) = bcStart;
                        *(bc - 1) = bcStart;
                        *bc = bcStart;
                        bc[1] = bcStart;
                        bc[2] = bcStart;
                        bc[3] = bcStart;
                        bc += 8;
                    } while (bcLeft);
                }
            }
        }
        ++bicOffset;
    } while (bicOffset != charend);
      
    int accCount = 0;
    unsigned int *bc1 = &bicCounter[1];
    unsigned int countLeft = bitBlockSize % 0x34 + 2;
    short count = 0;
    
    do {
      int bc1Count = *bc1++;
      count = bc1Count - accCount - (bitStream >> 7);
      accCount += (short)(bc1Count - accCount);
      int weightedCount = count;
      *normalizedCounter++ = count;
      if (count < 0) {
          weightedCount = -count;
      }
      --countLeft;
      remaining -= weightedCount;
    } while (countLeft);
    
    size_t result = ERROR(corruption_detected);
    
    if (!remaining) {
        *maxSVPtr = charnum;
        if (!bitCount) result = bitSize;
        *tableLogPtr = nbBits;
    }
    
    return result;
}

size_t FSE_readNCount_body(short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                           const void* headerBuffer, size_t hbSize)
{
#if defined(FSE_BIC)
    return FSE_readNCount_bic(normalizedCounter, headerBuffer, hbSize, maxSVPtr, tableLogPtr);
#else
    return FSE_readNCount_normal(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
#endif
}

/* Avoids the FORCE_INLINE of the _body() function. */
static size_t FSE_readNCount_body_default(
        short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
        const void* headerBuffer, size_t hbSize)
{
    return FSE_readNCount_body(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
}

#if DYNAMIC_BMI2
BMI2_TARGET_ATTRIBUTE static size_t FSE_readNCount_body_bmi2(
        short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
        const void* headerBuffer, size_t hbSize)
{
    return FSE_readNCount_body(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
}
#endif

size_t FSE_readNCount_bmi2(
        short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
        const void* headerBuffer, size_t hbSize, int bmi2)
{
#if DYNAMIC_BMI2
    if (bmi2) {
        return FSE_readNCount_body_bmi2(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
    }
#endif
    (void)bmi2;
    return FSE_readNCount_body_default(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
}

size_t FSE_readNCount(
        short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
        const void* headerBuffer, size_t hbSize)
{
    return FSE_readNCount_bmi2(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize, /* bmi2 */ 0);
}


/*! HUF_readStats() :
    Read compact Huffman tree, saved by HUF_writeCTable().
    `huffWeight` is destination buffer.
    `rankStats` is assumed to be a table of at least HUF_TABLELOG_MAX U32.
    @return : size read from `src` , or an error Code .
    Note : Needed by HUF_readCTable() and HUF_readDTableX?() .
*/
size_t HUF_readStats(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize)
{
    U32 wksp[HUF_READ_STATS_WORKSPACE_SIZE_U32];
    return HUF_readStats_wksp(huffWeight, hwSize, rankStats, nbSymbolsPtr, tableLogPtr, src, srcSize, wksp, sizeof(wksp), /* flags */ 0);
}

FORCE_INLINE_TEMPLATE size_t
HUF_readStats_body(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                   U32* nbSymbolsPtr, U32* tableLogPtr,
                   const void* src, size_t srcSize,
                   void* workSpace, size_t wkspSize,
                   int bmi2)
{
    U32 weightTotal;
    const BYTE* ip = (const BYTE*) src;
    size_t iSize;
    size_t oSize;

    if (!srcSize) return ERROR(srcSize_wrong);
    iSize = ip[0];
    /* ZSTD_memset(huffWeight, 0, hwSize);   *//* is not necessary, even though some analyzer complain ... */

    if (iSize >= 128) {  /* special header */
        oSize = iSize - 127;
        iSize = ((oSize+1)/2);
        if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
        if (oSize >= hwSize) return ERROR(corruption_detected);
        ip += 1;
        {   U32 n;
            for (n=0; n<oSize; n+=2) {
                huffWeight[n]   = ip[n/2] >> 4;
                huffWeight[n+1] = ip[n/2] & 15;
    }   }   }
    else  {   /* header compressed with FSE (normal case) */
        if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
        /* max (hwSize-1) values decoded, as last one is implied */
        oSize = FSE_decompress_wksp_bmi2(huffWeight, hwSize-1, ip+1, iSize, 6, workSpace, wkspSize, bmi2);
        if (FSE_isError(oSize)) return oSize;
    }

    /* collect weight stats */
    ZSTD_memset(rankStats, 0, (HUF_TABLELOG_MAX + 1) * sizeof(U32));
    weightTotal = 0;
    {   U32 n; for (n=0; n<oSize; n++) {
            if (huffWeight[n] > HUF_TABLELOG_MAX) return ERROR(corruption_detected);
            rankStats[huffWeight[n]]++;
            weightTotal += (1 << huffWeight[n]) >> 1;
    }   }
    if (weightTotal == 0) return ERROR(corruption_detected);

    /* get last non-null symbol weight (implied, total must be 2^n) */
    {   U32 const tableLog = ZSTD_highbit32(weightTotal) + 1;
        if (tableLog > HUF_TABLELOG_MAX) return ERROR(corruption_detected);
        *tableLogPtr = tableLog;
        /* determine last weight */
        {   U32 const total = 1 << tableLog;
            U32 const rest = total - weightTotal;
            U32 const verif = 1 << ZSTD_highbit32(rest);
            U32 const lastWeight = ZSTD_highbit32(rest) + 1;
            if (verif != rest) return ERROR(corruption_detected);    /* last value must be a clean power of 2 */
            huffWeight[oSize] = (BYTE)lastWeight;
            rankStats[lastWeight]++;
    }   }

    /* check tree construction validity */
    if ((rankStats[1] < 2) || (rankStats[1] & 1)) return ERROR(corruption_detected);   /* by construction : at least 2 elts of rank 1, must be even */

    /* results */
    *nbSymbolsPtr = (U32)(oSize+1);
    return iSize+1;
}

/* Avoids the FORCE_INLINE of the _body() function. */
static size_t HUF_readStats_body_default(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize,
                     void* workSpace, size_t wkspSize)
{
    return HUF_readStats_body(huffWeight, hwSize, rankStats, nbSymbolsPtr, tableLogPtr, src, srcSize, workSpace, wkspSize, 0);
}

#if DYNAMIC_BMI2
static BMI2_TARGET_ATTRIBUTE size_t HUF_readStats_body_bmi2(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize,
                     void* workSpace, size_t wkspSize)
{
    return HUF_readStats_body(huffWeight, hwSize, rankStats, nbSymbolsPtr, tableLogPtr, src, srcSize, workSpace, wkspSize, 1);
}
#endif

size_t HUF_readStats_wksp(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize,
                     void* workSpace, size_t wkspSize,
                     int flags)
{
#if DYNAMIC_BMI2
    if (flags & HUF_flags_bmi2) {
        return HUF_readStats_body_bmi2(huffWeight, hwSize, rankStats, nbSymbolsPtr, tableLogPtr, src, srcSize, workSpace, wkspSize);
    }
#endif
    (void)flags;
    return HUF_readStats_body_default(huffWeight, hwSize, rankStats, nbSymbolsPtr, tableLogPtr, src, srcSize, workSpace, wkspSize);
}
