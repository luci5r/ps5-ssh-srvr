#include <string.h>
#include "base64.h"

static const char b64_tab[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_encode_block(const unsigned char* in, int inlen, char* out) {
  int olen = 0;
  for(int i=0;i<inlen;i+=3) {
    unsigned v = in[i] << 16;
    if(i+1<inlen) v |= in[i+1] << 8;
    if(i+2<inlen) v |= in[i+2];
    out[olen++] = b64_tab[(v >> 18) & 0x3F];
    out[olen++] = b64_tab[(v >> 12) & 0x3F];
    out[olen++] = (i+1<inlen) ? b64_tab[(v >> 6) & 0x3F] : '=';
    out[olen++] = (i+2<inlen) ? b64_tab[v & 0x3F] : '=';
  }
  return olen;
}

static int b64_rev(char c) {
  if(c>='A'&&c<='Z') return c-'A';
  if(c>='a'&&c<='z') return c-'a'+26;
  if(c>='0'&&c<='9') return c-'0'+52;
  if(c=='+') return 62;
  if(c=='/') return 63;
  if(c=='=') return -2;
  return -1;
}

int b64_decode_block(const char* in, int inlen, unsigned char* out) {
  if(inlen % 4) return -1;
  int olen=0;
  for(int i=0;i<inlen;i+=4) {
    int a=b64_rev(in[i]), b=b64_rev(in[i+1]), c=b64_rev(in[i+2]), d=b64_rev(in[i+3]);
    if(a<0||b<0||c<-2||d<-2) return -1;
    unsigned v=(a<<18)|(b<<12)|((c<0?0:c)<<6)|(d<0?0:d);
    out[olen++] = (v>>16)&0xFF;
    if(c!=-2) out[olen++] = (v>>8)&0xFF;
    if(d!=-2) out[olen++] = v&0xFF;
  }
  return olen;
}