#include <assert.h>
#include "log.h"
FILE *
initialize_logfile(int rank){
        FILE *fp=NULL;
        char format[]="runtime.%02d.dat";
        char fname[64];
        sprintf(fname, format, rank);
        fp = fopen(fname, "w");
        assert(fp);
        return fp;
}

