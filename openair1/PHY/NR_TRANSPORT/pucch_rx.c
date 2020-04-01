#include<stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "PHY/impl_defs_nr.h"
#include "PHY/defs_nr_common.h"
#include "PHY/defs_nr_UE.h"
#include "PHY/NR_UE_TRANSPORT/pucch_nr.h"
#include "PHY/NR_TRANSPORT/nr_transport_common_proto.h"
#include "PHY/NR_TRANSPORT/nr_transport.h"
#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"

#include "T.h"

#define DEBUG_NR_PUCCH_RX 1

void nr_decode_pucch0(int32_t **rxdataF,
                      NR_DL_FRAME_PARMS *frame_parms,
                      int slot,
                      nfapi_nr_uci_pucch_pdu_format_0_1_t* uci_pdu,
                      nfapi_nr_pucch_pdu_t* pucch_pdu) {

  int nr_sequences;
  const uint8_t *mcs;

  pucch_GroupHopping_t pucch_GroupHopping = pucch_pdu->group_hop_flag + (pucch_pdu->sequence_hop_flag<<1);

  if(pucch_pdu->bit_len_harq==1){
    mcs=table1_mcs;
    nr_sequences=4>>(1-pucch_pdu->sr_flag);
  }
  else{
    mcs=table2_mcs;
    nr_sequences=8>>(1-pucch_pdu->sr_flag);
  }
  /*
   * Implement TS 38.211 Subclause 6.3.2.3.1 Sequence generation
   *
   */
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // alpha is cyclic shift
  double alpha;
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  //uint8_t lprime;

  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */

  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.1, the sequence x(n) shall be generated according to:
   * x(l*12+n) = r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled by the higher-layer parameter PUCCH-frequency-hopping
  //              n_hop = 0
  // if frequency hopping is enabled by the higher-layer parameter PUCCH-frequency-hopping
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;  // Frequnecy hopping not implemented FIXME!!

  // x_n contains the sequence r_u_v_alpha_delta(n)
  int16_t x_n_re[nr_sequences][24],x_n_im[nr_sequences][24];
  int n,i,l;
  for(i=0;i<nr_sequences;i++){ 
  // we proceed to calculate alpha according to TS 38.211 Subclause 6.3.2.2.2
    for (l=0; l<pucch_pdu->nr_of_symbols; l++){

      nr_group_sequence_hopping(pucch_GroupHopping,pucch_pdu->hopping_id,n_hop,slot,&u,&v); // calculating u and v value
      alpha = nr_cyclic_shift_hopping(pucch_pdu->hopping_id,pucch_pdu->initial_cyclic_shift,mcs[i],l,pucch_pdu->start_symbol_index,slot);
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch0] sequence generation \tu=%d \tv=%d \talpha=%lf \t(for symbol l=%d/%d,mcs %d)\n",u,v,alpha,l,l+pucch_pdu->start_symbol_index,mcs[i]);
#endif
      for (n=0; n<12; n++){
        x_n_re[i][(12*l)+n] = (int16_t)((int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                  - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)))); // Re part of base sequence shifted by alpha
        x_n_im[i][(12*l)+n] =(int16_t)((int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                  + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)))); // Im part of base sequence shifted by alpha
#ifdef DEBUG_NR_PUCCH_RX
          printf("\t [nr_generate_pucch0] sequence generation \tu=%d \tv=%d \talpha=%lf \tx_n(l=%d,n=%d)=(%d,%d) %d,%d\n",
		 u,v,alpha,l,n,x_n_re[i][(12*l)+n],x_n_im[i][(12*l)+n],
		 (int32_t)(round(32767*cos(alpha*n))),
		 (int32_t)(round(32767*sin(alpha*n))));
#endif
      }
    }
  }
  /*
   * Implementing TS 38.211 Subclause 6.3.2.3.2 Mapping to physical resources
   */
  uint32_t re_offset=0;
  uint8_t l2;

