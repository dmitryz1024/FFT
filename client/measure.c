#include "fft_client.h"

static volatile long double observed_output;
static double seconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)){perror("clock_gettime");exit(2);}
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
/* Independent reference: recursive, strided Cooley-Tukey, twiddles from libm.
 * It deliberately has no connection to the authored kernel or its tables. */
static void reference_recursive(uint32_t n,const EcoComplexL* in,uint32_t stride,EcoComplexL* out) {
    uint32_t k;
    if(n==1){out[0]=in[0];return;}
    reference_recursive(n/2,in,stride*2,out);
    reference_recursive(n/2,in+stride,stride*2,out+n/2);
    for(k=0;k<n/2;++k) {
        long double angle=-6.283185307179586476925286766559005768L*k/n;
        long double c=cosl(angle),s=sinl(angle);
        EcoComplexL a=out[k],b=out[k+n/2];
        long double re=b.re*c-b.im*s,im=b.re*s+b.im*c;
        out[k].re=a.re+re;out[k].im=a.im+im;
        out[k+n/2].re=a.re-re;out[k+n/2].im=a.im-im;
    }
}
static double median(double* v) {
    for(unsigned i=1;i<7;++i){double x=v[i];unsigned j=i;while(j && v[j-1]>x){v[j]=v[j-1];--j;}v[j]=x;}
    return v[3];
}
void client_benchmark(IEcoDSP* dsp) {
    const uint32_t sizes[]={16,64,127,256,1023,1024,4096,16384,65535,65536};
    char path[128];FILE* csv;
    snprintf(path,sizeof(path),"build/results/benchmark-%s.csv",dsp->pVTbl->Algorithm(dsp)?"dif":"dit");
    csv=fopen(path,"w");CHECK(csv!=0);if(!csv)return;
    fprintf(csv,"algorithm,n,soa_fft_us,soa_ifft_us,ooa_fft_us,ooa_ifft_us,reference_fft_us,reference_dft_us,plan_setup_us,service_bytes,vector_bytes,plan_bytes,filter_bytes\n");
    for(unsigned ix=0;ix<sizeof(sizes)/sizeof(sizes[0]);++ix) {
        uint32_t n=sizes[ix],batch=n<=256?32:(n<=4096?8:2);
        EcoComplexL *x=malloc(n*sizeof(*x)),*spectrum=malloc(n*sizeof(*x)),*out=malloc(n*sizeof(*x));
        IEcoFFTPlan* plan=0;IEcoVector *a=0,*b=0;IEcoDigitalFilter* filter=0;
        double values[7][7],times[7];int16_t status=0;
        CHECK(x&&spectrum&&out);if(!x||!spectrum||!out){free(x);free(spectrum);free(out);break;}
        client_signal(n,x,4);
        CHECK(dsp->pVTbl->fftl(dsp,n,x,spectrum)==0);
        CHECK(dsp->pVTbl->CreateVector(dsp,n,&a)==0);
        CHECK(dsp->pVTbl->CreateVector(dsp,n,&b)==0);
        CHECK(dsp->pVTbl->CreatePlan(dsp,n,&plan)==0);
        if(!a||!b||!plan){fprintf(stderr,"Benchmark allocation failure\n");exit(2);}
        for(uint32_t i=0;i<n;++i)CHECK(a->pVTbl->Set(a,i,x[i])==0);
        CHECK(dsp->pVTbl->CreateFilter(dsp,a,&filter)==0);
        for(unsigned metric=0;metric<7;++metric) {
            int skip=(metric==4 && (n&(n-1))) || (metric==5 && n>1024);
            uint32_t reps=metric==5?1:batch;
            if(metric==2 || metric==3)for(uint32_t i=0;i<n;++i)CHECK(a->pVTbl->Set(a,i,metric==2?x[i]:spectrum[i])==0);
            for(unsigned sample=0;sample<8;++sample) { /* first batch is warmup */
                double start=seconds();
                if(!skip)for(uint32_t r=0;r<reps;++r) {
                    if(metric==0)status|=dsp->pVTbl->fftl(dsp,n,x,out);
                    else if(metric==1)status|=dsp->pVTbl->ifftl(dsp,n,spectrum,out);
                    else if(metric==2||metric==3){
                        status|=plan->pVTbl->Load(plan,a);
                        status|=metric==2?plan->pVTbl->FFT(plan):plan->pVTbl->IFFT(plan);
                        status|=plan->pVTbl->Store(plan,b);
                    } else if(metric==4)reference_recursive(n,x,1,out);
                    else if(metric==5)client_reference_dft(n,x,out,0);
                    else {IEcoFFTPlan* temporary=0;status|=dsp->pVTbl->CreatePlan(dsp,n,&temporary);if(temporary)temporary->pVTbl->Release(temporary);}
                    if(metric<2||metric==4||metric==5)observed_output+=out[r%n].re;
                }
                if(sample)values[metric][sample-1]=skip?NAN:(seconds()-start)*1e6/reps;
            }
            times[metric]=skip?NAN:median(values[metric]);
        }
        CHECK(status==0);
        if(!(n&(n-1))){reference_recursive(n,x,1,out);CHECK(client_error(n,spectrum,out)<2e-11L);}
        fprintf(csv,"%s,%u",dsp->pVTbl->Algorithm(dsp)?"DIF":"DIT",n);
        for(unsigned m=0;m<7;++m)fprintf(csv,",%.6f",times[m]);
        fprintf(csv,",%u,%u,%u,%u\n",dsp->pVTbl->MemoryBytes(dsp),a->pVTbl->MemoryBytes(a),plan->pVTbl->MemoryBytes(plan),filter?filter->pVTbl->MemoryBytes(filter):0);
        printf("BENCH %s N=%u SOA=%.2fus OOA=%.2fus REF_FFT=%.2fus\n",dsp->pVTbl->Algorithm(dsp)?"DIF":"DIT",n,times[0],times[2],times[4]);
        if(filter)filter->pVTbl->Release(filter);
        plan->pVTbl->Release(plan);a->pVTbl->Release(a);b->pVTbl->Release(b);
        free(x);free(out);free(spectrum);
    }
    fclose(csv);
    CHECK(isfinite(observed_output));
}
void client_demo(IEcoDSP* dsp) {
    EcoComplexL x[8],y[8],back[8],ref[8],object[8];
    IEcoVector *a=0,*b=0;IEcoFFTPlan* plan=0;
    char path[128];FILE* csv;
    for(uint32_t i=0;i<8;++i){x[i].re=i+1;x[i].im=0;}
    CHECK(dsp->pVTbl->fftl(dsp,8,x,y)==0);
    CHECK(dsp->pVTbl->ifftl(dsp,8,y,back)==0);
    client_reference_dft(8,x,ref,0);
    CHECK(dsp->pVTbl->CreateVector(dsp,8,&a)==0);
    CHECK(dsp->pVTbl->CreateVector(dsp,8,&b)==0);
    CHECK(dsp->pVTbl->CreatePlan(dsp,8,&plan)==0);
    if(!a||!b||!plan){fprintf(stderr,"Demo allocation failure\n");exit(2);}
    for(uint32_t i=0;i<8;++i)CHECK(a->pVTbl->Set(a,i,x[i])==0);
    CHECK(plan->pVTbl->Load(plan,a)==0);CHECK(plan->pVTbl->FFT(plan)==0);CHECK(plan->pVTbl->Store(plan,b)==0);
    for(uint32_t i=0;i<8;++i)CHECK(b->pVTbl->Get(b,i,&object[i])==0);
    CHECK(client_error(8,y,object)==0);
    snprintf(path,sizeof(path),"build/results/demo-%s.csv",dsp->pVTbl->Algorithm(dsp)?"dif":"dit");
    csv=fopen(path,"w");CHECK(csv!=0);
    if(csv)fprintf(csv,"k,input_re,input_im,fft_re,fft_im,ifft_re,ifft_im,reference_re,reference_im\n");
    printf("k | x[k] | FFT real | FFT imag | IFFT real (SOA equals OOA)\n");
    for(uint32_t i=0;i<8;++i){
        printf("%u | %.1Lf | % .12Lf | % .12Lf | %.12Lf\n",i,x[i].re,y[i].re,y[i].im,back[i].re);
        if(csv)fprintf(csv,"%u,%.18Lg,%.18Lg,%.18Lg,%.18Lg,%.18Lg,%.18Lg,%.18Lg,%.18Lg\n",i,x[i].re,x[i].im,y[i].re,y[i].im,back[i].re,back[i].im,ref[i].re,ref[i].im);
    }
    printf("MEMORY service=%u vector=%u plan=%u bytes (requested, allocator metadata excluded)\n",dsp->pVTbl->MemoryBytes(dsp),a->pVTbl->MemoryBytes(a),plan->pVTbl->MemoryBytes(plan));
    if(csv)fclose(csv);
    plan->pVTbl->Release(plan);a->pVTbl->Release(a);b->pVTbl->Release(b);
}
