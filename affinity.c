#define __USE_GNU
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include "affinity.h"

void
set_cpu_affinity(int rank){
	int rc;
        cpu_set_t mask;

        CPU_ZERO(&mask);
	if(rank%2){
		CPU_SET(1,&mask);       // 0,2 work well, but 8p2c opt08 dies.
	}else{
		CPU_SET(2,&mask);       // used to be 3, kills paradis.
	}
	rc = sched_setaffinity( 0, sizeof(cpu_set_t), &mask );
	if(rc < 0){ perror("sched_getaffinity"); }
	rc = sched_yield();
	if(rc < 0){ perror("sched_yield"); }
}