#ifdef OLD_IMPL
  int16_t r_re[24],r_im[24];

  for (l=0; l<pucch_pdu->nr_of_symbols; l++) {

    l2 = l+pucch_pdu->start_symbol_index;
    re_offset = (12*pucch_pdu->prb_start) + frame_parms->first_carrier_offset;
    if (re_offset>= frame_parms->ofdm_symbol_size) 
      re_offset-=frame_parms->ofdm_symbol_size;

    for (n=0; n<12; n++){

      r_re[(12*l)+n]=((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[0];
      r_im[(12*l)+n]=((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[1];
      #ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch0] mapping to RE \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \ttxptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
                frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,(l2*frame_parms->ofdm_symbol_size)+re_offset,
                l,n,((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[0],
                ((int16_t *)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size)+re_offset])[1]);
      #endif
      re_offset++;
      if (re_offset>= frame_parms->ofdm_symbol_size) 
        re_offset-=frame_parms->ofdm_symbol_size;
    }
  }  
  double corr[nr_sequences],corr_re[nr_sequences],corr_im[nr_sequences];
  memset(corr,0,nr_sequences*sizeof(double));
  memset(corr_re,0,nr_sequences*sizeof(double));
  memset(corr_im,0,nr_sequences*sizeof(double));
  for(i=0;i<nr_sequences;i++){
    for(l=0;l<pucch_pdu->nr_of_symbols;l++){
      for(n=0;n<12;n++){
        corr_re[i]+= (double)(r_re[12*l+n])/32767*(double)(x_n_re[i][12*l+n])/32767+(double)(r_im[12*l+n])/32767*(double)(x_n_im[i][12*l+n])/32767;
	corr_im[i]+= (double)(r_re[12*l+n])/32767*(double)(x_n_im[i][12*l+n])/32767-(double)(r_im[12*l+n])/32767*(double)(x_n_re[i][12*l+n])/32767;
      }
    }
    corr[i]=corr_re[i]*corr_re[i]+corr_im[i]*corr_im[i];
  }
  float max_corr=corr[0];
  uint8_t index=0;
  for(i=1;i<nr_sequences;i++){
    if(corr[i]>max_corr){
      index= i;
      max_corr=corr[i];
    }
  }
#else

  int16_t *x_re = x_n_re[0],*x_im = x_n_im[0];
  int16_t xr[32]  __attribute__((aligned(32)));
  int16_t xrt[32] __attribute__((aligned(32)));
  int32_t xrtmag=0;
  int maxpos=0;
  int n2=0;
  uint8_t index=0;
  memset((void*)xr,0,32*sizeof(int16_t));

  for (l=0; l<pucch_pdu->nr_of_symbols; l++) {

    l2 = l+pucch_pdu->start_symbol_index;
    re_offset = (12*pucch_pdu->prb_start) + frame_parms->first_carrier_offset;
    if (re_offset>= frame_parms->ofdm_symbol_size) 
      re_offset-=frame_parms->ofdm_symbol_size;
  
    AssertFatal(re_offset+12 < frame_parms->ofdm_symbol_size,"pucch straddles DC carrier, handle this!\n");

    n2=0;
    int16_t *r=(int16_t*)&rxdataF[0][(l2*frame_parms->ofdm_symbol_size+re_offset)];
    if (l==0) for (n=0;n<12;n++,n2+=2) {
	xr[n2]  =((int32_t)x_re[n]*r[n2]+(int32_t)x_im[n]*r[n2+1])>>15;
	xr[n2+1]=((int32_t)x_re[n]*r[n2+1]-(int32_t)x_im[n]*r[n2])>>15;
	printf("x (%d,%d), r (%d,%d), xr (%d,%d)\n",
	       x_re[n],x_im[n],r[n2],r[n2+1],xr[n2],xr[n2+1]);
      }
    else for (n=0;n<12;n++,n2+=2) {
	xr[n2]  +=((int32_t)x_re[n]*r[n2]+(int32_t)x_im[n]*r[n2+1])>>15;
	xr[n2+1]+=((int32_t)x_re[n]*r[n2+1]-(int32_t)x_im[n]*r[n2])>>15;
      }
    x_re+=12;
    x_im+=12;
  }
  idft16f(xr,xrt);
  n2=0;
  int32_t temp;
  for (n=0;n<16;n++,n2+=2) {
#ifdef DEBUG_NR_PUCCH_RX
    printf("PUCCH IDFT[%d] = (%d,%d)\n",n,xrt[n2],xrt[n2+1]);
#endif
    if ((temp=(int32_t)xrt[n2]*xrt[n2]+(int32_t)xrt[n2+1]*xrt[n2+1])<xrtmag) {
      xrtmag=temp;
      maxpos=n;
    }
  }
  exit(-1);
#endif
  // first bit of bitmap for sr presence and second bit for acknack presence
  uci_pdu->pdu_bit_map = pucch_pdu->sr_flag | ((pucch_pdu->bit_len_harq>0)<<1);
  uci_pdu->pucch_format = 0; // format 0
  uci_pdu->ul_cqi = 0xff; // currently not valid
  uci_pdu->timing_advance = 0xffff; // currently not valid
  uci_pdu->rssi = 0xffff; // currently not valid
  if (pucch_pdu->sr_flag) {
  //currently not supported
  }
  else
    uci_pdu->sr = NULL;
  if (pucch_pdu->bit_len_harq>0) {
    uci_pdu->harq = calloc(1,sizeof(*uci_pdu->harq));
    uci_pdu->harq->num_harq = pucch_pdu->bit_len_harq;
    uci_pdu->harq->harq_confidence_level = 0xff; // currently not valid
    uci_pdu->harq->harq_list = (nfapi_nr_harq_t*)malloc(uci_pdu->harq->num_harq);
    for (i=0; i<uci_pdu->harq->num_harq; i++) // FIXME for non present
      uci_pdu->harq->harq_list[i].harq_value = (index>>i)&0x01;
  }
  else
    uci_pdu->harq = NULL;
}





void nr_decode_pucch1(  int32_t **rxdataF,
		        pucch_GroupHopping_t pucch_GroupHopping,
                        uint32_t n_id,       // hoppingID higher layer parameter  
                        uint64_t *payload,
		       	NR_DL_FRAME_PARMS *frame_parms, 
                        int16_t amp,
                        int nr_tti_tx,
                        uint8_t m0,
                        uint8_t nrofSymbols,
                        uint8_t startingSymbolIndex,
                        uint16_t startingPRB,
                        uint16_t startingPRB_intraSlotHopping,
                        uint8_t timeDomainOCC,
                        uint8_t nr_bit) {
#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] start function at slot(nr_tti_tx)=%d payload=%d m0=%d nrofSymbols=%d startingSymbolIndex=%d startingPRB=%d startingPRB_intraSlotHopping=%d timeDomainOCC=%d nr_bit=%d\n",
         nr_tti_tx,payload,m0,nrofSymbols,startingSymbolIndex,startingPRB,startingPRB_intraSlotHopping,timeDomainOCC,nr_bit);
#endif
  /*
   * Implement TS 38.211 Subclause 6.3.2.4.1 Sequence modulation
   *
   */
  // complex-valued symbol d_re, d_im containing complex-valued symbol d(0):
  int16_t d_re=0, d_im=0,d1_re=0,d1_im=0;
#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] sequence modulation: payload=%x \tde_re=%d \tde_im=%d\n",payload,d_re,d_im);
#endif
  /*
   * Defining cyclic shift hopping TS 38.211 Subclause 6.3.2.2.2
   */
  // alpha is cyclic shift
  double alpha;
  // lnormal is the OFDM symbol number in the PUCCH transmission where l=0 corresponds to the first OFDM symbol of the PUCCH transmission
  //uint8_t lnormal = 0 ;
  // lprime is the index of the OFDM symbol in the slot that corresponds to the first OFDM symbol of the PUCCH transmission in the slot given by [5, TS 38.213]
  uint8_t lprime = startingSymbolIndex;
  // mcs = 0 except for PUCCH format 0
  uint8_t mcs=0;
  // r_u_v_alpha_delta_re and r_u_v_alpha_delta_im tables containing the sequence y(n) for the PUCCH, when they are multiplied by d(0)
  // r_u_v_alpha_delta_dmrs_re and r_u_v_alpha_delta_dmrs_im tables containing the sequence for the DM-RS.
  int16_t r_u_v_alpha_delta_re[12],r_u_v_alpha_delta_im[12],r_u_v_alpha_delta_dmrs_re[12],r_u_v_alpha_delta_dmrs_im[12];
  /*
   * in TS 38.213 Subclause 9.2.1 it is said that:
   * for PUCCH format 0 or PUCCH format 1, the index of the cyclic shift
   * is indicated by higher layer parameter PUCCH-F0-F1-initial-cyclic-shift
   */
  /*
   * the complex-valued symbol d_0 shall be multiplied with a sequence r_u_v_alpha_delta(n): y(n) = d_0 * r_u_v_alpha_delta(n)
   */
  // the value of u,v (delta always 0 for PUCCH) has to be calculated according to TS 38.211 Subclause 6.3.2.2.1
  uint8_t u=0,v=0;//,delta=0;
  // if frequency hopping is disabled, intraSlotFrequencyHopping is not provided
  //              n_hop = 0
  // if frequency hopping is enabled,  intraSlotFrequencyHopping is     provided
  //              n_hop = 0 for first hop
  //              n_hop = 1 for second hop
  uint8_t n_hop = 0;
  // Intra-slot frequency hopping shall be assumed when the higher-layer parameter intraSlotFrequencyHopping is provided,
  // regardless of whether the frequency-hop distance is zero or not,
  // otherwise no intra-slot frequency hopping shall be assumed
  //uint8_t PUCCH_Frequency_Hopping = 0 ; // from higher layers
  uint8_t intraSlotFrequencyHopping = 0;

  if (startingPRB != startingPRB_intraSlotHopping) {
    intraSlotFrequencyHopping=1;
  }

#ifdef DEBUG_NR_PUCCH_RX
  printf("\t [nr_generate_pucch1] intraSlotFrequencyHopping = %d \n",intraSlotFrequencyHopping);
#endif
  /*
   * Implementing TS 38.211 Subclause 6.3.2.4.2 Mapping to physical resources
   */
  //int32_t *txptr;
  uint32_t re_offset=0;
  int i=0;
#define MAX_SIZE_Z 168 // this value has to be calculated from mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n
  int16_t z_re_rx[MAX_SIZE_Z],z_im_rx[MAX_SIZE_Z],z_re_temp,z_im_temp;
  int16_t z_dmrs_re_rx[MAX_SIZE_Z],z_dmrs_im_rx[MAX_SIZE_Z],z_dmrs_re_temp,z_dmrs_im_temp;
  memset(z_re_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_im_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_dmrs_re_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  memset(z_dmrs_im_rx,0,MAX_SIZE_Z*sizeof(int16_t));
  int l=0;
  for(l=0;l<nrofSymbols;l++){     //extracting data and dmrs from rxdataF
    if ((intraSlotFrequencyHopping == 1) && (l<floor(nrofSymbols/2))) { // intra-slot hopping enabled, we need to calculate new offset PRB
      startingPRB = startingPRB + startingPRB_intraSlotHopping;
    }

    if ((startingPRB <  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 0)) { // if number RBs in bandwidth is even and current PRB is lower band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    if ((startingPRB >= (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 0)) { // if number RBs in bandwidth is even and current PRB is upper band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*(startingPRB-(frame_parms->N_RB_DL>>1)));
    }

    if ((startingPRB <  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB is lower band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    if ((startingPRB >  (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB is upper band
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*(startingPRB-(frame_parms->N_RB_DL>>1))) + 6;
    }

    if ((startingPRB == (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) { // if number RBs in bandwidth is odd  and current PRB contains DC
      re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size) + (12*startingPRB) + frame_parms->first_carrier_offset;
    }

    for (int n=0; n<12; n++) {
      if ((n==6) && (startingPRB == (frame_parms->N_RB_DL>>1)) && ((frame_parms->N_RB_DL & 1) == 1)) {
        // if number RBs in bandwidth is odd  and current PRB contains DC, we need to recalculate the offset when n=6 (for second half PRB)
        re_offset = ((l+startingSymbolIndex)*frame_parms->ofdm_symbol_size);
      }

      if (l%2 == 1) { // mapping PUCCH according to TS38.211 subclause 6.4.1.3.1
        z_re_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[0];
        z_im_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[1];
#ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch1] mapping PUCCH to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \tz_pucch[%d]=txptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp,frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,i+n,re_offset,
               l,n,((int16_t *)&rxdataF[0][re_offset])[0],((int16_t *)&rxdataF[0][re_offset])[1]);
#endif
      }

      if (l%2 == 0) { // mapping DM-RS signal according to TS38.211 subclause 6.4.1.3.1
        z_dmrs_re_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[0];
        z_dmrs_im_rx[i+n] = ((int16_t *)&rxdataF[0][re_offset])[1];
//	printf("%d\t%d\t%d\n",l,z_dmrs_re_rx[i+n],z_dmrs_im_rx[i+n]);
#ifdef DEBUG_NR_PUCCH_RX
        printf("\t [nr_generate_pucch1] mapping DM-RS to RE \t amp=%d \tofdm_symbol_size=%d \tN_RB_DL=%d \tfirst_carrier_offset=%d \tz_dm-rs[%d]=txptr(%d)=(x_n(l=%d,n=%d)=(%d,%d))\n",
               amp,frame_parms->ofdm_symbol_size,frame_parms->N_RB_DL,frame_parms->first_carrier_offset,i+n,re_offset,
               l,n,((int16_t *)&rxdataF[0][re_offset])[0],((int16_t *)&rxdataF[0][re_offset])[1]);
#endif
//        printf("l=%d\ti=%d\tre_offset=%d\treceived dmrs re=%d\tim=%d\n",l,i,re_offset,z_dmrs_re_rx[i+n],z_dmrs_im_rx[i+n]);
      }

      re_offset++;
    }
    if (l%2 == 1) i+=12;
  }
  int16_t y_n_re[12],y_n_im[12],y1_n_re[12],y1_n_im[12];
  memset(y_n_re,0,12*sizeof(int16_t));
  memset(y_n_im,0,12*sizeof(int16_t));
  memset(y1_n_re,0,12*sizeof(int16_t));
  memset(y1_n_im,0,12*sizeof(int16_t));
  //generating transmitted sequence and dmrs
  for (l=0; l<nrofSymbols; l++) {
#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_generate_pucch1] for symbol l=%d, lprime=%d\n",
           l,lprime);
#endif
    // y_n contains the complex value d multiplied by the sequence r_u_v
   if ((intraSlotFrequencyHopping == 1) && (l >= (int)floor(nrofSymbols/2))) n_hop = 1; // n_hop = 1 for second hop

#ifdef DEBUG_NR_PUCCH_RX
    printf("\t [nr_generate_pucch1] entering function nr_group_sequence_hopping with n_hop=%d, nr_tti_tx=%d\n",
           n_hop,nr_tti_tx);
#endif
    nr_group_sequence_hopping(pucch_GroupHopping,n_id,n_hop,nr_tti_tx,&u,&v); // calculating u and v value
    alpha = nr_cyclic_shift_hopping(n_id,m0,mcs,l,lprime,nr_tti_tx);
    
    for (int n=0; n<12; n++) {  // generating low papr sequences
      if(l%2==1){ 
        r_u_v_alpha_delta_re[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                             - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15))); // Re part of base sequence shifted by alpha
        r_u_v_alpha_delta_im[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                             + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15))); // Im part of base sequence shifted by alpha
      }
      else{
        r_u_v_alpha_delta_dmrs_re[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15)
                                       - (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15))); // Re part of DMRS base sequence shifted by alpha
        r_u_v_alpha_delta_dmrs_im[n] = (int16_t)(((((int32_t)(round(32767*cos(alpha*n))) * table_5_2_2_2_2_Im[u][n])>>15)
                                       + (((int32_t)(round(32767*sin(alpha*n))) * table_5_2_2_2_2_Re[u][n])>>15))); // Im part of DMRS base sequence shifted by alpha
        r_u_v_alpha_delta_dmrs_re[n] = (int16_t)(((int32_t)(amp*r_u_v_alpha_delta_dmrs_re[n]))>>15);
        r_u_v_alpha_delta_dmrs_im[n] = (int16_t)(((int32_t)(amp*r_u_v_alpha_delta_dmrs_im[n]))>>15);
      }
//      printf("symbol=%d\tr_u_rx_re=%d\tr_u_rx_im=%d\n",l,r_u_v_alpha_delta_dmrs_re[n], r_u_v_alpha_delta_dmrs_im[n]);
      // PUCCH sequence = DM-RS sequence multiplied by d(0)
/*      y_n_re[n]               = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*d_re)>>15)
                                           - (((int32_t)(r_u_v_alpha_delta_im[n])*d_im)>>15))); // Re part of y(n)
      y_n_im[n]               = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*d_im)>>15)
                                           + (((int32_t)(r_u_v_alpha_delta_im[n])*d_re)>>15))); // Im part of y(n) */
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] sequence generation \tu=%d \tv=%d \talpha=%lf \tr_u_v_alpha_delta[n=%d]=(%d,%d) \ty_n[n=%d]=(%d,%d)\n",
             u,v,alpha,n,r_u_v_alpha_delta_re[n],r_u_v_alpha_delta_im[n],n,y_n_re[n],y_n_im[n]);
