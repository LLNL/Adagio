/* shim.c
 *
 * The two global functions here, shim_pre() and shim_post, are called from the
 * shim_functions file when an MPI call is intercepted.
 */
#include <stdio.h>
#include <stdlib.h>	// getenv
#include <string.h>	// strlen, strstr
#include <math.h>
#include <numa.h>
#include <sys/utsname.h>
#include "shim_enumeration.h"
#include "shim.h"
#include "gettimeofday_helpers.h"
#include "stacktrace.h"
#include "log.h"
#include "wpapi.h"	// PAPI wrappers.
#include "shift.h"	// shift, enums.  Brings in machine.h.
#include "affinity.h"	// Setting cpu affinity.
static int rank, size;

static struct timeval ts_start_communication, ts_start_computation,
			ts_stop_communication, ts_stop_computation;

static int g_algo;	// which algorithm(s) to use.
static int g_freq;	// frequency to use with fixedfreq.  
static int g_trace;	// tracing level.  

static int current_hash=0, previous_hash=-1, current_freq=0, next_freq=0;
static int in_computation=1;
static int MPI_Initialized_Already=0;

static double frequency[NUM_FREQS] = {1.8, 1.6, 1.4, 1.2, 1.0};
#define GMPI_MIN_COMP_SECONDS (0.1)     // In seconds.
#define GMPI_MIN_COMM_SECONDS (0.1)     // In seconds.
#define GMPI_BLOCKING_BUFFER (0.1)      // In seconds.

static int FASTEST_FREQ = __FASTEST_FREQ;
static int SLOWEST_FREQ = __SLOWEST_FREQ;

FILE *logfile = NULL;

// Don't change this without altering the hash function.
static struct entry schedule[8192];
static double current_comp_seconds[NUM_FREQS];
static double current_comp_insn[NUM_FREQS];

enum{ 
	// To run without library overhead, use the .pristine binary.
	algo_NONE	= 0x000,	// Identical to CLEAN.
	algo_FERMATA 	= 0x001,	// slow communication
	algo_ANDANTE 	= 0x002,	// slow comp before global sync pts.
	algo_ADAGIO  	= 0x004,	// fermata + andante
	algo_ALLEGRO 	= 0x008,	// slow computation everywhere.
	algo_FIXEDFREQ	= 0x010,	// run whole program at single freq.
	algo_JITTER  	= 0x020,	// per ncsu.
	algo_MISER   	= 0x040,	// per vt.
	algo_CLEAN   	= 0x080,	// Identical to fixedfreq=0.
	mods_FAKEJOULES = 0x100,	// Pretend power meter present.
	mods_FAKEFREQ   = 0x200,	// Pretend to change the cpu frequency.
	mods_BIGCOMM	= 0x400,	// Barrier before MPI_Alltoall.

	trace_NONE	= 0x000,
	trace_TS	= 0x001,	// Timestamp.
	trace_FILE	= 0x002,	// Filename where MPI call occurred.
	trace_LINE	= 0x004,	// Line # where MPI call occurred.
	trace_FN	= 0x008,	// MPI Function Name.
	trace_COMP	= 0x010,	// Elapsed comp time since the end of
					//   the previous MPI comm call.
	trace_COMM	= 0x020,	// Elapsed comm time spend in mpi lib.
	trace_RANK	= 0x040,	// MPI rank.
	trace_PCONTROL	= 0x080,	// Most recent pcontrol.
	trace_ALL	= 0xFFF
	
	
};

