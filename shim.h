/* shim.h
 *
 * The two global functions here, shim_pre() and shim_post, are called from the
 * shim_functions file when an MPI call is intercepted.
 */

#include "shim_parameters.h"
void shim_pre( int shim_id, union shim_parameters *p );
void shim_post( int shim_id, union shim_parameters *p );
char* f2str( int shim_id );
void Log( int shim_id, union shim_parameters *p );



// Schedule entry
enum{ NUM_FREQ=5,  NUM_FREQS=5,__SLOWEST_FREQ=4, __FASTEST_FREQ=0 };
//enum{ NUM_FREQ=4, NUM_FREQS=4, SLOWEST_FREQ=3 };
struct entry{
	double observed_comm_seconds;
	double observed_comp_seconds[NUM_FREQS];
	double observed_comp_insn[NUM_FREQS];
	double seconds_per_insn[NUM_FREQS];
	int following_entry;
};


// MPI_Init
static void pre_MPI_Init 	( union shim_parameters *p );
static void post_MPI_Init	( union shim_parameters *p );
// MPI_Finalize
static void pre_MPI_Finalize 	( union shim_parameters *p );
static void post_MPI_Finalize	( union shim_parameters *p );

// Scheduling
static void schedule_communication	( int idx );
static void schedule_computation  	( int idx );
static void initialize_handler    	(void);
static void signal_handler        	( int signal);
static void set_alarm			( double s );
