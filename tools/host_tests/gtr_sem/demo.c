/* demo.c · 手印 gtr_sem 的击打事件（下扫 + 上扫），肉眼核对时序/力度用
   跑法：node run_demo.mjs
   输出格式与 golden.tsv 的 E 行一致，便于逐案比对。 */
#include <math.h>
#include <stdio.h>
#include "gtr_sem.h"
int main(void){
    const int A[5]={45,52,57,61,64};
    struct gtr_event ev[16]; int len=0;
    struct gtr_strum_opts o = {.pattern_id=GTR_STROKE_DOWN, .spread=0.025, .gate=1.2};
    gtr_strum(A,5,&o,ev,16,&len);
    for(int i=0;i<len;i++)
        printf("E %d %d %lld %lld %d %d\n", ev[i].note, ev[i].velocity,
            (long long)llround(ev[i].tOn*1e6), (long long)llround(ev[i].tOff*1e6),
            ev[i].string, ev[i].stroke);
    struct gtr_strum_opts u = {.pattern_id=GTR_STROKE_UP, .spread=0.025, .gate=1.2};
    gtr_strum(A,5,&u,ev,16,&len);
    printf("up len=%d\n", len);
    for(int i=0;i<len;i++)
        printf("E %d %d %lld %lld %d %d\n", ev[i].note, ev[i].velocity,
            (long long)llround(ev[i].tOn*1e6), (long long)llround(ev[i].tOff*1e6),
            ev[i].string, ev[i].stroke);
    return 0;
}