////////////////////////////////////////////////////////////////////////////////
// Init
////////////////////////////////////////////////////////////////////////////////
static void
pre_MPI_Init( union shim_parameters *p ){

	struct utsname utsname;	// Holds hostname.
	char  *hostname;
	char *env_algo, *env_trace, *env_freq, *env_badnode, *env_mods;
	p=p;

	// Using "-mca key value" on the command line, the environment variable
	// "OMPI_MCA_key" is set to "value".  mpirun doesn't check the validity
	// of these (yay!), so we end up with an easy way of controlling the 
	// runtime environment without confusing the application 
	env_algo=getenv("OMPI_MCA_gmpi_algo");
	if(env_algo && strlen(env_algo) > 0){
		g_algo &= strstr(env_algo, "none"      ) ? algo_NONE      : 0xFFF;
		g_algo |= strstr(env_algo, "fermata"   ) ? algo_FERMATA   : 0;
		g_algo |= strstr(env_algo, "andante"   ) ? algo_ANDANTE   : 0;
		g_algo |= strstr(env_algo, "adagio"    ) ? algo_FERMATA | algo_ANDANTE    : 0;
		g_algo |= strstr(env_algo, "allegro"   ) ? algo_ALLEGRO   : 0;
		g_algo |= strstr(env_algo, "fixedfreq" ) ? algo_FIXEDFREQ : 0;
		g_algo |= strstr(env_algo, "jitter"    ) ? algo_JITTER    : 0;
		g_algo |= strstr(env_algo, "miser"     ) ? algo_MISER     : 0;
		g_algo |= strstr(env_algo, "clean"     ) ? algo_CLEAN     : 0;
	}
	env_mods=getenv("OMPI_MCA_gmpi_mods");
	if(env_mods && strlen(env_mods) > 0){
		g_algo |= strstr(env_mods, "fakejoules") ? mods_FAKEJOULES: 0;
		g_algo |= strstr(env_mods, "bigcomm"   ) ? mods_BIGCOMM   : 0;
	}

	env_freq=getenv("OMPI_MCA_gmpi_freq");
	if(env_freq && strlen(env_freq) > 0){
		g_freq = atoi(env_freq);	//FIXME make this a little more idiotproof.
	}

	env_trace=getenv("OMPI_MCA_gmpi_trace");
	if(env_trace && strlen(env_trace) > 0){
		g_trace &= strstr(env_trace, "none"    ) ? trace_NONE  	: ~trace_NONE;
		g_trace |= strstr(env_trace, "all"     ) ? trace_ALL   	: 0;
		g_trace |= strstr(env_trace, "ts"      ) ? trace_TS    	: 0;
		g_trace |= strstr(env_trace, "file"    ) ? trace_FILE  	: 0;
		g_trace |= strstr(env_trace, "line"    ) ? trace_LINE  	: 0;
		g_trace |= strstr(env_trace, "fn"      ) ? trace_FN    	: 0;
		g_trace |= strstr(env_trace, "comp"    ) ? trace_COMP  	: 0;
		g_trace |= strstr(env_trace, "comm"    ) ? trace_COMM  	: 0;
		g_trace |= strstr(env_trace, "rank"    ) ? trace_RANK  	: 0;
		g_trace |= strstr(env_trace, "pcontrol") ? trace_PCONTROL: 0;
		//fprintf(stdout,"g_trace=%s %d\n", env_trace, g_trace);
	}

	env_badnode=getenv("OMPI_MCA_gmpi_badnode");
	if(env_badnode && strlen(env_badnode) > 0){
		uname(&utsname);
		hostname = utsname.nodename;
		if( strstr( env_badnode, hostname ) ){
			g_algo |= mods_FAKEFREQ;
		}
	}

	// To bound miser, we assume top gear is 1 instead of 0.
	// We also allow fermata to be used.
	if(g_algo & algo_MISER){
		g_algo |= algo_FERMATA;
		FASTEST_FREQ = 1;
	}
	
	// Put a reasonable value in.
	gettimeofday(&ts_start_computation, NULL);  
	gettimeofday(&ts_stop_computation, NULL);  
	
	// Pretend computation started here.
	start_papi();	
	
	// Set up signal handling.
	initialize_handler();

	MPI_Initialized_Already=1;

}

static void
post_MPI_Init( union shim_parameters *p ){
	p=p;
	// Now that PMPI_Init has ostensibly succeeded, grab the rank & size.
	PMPI_Comm_rank(MPI_COMM_WORLD, &rank);
	PMPI_Comm_size(MPI_COMM_WORLD, &size);
	
	// Set CPU affinity and memory preference.
	set_cpu_affinity( rank );
	numa_set_localalloc();
	
	// Start us in a known frequency.
	shift(FASTEST_FREQ);
	
	// Fire up the logfile.
	if(g_trace){logfile = initialize_logfile( rank );}
	mark_joules(rank, size);
}
	
////////////////////////////////////////////////////////////////////////////////
// Finalize
////////////////////////////////////////////////////////////////////////////////
static void
pre_MPI_Finalize( union shim_parameters *p ){
	p=p;
	mark_joules(rank, size);
	PMPI_Barrier( MPI_COMM_WORLD );
	// Leave us in a known frequency.  This should always be 0.
	shift(0);
	
}
	
static void
post_MPI_Finalize( union shim_parameters *p ){
	p=p;
	if(g_trace){fclose(logfile);}
}

