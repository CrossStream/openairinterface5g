/*! \file slot_fep_ul.cu
 * \dft/idft feature of CUDA version
 * \author Shi-Yi Ou
 * \date 2017
 * \version 0.1
 * \company isIP LAB, Department of Computer Science, National Chiao Tung University, Hsinchu, Taiwan
 * \email: 
 * \note
 * \warning
 */
#include "stdio.h"
#include "cufft.h"
#include "defs.h"
#include "PHY/CUDA/extern.h"

typedef float2 Complex;

__global__ void k_rmcp(int16_t *x, Complex *y, int CP, int CP0)
{
  int i= blockDim.x * blockIdx.x+ threadIdx.x ;
  int j= (blockDim.x+CP )* blockIdx.x+ threadIdx.x + CP0;
  if (blockIdx.x > 6)
    j = j + CP0-CP;
  y[i].x = ( float )x[(j<<1)];
  y[i].y = ( float )x[(j<<1)+1];
}

__global__ void k_short(Complex *x, short *y)
{
  int i= blockDim.x * blockIdx.x+ threadIdx.x;
  y[i<<1] =   ( short )(x[i].x*0.04419417);
  y[(i<<1)+1]=( short )(x[i].y*0.04419417);
}

__global__ void k_adcp_extend( short *x, Complex *y )
{
  int i= blockDim.x * blockIdx.x+ threadIdx.x;
  y[i].x = ( float )x[ (i<<1) ];
  y[i].y = ( float )x[ (i<<1)+ 1 ];
}


void idft512ad_cu( int16_t *x, int16_t *y, int sf )
{//dl_cu
  int i;
//  printf("[CUDA] IN idft, sf num = %2d\n",sf);
  cudaMemcpyAsync( dl_cu[sf].d_txdata, 
                   x,
 				   sizeof(short)* 2 * dl_cu[sf].ifftsize* dl_cu[sf].symbols_per_tti,
				   cudaMemcpyHostToDevice,
				   dl_cu[sf].stream_dl );
				   
  k_adcp_extend<<< dl_cu[sf].symbols_per_tti, dl_cu[sf].ifftsize, 0, dl_cu[sf].stream_dl >>>
               ( dl_cu[sf].d_txdata,
     			 dl_cu[sf].d_txdata_ifft );
			   
  cufftExecC2C( dl_cu[sf].ifft,
               (cufftComplex *) dl_cu[sf].d_txdata_ifft,
			   (cufftComplex *) dl_cu[sf].d_txdata_ifft,
 			   CUFFT_INVERSE);
			   
  k_short      <<< dl_cu[sf].symbols_per_tti, dl_cu[sf].ifftsize, 0, dl_cu[sf].stream_dl >>>
              ( dl_cu[sf].d_txdata_ifft,
        	    dl_cu[sf].d_txdata );  
				
  cudaMemcpyAsync( dl_cu[sf].h_txdata,
                   dl_cu[sf].d_txdata,
 				   sizeof( short )* 2 * dl_cu[sf].ifftsize* dl_cu[sf].symbols_per_tti, 
				   cudaMemcpyDeviceToHost,
				   dl_cu[sf].stream_dl);
  
  int index = 0;
  short *temp = dl_cu[sf].h_txdata;
  cudaStreamSynchronize( dl_cu[sf].stream_dl );
  for ( i = 0; i < dl_cu[sf].symbols_per_tti; i++ )
  {
    int cp = 0;
    if( i == 0 || i == 7 )
      cp = dl_cu[sf].CP0;
    else
      cp = dl_cu[sf].CP;
    memcpy( &y[ index<<1 ], &temp[ (i+1)*dl_cu[sf].ifftsize*2-cp*2 ], cp*sizeof(short)*2 );
    memcpy( &y[ (index+cp)<<1 ], &temp[ i*dl_cu[sf].ifftsize*2 ], dl_cu[sf].ifftsize*2*sizeof(short) );
    index = index + cp + dl_cu[sf].ifftsize;
  }
}


void dft512rm_cu( int16_t *x, int16_t *y, int sf )
{
  //printf("[CUDA] symbols_per_tti = %2d samples_per_tti = %4d, fftsize = %3d, CP0 = %2d, CP= %2d\n", ul_cu[sf].symbols_per_tti,ul_cu[sf].samples_per_tti,ul_cu[sf].fftsize,ul_cu[sf].CP0,ul_cu[sf].CP);
  cudaMemcpyAsync(ul_cu[sf].d_rxdata[0], 
                  x,
				  sizeof(short)* ul_cu[sf].samples_per_tti*2,
				  cudaMemcpyHostToDevice,
				  ul_cu[sf].stream_ul );
  
  k_rmcp<<< ul_cu[sf].symbols_per_tti,
            ul_cu[sf].fftsize,
			0,
			ul_cu[sf].stream_ul>>>
			((short*)ul_cu[sf].d_rxdata[0], 
			 ul_cu[sf].d_rxdata_fft[0],
			 ul_cu[sf].CP,
			 ul_cu[sf].CP0);
  
  cufftExecC2C(ul_cu[sf].fft, 
               (cufftComplex *)ul_cu[sf].d_rxdata_fft[0],
	       (cufftComplex *)ul_cu[sf].d_rxdata_fft[0],
	       CUFFT_FORWARD);
  
  k_short<<< ul_cu[sf].symbols_per_tti,
             ul_cu[sf].fftsize,
			 0,
			 ul_cu[sf].stream_ul>>>
			 ( ul_cu[sf].d_rxdata_fft[0],
   			   (short *)ul_cu[sf].d_rxdataF[0]);
  
  cudaMemcpyAsync(y,
                  ul_cu[sf].d_rxdataF[0], 
				  sizeof(short)* ul_cu[sf].symbols_per_tti* 2* ul_cu[sf].fftsize, 
				  cudaMemcpyDeviceToHost, 
				  ul_cu[sf].stream_ul );
  cudaStreamSynchronize( ul_cu[sf].stream_ul);
}














