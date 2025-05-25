#include "kernel3.h"

void kernel3( float hist[ARRAY_SIZE], float weight[ARRAY_SIZE], int index[ARRAY_SIZE])
{
    for (int i=0; i<ARRAY_SIZE; ++i) {
#pragma HLS PIPELINE II=4
        hist[index[i]] = hist[index[i]]+ weight[i];
      }
}