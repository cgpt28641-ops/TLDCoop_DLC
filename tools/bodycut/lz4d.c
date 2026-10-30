#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
// usage: lz4d in out usize  (raw LZ4 block)
int main(int c,char**v){FILE*f=fopen(v[1],"rb");fseek(f,0,2);long n=ftell(f);fseek(f,0,0);uint8_t*s=malloc(n);fread(s,1,n,f);fclose(f);
long us=atol(v[3]);uint8_t*d=malloc(us+64);long ip=0,op=0;
while(ip<n){int t=s[ip++];long ll=t>>4;if(ll==15){int b;do{b=s[ip++];ll+=b;}while(b==255);}memcpy(d+op,s+ip,ll);ip+=ll;op+=ll;if(ip>=n)break;
long off=s[ip]|(s[ip+1]<<8);ip+=2;long ml=t&15;if(ml==15){int b;do{b=s[ip++];ml+=b;}while(b==255);}ml+=4;for(long i=0;i<ml;i++){d[op]=d[op-off];op++;}}
if(op!=us){fprintf(stderr,"size mismatch %ld %ld\n",op,us);}
f=fopen(v[2],"wb");fwrite(d,1,op,f);fclose(f);return 0;}
