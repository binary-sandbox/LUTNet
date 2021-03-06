#define AP_INT_MAX_W 9216
#include <ap_int.h>

template<unsigned int MACS, unsigned int PopCountIntWidth>
ap_uint<PopCountIntWidth> NaivePopCount(ap_uint<MACS> in) {
        ap_uint<PopCountIntWidth> pct = 0;
        for (unsigned int i = 0; i < MACS; i++) {
                pct += in(i, i);
        }
        return pct;
}

template<unsigned int MACS, unsigned int M>
void LUTARRAY(ap_uint<MACS> in, ap_uint<MACS> in_1, ap_uint<((MACS-1)/32+1)*32> *weight_0, ap_uint<((MACS-1)/32+1)*32> *weight_1, ap_uint<((MACS-1)/32+1)*32> *weight_2, ap_uint<MACS> *lut_out){
#pragma HLS inline off
        for (int tm = 0; tm < M; tm++) {
        #pragma HLS UNROLL
            lut_out[tm] = in ^ in_1 ^ weight_0[tm].range(MACS-1,0) ^ weight_1[tm].range(MACS-1,0) ^ weight_2[tm].range(MACS-1,0);
            //for (int i =0; i < MACS; i++) lut_out[tm].range(i,i) = 1;
        }
}


template<unsigned int PRECISION_REB, unsigned int MACS, unsigned int PopCountWidth, unsigned int PopCountIntWidth, unsigned int M, unsigned int TN, unsigned int TM>
ap_uint<PRECISION_REB*M> LUT_TM(ap_uint<MACS> in[PRECISION_REB][TN], ap_uint<MACS> in_1[PRECISION_REB][TN], const ap_uint<32> weightMem0[M/TM][TN][TM][(MACS-1)/32+1], const ap_uint<32> weightMem1[M/TM][TN][TM][(MACS-1)/32+1], const ap_uint<32> weightMem2[M/TM][TN][TM][(MACS-1)/32+1], const ap_fixed<PopCountWidth, PopCountIntWidth> *thresh, const ap_fixed<PopCountWidth, PopCountIntWidth> *alpha, const ap_fixed<PopCountWidth, PopCountIntWidth> *next_layer_means, const unsigned int *rand_map_0, bool pt) {

        ap_uint<PRECISION_REB*M> tmp_out = 0;

        ap_fixed<PopCountWidth, PopCountIntWidth, AP_TRN, AP_SAT> accReg[TM][M/TM];
        #pragma HLS ARRAY_PARTITION variable=accReg complete dim=1
        #pragma HLS ARRAY_PARTITION variable=accReg complete dim=2
        #pragma HLS DEPENDENCE variable=accReg inter false
        #pragma HLS DEPENDENCE variable=accReg intra false
        const unsigned int max_fold = (MACS-1)/32+1;

        for (int tile_m = 0; tile_m < TM; tile_m++){
            #pragma HLS UNROLL
            for (int tm = 0; tm < M/TM; tm++){
                #pragma HLS UNROLL
                accReg[tile_m][tm] = 0;
            }
        }

        ap_uint<max_fold*32> weightMem_buf_0[M/TM];
        #pragma HLS ARRAY_PARTITION variable=weightMem_buf_0 complete dim=1
        ap_uint<max_fold*32> weightMem_buf_1[M/TM];
        #pragma HLS ARRAY_PARTITION variable=weightMem_buf_1 complete dim=1
        ap_uint<max_fold*32> weightMem_buf_2[M/TM];
        #pragma HLS ARRAY_PARTITION variable=weightMem_buf_2 complete dim=1

        for (int tile_m = 0; tile_m < TM; tile_m++){
            for (int tile_n = 0; tile_n < TN; tile_n++){
            #pragma HLS PIPELINE II=1

                for (int tm = 0; tm < M/TM; tm++){
                #pragma HLS UNROLL
                    for (int fold = 0; fold < max_fold; fold++){
                    #pragma HLS UNROLL
                        weightMem_buf_0[tm].range(32*(fold+1)-1, 32*fold) = weightMem0[tm][tile_n][tile_m][fold];
                        weightMem_buf_1[tm].range(32*(fold+1)-1, 32*fold) = weightMem1[tm][tile_n][tile_m][fold];
                        weightMem_buf_2[tm].range(32*(fold+1)-1, 32*fold) = weightMem2[tm][tile_n][tile_m][fold];
                    }

                }

                ap_uint<MACS> lut_out_b0[M/TM];
                #pragma HLS ARRAY_PARTITION variable=lut_out_b0 complete dim=1
                LUTARRAY<MACS, M/TM>(in[0][tile_n], in_1[0][tile_n], weightMem_buf_0, weightMem_buf_1, weightMem_buf_2, lut_out_b0);
        
                ap_uint<MACS> lut_out_b1[M/TM];
                #pragma HLS ARRAY_PARTITION variable=lut_out_b1 complete dim=1
                LUTARRAY<MACS, M/TM>(in[1][tile_n], in_1[1][tile_n], weightMem_buf_0, weightMem_buf_1, weightMem_buf_2, lut_out_b1);
        
                for (int tm = 0; tm < M/TM; tm++) {
                #pragma HLS UNROLL
                    accReg[tile_m][tm] += NaivePopCount<MACS, PopCountIntWidth>(lut_out_b1[tm]) * alpha[0];// alpha index swapped on purpose: bit index matching
                       
                    accReg[tile_m][tm] += NaivePopCount<MACS, PopCountIntWidth>(lut_out_b0[tm]) * alpha[1];
        
        
                }
                if (tile_m==TM-1 && tile_n==TN-1){
                    for (int tile_mm = 0; tile_mm < TM; tile_mm++){
                    #pragma HLS UNROLL
                        for (int tm = 0; tm < M/TM; tm++){
                        #pragma HLS UNROLL

                            //printf("%d ", (unsigned int)accReg[tm]);
                            ap_uint<PRECISION_REB> tmp_thresh;
                            accReg[tile_mm][tm] -= thresh[tile_mm*M/TM+tm];

                            for (int tb = PRECISION_REB-1; tb >= 0; tb--){ // TODO: this loop only works for 2b at the moment
                                tmp_thresh.range(tb, tb) = accReg[tile_mm][tm] > 0 ? 1 : 0; // MSB-first
                                accReg[tile_mm][tm] = (accReg[tile_mm][tm] > 0) ? (accReg[tile_mm][tm] - next_layer_means[tile_mm*M/TM+tm]) : (accReg[tile_mm][tm] + next_layer_means[tile_mm*M/TM+tm]);
                            }

                            tmp_out.range(PRECISION_REB*(tile_mm*M/TM+tm+1)-1,PRECISION_REB*(tile_mm*M/TM+tm)) = tmp_thresh;

                        }
                    }
                }
            }
        }

        //for (int tm = 0; tm < M; tm++){
        //#pragma HLS UNROLL
    
        //    ap_uint<PRECISION_REB> tmp_thresh;
        //    accReg[tm] -= thresh[tm];
        //    
        //    for (int tb = PRECISION_REB-1; tb >= 0; tb--){ // TODO: this loop only works for 2b at the moment
        //        tmp_thresh.range(tb, tb) = accReg[tm] > 0 ? 1 : 0; // MSB-first
        //        accReg[tm] = (accReg[tm] > 0) ? (accReg[tm] - next_layer_means[tm]) : (accReg[tm] + next_layer_means[tm]);
        //    }
        //    
        //    tmp_out.range(PRECISION_REB*(tm+1)-1,PRECISION_REB*tm) = tmp_thresh;
    
        //}
 
        return tmp_out;
}


