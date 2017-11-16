/* sts_vertex_cache_optimizer - v0.02 - public domain triangle index optimizer
 no warranty implied; use at your own risk
 
 I have only tested this library with gcc and clang. If you successfully or
 unsuccesfully are able to use it with another compiler, please drop me a line
 on sigurdts <[at]> gmail dot com
 
 Do this:
 #define STS_VERTEX_CACHE_OPTIMIZER_IMPLEMENTATION
 before you include this file in *one* C or C++ file to create the implementation.
 
 // i.e. it should look like this:
 #include ...
 #include ...
 #include ...
 #define STS_VERTEX_CACHE_OPTIMIZER_IMPLEMENTATION
 #include "sts_vertex_cache_optimizer.h"
 
 LICENSE
 
 See end of file for license information.
 
 REVISION HISTORY:
 
 0.02  (2017-xx-xx) Initial public release
 */

#ifndef sts_vertex_cache_optimizer_h
#define sts_vertex_cache_optimizer_h

// DOCUMENTATION
//
//
// The triangle index algorithm implemented here was developed by Tom Forsyth.
// Read more about the algorithm here: https://tomforsyth1000.github.io/papers/fast_vert_cache_opt.html
//
// Vertex indices input has to be triangles, and the resulting optimized indices will be stored in same index buffer.
// The default cache inout size is 32, but the algorithm is universially good for all cache sizes regardless of
// the input cache size. So, if you don't know the cache size you are optimizing for, this is AFAIK as good
// as you can get.
//
// However, if you know the cache size you are optimizing for then you can change the default cache size.
//
// The algorithm is fast and runs in linear time, and is within a few percentages of the best known alternative
// algorithm (Tom Forsyth´s words).
//
// Example:
//
//  float ACMR_before = stsvco_compute_ACMR( indices, numIndices, 8 );
//  stsvco_optimize( indices, numIndices, numVertices );
//  float ACMR_after = stsvco_compute_ACMR( indices, numIndices, 8 );
//
//

#ifdef __cplusplus
extern "C" {
#endif

// Function that does the optimization.
// NOTE: numVertices has to equal the MAX vertex index in the indices.
extern void stsvco_optimize( unsigned int *indices, const unsigned int numIndices, const unsigned int numVertices, const int cacheSize = 32 );

// Function that computes ACMR (Average Cache Miss Ratio) for a given index list and cache size.
// The function returns the average number of cache misses per triangle.
extern float stsvco_compute_ACMR( const unsigned int *indices, const unsigned int numIndices, const unsigned int cacheSize );

#ifdef __cplusplus
};
#endif

#endif /* sts_vertex_cache_optimizer_h */

#ifdef STS_VERTEX_CACHE_OPTIMIZER_IMPLEMENTATION

#include <assert.h>

// Computes the score for a vertex with numTris using the vertex
const float stsvco_valenceScore( const int numTris ) {
    return 2*powf( numTris, -.5f );
}