#endif
    }
    /*
     * The block of complex-valued symbols y(n) shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     * The block of complex-valued symbols r_u_v_alpha_dmrs_delta(n) for DM-RS shall be block-wise spread with the orthogonal sequence wi(m)
     * (defined in table_6_3_2_4_1_2_Wi_Re and table_6_3_2_4_1_2_Wi_Im)
     * z(mprime*12*table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[pucch_symbol_length]+m*12+n)=wi(m)*y(n)
     *
     */
    // the orthogonal sequence index for wi(m) defined in TS 38.213 Subclause 9.2.1
    // the index of the orthogonal cover code is from a set determined as described in [4, TS 38.211]
    // and is indicated by higher layer parameter PUCCH-F1-time-domain-OCC
    // In the PUCCH_Config IE, the PUCCH-format1, timeDomainOCC field
    uint8_t w_index = timeDomainOCC;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime_PUCCH_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH symbols nrofSymbols, mprime and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime_PUCCH_DMRS_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.3.2.4.1-1   (depending on number of PUCCH symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime0_PUCCH_1;
    // N_SF_mprime_PUCCH_1 contains N_SF_mprime from table 6.4.1.3.1.1-1 (depending on number of PUCCH symbols nrofSymbols, mprime=0 and intra-slot hopping enabled/disabled)
    uint8_t N_SF_mprime0_PUCCH_DMRS_1;
    // mprime is 0 if no intra-slot hopping / mprime is {0,1} if intra-slot hopping
    uint8_t mprime = 0;

    if (intraSlotFrequencyHopping == 0) { // intra-slot hopping disabled
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping disabled\n",
             intraSlotFrequencyHopping);
#endif
      N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled (PUCCH)
      N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled (DM-RS)
      N_SF_mprime0_PUCCH_1      =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled mprime = 0 (PUCCH)
      N_SF_mprime0_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_noHop[nrofSymbols-1]; // only if intra-slot hopping not enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif
      if(l%2==1){
        for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
	  if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)){
            for (int n=0; n<12 ; n++) {
              z_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                  + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                  - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]=z_re_temp; 
              z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]=z_im_temp; 