////////////////////////////////////////////////////////////////////////////////
// Logging
////////////////////////////////////////////////////////////////////////////////
char*
f2str( int shim_id ){
	char *str;
	switch(shim_id){
#include "shim_str.h"
		default:  str = "Unknown"; break;
	}
	return str;
}

void
Log( int shim_id, union shim_parameters *p ){

	static int initialized=0;
	MPI_Aint lb; 
	MPI_Aint extent;
	int MsgSz=-1;
	char *var_format =
		"%5d %14s %06d "\
		" %9.6lf %9.6lf %9.6lf %9.6lf %9.6lf"\
		" %9.6lf %7d\n";
	char *hdr_format =
		"%5s %14s %6s"\
		" %9s %9s %9s %9s %9s"\
		" %9s %7s\n";


	// One-time initialization.
	if(!initialized){
		//Write the header line.
		if(rank==0){
			fprintf(logfile, 
				hdr_format,
				" Rank", "Function", "Hash", 
				"Comp0", "Comp1", "Comp2", "Comp3", "Comp4",
				"Comm", "MsgSz");
		}else{
			fprintf(logfile, 
				hdr_format,
				"#Rank", "Function", "Hash", 
				"Comp0", "Comp1", "Comp2", "Comp3", "Comp4",
				"Comm", "MsgSz");
		}
		initialized=1;
	}

	// Determine message size for selected function calls.
	switch(shim_id){
		case GMPI_ISEND: 
			PMPI_Type_get_extent( p->MPI_Isend_p.datatype, &lb, &extent ); 
			MsgSz = p->MPI_Isend_p.count * extent;
			break;
		case GMPI_IRECV: 
			PMPI_Type_get_extent( p->MPI_Irecv_p.datatype, &lb, &extent );
			MsgSz = p->MPI_Irecv_p.count * extent;
			break;
		case GMPI_SEND:
			PMPI_Type_get_extent( p->MPI_Send_p.datatype, &lb, &extent );
			MsgSz = p->MPI_Send_p.count * extent;
			break;
		case GMPI_RECV:
			PMPI_Type_get_extent( p->MPI_Recv_p.datatype, &lb, &extent );
			MsgSz = p->MPI_Recv_p.count * extent;
			break;
		case GMPI_SSEND: 
			PMPI_Type_get_extent( p->MPI_Ssend_p.datatype, &lb, &extent ); 
			MsgSz = p->MPI_Ssend_p.count * extent;
			break;
		case GMPI_REDUCE:
			PMPI_Type_get_extent( p->MPI_Reduce_p.datatype, &lb, &extent ); 
			MsgSz = p->MPI_Reduce_p.count * extent;
			break;
		case GMPI_ALLREDUCE:
			PMPI_Type_get_extent( p->MPI_Allreduce_p.datatype, &lb, &extent ); 
			MsgSz = p->MPI_Allreduce_p.count * extent;
			break;
			
		default:
			MsgSz = -1;	// We don't have complete coverage, obviously.
	}
			
	// Write to the logfile.
	fprintf( logfile,
		var_format,
		rank,
		f2str(p->MPI_Dummy_p.shim_id),	
		current_hash,
		schedule[current_hash].observed_comp_seconds[0],
		schedule[current_hash].observed_comp_seconds[1],
		schedule[current_hash].observed_comp_seconds[2],
		schedule[current_hash].observed_comp_seconds[3],
		schedule[current_hash].observed_comp_seconds[4],
		schedule[current_hash].observed_comm_seconds,
		MsgSz
	);

#ifdef BLR_USE_EAGER_LOGGING
	fflush( logfile );
#endif
}


