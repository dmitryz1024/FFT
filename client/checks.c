#include "fft_client.h"

static const long double pi = 3.141592653589793238462643383279502884L;
void client_reference_dft(uint32_t n, const EcoComplexL* x, EcoComplexL* y, int inverse) {
    uint32_t k,j;
    for(k=0;k<n;++k) {
        long double re=0, im=0;
        for(j=0;j<n;++j) {
            long double a=(inverse?2:-2)*pi*(long double)(((uint64_t)k*j)%n)/(long double)n;
            long double c=cosl(a),s=sinl(a);
            re+=x[j].re*c-x[j].im*s;
            im+=x[j].re*s+x[j].im*c;
        }
        y[k].re=inverse?re/n:re; y[k].im=inverse?im/n:im;
    }
}
long double client_error(uint32_t n,const EcoComplexL* a,const EcoComplexL* b) {
    long double e=0; uint32_t i;
    for(i=0;i<n;++i) {
        long double r=fabsl(a[i].re-b[i].re),v=fabsl(a[i].im-b[i].im);
        if(!isfinite(r)||!isfinite(v)) return INFINITY;
        if(r>e)e=r;
        if(v>e)e=v;
    }
    return e;
}
void client_signal(uint32_t n,EcoComplexL* x,unsigned kind) {
    uint32_t i,seed=0x705171U;
    for(i=0;i<n;++i) {
        x[i].im=0;
        if(kind==0) x[i].re=i==0?1:0;
        else if(kind==1)x[i].re=2;
        else if(kind==2)x[i].re=cosl(2*pi*i/n);
        else if(kind==3) {x[i].re=cosl(2*pi*i/n);x[i].im=sinl(2*pi*i/n);}
        else {
            seed=1664525U*seed+1013904223U;
            x[i].re=((long double)(seed>>8)/16777216.0L)-0.5L;
            seed=1664525U*seed+1013904223U;
            x[i].im=((long double)(seed>>8)/16777216.0L)-0.5L;
        }
    }
}
static void numeric_cases(IEcoDSP* dsp) {
    const uint32_t sizes[]={1,2,3,5,7,8,16,31,64,127,256,257};
    EcoComplexL x[257],out[257],ref[257],back[257],converted[257];
    EcoComplexD xd[257],yd[257]; EcoComplexF xf[257],yf[257];
    int xi[257]; long xl[257];
    unsigned s,kind,type,direction; uint32_t i,n;
    char path[128]; FILE* csv;
    snprintf(path,sizeof(path),"build/results/accuracy-%s.csv",dsp->pVTbl->Algorithm(dsp)?"dif":"dit");
    csv=fopen(path,"w"); CHECK(csv!=0);
    if(csv)fprintf(csv,"algorithm,n,signal,type,direction,max_abs_error,max_scaled_error,roundtrip_error\n");
    for(s=0;s<sizeof(sizes)/sizeof(sizes[0]);++s) {
        n=sizes[s];
        for(kind=0;kind<5;++kind) {
            client_signal(n,x,kind);
            for(type=0;type<5;++type) {
                for(i=0;i<n;++i) {
                    xd[i].re=(double)x[i].re;xd[i].im=(double)x[i].im;
                    xf[i].re=(float)x[i].re;xf[i].im=(float)x[i].im;
                    xi[i]=(int)(x[i].re*100);xl[i]=(long)xi[i]*10000001L;
                    converted[i]=x[i];
                    if(type==0){converted[i].re=xd[i].re;converted[i].im=xd[i].im;}
                    if(type==1){converted[i].re=xf[i].re;converted[i].im=xf[i].im;}
                    if(type==3){converted[i].re=xi[i];converted[i].im=0;}
                    if(type==4){converted[i].re=xl[i];converted[i].im=0;}
                }
                for(direction=0;direction<2;++direction) {
                    int16_t rc; long double error,scaled=0,roundtrip=0,tolerance;
                    client_reference_dft(n,converted,ref,(int)direction);
                    if(type==0)rc=direction?dsp->pVTbl->ifft(dsp,n,xd,yd):dsp->pVTbl->fft(dsp,n,xd,yd);
                    else if(type==1)rc=direction?dsp->pVTbl->ifftf(dsp,n,xf,yf):dsp->pVTbl->fftf(dsp,n,xf,yf);
                    else if(type==2)rc=direction?dsp->pVTbl->ifftl(dsp,n,x,out):dsp->pVTbl->fftl(dsp,n,x,out);
                    else if(type==3)rc=direction?dsp->pVTbl->iffti(dsp,n,xi,yd):dsp->pVTbl->ffti(dsp,n,xi,yd);
                    else rc=direction?dsp->pVTbl->ifftlong(dsp,n,xl,yd):dsp->pVTbl->fftlong(dsp,n,xl,yd);
                    CHECK(rc==0);
                    if(rc)continue;
                    for(i=0;i<n;++i) {
                        if(type==1){out[i].re=yf[i].re;out[i].im=yf[i].im;}
                        else if(type!=2){out[i].re=yd[i].re;out[i].im=yd[i].im;}
                        long double e=fmaxl(fabsl(out[i].re-ref[i].re),fabsl(out[i].im-ref[i].im));
                        long double denom=fmaxl(1,fmaxl(fabsl(ref[i].re),fabsl(ref[i].im)));
                        scaled=fmaxl(scaled,e/denom);
                    }
                    error=client_error(n,out,ref);
                    tolerance=type==1?3e-6L: (type==4?2e-6L:2e-12L);
                    CHECK(scaled<tolerance);
                    if(!direction) {
                        CHECK(dsp->pVTbl->ifftl(dsp,n,out,back)==0);
                        roundtrip=client_error(n,converted,back);
                        CHECK(roundtrip < (type==1?2e-6L:(type==4?2e-6L:2e-12L)));
                    }
                    if(csv)fprintf(csv,"%s,%u,%u,%s,%s,%.12Le,%.12Le,%.12Le\n",dsp->pVTbl->Algorithm(dsp)?"DIF":"DIT",n,kind,
                        (const char*[]){"double","float","long double","int","long"}[type],direction?"IFFT":"FFT",error,scaled,roundtrip);
                }
            }
        }
    }
    if(csv)fclose(csv);
    /* Analytic sign/order/normalization oracle independent of roundtrip. */
    for(i=0;i<8;++i){x[i].re=i==1?1:0;x[i].im=0;}
    CHECK(dsp->pVTbl->fftl(dsp,8,x,out)==0);
    CHECK(fabsl(out[2].re)<1e-17L && fabsl(out[2].im+1)<1e-17L);
    CHECK(dsp->pVTbl->ifftl(dsp,8,x,out)==0);
    CHECK(fabsl(out[2].re)<1e-17L && fabsl(out[2].im-0.125L)<1e-17L);
    for(i=0;i<8;++i){x[i].re=i+1;x[i].im=0;}
    CHECK(dsp->pVTbl->fftl(dsp,8,x,out)==0);
    CHECK(fabsl(out[0].re-36)<1e-17L && fabsl(out[4].re+4)<1e-17L);
    CHECK(dsp->pVTbl->fftl(dsp,8,x,x)==0);
    CHECK(client_error(8,x,out)<1e-17L);
}
static void object_cases(IEcoDSP* dsp) {
    IEcoVector *a=0,*b=0,*kernel=0,*shortv=0;
    IEcoMatrix* matrix=0; IEcoFFTPlan *plan=0,*other=0;
    IEcoDigitalFilter* filter=0;
    EcoComplexL x[8],y[8],z[8],value; uint32_t i; void* q=0;
    CHECK(dsp->pVTbl->CreateVector(dsp,8,&a)==0);
    CHECK(dsp->pVTbl->CreateVector(dsp,8,&b)==0);
    CHECK(dsp->pVTbl->CreateVector(dsp,8,&kernel)==0);
    CHECK(dsp->pVTbl->CreateVector(dsp,3,&shortv)==0);
    CHECK(dsp->pVTbl->CreatePlan(dsp,8,&plan)==0);
    CHECK(dsp->pVTbl->CreatePlan(dsp,8,&other)==0);
    if(!a||!b||!kernel||!shortv||!plan||!other)goto done;
    CHECK(plan->pVTbl->FFT(plan)==ECO_E_STATE);
    CHECK(plan->pVTbl->Store(plan,b)==ECO_E_STATE);
    client_signal(8,x,4);
    for(i=0;i<8;++i)CHECK(a->pVTbl->Set(a,i,x[i])==0);
    CHECK(plan->pVTbl->Load(plan,a)==0);
    CHECK(other->pVTbl->Load(other,a)==0);
    value.re=999;value.im=0;CHECK(a->pVTbl->Set(a,0,value)==0);
    CHECK(plan->pVTbl->FFT(plan)==0);
    CHECK(plan->pVTbl->Store(plan,b)==0);
    for(i=0;i<8;++i)CHECK(b->pVTbl->Get(b,i,&y[i])==0);
    client_reference_dft(8,x,z,0);CHECK(client_error(8,y,z)<1e-15L);
    CHECK(other->pVTbl->Store(other,b)==0);
    for(i=0;i<8;++i)CHECK(b->pVTbl->Get(b,i,&y[i])==0);
    CHECK(client_error(8,x,y)==0);
    CHECK(plan->pVTbl->IFFT(plan)==0);
    CHECK(plan->pVTbl->Load(plan,shortv)!=0);
    CHECK(plan->pVTbl->Store(plan,b)==0);
    for(i=0;i<8;++i)CHECK(b->pVTbl->Get(b,i,&y[i])==0);
    CHECK(client_error(8,x,y)<1e-15L);
    CHECK(a->pVTbl->Get(a,8,&value)!=0);
    CHECK(a->pVTbl->Set(a,8,value)!=0);
    CHECK(a->pVTbl->QueryInterface(a,&IID_IEcoUnknown,&q)==0 && q==a);
    if(q)((IEcoUnknown*)q)->pVTbl->Release(q);
    q=(void*)1;CHECK(a->pVTbl->QueryInterface(a,&IID_IEcoDSP,&q)!=0 && !q);
    CHECK(dsp->pVTbl->CreateMatrix(dsp,2,3,&matrix)==0);
    if(matrix){
        CHECK(matrix->pVTbl->Rows(matrix)==2 && matrix->pVTbl->Cols(matrix)==3);
        value.re=7;value.im=-2;CHECK(matrix->pVTbl->Set(matrix,1,2,value)==0);
        CHECK(matrix->pVTbl->Get(matrix,1,2,&value)==0 && value.re==7 && value.im==-2);
        CHECK(matrix->pVTbl->Get(matrix,2,0,&value)!=0);
        CHECK(matrix->pVTbl->Get(matrix,0,3,&value)!=0);
    }
    value.re=0.5L;value.im=0;
    CHECK(kernel->pVTbl->Set(kernel,0,value)==0);CHECK(kernel->pVTbl->Set(kernel,1,value)==0);
    CHECK(dsp->pVTbl->CreateFilter(dsp,kernel,&filter)==0);
    if(filter)for(unsigned pass=0;pass<2;++pass) {
        client_signal(8,x,pass?3:4);
        for(i=0;i<8;++i)CHECK(a->pVTbl->Set(a,i,x[i])==0);
        CHECK(filter->pVTbl->Apply(filter,a,b)==0);
        for(i=0;i<8;++i){CHECK(b->pVTbl->Get(b,i,&y[i])==0);z[i].re=(x[i].re+x[(i+7)%8].re)/2;z[i].im=(x[i].im+x[(i+7)%8].im)/2;}
        CHECK(client_error(8,y,z)<1e-15L);
        CHECK(dsp->pVTbl->Filter(dsp,a,kernel,a)==0);
        for(i=0;i<8;++i)CHECK(a->pVTbl->Get(a,i,&y[i])==0);
        CHECK(client_error(8,y,z)<1e-15L);
    }
    if(filter){
        value.re=100;value.im=0;CHECK(kernel->pVTbl->Set(kernel,0,value)==0);
        CHECK(filter->pVTbl->Apply(filter,b,a)==0);
        for(i=0;i<8;++i){CHECK(a->pVTbl->Get(a,i,&y[i])==0);x[i].re=(z[i].re+z[(i+7)%8].re)/2;x[i].im=(z[i].im+z[(i+7)%8].im)/2;}
        CHECK(client_error(8,x,y)<1e-15L);
    }
done:
    if(filter)filter->pVTbl->Release(filter);
    if(matrix)matrix->pVTbl->Release(matrix);
    if(other)other->pVTbl->Release(other);
    if(plan)plan->pVTbl->Release(plan);
    if(shortv)shortv->pVTbl->Release(shortv);
    if(kernel)kernel->pVTbl->Release(kernel);
    if(b)b->pVTbl->Release(b);
    if(a)a->pVTbl->Release(a);
}
void client_tests(IEcoDSP* dsp) {
    EcoComplexD x[2]={{1,0},{2,0}},out[2]={{77,88},{99,111}};
    IEcoVector* invalid=(void*)1; void* q=(void*)1;
    CHECK(sizeof(UGUID)==18 && sizeof(uint32_t)==4 && sizeof(EcoComplexL)==32);
    CHECK(dsp->pVTbl->Add(dsp,7,4)==11 && dsp->pVTbl->Subtract(dsp,7,4)==3);
    CHECK(dsp->pVTbl->QueryInterface(dsp,0,&q)!=0 && !q);
    CHECK(dsp->pVTbl->QueryInterface(dsp,&IID_IEcoDSP,&q)==0 && q==dsp);
    if(q)((IEcoUnknown*)q)->pVTbl->Release(q);
    CHECK(dsp->pVTbl->fft(0,2,x,out)!=0);
    CHECK(dsp->pVTbl->fft(dsp,0,x,out)!=0);
    CHECK(dsp->pVTbl->fft(dsp,65537,x,out)!=0);
    CHECK(dsp->pVTbl->fft(dsp,2,0,out)!=0);
    CHECK(dsp->pVTbl->fft(dsp,2,x,0)!=0);
    x[0].re=NAN;CHECK(dsp->pVTbl->fft(dsp,2,x,out)!=0 && out[0].re==77);
    x[0].re=INFINITY;CHECK(dsp->pVTbl->fft(dsp,2,x,out)!=0 && out[0].re==77);
    CHECK(dsp->pVTbl->CreateVector(dsp,0,&invalid)!=0 && !invalid);
    numeric_cases(dsp);object_cases(dsp);
    for(unsigned s=0;s<3;++s){
        uint32_t n=(uint32_t[]){4096,65535,65536}[s];
        EcoComplexL *a=malloc(n*sizeof(*a)),*b=malloc(n*sizeof(*b));
        CHECK(a&&b);if(!a||!b){free(a);free(b);continue;}
        client_signal(n,a,4);
        CHECK(dsp->pVTbl->fftl(dsp,n,a,b)==0);
        long double ex=0,ey=0;
        for(uint32_t i=0;i<n;++i){ex+=a[i].re*a[i].re+a[i].im*a[i].im;ey+=b[i].re*b[i].re+b[i].im*b[i].im;}
        CHECK(fabsl(ex-ey/n)<1e-11L*ex);
        CHECK(dsp->pVTbl->ifftl(dsp,n,b,b)==0);
        CHECK(client_error(n,a,b)<2e-12L);
        free(a);free(b);
    }
    printf("UNIT algorithm=%s cumulative_checks=%u failures=%u\n",dsp->pVTbl->Algorithm(dsp)?"DIF":"DIT",client_checks,client_failures);
}
