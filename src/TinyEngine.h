#pragma once
#include <Arduino.h>
#include <FS.h>

class TinyEngine {
public:
  using ProgressFn = void (*)(int,int);
  using CharFn = void (*)(char);
  using LayerFn = void (*)(int,float);
  TinyEngine();
  ~TinyEngine();
  bool load(fs::FS &fs, const char *path);
  bool generate(const char *question, char *out, size_t outSize, int creativity,
                ProgressFn progress=nullptr, CharFn character=nullptr, LayerFn layer=nullptr);
  bool loaded() const { return ready; }
  int layers() const { return L; }
  int width() const { return D; }
  String error() const { return err; }
private:
  struct Table { float *scale=nullptr; int8_t *w=nullptr; int rows=0, cols=0; };
  static constexpr int VOCAB=97, CTX=128, MAXD=128, MAXHID=512, MAXL=8;
  int D=0,L=0,H=0,HD=0,HID=0; float attDiv=1; bool ready=false; String err;
  Table emb,pos,wq[MAXL],wk[MAXL],wv[MAXL],wo[MAXL],f1[MAXL],f2[MAXL];
  float *g1[MAXL]={},*g2[MAXL]={},*gf=nullptr;
  int8_t *kc[MAXL]={},*vc[MAXL]={}; float *ks[MAXL]={},*vs[MAXL]={};
  float bx[MAXD],bh[MAXD],bq[MAXD],bt[MAXD],ba[MAXD],bm[MAXHID],bs[CTX],score[VOCAB],activity[MAXL];
  void release(); bool readTable(File &f, Table &t,int rows,int cols); float *readGains(File &f);
  void matvec(const Table &t,const float *x,float *y); void norm(const float *x,const float *g,float *y);
  float store(int8_t *dst,const float *src); void step(int tok,int p); int pick(int creativity,float &confidence);
};
