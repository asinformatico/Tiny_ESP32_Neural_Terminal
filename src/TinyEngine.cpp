#include "TinyEngine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void *tinyAlloc(size_t n) { void *p = ps_malloc(n); if(!p) p=malloc(n); return p; }
static int read16le(File &f) { int a=f.read(), b=f.read(); return (a<0||b<0)?-1:(a|(b<<8)); }
TinyEngine::TinyEngine() {}
TinyEngine::~TinyEngine(){ release(); }
void TinyEngine::release(){
  auto freeTable=[](Table &t){ if(t.scale) free(t.scale); if(t.w) free(t.w); t={}; };
  freeTable(emb); freeTable(pos); if(gf) free(gf); gf=nullptr;
  for(int l=0;l<MAXL;l++){ freeTable(wq[l]);freeTable(wk[l]);freeTable(wv[l]);freeTable(wo[l]);freeTable(f1[l]);freeTable(f2[l]); if(g1[l])free(g1[l]);if(g2[l])free(g2[l]);if(kc[l])free(kc[l]);if(vc[l])free(vc[l]);if(ks[l])free(ks[l]);if(vs[l])free(vs[l]);g1[l]=g2[l]=nullptr;kc[l]=vc[l]=nullptr;ks[l]=vs[l]=nullptr; }
  ready=false; D=L=H=HD=HID=0;
}
bool TinyEngine::readTable(File &f,Table &t,int rows,int cols){
  t.rows=rows;t.cols=cols;t.scale=(float*)tinyAlloc(rows*sizeof(float));t.w=(int8_t*)tinyAlloc((size_t)rows*cols);
  if(!t.scale||!t.w){err="Sin memoria para tabla de pesos";return false;}
  for(int r=0;r<rows;r++){ if(f.read((uint8_t*)&t.scale[r],4)!=4 || f.read((uint8_t*)(t.w+(size_t)r*cols),cols)!=(size_t)cols){err="TINY.BIN truncado o dañado";return false;} }
  return true;
}
float *TinyEngine::readGains(File &f) {
  const size_t expected = (size_t)D * sizeof(float);
  float *g = (float *)tinyAlloc(expected);
  if (!g) {
    err = "Sin memoria para ganancias";
    return nullptr;
  }

  const size_t received = f.read((uint8_t *)g, expected);
  if (received != expected) {
    Serial.printf("[MODEL] Gains read error: offset=%u expected=%u received=%u\n",
                  (unsigned)f.position(), (unsigned)expected, (unsigned)received);
    free(g);
    err = "TINY.BIN truncado en ganancias";
    return nullptr;
  }
  return g;
}
bool TinyEngine::load(fs::FS &fs,const char *path){
  release(); err=""; File f=fs.open(path,"r"); if(!f){err="No se encuentra TINY.BIN";return false;}
  char magic[4]; if(f.read((uint8_t*)magic,4)!=4||memcmp(magic,"TNY2",4)){err="Formato incorrecto: se esperaba TNY2";f.close();return false;}
  D=read16le(f);L=read16le(f);H=read16le(f);HID=read16le(f);
  if(D<8||D>MAXD||L<1||L>MAXL||H<1||D%H||HID<8||HID>MAXHID){err="Dimensiones del modelo no compatibles";f.close();release();return false;}
  HD=D/H;attDiv=sqrtf((float)HD);
  bool ok=readTable(f,emb,VOCAB,D)&&readTable(f,pos,CTX,D);
  for(int l=0;l<L&&ok;l++){
    g1[l]=readGains(f); if(!g1[l]){ok=false;break;}
    ok=readTable(f,wq[l],D,D)&&readTable(f,wk[l],D,D)&&readTable(f,wv[l],D,D)&&readTable(f,wo[l],D,D);
    if(!ok)break; g2[l]=readGains(f); if(!g2[l]){ok=false;break;}
    ok=readTable(f,f1[l],HID,D)&&readTable(f,f2[l],D,HID);
  }
  if(ok){gf=readGains(f);if(!gf)ok=false;}
  if(ok&&f.available()!=0){err="Tamaño de TINY.BIN inesperado";ok=false;}
  f.close();
  if(ok){for(int l=0;l<L;l++){kc[l]=(int8_t*)tinyAlloc((size_t)CTX*D);vc[l]=(int8_t*)tinyAlloc((size_t)CTX*D);ks[l]=(float*)tinyAlloc(CTX*sizeof(float));vs[l]=(float*)tinyAlloc(CTX*sizeof(float));if(!kc[l]||!vc[l]||!ks[l]||!vs[l]){err="PSRAM insuficiente para memoria de atención";ok=false;break;}}}
  if(!ok){release();return false;} ready=true; return true;
}
void TinyEngine::matvec(const Table&t,const float*x,float*y){const int8_t*w=t.w;for(int r=0;r<t.rows;r++){float s=0;for(int c=0;c<t.cols;c++)s+=(float)w[c]*x[c];y[r]=s*t.scale[r];w+=t.cols;}}
void TinyEngine::norm(const float*x,const float*g,float*y){float s=0;for(int i=0;i<D;i++)s+=x[i]*x[i];s=1.0f/sqrtf(s/D+1e-5f);for(int i=0;i<D;i++)y[i]=x[i]*s*g[i];}
float TinyEngine::store(int8_t*dst,const float*src){float big=0;for(int i=0;i<D;i++){float v=fabsf(src[i]);if(v>big)big=v;}if(big<1e-20f)big=1e-20f;float k=127.0f/big;for(int i=0;i<D;i++){float v=src[i]*k;int q=(int)(v<0?v-0.5f:v+0.5f);dst[i]=(int8_t)constrain(q,-127,127);}return big/127.0f;}
void TinyEngine::step(int tok,int p){
  const int8_t*e=emb.w+(size_t)tok*D;const int8_t*q=pos.w+(size_t)p*D;float es=emb.scale[tok],ps=pos.scale[p];
  for(int i=0;i<D;i++)bx[i]=e[i]*es+q[i]*ps;
  for(int l=0;l<L;l++){
    norm(bx,g1[l],bh);matvec(wk[l],bh,bt);ks[l][p]=store(kc[l]+(size_t)p*D,bt);matvec(wv[l],bh,bt);vs[l][p]=store(vc[l]+(size_t)p*D,bt);matvec(wq[l],bh,bq);
    for(int h=0;h<H;h++){
      float big=-1e30f;
      for(int j=0;j<=p;j++){const int8_t*kp=kc[l]+(size_t)j*D+h*HD;float s=0;for(int i=0;i<HD;i++)s+=bq[h*HD+i]*kp[i];s=s*ks[l][j]/attDiv;bs[j]=s;if(s>big)big=s;}
      float sum=0;for(int j=0;j<=p;j++){bs[j]=(bs[j]<-60)?0:expf(bs[j]-big);sum+=bs[j];}
      for(int i=0;i<HD;i++)ba[h*HD+i]=0;
      for(int j=0;j<=p;j++){if(bs[j]==0)continue;const int8_t*vp=vc[l]+(size_t)j*D+h*HD;float s=bs[j]*vs[l][j]/sum;for(int i=0;i<HD;i++)ba[h*HD+i]+=s*vp[i];}
    }
    matvec(wo[l],ba,bt);for(int i=0;i<D;i++)bx[i]+=bt[i];norm(bx,g2[l],bh);matvec(f1[l],bh,bm);float act=0;for(int i=0;i<HID;i++){if(bm[i]<0)bm[i]=0;else act+=bm[i];}activity[l]=act/HID;matvec(f2[l],bm,bt);for(int i=0;i<D;i++)bx[i]+=bt[i];
  }
  norm(bx,gf,bh);matvec(emb,bh,score);
}
int TinyEngine::pick(int creativity,float &confidence){int best=0;for(int i=1;i<VOCAB;i++)if(score[i]>score[best])best=i;float sum=0;for(int i=0;i<VOCAB;i++)sum+=expf(constrain(score[i]-score[best],-60.0f,0.0f));confidence=sum>0?1.0f/sum:0;if(creativity<=0)return best;float temp=creativity/5.0f,total=0;for(int i=0;i<VOCAB;i++){bs[i]=expf(constrain((score[i]-score[best])/temp,-60.0f,0.0f));if(bs[i]<0.15f)bs[i]=0;total+=bs[i];}float r=((float)esp_random()/4294967295.0f)*total;for(int i=0;i<VOCAB;i++){r-=bs[i];if(r<0&&bs[i]>0)return i;}return best;}
bool TinyEngine::generate(const char*question,char*out,size_t outSize,int creativity,ProgressFn progress,CharFn character,LayerFn layer){
  if(!ready){err="Modelo no cargado";return false;}if(!question||!out||outSize<2)return false;
  char q[73];int n=0;for(;question[n]&&n<72;n++){unsigned char c=question[n];if(c<32||c>126)c=' ';q[n]=(char)c;}while(n>0&&q[n-1]==' ')n--;q[n]=0;if(!n){err="Pregunta vacía";return false;}
  int p=0,letters=0;for(;p<n&&p<CTX-1;p++){if(progress)progress(p,n);step((uint8_t)q[p]-30,p);for(int l=0;l<L;l++)if(layer)layer(l,activity[l]);}
  step(1,p++);for(;p<CTX&&letters<(int)outSize-1;p++){float confidence=0;int tok=pick(creativity,confidence);if(tok<=1)break;char c=(char)(tok+30);out[letters++]=c;if(character)character(c);step(tok,p);for(int l=0;l<L;l++)if(layer)layer(l,activity[l]);}
  out[letters]=0;return true;
}