////////////////////////////////////////////////////////////////////////////////
// Interception redirect.
////////////////////////////////////////////////////////////////////////////////
void 
shim_pre( int shim_id, union shim_parameters *p ){
	// ft kludge.
	// In the normal case, any MPI call on or close to the critical path
	// will take less than GMPI_BLOCKING_BUFFER.  ft messages are so large,
	// though, that it takes ~10 seconds to complete an MPI_Alltoall on 32
	// nodes.  This makes it look like every process than slow down -- 
	// there's plenty of communiation time -- which inevitably leads to 
	// the process(es) on the critical path slowing down.
	//
	// If we had a reliable way to distinguish blocking time from 
	// communication time, we might be able to work with this.  However, 
	// I don't think that even a complex scheme will hold up over 32 
	// processes trying to communicate with 31 other processes, each
	// starting at a slightly different time.
	//
	// To fix the problem, we're going to stick a barrier in front of the
	// MPI_Alltoall call.  This will do two things:
	//
	// 1)  The communication time for the barrier is much less than 
	// GMPI_BLOCKING_BUFFER.  Any extra time spent here is real blocking
	// time, and computation can be slowed to take advantage of this.
	// Processes on the critical path will see very little communication
	// time and no blocking time, and thus their computation will not
	// be slowed.
	// 
	// 2)  The task preceding the MPI_Alltoall will take to little time
	// to be scheduled, leaving the fermata algorithm to kick in to 
	// pick up the energy savings.
	
	// DO NOT MOVE THIS CALL.  This code isn't particularly reentrant,
	// so this needs to be executed before any bookkeeping occurs.
	if( (g_algo  &  algo_ANDANTE || g_algo & algo_FERMATA) 
	&&  (g_algo  &  mods_BIGCOMM) 
	&&  (shim_id == GMPI_ALLTOALL)
	  ) { MPI_Barrier( MPI_COMM_WORLD ); }	//NOT PMPI.  
	
	// Kill the timer.
	set_alarm(0.0);

	// Bookkeeping.
	in_computation = 0;
	gettimeofday(&ts_stop_computation, NULL);  

	// Which call is this?
	current_hash=hash_backtrace(shim_id);
	
	// Function-specific intercept code.
	if(shim_id == GMPI_INIT){ pre_MPI_Init( p ); }
	if(shim_id == GMPI_FINALIZE){ pre_MPI_Finalize( p ); }
	
	// If we aren't initialized yet, stop here.
	if(!MPI_Initialized_Already){ return; }
	
	// Bookkeeping.
	current_comp_insn[current_freq]=stop_papi();
	
	// Write the schedule entry.  MUST COME BEFORE LOGGING.
	memcpy(	// Copy time accrued before we shifted into current freq.
		schedule[current_hash].observed_comp_seconds, 
		current_comp_seconds, 
		sizeof( double ) * NUM_FREQS);
	memset(
		current_comp_seconds,
		0,
		sizeof( double ) * NUM_FREQS);

	memcpy( // Copy insn accrued before we shifted into current freq.
		schedule[current_hash].observed_comp_insn,
		current_comp_insn,
		sizeof( double ) * NUM_FREQS);
	memset(
		current_comp_insn,
		0,
		sizeof( double ) * NUM_FREQS);

	schedule[current_hash].observed_comp_seconds[current_freq] = 
		delta_seconds(&ts_start_computation, &ts_stop_computation);

	// Schedule communication.
	gettimeofday(&ts_start_communication, NULL);  
	if( g_algo & algo_FERMATA ) { schedule_communication( current_hash ); }
}

void 
shim_post( int shim_id, union shim_parameters *p ){
	// If we aren't initialized yet, stop here.
	if(!MPI_Initialized_Already){ return; }
	
	// Kill the timer (may have been set by fermata algo).
	set_alarm(0.0);

	// Bookkeeping.
	gettimeofday(&ts_stop_communication, NULL);  
	//dump_timeval(logfile, "Communication halted. ", &ts_stop_communication);

	// (Most) Function-specific intercept code.
	if(shim_id == GMPI_INIT){ post_MPI_Init( p ); }
	
	schedule[current_hash].observed_comm_seconds = 
		delta_seconds(&ts_start_communication, &ts_stop_communication);
	if( previous_hash >= 0 ){
		schedule[previous_hash].following_entry = current_hash;
	}

	// Logging occurs as we're about to leave the task.
	if(g_trace){ Log( shim_id, p ); };
	
	// Bookkeeping.  MUST COME AFTER LOGGING.
	previous_hash = current_hash;
	gettimeofday(&ts_start_computation, NULL);  
	//dump_timeval(logfile, "COMPUTATION started.  ", &ts_start_computation);
	start_papi();	//Computation.

	// Setup computation schedule.
	if(g_algo & algo_ANDANTE){
		schedule_computation( schedule[current_hash].following_entry );
	}else{
		current_freq = FASTEST_FREQ;	// Default case.
	}
	// Regardless of computation scheduling algorithm, always shift here.
	// (Most of the time it should have no effect.)
	if( ! (g_algo & mods_FAKEFREQ) ) { shift( current_freq ); }

	// NOTE:  THIS HAS TO GO LAST.  Otherwise the logfile will be closed
	// prematurely.
	if(shim_id == GMPI_FINALIZE){ post_MPI_Finalize( p ); }
	in_computation = 1;
}

