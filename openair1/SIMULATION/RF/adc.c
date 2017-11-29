/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.0  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
//#define DEBUG_ADC
void adc(double *r_re[2],
         double *r_im[2],
         unsigned int input_offset,
         unsigned int output_offset,
         unsigned int **output,
         unsigned int nb_rx_antennas,
         unsigned int length,
         unsigned char B)
{
  int i;
  int aa;
  //FILE *file1=NULL;
  //FILE *file2=NULL;
  //file1 = fopen("adc1","w+");
  //file1 = fopen("adc2","w+");
  double gain = (double)(1<<(B-1));
  //double gain = 1.0;
  //for (i=0;i<length;i++){
//	fprintf(file1,"%d\t%d\t%d\t%d\t%d\n",i,(short)(r_re[0][i+input_offset]*gain),(short)(r_im[0][i+input_offset]*gain),output_offset/14336,i+output_offset);
 // } 
  for (i=0; i<length; i++) {
    for (aa=0; aa<nb_rx_antennas; aa++) {
      ((short *)output[aa])[((i+output_offset)<<1)]   = (short)(r_re[aa][i+input_offset]*gain);
      ((short *)output[aa])[1+((i+output_offset)<<1)] = (short)(r_im[aa][i+input_offset]*gain);
      //if (i>10 && i<20)
      //printf("Adc outputs %d (%d,%d)-(%d,%d)\n",i+output_offset,((short *)output[aa])[((i+output_offset)<<1)],((short *)output[aa])[1+((i+output_offset)<<1)],(short)(r_re[aa][i+input_offset]*gain),(short)(r_im[aa][i+input_offset]*gain));
      if ((r_re[aa][i+input_offset]*gain) > 30000) {
        //("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
      }
    }
    //printf("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
  } 
}
void adc_freq(double *r_re[2],
         double *r_im[2],
         unsigned int input_offset,
         unsigned int output_offset,
         unsigned int **output,
         unsigned int nb_rx_antennas,
         unsigned int length,
         unsigned char B)
{

  int i;
  int aa;
  //FILE *file1=NULL;
  //FILE *file2=NULL;
  //file1 = fopen("adc1","w+");
  //file1 = fopen("adc2","w+");
  double gain = (double)(1<<(B-1));
  //double gain = 1.0;
  //for (i=0;i<length;i++){
//	fprintf(file1,"%d\t%d\t%d\t%d\t%d\n",i,(short)(r_re[0][i+input_offset]*gain),(short)(r_im[0][i+input_offset]*gain),output_offset/14336,i+output_offset);
 // } 
  for (i=0; i<length; i++) {
    for (aa=0; aa<nb_rx_antennas; aa++) {
      ((short *)output[aa])[((i+output_offset)<<1)]   = (short)(r_re[aa][i+input_offset]*gain);
      ((short *)output[aa])[1+((i+output_offset)<<1)] = (short)(r_im[aa][i+input_offset]*gain);
      //if (i>10 && i<20)
      //printf("Adc outputs %d (%d,%d)-(%d,%d)\n",i+output_offset,((short *)output[aa])[((i+output_offset)<<1)],((short *)output[aa])[1+((i+output_offset)<<1)],(short)(r_re[aa][i+input_offset]*gain),(short)(r_im[aa][i+input_offset]*gain));
      if ((r_re[aa][i+input_offset]*gain) > 30000) {
        //("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
      }
    }
    //printf("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
  } 
}
/*void adc_freq(double *r_re[2],
         double *r_im[2],
         unsigned int input_offset,
         unsigned int output_offset,
         unsigned int **output1,//thread th_id
         unsigned int **output2,//thread 0
         unsigned int **output3,//thread 1
         unsigned int nb_rx_antennas,
         unsigned int length,
         unsigned char B,
	 int thread)
{
  int i;
  //int th_id;
  int aa;
  double gain = (double)(1<<(B-1));*/

  /*int dummy_rx[nb_rx_antennas][length] __attribute__((aligned(32)));
  for (aa=0; aa<nb_rx_antennas; aa++) {
	memset (&output1[aa][output_offset],0,length*sizeof(int));
  }*/
  //double gain = 1.0;

  /*for (i=0; i<length; i++) {
    for (aa=0; aa<nb_rx_antennas; aa++) {
      ((short *)output1[aa])[((i+output_offset)<<1)]   = (short)(r_re[aa][i+input_offset]*gain);
      ((short *)output1[aa])[1+((i+output_offset)<<1)] = (short)(r_im[aa][i+input_offset]*gain);

      if ((r_re[aa][i+input_offset]*gain) > 30000) {
        //("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
      }
      if (i < 300) {
        printf("rxdataF (thread[%d]) %d: (%d,%d)\n",thread,i,((short *)output1[aa])[((i+output_offset)<<1)],((short *)output1[aa])[1+((i+output_offset)<<1)]);
	if (thread==0 && output_offset>length)
        	printf("rxdataF (thread[1]) %d: (%d,%d) \n",i,((short *)output3[aa])[((i+output_offset-length-4)<<1)],((short *)output3[aa])[1+((i+output_offset-length-4)<<1)]);
	else if (thread==1)
		printf("rxdataF (thread[0]) %d: (%d,%d) \n",i,((short *)output2[aa])[((i+output_offset+length+4)<<1)],((short *)output2[aa])[1+((i+output_offset+length+4)<<1)]);

      }
    }*/
  /*for (aa=0; aa<nb_rx_antennas; aa++) {
  	for (th_id=1; th_id<2; th_id++)
	{
		memcpy((void *)output[aa][output_offset],
              	 	(void *)output[aa][output_offset],
               		length*sizeof(int));
	}
  }*/
  /*}
  printf("thread %d\n",(unsigned int)thread);
			//write_output("adc_rxsigF_frame0.m","adc_rxsF0", output1[0],10*length,1,16);
			//write_output("adc_rxsigF_frame1.m","adc_rxsF1", output2[0],10*length,1,16);
}*/
void adc_prach(double *r_re[2],
         double *r_im[2],
         unsigned int input_offset,
         unsigned int output_offset,
         unsigned int **output,
         unsigned int nb_rx_antennas,
         unsigned int length,
         unsigned char B)
{

  int i;
  int aa;
  double gain = (double)(1<<(B-1));
  //double gain = 1.0;

  for (i=0; i<length; i++) {
    for (aa=0; aa<nb_rx_antennas; aa++) {
      ((short *)output[aa])[((i+output_offset/2)<<1)]   = (short)(r_re[aa][i+input_offset]*gain);
      ((short *)output[aa])[1+((i+output_offset/2)<<1)] = (short)(r_im[aa][i+input_offset]*gain);
#ifdef DEBUG_ADC
      if (i<10)
      	printf("[adc_prach]i %d.  input (%d,%d), output (%d,%d)\n",i,(short)(r_re[aa][i+input_offset]),(short)(r_im[aa][i+input_offset]),((short *)output[aa])[((i+output_offset/2)<<1)],((short *)output[aa])[1+((i+output_offset/2)<<1)]);
      if (i>length-10&&i<length)
#endif
      if ((r_re[aa][i+input_offset]*gain) > 30000) {
        //("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
      }
    }

    //printf("Adc outputs %d %e  %d \n",i,((short *)output[0])[((i+output_offset)<<1)], ((i+output_offset)<<1) );
  }
}