void stsvco_optimize( unsigned int *indices, const unsigned int numIndices, const unsigned int numVertices, const int cacheSize ) {
    
    struct cacheVert {
        int numAdjecentTris;
        int numTrisLeft;
        int triListIndex;
        int cacheIndex;
    };
    
    struct cacheTris {
        int vertices[3];
        bool drawn;
    };
    
    cacheVert   vertices[ numVertices ];
    const int   numTriangles = numIndices/3;
    cacheTris   triangles[ numTriangles ];
    
    assert( !(numIndices % 3) && "Index input has to be triangles" );
    
    for( int v = 0; v < numVertices; ++v) {
        vertices[v].numAdjecentTris = 0;
        vertices[v].numTrisLeft = 0;
        vertices[v].triListIndex = 0;
        vertices[v].cacheIndex = -1;
    }
    
    for( int t = 0; t < numTriangles; ++t) {
        
        for(int v = 0; v < 3; ++v) {
            triangles[t].vertices[v] = indices[ t*3 +v ];
            ++vertices[triangles[t].vertices[v]].numAdjecentTris;
        };
        
        triangles[t].drawn = false;
    };
    
    // Loop through and find index for the tri list for vertex->tri
    for(int v = 1; v < numVertices; ++v ) {
        vertices[v].triListIndex = vertices[v-1].triListIndex+vertices[v-1].numAdjecentTris;
    }
    
    const int numVertToTri = vertices[numVertices-1].triListIndex+vertices[numVertices-1].numAdjecentTris;
    unsigned int vertToTri[ numVertToTri ];
    
    for(int t = 0; t < numTriangles; ++t) {
        for(int v = 0; v < 3; ++v) {
            const int index = triangles[t].vertices[v];
            const int triListIndex = vertices[ index ].triListIndex + vertices[index].numTrisLeft;
            vertToTri[ triListIndex ] = t;
            ++vertices[index].numTrisLeft;
        }
    }
    
    // Make LRU cache
    const int   LRUCacheSize = cacheSize;
    int         LRUCache[ LRUCacheSize ];
    float       scoring[ LRUCacheSize ];
    
    for(int i = 0; i < LRUCacheSize; ++i) {
        LRUCache[i] = -1;
        scoring[i] = -1.0f;
    }
    
    int numIndicesDone = 0;
    while( numIndicesDone != numIndices) {
        // update vertex scoring
        for( int i = 0; i < LRUCacheSize && LRUCache[i] >= 0; ++i){
            int vertexIndex = LRUCache[i];
            if( vertexIndex != -1 ) {
                // Do scoring based on cache position
                if( i < 3 ) {
                    scoring[i] = 0.75f;
                } else {
                    const float scaler = 1.0f / ( LRUCacheSize - 3 );
                    const float scoreBase = 1.0f - ( i - 3 ) * scaler;
                    scoring[i] = powf ( scoreBase, 1.5f );
                };
                // Add score based on tris left for vertex (valence score)
                const int numTrisLeft = vertices[ vertexIndex ].numTrisLeft;
                scoring[i] += stsvco_valenceScore(numTrisLeft);
            }
        }
        // find triangle to draw based on score
        // Update score for all triangles with vertexes in cache
        int triangleToDraw = -1;
        float bestTriScore = 0.0f;
        for(int i = 0; i < LRUCacheSize && LRUCache[i] >= 0; ++i) {
            const int vIndex = LRUCache[i];
            
            if( vertices[vIndex].numTrisLeft > 0 ) {
                for(int t = 0; t < vertices[vIndex].numAdjecentTris; ++t) {
                    const int tIndex = vertToTri[ vertices[vIndex].triListIndex + t];
                    if( !triangles[ tIndex ].drawn ) {
                        float triScore = .0f;
                        for(int v = 0; v < 3; ++v) {
                            const int cacheIndex = vertices[triangles[ tIndex ].vertices[v]].cacheIndex;
                            if( cacheIndex >= 0) {
                                triScore += scoring[ cacheIndex ];
                            };
                        }
                        
                        if( triScore > bestTriScore ) {
                            triangleToDraw = tIndex;
                            bestTriScore = triScore;
                        }
                    }
                }
            }
        }
        
        if( triangleToDraw < 0 ) {
            // No triangle can be found by heuristic, simply choose first and best
            for(int t = 0; t < numTriangles; ++t ) {
                if( !triangles[t].drawn ) {
                    //compute valence for each vertex
                    float triScore = .0f;
                    for(int v = 0; v < 3; ++v ) {
                        const unsigned int vertexIndex = triangles[t].vertices[v];
                        // Add score based on tris left for vertex (valence score)
                        const int numTrisLeft = vertices[ vertexIndex ].numTrisLeft;
                        triScore += stsvco_valenceScore(numTrisLeft);
                    }
                    if( triScore >= bestTriScore ) {
                        triangleToDraw = t;
                        bestTriScore = triScore;
                    }
                }
            }
        };
        
        // update cache
        int cacheIndex = 3;
        int numVerticesFound = 0;
        while( LRUCache[numVerticesFound] >= 0 && numVerticesFound < 3 && cacheIndex < LRUCacheSize ) {
            bool topOfCacheInTri = false;
            // Check if index is in triangle
            for(int i = 0; i < 3; ++i ) {
                if( triangles[triangleToDraw].vertices[i] == LRUCache[numVerticesFound]) {
                    ++numVerticesFound;
                    topOfCacheInTri = true;
                    break;
                }
            }
            
            if(!topOfCacheInTri) {
                int topIndex = LRUCache[ numVerticesFound ];
                for( int j = numVerticesFound; j < 2; ++j) {
                    LRUCache[j] = LRUCache[j+1];
                }
                LRUCache[2] = LRUCache[cacheIndex];
                if( LRUCache[2] >= 0) vertices[ LRUCache[2] ].cacheIndex = -1;
                
                LRUCache[cacheIndex] = topIndex;
                if(topIndex >= 0) vertices[ topIndex ].cacheIndex = cacheIndex;
                ++cacheIndex;
            };
        }
        
        // Set triangle as drawn
        for(int v = 0; v < 3; ++v) {
            const int index = triangles[triangleToDraw].vertices[v];
            
            LRUCache[ v ] = index;
            vertices[ index ].cacheIndex = v;
            
            --vertices[index].numTrisLeft;
            
            indices[ numIndicesDone ] = index;
            ++numIndicesDone;
        }
        
        
        triangles[ triangleToDraw ].drawn = true;
    }
};

float stsvco_compute_ACMR( const unsigned int *indices, const unsigned int numIndices, const unsigned int cacheSize ) {
    
    unsigned int numCacheMisses = 0;
    int cache[ cacheSize ];
    
    assert( !(numIndices % 3) && "Index input has to be triangles" );
    
    for(int i = 0; i < cacheSize; ++i) cache[i] = -1;
    
    for(int v = 0; v < numIndices; ++v ) {
        
        const unsigned int index = indices[v];
        // check if vertex in cache
        bool foundInCache = false;
        for(int c = 0; c < cacheSize && cache[c] >= 0 && !foundInCache; ++c) {
            if( cache[ c ] == index ) foundInCache = true;
        };
        
        if( !foundInCache ) {
            ++numCacheMisses;
            for(int c = cacheSize-1; c >=1; --c) {
                cache[c] = cache[ c-1 ];
            }
            cache[0] = index;
        }
    }
    
    return (float)numCacheMisses/(float)(numIndices/3);
};
#endif

/*
 ------------------------------------------------------------------------------
 This software is available under 2 licenses -- choose whichever you prefer.
 ------------------------------------------------------------------------------
 ALTERNATIVE A - MIT License
 Copyright (c) 2017 Sigurd Seteklev
 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 ------------------------------------------------------------------------------
 ALTERNATIVE B - Public Domain (www.unlicense.org)
 Copyright (c) 2017 Sigurd Seteklev
 This is free and unencumbered software released into the public domain.
 Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
 software, either in source code form or as a compiled binary, for any purpose,
 commercial or non-commercial, and by any means.
 In jurisdictions that recognize copyright laws, the author or authors of this
 software dedicate any and all copyright interest in the software to the public
 domain. We make this dedication for the benefit of the public at large and to
 the detriment of our heirs and successors. We intend this dedication to be an
 overt act of relinquishment in perpetuity of all present and future rights to
 this software under copyright law.
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ------------------------------------------------------------------------------
 */