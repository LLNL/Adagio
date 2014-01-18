#include <assert.h>
#include <stdio.h>
#include "shift.h"
#include "cpuid.h"
#define BLR_USE_SHIFT
//#undef BLR_USE_SHIFT
static int current_freq=0;




int
shift(int freq_idx){
#ifdef BLR_USE_SHIFT
        char *cpufreq_filename[] = {
		"/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed",
		"/sys/devices/system/cpu/cpu1/cpufreq/scaling_setspeed",
		"/sys/devices/system/cpu/cpu2/cpufreq/scaling_setspeed",
		"/sys/devices/system/cpu/cpu3/cpufreq/scaling_setspeed"};
        static char *freq_str[]=
                {"1800000", "1600000", "1400000", "1200000", "1000000"};
        FILE *sfp;
#endif
        static int prev_freq_idx[4]={0, 0, 0, 0};
	static int cpuid = -1;
	static int shift_initialized=0;
	int temp_cpuid;
	if(!shift_initialized){
		cpuid = get_cpuid();
		// Shift both processors to top gear.  This is clumsy, but
		// we can clean it up later.
		sfp = fopen(cpufreq_filename[ 1 ], "w");
		if(!sfp){
			fprintf(stderr, "!!! cpufreq_filename[%d]=%s does not exist.  Bye!\n", 
					cpuid, cpufreq_filename[cpuid]);
		}
		assert(sfp);
		fprintf(sfp, freq_str[ 0 ]);
		fclose(sfp);

		sfp = fopen(cpufreq_filename[ 2 ], "w");
		if(!sfp){
			fprintf(stderr, 
				"!!! cpufreq_filename[%d]=%s does not exist.  Bye!\n", 
				cpuid, cpufreq_filename[cpuid]);
		}
		assert(sfp);
		fprintf(sfp, freq_str[ 0 ]);
		fclose(sfp);

		shift_initialized=1;
	}
	temp_cpuid = get_cpuid();
	assert( temp_cpuid == cpuid );

        if( freq_idx == prev_freq_idx[ cpuid ] ){
                return freq_idx;
        }
	prev_freq_idx[ cpuid ] = freq_idx;
	
	assert( (freq_idx >= 0) && (freq_idx <= 4) );
	
	//Make the change
#ifdef BLR_USE_SHIFT
	sfp = fopen(cpufreq_filename[ cpuid ], "w");
	if(!sfp){
		fprintf(stderr, "!!! cpufreq_filename[%d]=%s does not exist.  Bye!\n", 
				cpuid, cpufreq_filename[cpuid]);
	}
	assert(sfp);
	fprintf(sfp, freq_str[ freq_idx ]);
	fclose(sfp);
#endif
	current_freq = freq_idx;
	return freq_idx;
}