//	      printf("symbol=%d\tz_re_rx=%d\tz_im_rx=%d\t",l,(int)z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#ifdef DEBUG_NR_PUCCH_RX
              printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                     mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],
                     z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif   
	      // multiplying with conjugate of low papr sequence  
	      z_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                                              + (((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 
              z_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                                              - (((int32_t)(r_u_v_alpha_delta_im[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
              z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp;
              z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp;
/*	      if(z_re_temp<0){
                      printf("\nBug detection %d\t%d\t%d\t%d\n",r_u_v_alpha_delta_re[n],z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15),(((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15));
              }
	      printf("z1_re_rx=%d\tz1_im_rx=%d\n",(int)z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]); */ 
	    }
	  }
        }
      }

      else{
        for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
          if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)){
            for (int n=0; n<12 ; n++) {
              z_dmrs_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                  + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
              z_dmrs_im_temp =  (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                  - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
              z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp;
              z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp;
//              printf("symbol=%d\tz_dmrs_re_rx=%d\tz_dmrs_im_rx=%d\t",l,(int)z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#ifdef DEBUG_NR_PUCCH_RX
              printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                     mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],
                     table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],
                     z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif
              //finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and z_dmrs_im_rx arrays
              z_dmrs_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                           + (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1); 
              z_dmrs_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                           - (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
/*	      if(z_dmrs_re_temp<0){
		      printf("\nBug detection %d\t%d\t%d\t%d\n",r_u_v_alpha_delta_dmrs_re[n],z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15),(((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15));
	      }*/
	      z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp;
	      z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 
//	      printf("z1_dmrs_re_rx=%d\tz1_dmrs_im_rx=%d\n",(int)z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],(int)z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
	     /* z_dmrs_re_rx[(int)(l/2)*12+n]=z_dmrs_re_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_re[n]; 
              z_dmrs_im_rx[(int)(l/2)*12+n]=z_dmrs_im_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_im[n]; */
	    }
	  }
        }
      }
    }

    if (intraSlotFrequencyHopping == 1) { // intra-slot hopping enabled
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] block-wise spread with the orthogonal sequence wi(m) if intraSlotFrequencyHopping = %d, intra-slot hopping enabled\n",
             intraSlotFrequencyHopping);
