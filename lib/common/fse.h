/* ******************************************************************
 * FSE : Finite State Entropy codec
 * Public Prototypes declaration
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */
#ifndef FSE_H
#define FSE_H


/*-*****************************************
*  Dependencies
******************************************/
#include "zstd_deps.h"    /* size_t, ptrdiff_t */

/*-*****************************************
*  FSE_PUBLIC_API : control library symbols visibility
******************************************/
#if defined(FSE_DLL_EXPORT) && (FSE_DLL_EXPORT==1) && defined(__GNUC__) && (__GNUC__ >= 4)
#  define FSE_PUBLIC_API __attribute__ ((visibility ("default")))
#elif defined(FSE_DLL_EXPORT) && (FSE_DLL_EXPORT==1)   /* Visual expected */
#  define FSE_PUBLIC_API __declspec(dllexport)
#elif defined(FSE_DLL_IMPORT) && (FSE_DLL_IMPORT==1)
#  define FSE_PUBLIC_API __declspec(dllimport) /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define FSE_PUBLIC_API
#endif

/*------   Version   ------*/
#define FSE_VERSION_MAJOR    0
#define FSE_VERSION_MINOR    9
#define FSE_VERSION_RELEASE  0

#define FSE_LIB_VERSION FSE_VERSION_MAJOR.FSE_VERSION_MINOR.FSE_VERSION_RELEASE
#define FSE_QUOTE(str) #str
#define FSE_EXPAND_AND_QUOTE(str) FSE_QUOTE(str)
#define FSE_VERSION_STRING FSE_EXPAND_AND_QUOTE(FSE_LIB_VERSION)

#define FSE_VERSION_NUMBER  (FSE_VERSION_MAJOR *100*100 + FSE_VERSION_MINOR *100 + FSE_VERSION_RELEASE)
FSE_PUBLIC_API unsigned FSE_versionNumber(void);   /**< library version number; to be used when checking dll version */


/*-*****************************************
*  Tool functions
******************************************/
FSE_PUBLIC_API size_t FSE_compressBound(size_t size);       /* maximum compressed size */

/* Error Management */
FSE_PUBLIC_API unsigned    FSE_isError(size_t code);        /* tells if a return value is an error code */
FSE_PUBLIC_API const char* FSE_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/*-*****************************************
*  FSE detailed API
******************************************/
/*!
FSE_compress() does the following:
1. count symbol occurrence from source[] into table count[] (see hist.h)
2. normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
3. save normalized counters to memory buffer using writeNCount()
4. build encoding table 'CTable' from normalized counters
5. encode the data stream using encoding table 'CTable'

FSE_decompress() does the following:
1. read normalized counters with readNCount()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using external method.
*/

/* *** COMPRESSION *** */

/*! FSE_optimalTableLog():
    dynamically downsize 'tableLog' when conditions are met.
    It saves CPU time, by using smaller tables, while preserving or even improving compression ratio.
    @return : recommended tableLog (necessarily <= 'maxTableLog') */
FSE_PUBLIC_API unsigned FSE_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);

/*! FSE_normalizeCount():
    normalize counts so that sum(count[]) == Power_of_2 (2^tableLog)
    'normalizedCounter' is a table of short, of minimum size (maxSymbolValue+1).
    useLowProbCount is a boolean parameter which trades off compressed size for
    faster header decoding. When it is set to 1, the compressed data will be slightly
    smaller. And when it is set to 0, FSE_readNCount() and FSE_buildDTable() will be
    faster. If you are compressing a small amount of data (< 2 KB) then useLowProbCount=0
    is a good default, since header deserialization makes a big speed difference.
    Otherwise, useLowProbCount=1 is a good default, since the speed difference is small.
    @return : tableLog,
              or an errorCode, which can be tested using FSE_isError() */
FSE_PUBLIC_API size_t FSE_normalizeCount(short* normalizedCounter, unsigned tableLog,
                    const unsigned* count, size_t srcSize, unsigned maxSymbolValue, unsigned useLowProbCount);

/*! FSE_NCountWriteBound():
    Provides the maximum possible size of an FSE normalized table, given 'maxSymbolValue' and 'tableLog'.
    Typically useful for allocation purpose. */
FSE_PUBLIC_API size_t FSE_NCountWriteBound(unsigned maxSymbolValue, unsigned tableLog);

/*! FSE_writeNCount():
    Compactly save 'normalizedCounter' into 'buffer'.
    @return : size of the compressed table,
              or an errorCode, which can be tested using FSE_isError(). */
FSE_PUBLIC_API size_t FSE_writeNCount (void* buffer, size_t bufferSize,
                                 const short* normalizedCounter,
                                 unsigned maxSymbolValue, unsigned tableLog, unsigned useLowProbCount);