////////////////////////////////////////////////////////////////////////////////
// Signals
////////////////////////////////////////////////////////////////////////////////
static struct itimerval itv;
static void signal_handler(int signal);
static void initialize_handler(void);
static void
signal_handler(int signal){
	// Keep this really simple and stupid.  
	// When we catch a signal, shift(next_freq).
	// If we're in the computation phase, record time spent computing.
        signal = signal;
	//fprintf( logfile, "++> SIGNAL HANDLER\n");
	if(in_computation){
		gettimeofday(&ts_stop_computation, NULL);
		current_comp_seconds[current_freq] 
			= delta_seconds(&ts_start_computation, &ts_stop_computation);
		current_comp_insn[current_freq]=stop_papi();	//  <---|
	}							//  	|
								//	|
	if( ! (g_algo & mods_FAKEFREQ) ) { shift( next_freq ); }  //      |
	//fprintf(logfile, "==> Signal handler shifting to %d, in_computation=%d\n", 
	//		next_freq, in_computation);
	current_freq = next_freq;				//	|
	next_freq = 0;						//	|
								//	|
	if(in_computation){					//	|
		gettimeofday(&ts_start_computation, NULL);	//	|
		start_papi();					//  <---|
	}
}

static void
initialize_handler(void){
        sigset_t sig;
        struct sigaction act;
        sigemptyset(&sig);
        act.sa_handler = signal_handler;
        act.sa_flags = SA_RESTART;
        act.sa_mask = sig;
        sigaction(SIGALRM, &act, NULL);
}

static void
set_alarm(double s){
	itv.it_value.tv_sec  = (suseconds_t)s;
	itv.it_value.tv_usec = (suseconds_t)( (s-floor(s) )*1000000.0);
	setitimer(ITIMER_REAL, &itv, NULL);
}

////////////////////////////////////////////////////////////////////////////////
// Schedules
////////////////////////////////////////////////////////////////////////////////

static void
schedule_communication( int idx ){
	// If we have no data to work with, go home.
	if( idx==0 ){ return; }

	// If we haven't measured communication time yet, or there isn't 
	// enough to bother with, go home.
	if( schedule[ idx ].observed_comm_seconds < 2*GMPI_MIN_COMM_SECONDS ){
		return;
	}

	// Ok, so we have a chunk of communication time to work with.
	// Set the timer to go off and return.
	next_freq = SLOWEST_FREQ;
	set_alarm( GMPI_MIN_COMM_SECONDS );
}