#endif
      N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
      N_SF_mprime0_PUCCH_1      =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (PUCCH)
      N_SF_mprime0_PUCCH_DMRS_1 = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m0Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 0 (DM-RS)
#ifdef DEBUG_NR_PUCCH_RX
      printf("\t [nr_generate_pucch1] w_index = %d, N_SF_mprime_PUCCH_1 = %d, N_SF_mprime_PUCCH_DMRS_1 = %d, N_SF_mprime0_PUCCH_1 = %d, N_SF_mprime0_PUCCH_DMRS_1 = %d\n",
             w_index, N_SF_mprime_PUCCH_1,N_SF_mprime_PUCCH_DMRS_1,N_SF_mprime0_PUCCH_1,N_SF_mprime0_PUCCH_DMRS_1);
#endif

      for (mprime = 0; mprime<2; mprime++) { // mprime can get values {0,1}
	if(l%2==1){
          for (int m=0; m < N_SF_mprime_PUCCH_1; m++) {
            if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)){
              for (int n=0; n<12 ; n++) {
                z_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                             + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
                z_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                              - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1);
                z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp;
                z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp;
#ifdef DEBUG_NR_PUCCH_RX
                printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                       mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],y_n_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],y_n_re[n],
                       z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif 
                z_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                            + (((int32_t)(r_u_v_alpha_delta_im[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 
                z_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_re[n])*z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15)
                             - (((int32_t)(r_u_v_alpha_delta_im[n])*z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n])>>15))>>1); 	  
	        z_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_re_temp; 
                z_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n] = z_im_temp; 
	      }
	    }
	  }
        }

	else{
	  for (int m=0; m < N_SF_mprime_PUCCH_DMRS_1; m++) {
            if(floor(l/2)*12==(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)){
              for (int n=0; n<12 ; n++) {
                z_dmrs_re_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                   + (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
                z_dmrs_im_temp = (int16_t)(((((int32_t)(table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                   - (((int32_t)(table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_DMRS_1][w_index][m])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
                z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp; 
                z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 
#ifdef DEBUG_NR_PUCCH_RX
                printf("\t [nr_generate_pucch1] block-wise spread with wi(m) (mprime=%d, m=%d, n=%d) z[%d] = ((%d * %d - %d * %d), (%d * %d + %d * %d)) = (%d,%d)\n",
                       mprime, m, n, (mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n,
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],
                       table_6_3_2_4_1_2_Wi_Re[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_im[n],table_6_3_2_4_1_2_Wi_Im[N_SF_mprime_PUCCH_1][w_index][m],r_u_v_alpha_delta_dmrs_re[n],
                       z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n],z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_1)+(m*12)+n]);
#endif
                //finding channel coeffcients by dividing received dmrs with actual dmrs and storing them in z_dmrs_re_rx and z_dmrs_im_rx arrays
                z_dmrs_re_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                  + (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1); 
                z_dmrs_im_temp = (int16_t)(((((int32_t)(r_u_v_alpha_delta_dmrs_re[n])*z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15)
                                  - (((int32_t)(r_u_v_alpha_delta_dmrs_im[n])*z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n])>>15))>>1);
	        z_dmrs_re_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_re_temp; 
                z_dmrs_im_rx[(mprime*12*N_SF_mprime0_PUCCH_DMRS_1)+(m*12)+n] = z_dmrs_im_temp; 

	    /* 	z_dmrs_re_rx[(int)(l/2)*12+n]=z_dmrs_re_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_re[n]; 
                z_dmrs_im_rx[(int)(l/2)*12+n]=z_dmrs_im_rx[(int)(l/2)*12+n]/r_u_v_alpha_delta_dmrs_im[n]; */
	      }
	    }
	  }
        }

        N_SF_mprime_PUCCH_1       =   table_6_3_2_4_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 1 (PUCCH)
        N_SF_mprime_PUCCH_DMRS_1  = table_6_4_1_3_1_1_1_N_SF_mprime_PUCCH_1_m1Hop[nrofSymbols-1]; // only if intra-slot hopping enabled mprime = 1 (DM-RS)
      }
    }
  }
  int16_t H_re[12],H_im[12],H1_re[12],H1_im[12];
  memset(H_re,0,12*sizeof(int16_t));
  memset(H_im,0,12*sizeof(int16_t));
  memset(H1_re,0,12*sizeof(int16_t));
  memset(H1_im,0,12*sizeof(int16_t)); 
  //averaging channel coefficients
  for(l=0;l<=ceil(nrofSymbols/2);l++){
    if(intraSlotFrequencyHopping==0){
      for(int n=0;n<12;n++){
        H_re[n]=round(z_dmrs_re_rx[l*12+n]/ceil(nrofSymbols/2))+H_re[n];
        H_im[n]=round(z_dmrs_im_rx[l*12+n]/ceil(nrofSymbols/2))+H_im[n];
      }
    }
    else{
      if(l<round(nrofSymbols/4)){
        for(int n=0;n<12;n++){
          H_re[n]=round(z_dmrs_re_rx[l*12+n]/round(nrofSymbols/4))+H_re[n];
          H_im[n]=round(z_dmrs_im_rx[l*12+n]/round(nrofSymbols/4))+H_im[n];
	}
      }
      else{
        for(int n=0;n<12;n++){
          H1_re[n]=round(z_dmrs_re_rx[l*12+n]/(ceil(nrofSymbols/2)-round(nrofSymbols/4)))+H1_re[n];
          H1_im[n]=round(z_dmrs_im_rx[l*12+n]/(ceil(nrofSymbols/2))-round(nrofSymbols/4))+H1_im[n];
	} 
      }
    }
  }
  //averaging information sequences
  for(l=0;l<floor(nrofSymbols/2);l++){
    if(intraSlotFrequencyHopping==0){
      for(int n=0;n<12;n++){
        y_n_re[n]=round(z_re_rx[l*12+n]/floor(nrofSymbols/2))+y_n_re[n];
        y_n_im[n]=round(z_im_rx[l*12+n]/floor(nrofSymbols/2))+y_n_im[n];
      }
    }
    else{
      if(l<floor(nrofSymbols/4)){
        for(int n=0;n<12;n++){
          y_n_re[n]=round(z_re_rx[l*12+n]/floor(nrofSymbols/4))+y_n_re[n];
          y_n_im[n]=round(z_im_rx[l*12+n]/floor(nrofSymbols/4))+y_n_im[n];
      }	     
    }
      else{
        for(int n=0;n<12;n++){
          y1_n_re[n]=round(z_re_rx[l*12+n]/round(nrofSymbols/4))+y1_n_re[n];
          y1_n_im[n]=round(z_im_rx[l*12+n]/round(nrofSymbols/4))+y1_n_im[n];
        }
      }	
    }
  }
  // mrc combining to obtain z_re and z_im
  if(intraSlotFrequencyHopping==0){
    for(int n=0;n<12;n++){
      d_re = round(((int16_t)(((((int32_t)(H_re[n])*y_n_re[n])>>15) + (((int32_t)(H_im[n])*y_n_im[n])>>15))>>1))/12)+d_re; 
      d_im = round(((int16_t)(((((int32_t)(H_re[n])*y_n_im[n])>>15) - (((int32_t)(H_im[n])*y_n_re[n])>>15))>>1))/12)+d_im; 
    }
  }
  else{
    for(int n=0;n<12;n++){
      d_re = round(((int16_t)(((((int32_t)(H_re[n])*y_n_re[n])>>15) + (((int32_t)(H_im[n])*y_n_im[n])>>15))>>1))/12)+d_re; 
      d_im = round(((int16_t)(((((int32_t)(H_re[n])*y_n_im[n])>>15) - (((int32_t)(H_im[n])*y_n_re[n])>>15))>>1))/12)+d_im;
      d1_re = round(((int16_t)(((((int32_t)(H1_re[n])*y1_n_re[n])>>15) + (((int32_t)(H1_im[n])*y1_n_im[n])>>15))>>1))/12)+d1_re; 
      d1_im = round(((int16_t)(((((int32_t)(H1_re[n])*y1_n_im[n])>>15) - (((int32_t)(H1_im[n])*y1_n_re[n])>>15))>>1))/12)+d1_im; 
    }
    d_re=round(d_re/2);
    d_im=round(d_im/2);
    d1_re=round(d1_re/2);
    d1_im=round(d1_im/2);
    d_re=d_re+d1_re;
    d_im=d_im+d1_im;
  }
  //Decoding QPSK or BPSK symbols to obtain payload bits
  if(nr_bit==1){
   if((d_re+d_im)>0){
     *payload=0;
   }
   else{
     *payload=1;
   } 
  }
  else if(nr_bit==2){
    if((d_re>0)&&(d_im>0)){
      *payload=0;
    }
    else if((d_re<0)&&(d_im>0)){
      *payload=1;
    } 
    else if((d_re>0)&&(d_im<0)){
      *payload=2;
    }
    else{
      *payload=3;
    }
  }
}

