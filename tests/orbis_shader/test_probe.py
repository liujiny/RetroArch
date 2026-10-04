#!/usr/bin/env python3
"""Exercise actual production diagnostic code with two allocator owners."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'frontend/drivers/platform_orbis.c').read_text()
a=s.index('void frontend_orbis_shader_probe('); b=s.index('\nint rarch_main(',a)
fixture=r"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int SceKernelModule;
typedef struct {size_t size;char name[256];struct {void *address;} segmentInfo[4];} SceKernelModuleInfo;
uint64_t sceLibcHeapExtendedAlloc=1;
const unsigned char sceLibcMallocReplace[112]={0};
const unsigned char sceLibcNewReplace[112]={0};
const unsigned char sceLibcMallocReplaceForTls[56]={0};
#include "platform_orbis_heap.h"
static int fail_resolve,sys_allocs,sys_frees,app_allocs,app_frees,logs;
static void *sys_live,*app_live;
static void *sm(size_t n){assert(!sys_live);sys_allocs++;return sys_live=n>1048576?NULL:malloc(n);}
static void sf(void *p){assert(p && p==sys_live && p!=app_live);sys_frees++;free(p);sys_live=NULL;}
static void *am(size_t n){assert(!app_live);app_allocs++;return app_live=malloc(n);}
static void af(void *p){assert(p==app_live && p!=sys_live);app_frees++;free(p);app_live=NULL;}
static int sceKernelDebugOutText(int c,const char *s){logs++;return 0;}
static int process_type(int pid){assert(pid==-1);return 1;}
static const uint64_t *process_param(void){
 static uint64_t p[8];p[0]=0x40;p[7]=(uintptr_t)&_sceLibcParam;return p;
}
static int sceKernelGetModuleList(int *h,size_t cap,size_t *n){assert(cap>=2);h[0]=1;h[1]=2;*n=2;return 0;}
static int sceKernelGetModuleInfo(int h,SceKernelModuleInfo *i){strcpy(i->name,h==1?"libSceLibcInternal":"libkernel_sys");return 0;}
static int sceKernelDlsym(int h,const char *n,void **p){if(fail_resolve)return -1;if(!strcmp(n,"sceKernelGetProcessType")){*p=(void*)process_type;return 0;}if(!strcmp(n,"sceKernelGetProcParam")){*p=(void*)process_param;return 0;}*p=!strcmp(n,"malloc")?(void*)sm:(void*)sf;return 0;}
#define malloc am
#define free af
"""
fixture+=s[a:b]
fixture+=r"""
#undef malloc
#undef free
int main(int argc,char **argv){
 fail_resolve=argc>1;
 for(int i=0;i<20;i++) frontend_orbis_shader_probe("/test/crt.glsl");
 assert(app_allocs==36 && app_frees==36);
 assert(sys_allocs==(fail_resolve?0:36) && sys_frees==(fail_resolve?0:24));
 assert(!sys_live && !app_live);int old=logs;
 frontend_orbis_shader_probe("/test/another.glsl");assert(logs==old);
 puts("PASS: allocator ownership, allocation/resolution failure, 12-preset bound and cleanup");
}
"""
with tempfile.TemporaryDirectory() as t:
 f=Path(t)/'test.c';f.write_text(fixture);exe=Path(t)/'test'
 subprocess.run(['cc','-std=c99','-fsanitize=address,undefined','-g','-I'+str(root/'frontend/drivers'),str(f),'-o',str(exe)],check=True)
 for args in [[],['unresolved']]:subprocess.run([str(exe)]+args,check=True)