template<
// layer size
const unsigned int inRow,
const unsigned int inCol,
const unsigned int N,
const unsigned int outRow,
const unsigned int outCol,
const unsigned int M,
const unsigned int K,
const unsigned int ST,
const unsigned int PRECISION_REB,
const unsigned int PopCountWidth, // number of bits in popcount accumulator (>=log2(fanin))
const unsigned int PopCountIntWidth, // number of bits in popcount accumulator (>=log2(fanin))
const unsigned int TN,// number of tiles across ch_in dimension
const unsigned int TM,// number of tiles across ch_out dimension
class frame_in_type,
class frame_out_type
>
void LUTNET_LUT53MV_TM(
  frame_in_type &frame_in,
  frame_out_type &frame_out,
  const ap_uint<32> weightMem0[M/TM][TN][TM][(N*K*K/TN-1)/32+1],
  const ap_uint<32> weightMem1[M/TM][TN][TM][(N*K*K/TN-1)/32+1],
  const ap_uint<32> weightMem2[M/TM][TN][TM][(N*K*K/TN-1)/32+1],
  const ap_fixed<PopCountWidth, PopCountIntWidth> *thresh,
  const ap_fixed<PopCountWidth, PopCountIntWidth> *alpha,
  const ap_fixed<PopCountWidth, PopCountIntWidth> *next_layer_means,
  const unsigned int *rand_map_0
)
{

//#pragma HLS DATAFLOW

  ap_uint<PRECISION_REB*M> tmp_out = 0;
  ap_uint<PRECISION_REB*N*K*K> tmp_in = 0;

//  ap_uint<N*K*K> tmp_in_reb [PRECISION_REB];
  ap_uint<N*K*K/TN> tmp_in_reb [PRECISION_REB][TN];
  #pragma HLS ARRAY_PARTITION variable=tmp_in_reb complete dim=1
  #pragma HLS ARRAY_PARTITION variable=tmp_in_reb complete dim=2
//  ap_uint<N*K*K> tmp_in_1_reb [PRECISION_REB];
  ap_uint<N*K*K/TN> tmp_in_1_reb [PRECISION_REB][TN];
  #pragma HLS ARRAY_PARTITION variable=tmp_in_1_reb complete dim=1
  #pragma HLS ARRAY_PARTITION variable=tmp_in_1_reb complete dim=2

  bool pt = 1;

  // control loop
  for (int tr=0; tr<outRow; tr++){
    for (int tc=0; tc<outCol; tc++){
      for (int ti=0; ti<K; ti++){
        for (int tj=0; tj<K; tj++){
          #pragma HLS PIPELINE II=1
          ap_uint<PRECISION_REB*N> input_buf = frame_in.read();
          //if (tr==0 & tc==0) printf("%lu ", (unsigned long int)input_buf);
          tmp_in.range(PRECISION_REB*N*(ti*K+tj+1)-1,PRECISION_REB*N*(ti*K+tj)) = input_buf;
          if (ti==K-1 && tj==K-1){
            for (int tb = 0; tb < PRECISION_REB; tb++){
                #pragma HLS UNROLL
                for (int t_ch_in = 0; t_ch_in < N/TN; t_ch_in++){
                    #pragma HLS UNROLL
                    for (int t_tn = 0; t_tn < TN; t_tn++){
                        #pragma HLS UNROLL
                        for (int t_kk = 0; t_kk < K*K; t_kk++){
                            #pragma HLS UNROLL
//                            tmp_in_reb[tb].range(t_mac, t_mac) = tmp_in.range(t_mac*PRECISION_REB+tb, t_mac*PRECISION_REB+tb); // tmp_in_reb[0] is LSB
                            tmp_in_reb[tb][t_tn].range(t_kk*N/TN+t_ch_in, t_kk*N/TN+t_ch_in) = tmp_in.range(t_kk*N*PRECISION_REB+t_tn*N/TN*PRECISION_REB+t_ch_in*PRECISION_REB+tb, t_kk*N*PRECISION_REB+t_tn*N/TN*PRECISION_REB+t_ch_in*PRECISION_REB+tb); // tmp_in_reb[0] is LSB
                        }
                    }
                }
                // perform randomisation on subsequent inputs
                for (int t_tn = 0; t_tn < TN; t_tn++){
                    #pragma HLS UNROLL
                    for (int t_mac = 0; t_mac < N*K*K/TN; t_mac++){
                        #pragma HLS UNROLL
                        tmp_in_1_reb[tb][t_tn].range(t_mac, t_mac) = tmp_in_reb[tb][t_tn].range(rand_map_0[t_mac], rand_map_0[t_mac]);
                    }
                }

            }
          }
        }
      }
      tmp_out = LUT_TM<PRECISION_REB, N*K*K/TN, PopCountWidth, PopCountIntWidth, M, TN, TM> (tmp_in_reb, tmp_in_1_reb, weightMem0, weightMem1, weightMem2, thresh, alpha, next_layer_means, rand_map_0, pt);
      frame_out.write(tmp_out);
    }
  }
  //printf("\n ");
}

// Note: trying to hint the hls to generate parameters per channel (each channel is a new module) without going through extra codegen.