static void
schedule_computation( int idx ){
	int i, first_freq=0; 
	double p=0.0, d=0.0, I=0.0, seconds_until_interrupt=0.0;

	// WARNING:  The actual shift to the value placed in current_freq
	// happens after this function returns.  The default case is to 
	// run at the highest frequency (0).  
	current_freq = FASTEST_FREQ;

	//fprintf( logfile, "==> schedule_computation called with idx=%d\n", idx);
	// If we have no data to work with, go home.
	if( idx==0 ){ return; }

	// On the first time through, establish worst-case slowdown rates.
	if( schedule[ idx ].seconds_per_insn[ FASTEST_FREQ ] == 0.0 ){
		//fprintf( logfile, "==> schedule_computation First time through.\n");
		if( schedule[ idx ].observed_comp_seconds[ FASTEST_FREQ ] <= GMPI_MIN_COMP_SECONDS ){
			//fprintf( logfile, "==> schedule_computation min_seconds violation.\n");
			return;
		}
		schedule[ idx ].seconds_per_insn[ FASTEST_FREQ ] = 
			schedule[ idx ].observed_comp_seconds[ FASTEST_FREQ ]/
			schedule[ idx ].observed_comp_insn[ FASTEST_FREQ ];
		for( i=FASTEST_FREQ+1; i<NUM_FREQS; i++ ){
			schedule[ idx ].seconds_per_insn[ i ] = 
				schedule[ idx ].seconds_per_insn[ FASTEST_FREQ ] *
				( frequency[ FASTEST_FREQ ]  / frequency[ i ] );
		}
		/*
		for( i=0; i<NUM_FREQS; i++ ){
			fprintf(logfile, "&&& SPI %d = %16.15lf\n", 
					i, schedule[idx].seconds_per_insn[ i ]);
		}
		*/
	}

	// On subsequent execution, only update where we have data.
	for( i=FASTEST_FREQ; i<NUM_FREQS; i++ ){
		if( schedule[ idx ].observed_comp_seconds[ i ] > GMPI_MIN_COMP_SECONDS ){
			schedule[ idx ].seconds_per_insn[ i ] = 
				schedule[ idx ].observed_comp_seconds[ i ]/
				schedule[ idx ].observed_comp_insn[ i ];
		}
		/*
		for( i=0; i<NUM_FREQS; i++ ){
			fprintf(logfile, "&&& SPI %d = %16.15lf\n", 
					i, schedule[idx].seconds_per_insn[ i ]);
		}
		*/
	}

	// Given I instructions to be executed over d time using available
	// rates r[], 
	//
	// pIr[i] + (1-p)Ir[i+1] = d
	//
	// thus
	//
	// p = ( d - Ir[i+1] )/( Ir[i] - Ir[i+1] ) 
	
	for( i=0; i<NUM_FREQS; i++ ){
		d += schedule[ idx ].observed_comp_seconds[ i ];
		I += schedule[ idx ].observed_comp_insn[ i ];
	}
	// If there's not enough computation to bother scaling, skip it.
	if( d <= GMPI_MIN_COMP_SECONDS ){
		//fprintf( logfile, "==> schedule_computation min_seconds total violation.\n");
		return;
	}

	// The deadline includes blocking time, minus a buffer.
	d += schedule[ idx ].observed_comm_seconds - GMPI_BLOCKING_BUFFER;

	// Create the schedule.
	
	// If the fastest frequency isn't fast enough, use f0 all the time.
	if( I * schedule[ idx ].seconds_per_insn[ FASTEST_FREQ ] >= d ){
		//fprintf( logfile, "==> schedule_computation GO FASTEST.\n");
		//fprintf( logfile, "==>     I=%lf\n", I);
		//fprintf( logfile, "==>   SPI=%lf\n",   
		//		schedule[ idx ].seconds_per_insn[0]);
		//fprintf( logfile, "==> I*SPI=%lf\n", 
		//		I*schedule[ idx ].seconds_per_insn[0]);
		//fprintf( logfile, "==>     d=%lf\n", d);
		current_freq = FASTEST_FREQ;
	}
	// If the slowest frequency isn't slow enough, use that.
	else if( I * schedule[ idx ].seconds_per_insn[ SLOWEST_FREQ ] <= d ){
		//fprintf( logfile, "==> schedule_computation GO SLOWEST.\n");
		//fprintf( logfile, "==>     I=%lf\n", I);
		//fprintf( logfile, "==>   SPI=%lf\n",   
		//		schedule[ idx ].seconds_per_insn[ SLOWEST_FREQ ]);
		//fprintf( logfile, "==> I*SPI=%lf\n", 
		//		I*schedule[ idx ].seconds_per_insn[ SLOWEST_FREQ ]);
		//fprintf( logfile, "==>     d=%lf\n", d);
		current_freq = SLOWEST_FREQ;
	}
	// Find the slowest frequency that allows the work to be completed in time.
	else{
		for( i=SLOWEST_FREQ-1; i>FASTEST_FREQ; i-- ){
			if( I * schedule[ idx ].seconds_per_insn[ i ] < d ){
				// We have a winner.
				p = 
					( d - I * schedule[ idx ].seconds_per_insn[ i+1 ] )
					/
					( I * schedule[ idx ].seconds_per_insn[ i ] - 
					  I * schedule[ idx ].seconds_per_insn[ i+1 ] );

				/*
				fprintf( logfile, "==> schedule_computation GO %d.\n", i);
				fprintf( logfile, "----> SPI[%d] = %15.14lf\n", 0, schedule[idx].seconds_per_insn[0]);
				fprintf( logfile, "----> SPI[%i] = %15.14lf\n", i, schedule[idx].seconds_per_insn[i]);
				fprintf( logfile, "---->       d = %lf\n", d);
				fprintf( logfile, "---->       I = %lf\n", I);
				fprintf( logfile, "---->       p = %lf\n", p);
				fprintf( logfile, "---->     idx = %d\n", idx);
				*/
				current_freq = i;

				// Do we need to shift down partway through?
				if((    p * I * schedule[idx].seconds_per_insn[i  ]>GMPI_MIN_COMP_SECONDS)
				&& (1.0-p * I * schedule[idx].seconds_per_insn[i+1]>GMPI_MIN_COMP_SECONDS)
				){
					seconds_until_interrupt = 
						p * I * schedule[ idx ].seconds_per_insn[ i ];
					next_freq = i+1;
					/*
					fprintf( logfile, "==> schedule_computation alarm=%15.14lf.\n", 
							seconds_until_interrupt);
					*/
					set_alarm(seconds_until_interrupt);
				}
				break;
			}
		}
	}
}