/*! Constructor and Destructor of FSE_CTable.
    Note that FSE_CTable size depends on 'tableLog' and 'maxSymbolValue' */
typedef unsigned FSE_CTable;   /* don't allocate that. It's only meant to be more restrictive than void* */

/*! FSE_buildCTable():
    Builds `ct`, which must be already allocated, using FSE_createCTable().
    @return : 0, or an errorCode, which can be tested using FSE_isError() */
FSE_PUBLIC_API size_t FSE_buildCTable(FSE_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! FSE_compress_usingCTable():
    Compress `src` using `ct` into `dst` which must be already allocated.
    @return : size of compressed data (<= `dstCapacity`),
              or 0 if compressed data could not fit into `dst`,
              or an errorCode, which can be tested using FSE_isError() */
FSE_PUBLIC_API size_t FSE_compress_usingCTable (void* dst, size_t dstCapacity, const void* src, size_t srcSize, const FSE_CTable* ct);

/*!
Tutorial :
----------
The first step is to count all symbols. FSE_count() does this job very fast.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxSymbolValuePtr[0]+1' cells.
'src' is a table of bytes of size 'srcSize'. All values within 'src' MUST be <= maxSymbolValuePtr[0]
maxSymbolValuePtr[0] will be updated, with its real value (necessarily <= original value)
FSE_count() will return the number of occurrence of the most frequent symbol.
This can be used to know if there is a single symbol within 'src', and to quickly evaluate its compressibility.
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).

The next step is to normalize the frequencies.
FSE_normalizeCount() will ensure that sum of frequencies is == 2 ^'tableLog'.
It also guarantees a minimum of 1 to any Symbol with frequency >= 1.
You can use 'tableLog'==0 to mean "use default tableLog value".
If you are unsure of which tableLog value to use, you can ask FSE_optimalTableLog(),
which will provide the optimal valid tableLog given sourceSize, maxSymbolValue, and a user-defined maximum (0 means "default").

The result of FSE_normalizeCount() will be saved into a table,
called 'normalizedCounter', which is a table of signed short.
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValue+1' cells.
The return value is tableLog if everything proceeded as expected.
It is 0 if there is a single symbol within distribution.
If there is an error (ex: invalid tableLog value), the function will return an ErrorCode (which can be tested using FSE_isError()).

'normalizedCounter' can be saved in a compact manner to a memory area using FSE_writeNCount().
'buffer' must be already allocated.
For guaranteed success, buffer size must be at least FSE_headerBound().
The result of the function is the number of bytes written into 'buffer'.
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError(); ex : buffer size too small).

'normalizedCounter' can then be used to create the compression table 'CTable'.
The space required by 'CTable' must be already allocated, using FSE_createCTable().
You can then use FSE_buildCTable() to fill 'CTable'.
If there is an error, both functions will return an ErrorCode (which can be tested using FSE_isError()).

'CTable' can then be used to compress 'src', with FSE_compress_usingCTable().
Similar to FSE_count(), the convention is that 'src' is assumed to be a table of char of size 'srcSize'
The function returns the size of compressed data (without header), necessarily <= `dstCapacity`.
If it returns '0', compressed data could not fit into 'dst'.
If there is an error, the function will return an ErrorCode (which can be tested using FSE_isError()).
*/


/* *** DECOMPRESSION *** */

/*! FSE_readNCount():
    Read compactly saved 'normalizedCounter' from 'rBuffer'.
    @return : size read from 'rBuffer',
              or an errorCode, which can be tested using FSE_isError().
              maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
FSE_PUBLIC_API size_t FSE_readNCount (short* normalizedCounter,
                           unsigned* maxSymbolValuePtr, unsigned* tableLogPtr,
                           const void* rBuffer, size_t rBuffSize);

/*! FSE_readNCount_bmi2():
 * Same as FSE_readNCount() but pass bmi2=1 when your CPU supports BMI2 and 0 otherwise.
 */
FSE_PUBLIC_API size_t FSE_readNCount_bmi2(short* normalizedCounter,
                           unsigned* maxSymbolValuePtr, unsigned* tableLogPtr,
                           const void* rBuffer, size_t rBuffSize, int bmi2);

typedef unsigned FSE_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */

/*!
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by FSE_readNCount() if it was saved using FSE_writeNCount().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of signed short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSE_readNCount() will provide 'tableLog' and 'maxSymbolValue'.
The result of FSE_readNCount() is the number of bytes read from 'rBuffer'.
Note that 'rBufferSize' must be at least 4 bytes, even if useful information is less than that.
If there is an error, the function will return an error code, which can be tested using FSE_isError().

The next step is to build the decompression tables 'FSE_DTable' from 'normalizedCounter'.
This is performed by the function FSE_buildDTable().
The space required by 'FSE_DTable' must be already allocated using FSE_createDTable().
If there is an error, the function will return an error code, which can be tested using FSE_isError().

`FSE_DTable` can then be used to decompress `cSrc`, with FSE_decompress_usingDTable().
`cSrcSize` must be strictly correct, otherwise decompression will fail.
FSE_decompress_usingDTable() result will tell how many bytes were regenerated (<=`dstCapacity`).
If there is an error, the function will return an error code, which can be tested using FSE_isError(). (ex: dst buffer too small)
*/

#endif  /* FSE_H */


#if defined(FSE_STATIC_LINKING_ONLY) && !defined(FSE_H_FSE_STATIC_LINKING_ONLY)
#define FSE_H_FSE_STATIC_LINKING_ONLY
#include "bitstream.h"

/* *****************************************
*  Static allocation
*******************************************/
/* FSE buffer bounds */
#define FSE_NCOUNTBOUND 512
#define FSE_BLOCKBOUND(size) ((size) + ((size)>>7) + 4 /* fse states */ + sizeof(size_t) /* bitContainer */)
#define FSE_COMPRESSBOUND(size) (FSE_NCOUNTBOUND + FSE_BLOCKBOUND(size))   /* Macro version, useful for static allocation */

/* It is possible to statically allocate FSE CTable/DTable as a table of FSE_CTable/FSE_DTable using below macros */
#define FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue)   (1 + (1<<((maxTableLog)-1)) + (((maxSymbolValue)+1)*2))
#define FSE_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<(maxTableLog)))

/* or use the size to malloc() space directly. Pay attention to alignment restrictions though */
#define FSE_CTABLE_SIZE(maxTableLog, maxSymbolValue)   (FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) * sizeof(FSE_CTable))
#define FSE_DTABLE_SIZE(maxTableLog)                   (FSE_DTABLE_SIZE_U32(maxTableLog) * sizeof(FSE_DTable))


/* *****************************************
 *  FSE advanced API
 ***************************************** */

unsigned FSE_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
/**< same as FSE_optimalTableLog(), which used `minus==2` */

size_t FSE_buildCTable_rle (FSE_CTable* ct, unsigned char symbolValue);
/**< build a fake FSE_CTable, designed to compress always the same symbolValue */

/* FSE_buildCTable_wksp() :
 * Same as FSE_buildCTable(), but using an externally allocated scratch buffer (`workSpace`).
 * `wkspSize` must be >= `FSE_BUILD_CTABLE_WORKSPACE_SIZE_U32(maxSymbolValue, tableLog)` of `unsigned`.
 * See FSE_buildCTable_wksp() for breakdown of workspace usage.
 */
#define FSE_BUILD_CTABLE_WORKSPACE_SIZE_U32(maxSymbolValue, tableLog) (((maxSymbolValue + 2) + (1ull << (tableLog)))/2 + sizeof(U64)/sizeof(U32) /* additional 8 bytes for potential table overwrite */)
#define FSE_BUILD_CTABLE_WORKSPACE_SIZE(maxSymbolValue, tableLog) (sizeof(unsigned) * FSE_BUILD_CTABLE_WORKSPACE_SIZE_U32(maxSymbolValue, tableLog))
size_t FSE_buildCTable_wksp(FSE_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);

#define FSE_BUILD_DTABLE_WKSP_SIZE(maxTableLog, maxSymbolValue) (sizeof(short) * (maxSymbolValue + 1) + (1ULL << maxTableLog) + 8)
#define FSE_BUILD_DTABLE_WKSP_SIZE_U32(maxTableLog, maxSymbolValue) ((FSE_BUILD_DTABLE_WKSP_SIZE(maxTableLog, maxSymbolValue) + sizeof(unsigned) - 1) / sizeof(unsigned))
FSE_PUBLIC_API size_t FSE_buildDTable_wksp(FSE_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);
/**< Same as FSE_buildDTable(), using an externally allocated `workspace` produced with `FSE_BUILD_DTABLE_WKSP_SIZE_U32(maxSymbolValue)` */

#define FSE_DECOMPRESS_WKSP_SIZE_U32(maxTableLog, maxSymbolValue) (FSE_DTABLE_SIZE_U32(maxTableLog) + 1 + FSE_BUILD_DTABLE_WKSP_SIZE_U32(maxTableLog, maxSymbolValue) + (FSE_MAX_SYMBOL_VALUE + 1) / 2 + 1)
#define FSE_DECOMPRESS_WKSP_SIZE(maxTableLog, maxSymbolValue) (FSE_DECOMPRESS_WKSP_SIZE_U32(maxTableLog, maxSymbolValue) * sizeof(unsigned))
size_t FSE_decompress_wksp_bmi2(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, unsigned maxLog, void* workSpace, size_t wkspSize, int bmi2);
/**< same as FSE_decompress(), using an externally allocated `workSpace` produced with `FSE_DECOMPRESS_WKSP_SIZE_U32(maxLog, maxSymbolValue)`.
 * Set bmi2 to 1 if your CPU supports BMI2 or 0 if it doesn't */

typedef enum {
   FSE_repeat_none,  /**< Cannot use the previous table */
   FSE_repeat_check, /**< Can use the previous table but it must be checked */
   FSE_repeat_valid  /**< Can use the previous table and it is assumed to be valid */
 } FSE_repeat;

/* *****************************************
*  FSE symbol compression API
*******************************************/
/*!
   This API consists of small unitary functions, which highly benefit from being inlined.
   Hence their body are included in next section.
*/
typedef struct {
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} FSE_CState_t;

static void FSE_initCState(FSE_CState_t* CStatePtr, const FSE_CTable* ct);

static void FSE_encodeSymbol(BIT_CStream_t* bitC, FSE_CState_t* CStatePtr, unsigned symbol);

static void FSE_flushCState(BIT_CStream_t* bitC, const FSE_CState_t* CStatePtr);

/**<
These functions are inner components of FSE_compress_usingCTable().
They allow the creation of custom streams, mixing multiple tables and bit sources.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a LIFO stack.

You will need a few variables to track your CStream. They are :

FSE_CTable    ct;         // Provided by FSE_buildCTable()
BIT_CStream_t bitStream;  // bitStream tracking structure
FSE_CState_t  state;      // State tracking structure (can have several)


The first thing to do is to init bitStream and state.
    size_t errorCode = BIT_initCStream(&bitStream, dstBuffer, maxDstSize);
    FSE_initCState(&state, ct);

Note that BIT_initCStream() can produce an error code, so its result should be tested, using FSE_isError();
You can then encode your input data, byte after byte.
FSE_encodeSymbol() outputs a maximum of 'tableLog' bits at a time.
Remember decoding will be done in reverse direction.
    FSE_encodeByte(&bitStream, &state, symbol);

At any time, you can also add any bit sequence.
Note : maximum allowed nbBits is 25, for compatibility with 32-bits decoders
    BIT_addBits(&bitStream, bitField, nbBits);

The above methods don't commit data to memory, they just store it into local register, for speed.
Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
Writing data to memory is a manual operation, performed by the flushBits function.
    BIT_flushBits(&bitStream);

Your last FSE encoding operation shall be to flush your last state value(s).
    FSE_flushState(&bitStream, &state);

Finally, you must close the bitStream.
The function returns the size of CStream in bytes.
If data couldn't fit into dstBuffer, it will return a 0 ( == not compressible)
If there is an error, it returns an errorCode (which can be tested using FSE_isError()).
    size_t size = BIT_closeCStream(&bitStream);
*/


/* *****************************************
*  FSE symbol decompression API
*******************************************/
typedef struct {
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} FSE_DState_t;


static void     FSE_initDState(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD, const FSE_DTable* dt);

static unsigned char FSE_decodeSymbol(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD);

static unsigned FSE_endOfDState(const FSE_DState_t* DStatePtr);

/**<
Let's now decompose FSE_decompress_usingDTable() into its unitary components.
You will decode FSE-encoded symbols from the bitStream,
and also any other bitFields you put in, **in reverse order**.

You will need a few variables to track your bitStream. They are :

BIT_DStream_t DStream;    // Stream context
FSE_DState_t  DState;     // State context. Multiple ones are possible
FSE_DTable*   DTablePtr;  // Decoding table, provided by FSE_buildDTable()

The first thing to do is to init the bitStream.
    errorCode = BIT_initDStream(&DStream, srcBuffer, srcSize);

You should then retrieve your initial state(s)
(in reverse flushing order if you have several ones) :
    errorCode = FSE_initDState(&DState, &DStream, DTablePtr);

You can then decode your data, symbol after symbol.
For information the maximum number of bits read by FSE_decodeSymbol() is 'tableLog'.
Keep in mind that symbols are decoded in reverse order, like a LIFO stack (last in, first out).
    unsigned char symbol = FSE_decodeSymbol(&DState, &DStream);

You can retrieve any bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25, for 32-bits compatibility
    size_t bitField = BIT_readBits(&DStream, nbBits);

All above operations only read from local register (which size depends on size_t).
Refueling the register from memory is manually performed by the reload method.
    endSignal = FSE_reloadDStream(&DStream);

BIT_reloadDStream() result tells if there is still some more data to read from DStream.
BIT_DStream_unfinished : there is still some data left into the DStream.
BIT_DStream_endOfBuffer : Dstream reached end of buffer. Its container may no longer be completely filled.
BIT_DStream_completed : Dstream reached its exact end, corresponding in general to decompression completed.
BIT_DStream_tooFar : Dstream went too far. Decompression result is corrupted.

When reaching end of buffer (BIT_DStream_endOfBuffer), progress slowly, notably if you decode multiple symbols per loop,
to properly detect the exact end of stream.
After each decoded symbol, check if DStream is fully consumed using this simple test :
    BIT_reloadDStream(&DStream) >= BIT_DStream_completed

When it's done, verify decompression is fully completed, by checking both DStream and the relevant states.
Checking if DStream has reached its end is performed by :
    BIT_endOfDStream(&DStream);
Check also the states. There might be some symbols left there, if some high probability ones (>50%) are possible.
    FSE_endOfDState(&DState);
*/


/* *****************************************
*  FSE unsafe API
*******************************************/
static unsigned char FSE_decodeSymbolFast(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD);
/* faster, but works only if nbBits is always >= 1 (otherwise, result will be corrupted) */


/* *****************************************
*  Implementation of inlined functions
*******************************************/
typedef struct {
    int deltaFindState;
    U32 deltaNbBits;
} FSE_symbolCompressionTransform; /* total 8 bytes */

MEM_STATIC void FSE_initCState(FSE_CState_t* statePtr, const FSE_CTable* ct)
{
    const void* ptr = ct;
    const U16* u16ptr = (const U16*) ptr;
    const U32 tableLog = MEM_read16(ptr);
    statePtr->value = (ptrdiff_t)1<<tableLog;
    statePtr->stateTable = u16ptr+2;
    statePtr->symbolTT = ct + 1 + (tableLog ? (1<<(tableLog-1)) : 1);
    statePtr->stateLog = tableLog;
}


/*! FSE_initCState2() :
*   Same as FSE_initCState(), but the first symbol to include (which will be the last to be read)
*   uses the smallest state value possible, saving the cost of this symbol */
MEM_STATIC void FSE_initCState2(FSE_CState_t* statePtr, const FSE_CTable* ct, U32 symbol)
{
    FSE_initCState(statePtr, ct);
    {   const FSE_symbolCompressionTransform symbolTT = ((const FSE_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
        const U16* stateTable = (const U16*)(statePtr->stateTable);
        U32 nbBitsOut  = (U32)((symbolTT.deltaNbBits + (1<<15)) >> 16);
        statePtr->value = (nbBitsOut << 16) - symbolTT.deltaNbBits;
        statePtr->value = stateTable[(statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
    }
}

MEM_STATIC void FSE_encodeSymbol(BIT_CStream_t* bitC, FSE_CState_t* statePtr, unsigned symbol)
{
    FSE_symbolCompressionTransform const symbolTT = ((const FSE_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
    const U16* const stateTable = (const U16*)(statePtr->stateTable);
    U32 const nbBitsOut  = (U32)((statePtr->value + symbolTT.deltaNbBits) >> 16);
    BIT_addBits(bitC, (BitContainerType)statePtr->value, nbBitsOut);
    statePtr->value = stateTable[ (statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
}

MEM_STATIC void FSE_flushCState(BIT_CStream_t* bitC, const FSE_CState_t* statePtr)
{
    BIT_addBits(bitC, (BitContainerType)statePtr->value, statePtr->stateLog);
    BIT_flushBits(bitC);
}


/* FSE_getMaxNbBits() :
 * Approximate maximum cost of a symbol, in bits.
 * Fractional get rounded up (i.e. a symbol with a normalized frequency of 3 gives the same result as a frequency of 2)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
MEM_STATIC U32 FSE_getMaxNbBits(const void* symbolTTPtr, U32 symbolValue)
{
    const FSE_symbolCompressionTransform* symbolTT = (const FSE_symbolCompressionTransform*) symbolTTPtr;
    return (symbolTT[symbolValue].deltaNbBits + ((1<<16)-1)) >> 16;
}

/* FSE_bitCost() :
 * Approximate symbol cost, as fractional value, using fixed-point format (accuracyLog fractional bits)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
MEM_STATIC U32 FSE_bitCost(const void* symbolTTPtr, U32 tableLog, U32 symbolValue, U32 accuracyLog)
{
    const FSE_symbolCompressionTransform* symbolTT = (const FSE_symbolCompressionTransform*) symbolTTPtr;
    U32 const minNbBits = symbolTT[symbolValue].deltaNbBits >> 16;
    U32 const threshold = (minNbBits+1) << 16;
    assert(tableLog < 16);
    assert(accuracyLog < 31-tableLog);  /* ensure enough room for renormalization double shift */
    {   U32 const tableSize = 1 << tableLog;
        U32 const deltaFromThreshold = threshold - (symbolTT[symbolValue].deltaNbBits + tableSize);
        U32 const normalizedDeltaFromThreshold = (deltaFromThreshold << accuracyLog) >> tableLog;   /* linear interpolation (very approximate) */
        U32 const bitMultiplier = 1 << accuracyLog;
        assert(symbolTT[symbolValue].deltaNbBits + tableSize <= threshold);
        assert(normalizedDeltaFromThreshold <= bitMultiplier);
        return (minNbBits+1)*bitMultiplier - normalizedDeltaFromThreshold;
    }
}


/* ======    Decompression    ====== */

typedef struct {
    U16 tableLog;
    U16 fastMode;
} FSE_DTableHeader;   /* sizeof U32 */

typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} FSE_decode_t;   /* size == U32 */

MEM_STATIC void FSE_initDState(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD, const FSE_DTable* dt)
{
    const void* ptr = dt;
    const FSE_DTableHeader* const DTableH = (const FSE_DTableHeader*)ptr;
    DStatePtr->state = BIT_readBits(bitD, DTableH->tableLog);
    BIT_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

MEM_STATIC BYTE FSE_peekSymbol(const FSE_DState_t* DStatePtr)
{
    FSE_decode_t const DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    return DInfo.symbol;
}

MEM_STATIC void FSE_updateState(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD)
{
    FSE_decode_t const DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BIT_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;
}

MEM_STATIC BYTE FSE_decodeSymbol(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD)
{
    FSE_decode_t const DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BIT_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/*! FSE_decodeSymbolFast() :
    unsafe, only works if no symbol has a probability > 50% */
MEM_STATIC BYTE FSE_decodeSymbolFast(FSE_DState_t* DStatePtr, BIT_DStream_t* bitD)
{
    FSE_decode_t const DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BIT_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

MEM_STATIC unsigned FSE_endOfDState(const FSE_DState_t* DStatePtr)
{
    return DStatePtr->state == 0;
}

/* *****************************************
*  ZBIC support
*******************************************/
/*!
   Precomputed table of (middle, fist, last) indices.
   Used during binary interpolative coding of entropy tables when supported.
*/
static UNUSED_ATTR const U32 BIC_table[0x300] = {
    0x80, 0x0, 0x100, 0xC0, 0x80, 0x100, 0xE0, 0xC0,
    0x100, 0xF0, 0xE0, 0x100, 0xF8, 0xF0, 0x100, 0xFC,
    0xF8, 0x100, 0xFE, 0xFC, 0x100, 0xFF, 0xFE, 0x100,
    0xFD, 0xFC, 0xFE, 0xFA, 0xF8, 0xFC, 0xFB, 0xFA,
    0xFC, 0xF9, 0xF8, 0xFA, 0xF4, 0xF0, 0xF8, 0xF6,
    0xF4, 0xF8, 0xF7, 0xF6, 0xF8, 0xF5, 0xF4, 0xF6,
    0xF2, 0xF0, 0xF4, 0xF3, 0xF2, 0xF4, 0xF1, 0xF0,
    0xF2, 0xE8, 0xE0, 0xF0, 0xEC, 0xE8, 0xF0, 0xEE,
    0xEC, 0xF0, 0xEF, 0xEE, 0xF0, 0xED, 0xEC, 0xEE,
    0xEA, 0xE8, 0xEC, 0xEB, 0xEA, 0xEC, 0xE9, 0xE8,
    0xEA, 0xE4, 0xE0, 0xE8, 0xE6, 0xE4, 0xE8, 0xE7,
    0xE6, 0xE8, 0xE5, 0xE4, 0xE6, 0xE2, 0xE0, 0xE4,
    0xE3, 0xE2, 0xE4, 0xE1, 0xE0, 0xE2, 0xD0, 0xC0,
    0xE0, 0xD8, 0xD0, 0xE0, 0xDC, 0xD8, 0xE0, 0xDE,
    0xDC, 0xE0, 0xDF, 0xDE, 0xE0, 0xDD, 0xDC, 0xDE,
    0xDA, 0xD8, 0xDC, 0xDB, 0xDA, 0xDC, 0xD9, 0xD8,
    0xDA, 0xD4, 0xD0, 0xD8, 0xD6, 0xD4, 0xD8, 0xD7,
    0xD6, 0xD8, 0xD5, 0xD4, 0xD6, 0xD2, 0xD0, 0xD4,
    0xD3, 0xD2, 0xD4, 0xD1, 0xD0, 0xD2, 0xC8, 0xC0,
    0xD0, 0xCC, 0xC8, 0xD0, 0xCE, 0xCC, 0xD0, 0xCF,
    0xCE, 0xD0, 0xCD, 0xCC, 0xCE, 0xCA, 0xC8, 0xCC,
    0xCB, 0xCA, 0xCC, 0xC9, 0xC8, 0xCA, 0xC4, 0xC0,
    0xC8, 0xC6, 0xC4, 0xC8, 0xC7, 0xC6, 0xC8, 0xC5,
    0xC4, 0xC6, 0xC2, 0xC0, 0xC4, 0xC3, 0xC2, 0xC4,
    0xC1, 0xC0, 0xC2, 0xA0, 0x80, 0xC0, 0xB0, 0xA0,
    0xC0, 0xB8, 0xB0, 0xC0, 0xBC, 0xB8, 0xC0, 0xBE,
    0xBC, 0xC0, 0xBF, 0xBE, 0xC0, 0xBD, 0xBC, 0xBE,
    0xBA, 0xB8, 0xBC, 0xBB, 0xBA, 0xBC, 0xB9, 0xB8,
    0xBA, 0xB4, 0xB0, 0xB8, 0xB6, 0xB4, 0xB8, 0xB7,
    0xB6, 0xB8, 0xB5, 0xB4, 0xB6, 0xB2, 0xB0, 0xB4,
    0xB3, 0xB2, 0xB4, 0xB1, 0xB0, 0xB2, 0xA8, 0xA0,
    0xB0, 0xAC, 0xA8, 0xB0, 0xAE, 0xAC, 0xB0, 0xAF,
    0xAE, 0xB0, 0xAD, 0xAC, 0xAE, 0xAA, 0xA8, 0xAC,
    0xAB, 0xAA, 0xAC, 0xA9, 0xA8, 0xAA, 0xA4, 0xA0,
    0xA8, 0xA6, 0xA4, 0xA8, 0xA7, 0xA6, 0xA8, 0xA5,
    0xA4, 0xA6, 0xA2, 0xA0, 0xA4, 0xA3, 0xA2, 0xA4,
    0xA1, 0xA0, 0xA2, 0x90, 0x80, 0xA0, 0x98, 0x90,
    0xA0, 0x9C, 0x98, 0xA0, 0x9E, 0x9C, 0xA0, 0x9F,
    0x9E, 0xA0, 0x9D, 0x9C, 0x9E, 0x9A, 0x98, 0x9C,
    0x9B, 0x9A, 0x9C, 0x99, 0x98, 0x9A, 0x94, 0x90,
    0x98, 0x96, 0x94, 0x98, 0x97, 0x96, 0x98, 0x95,
    0x94, 0x96, 0x92, 0x90, 0x94, 0x93, 0x92, 0x94,
    0x91, 0x90, 0x92, 0x88, 0x80, 0x90, 0x8C, 0x88,
    0x90, 0x8E, 0x8C, 0x90, 0x8F, 0x8E, 0x90, 0x8D,
    0x8C, 0x8E, 0x8A, 0x88, 0x8C, 0x8B, 0x8A, 0x8C,
    0x89, 0x88, 0x8A, 0x84, 0x80, 0x88, 0x86, 0x84,
    0x88, 0x87, 0x86, 0x88, 0x85, 0x84, 0x86, 0x82,
    0x80, 0x84, 0x83, 0x82, 0x84, 0x81, 0x80, 0x82,
    0x40, 0x0, 0x80, 0x60, 0x40, 0x80, 0x70, 0x60,
    0x80, 0x78, 0x70, 0x80, 0x7C, 0x78, 0x80, 0x7E,
    0x7C, 0x80, 0x7F, 0x7E, 0x80, 0x7D, 0x7C, 0x7E,
    0x7A, 0x78, 0x7C, 0x7B, 0x7A, 0x7C, 0x79, 0x78,
    0x7A, 0x74, 0x70, 0x78, 0x76, 0x74, 0x78, 0x77,
    0x76, 0x78, 0x75, 0x74, 0x76, 0x72, 0x70, 0x74,
    0x73, 0x72, 0x74, 0x71, 0x70, 0x72, 0x68, 0x60,
    0x70, 0x6C, 0x68, 0x70, 0x6E, 0x6C, 0x70, 0x6F,
    0x6E, 0x70, 0x6D, 0x6C, 0x6E, 0x6A, 0x68, 0x6C,
    0x6B, 0x6A, 0x6C, 0x69, 0x68, 0x6A, 0x64, 0x60,
    0x68, 0x66, 0x64, 0x68, 0x67, 0x66, 0x68, 0x65,
    0x64, 0x66, 0x62, 0x60, 0x64, 0x63, 0x62, 0x64,
    0x61, 0x60, 0x62, 0x50, 0x40, 0x60, 0x58, 0x50,
    0x60, 0x5C, 0x58, 0x60, 0x5E, 0x5C, 0x60, 0x5F,
    0x5E, 0x60, 0x5D, 0x5C, 0x5E, 0x5A, 0x58, 0x5C,
    0x5B, 0x5A, 0x5C, 0x59, 0x58, 0x5A, 0x54, 0x50,
    0x58, 0x56, 0x54, 0x58, 0x57, 0x56, 0x58, 0x55,
    0x54, 0x56, 0x52, 0x50, 0x54, 0x53, 0x52, 0x54,
    0x51, 0x50, 0x52, 0x48, 0x40, 0x50, 0x4C, 0x48,
    0x50, 0x4E, 0x4C, 0x50, 0x4F, 0x4E, 0x50, 0x4D,
    0x4C, 0x4E, 0x4A, 0x48, 0x4C, 0x4B, 0x4A, 0x4C,
    0x49, 0x48, 0x4A, 0x44, 0x40, 0x48, 0x46, 0x44,
    0x48, 0x47, 0x46, 0x48, 0x45, 0x44, 0x46, 0x42,
    0x40, 0x44, 0x43, 0x42, 0x44, 0x41, 0x40, 0x42,
    0x20, 0x0, 0x40, 0x30, 0x20, 0x40, 0x38, 0x30,
    0x40, 0x3C, 0x38, 0x40, 0x3E, 0x3C, 0x40, 0x3F,
    0x3E, 0x40, 0x3D, 0x3C, 0x3E, 0x3A, 0x38, 0x3C,
    0x3B, 0x3A, 0x3C, 0x39, 0x38, 0x3A, 0x34, 0x30,
    0x38, 0x36, 0x34, 0x38, 0x37, 0x36, 0x38, 0x35,
    0x34, 0x36, 0x32, 0x30, 0x34, 0x33, 0x32, 0x34,
    0x31, 0x30, 0x32, 0x28, 0x20, 0x30, 0x2C, 0x28,
    0x30, 0x2E, 0x2C, 0x30, 0x2F, 0x2E, 0x30, 0x2D,
    0x2C, 0x2E, 0x2A, 0x28, 0x2C, 0x2B, 0x2A, 0x2C,
    0x29, 0x28, 0x2A, 0x24, 0x20, 0x28, 0x26, 0x24,
    0x28, 0x27, 0x26, 0x28, 0x25, 0x24, 0x26, 0x22,
    0x20, 0x24, 0x23, 0x22, 0x24, 0x21, 0x20, 0x22,
    0x10, 0x0, 0x20, 0x18, 0x10, 0x20, 0x1C, 0x18,
    0x20, 0x1E, 0x1C, 0x20, 0x1F, 0x1E, 0x20, 0x1D,
    0x1C, 0x1E, 0x1A, 0x18, 0x1C, 0x1B, 0x1A, 0x1C,
    0x19, 0x18, 0x1A, 0x14, 0x10, 0x18, 0x16, 0x14,
    0x18, 0x17, 0x16, 0x18, 0x15, 0x14, 0x16, 0x12,
    0x10, 0x14, 0x13, 0x12, 0x14, 0x11, 0x10, 0x12,
    0x8, 0x0, 0x10, 0xC, 0x8, 0x10, 0xE, 0xC,
    0x10, 0xF, 0xE, 0x10, 0xD, 0xC, 0xE, 0xA,
    0x8, 0xC, 0xB, 0xA, 0xC, 0x9, 0x8, 0xA,
    0x4, 0x0, 0x8, 0x6, 0x4, 0x8, 0x7, 0x6,
    0x8, 0x5, 0x4, 0x6, 0x2, 0x0, 0x4, 0x3,
    0x2, 0x4, 0x1, 0x0, 0x2, 0x0, 0x0, 0x1
};

#ifndef FSE_COMMONDEFS_ONLY

/* **************************************************************
*  Tuning parameters
****************************************************************/
/*!MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#ifndef FSE_MAX_MEMORY_USAGE
#  define FSE_MAX_MEMORY_USAGE 14
#endif
#ifndef FSE_DEFAULT_MEMORY_USAGE
#  define FSE_DEFAULT_MEMORY_USAGE 13
#endif
#if (FSE_DEFAULT_MEMORY_USAGE > FSE_MAX_MEMORY_USAGE)
#  error "FSE_DEFAULT_MEMORY_USAGE must be <= FSE_MAX_MEMORY_USAGE"
#endif

/*!FSE_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#ifndef FSE_MAX_SYMBOL_VALUE
#  define FSE_MAX_SYMBOL_VALUE 255
#endif

/* **************************************************************
*  template functions type & suffix
****************************************************************/
#define FSE_FUNCTION_TYPE BYTE
#define FSE_FUNCTION_EXTENSION
#define FSE_DECODE_TYPE FSE_decode_t


#endif   /* !FSE_COMMONDEFS_ONLY */


/* ***************************************************************
*  Constants
*****************************************************************/
#define FSE_MAX_TABLELOG  (FSE_MAX_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_DEFAULT_TABLELOG (FSE_DEFAULT_MEMORY_USAGE-2)
#define FSE_MIN_TABLELOG 5

#define FSE_TABLELOG_ABSOLUTE_MAX 15
#if FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX
#  error "FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define FSE_TABLESTEP(tableSize) (((tableSize)>>1) + ((tableSize)>>3) + 3)

#endif /* FSE_STATIC_LINKING_ONLY */
