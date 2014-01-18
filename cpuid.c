#include "cpuid.h"
int
get_cpuid(void){
	//Return value should be in the range 0-3.
        int a,b,c,d;
        int apic_id;

#define cpuid( in, a, b, c, d ) \
    asm ( "cpuid" :                                     \
          "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (in));
#define INITIAL_APIC_ID_BITS  0xFF000000

        //Figure out which core we're on.
        cpuid( 1, a, b, c, d );
        a=a; b=b; c=c; d=d;
        apic_id = ( b & INITIAL_APIC_ID_BITS ) >> 24;

#undef cpuid
#undef INITAL_APIC_ID_BITS
	return apic_id;
}
