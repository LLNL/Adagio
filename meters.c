#include <mpi.h>
#include <assert.h>
#include <stdlib.h>	//system()
#include <stdio.h>
#include <sys/time.h>
#include "meters.h"
#include "gettimeofday_helpers.h"

long long int
mark_joules(int rank, int size){
	
	// Nodes:   0 12 34567 8901 2345 6
	//          | |  |//// |/// |/// |
	// Meters:  0 1  3     8    2    6
	
        //This is really stupid.
        FILE *jfp;
        long long int total_joules=-1, joules=-1, rc;
	char buf[128];
	static long long int prev_total_joules=-1;
	static int initialized = 0;
	static struct timeval start, stop;
	double seconds = -1.0;

        //Capture current joules.
	snprintf(buf, 127, "/osr/pac/bin/powermeter -j -U > /tmp/blr_n%03d_j", rank);
        system(buf);
	
        //read the results.
	snprintf(buf, 127, "/tmp/blr_n%03d_j", rank);
	jfp=fopen(buf, "r");
	assert(jfp);
	rc=fscanf(jfp, "%lld", &joules);
	assert(rc);
	fclose(jfp);

        PMPI_Reduce (
                &joules,  	//void *sendbuf,
                &total_joules,  //void *recvbuf,
                1,              //int count,
                MPI_INT,        //MPI_Datatype datatype,
                MPI_SUM,        //MPI_Op op,
                0,              //int root,
                MPI_COMM_WORLD  //MPI_Comm comm )
        );

	if( rank == 0 ){
		if( !initialized ){
			gettimeofday( &start, NULL );
			prev_total_joules = total_joules;
			initialized = 1;
		}else{
			gettimeofday( &stop, NULL );
			seconds = delta_seconds( &start, &stop ),
			fprintf(stderr, "QQQ Size= %3d  Seconds= %7.2f Joules= %12lld AvgWatts=%3.2f\n",
				size,
				seconds,
				total_joules-prev_total_joules,
				(double) (total_joules-prev_total_joules) / seconds / (double) size);
		}
	}
	return total_joules;

}
