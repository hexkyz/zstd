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
size_t FSE_readNCount_bic(short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                           const void* headerBuffer, size_t hbSize)
{    
    U32 bicCounter[257];
    ZSTD_memset(bicCounter, 0, sizeof(bicCounter));
    if (!hbSize) return ERROR(corruption_detected);
    const BYTE* ip = (const BYTE*) headerBuffer;
    U32 bitStream = *ip;
    size_t rawDataSize = bitStream & 0x7F;     /* Bitstream encodes the segment's raw data size in the first 7 bits. */
    U32 useLowProbCount = bitStream >> 7;
    if (rawDataSize >= hbSize) return ERROR(corruption_detected);
    size_t dataSize = rawDataSize + 1;
    if (dataSize >= hbSize) return ERROR(corruption_detected);
    
    U64 i, l = 0;
    int j, k = 0;
    
    for (i = 0; rawDataSize; i = *(ip + rawDataSize--) | (i << 8)) {
        if (((i >> 32) & 0xFFFFFFFF) >= 0x100) break;
    }
    
    U64 encodedCharTable = i / 0x34;  
    for (j = i % 0x34; rawDataSize; encodedCharTable = *(ip + rawDataSize--) | (encodedCharTable << 8)) {
        if (((encodedCharTable >> 32) & 0xFFFFFFFF) >= 0x100) break;
    }
    
    U32 charNum = j + 1;
    if (charNum > *maxSVPtr) return ERROR(corruption_detected);
    
    U64 charTable = encodedCharTable >> 3;
    for (k = encodedCharTable & 0x7; rawDataSize; charTable = *(ip + rawDataSize--) | (charTable << 8)) {
        if (((charTable >> 32) & 0xFFFFFFFF) >= 0x100) break;
    }
    
    U32 tableLog = k + 5;
    U32 remaining = 1 << tableLog;
    for (l = charTable / remaining; rawDataSize; l = *(ip + rawDataSize--) | (l << 8)) {
        if (((l >> 32) & 0xFFFFFFFF) >= 0x100) break;
    }
    
    /* Find the last entry of the symbol occurrence table. */
    int charLast = charTable % remaining + 1;
    if (useLowProbCount) charLast = charNum + charTable % remaining + 2;
    
    /* Strictly calculate the next power of 2. */ 
    U32 charNumNextPow2 = charNum;
    charNumNextPow2 |= (charNumNextPow2 >> 1);
    charNumNextPow2 |= (charNumNextPow2 >> 2);
    charNumNextPow2 |= (charNumNextPow2 >> 4);
    charNumNextPow2 |= (charNumNextPow2 >> 8);
    charNumNextPow2 |= (charNumNextPow2 >> 16);
    charNumNextPow2++;
    
    if (charNumNextPow2 > 0xFF) return ERROR(corruption_detected);
    bicCounter[charNumNextPow2] = charLast;
    
    /* Perform interpolative decoding (cumulative).
     * l encodes the symbol occurrence table as an U64.
     */
    U32 bicCount = 0;
    if (charNumNextPow2 != 0xFF) {
        do {
            U64 bicTableOffset = 3 * (bicCount - charNumNextPow2 + 0x100);
            U32 btMiddleIndex = BIC_table[bicTableOffset + 0];
            U32 btFirstIndex = BIC_table[bicTableOffset + 1];
            U32 btLastIndex = BIC_table[bicTableOffset + 2];
            U32 bcFirstIndexEntry = bicCounter[btFirstIndex];
            U32 bcLastIndexEntry = bicCounter[btLastIndex];
            
            if (bcFirstIndexEntry == bcLastIndexEntry) {
                /* Indices are the same.
                 * Update our counter array with the entry at the first index
                 * for the length of the sequence imin+1 to imax-1.
                */
                U32 bicCounterIndex = btFirstIndex + 1;
                if (bicCounterIndex < btLastIndex) {
                    U32 btIndexDist = btLastIndex - bicCounterIndex;
                    while (btIndexDist > 0) {
                        bicCounter[bicCounterIndex++] = bcFirstIndexEntry;
                        --btIndexDist;
                    }
                }
            } else {
                /* Do recursive interpolative decoding.
                 * l is updated by l div (lastIndexEntry - firstIndexEntry + 1).
                 * The entry at the middle index is decoded by l mod (lastIndexEntry - firstIndexEntry + 1) + firstIndexEntry.
                */
                U64 lNext = l / (bcLastIndexEntry - bcFirstIndexEntry + 1);
                U64 lEntry = l % (bcLastIndexEntry - bcFirstIndexEntry + 1);
                for (l = lNext; rawDataSize; l = *(ip + rawDataSize--) | (l << 8)) {
                    if (((l >> 32) & 0xFFFFFFFF) >= 0x100) break;
                }
                bicCounter[btMiddleIndex] = lEntry + bcFirstIndexEntry;
            }
            
            ++bicCount;
        } while (bicCount < charNumNextPow2);
    }
    
    if (charNum != 0xFF) {
        int accCount = 0;
        U32 *bc = &bicCounter[1];
        U32 charNumLeft = charNum + 1;
        do {
            short bcCount = *(short *)bc++;
            short countDist = bcCount - accCount;
            short count = countDist - useLowProbCount;
            accCount += countDist;
            *normalizedCounter++ = count;
            int weightedCount = count;
            if (count < 0) weightedCount = -count;
            remaining -= weightedCount;
            --charNumLeft;
        } while (charNumLeft);
    }
    
    if (remaining) return ERROR(corruption_detected);
    *maxSVPtr = charNum;
    *tableLogPtr = tableLog;
    if (rawDataSize) return ERROR(corruption_detected);
    
    return dataSize;
}

size_t FSE_readNCount_body(short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                           const void* headerBuffer, size_t hbSize)
{
#if ZSTD_ZBIC_SUPPORT
    return FSE_readNCount_bic(normalizedCounter, maxSVPtr, tableLogPtr, headerBuffer, hbSize);
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
