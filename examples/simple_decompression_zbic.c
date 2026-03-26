/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <stdio.h>     // printf
#include <stdlib.h>    // free
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>      // presumes zstd library is installed
#include "common.h"    // Helper functions, CHECK(), and CHECK_ZSTD()

static char* createOutFilename_orDie(const char* filename)
{
    size_t const inL = strlen(filename);
    size_t const outL = inL + 5;
    void* const outSpace = malloc_orDie(outL);
    memset(outSpace, 0, outL);
    strcat(outSpace, filename);
    strcat(outSpace, ".zst");
    return (char*)outSpace;
}

static void decompress(const char* fname)
{
    size_t cSize;
    void* const cBuff = mallocAndLoadFile_orDie(fname, &cSize);
    
    size_t margin = ZSTD_decompressionMargin(cBuff, cSize);
    
    if (!ZSTD_isError(margin)) {
        printf("Margin: 0x%lx\n", margin);
        
        size_t DCtxWorkspaceSize = ZSTD_estimateDCtxSize();
        printf("DCtxWorkspaceSize: 0x%lx\n", DCtxWorkspaceSize);
        
        void* const workspace = malloc(DCtxWorkspaceSize);
        ZSTD_DCtx* dctx = ZSTD_initStaticDCtx(workspace, DCtxWorkspaceSize);
        
        size_t dSize = cSize*4;
        void* const dBuff = malloc(dSize);
        
        size_t dec_size = ZSTD_decompressDCtx(dctx, dBuff, dSize, cBuff, cSize);
        
        if (!ZSTD_isError(dec_size)) {
            char* const outFilename = createOutFilename_orDie(fname);
            saveFile_orDie(outFilename, dBuff, dec_size);
        } else {
            printf("ZSTD_decompressDCtx failed!\n");
        }
        
        free(dBuff);
        free(workspace);
    } else {
        printf("ZSTD_decompressionMargin failed!\n");
    }
    
    free(cBuff);
}

int main(int argc, const char** argv)
{
    const char* const exeName = argv[0];

    if (argc!=2) {
        printf("wrong arguments\n");
        printf("usage:\n");
        printf("%s FILE\n", exeName);
        return 1;
    }

    decompress(argv[1]);

    return 0;
}
