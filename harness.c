#include <unistd.h>	// usleep
#include <stdlib.h>	// exit, malloc
#include <getopt.h>	// getopt_long
#include <stdio.h>	// fprintf
#include <string.h>	// memset
#include <assert.h>
#include <mpi.h>

static int g_rank, g_size;

volatile int dummy_var;
void
spin( int n ){
	int h, i, j, k;
	for( h=0; h<n; h++ ){
		for( i=0; i<500; i++){
			for(j=0; j<500; j++){
				for(k=0; k<1000; k++){
					dummy_var += h*i*j*k;
				}
			}
		}
	}
}



struct harness_options{
	int test_hash;
	int test_rank;
	int test_ping;
	int test_spin;
};

static void
print_version(){
	if( g_rank == 0 ){
		fprintf(stdout,
			"=== GreenMPI test harness v0.0.0 built %s %s. ===\n",
			__DATE__, __TIME__);
	}
}

static void
print_help(){
	if( g_rank == 0 ){
		fprintf(stdout, "\t--help      -h  Print this message.                \n");
		fprintf(stdout, "\t--test_rank -r  Test:  Print rank, size, then exit.\n");
		fprintf(stdout, "\t--test_sane -s  Test:  Sanity check (scratchpad).  \n");
		fprintf(stdout, "\t--version   -v  Print version information.         \n");
	}
}

static void
parse_options( int argc, char **argv, struct harness_options *o ){
	
	int c = 0;
	int option_index = 0;
	static struct option long_options[] = {
		//name  	hasarg 	flag 	val
		{"help", 	0, 	NULL, 	'h'},
		{"test_hash",	0,	NULL,	'H'},
		{"test_ping",	0,	NULL,	'p'},
		{"test_rank", 	0, 	NULL, 	'r'},
		{"test_spin",	0, 	NULL,	'S'},
		{"test_sane",  	0,	NULL,	's'},
		{"version", 	0, 	NULL, 	'v'},
		{0, 		0, 	0, 	0}
	};

	while (1) {
		c = getopt_long(argc, argv, "HhprSsv", long_options, &option_index);
		if( c==-1 ){ break; }

		switch( c ){
			case 'H':	o->test_hash = 1;	break;
			case 'h':	print_help();		break;
			case 'p':	o->test_ping = 1;	break;
			case 'r':	o->test_rank = 1; 	break;
			case 'v':	print_version();	break;
			case 'S':	o->test_spin = 1; 	break;
			case 's':				break;
			default : 	fprintf(stderr, 
					"Unknown options '%c'.  Bye!\n", 
					(char)c );
					exit(-1);
					break;
		}
	}
}

static int
test_spin(){
	int i;
	MPI_Barrier( MPI_COMM_WORLD );
	MPI_Barrier( MPI_COMM_WORLD );
	MPI_Barrier( MPI_COMM_WORLD );
	MPI_Pcontrol( 7 );
	for( i=0; i<4; i++ ){
		if( g_rank==0 ){
			spin(11);
		}else{
			spin(7);
		}
		MPI_Barrier( MPI_COMM_WORLD );
	}
	return 0;
}


static int
test_rank(){
	fprintf( stdout, "node %02d of %02d.\n", g_rank, g_size);
	return  0;
}

static int
test_ping(){
#define PP_BUFSZ (1024*1024*2)
	int s, r, reps=1, i;
	char *buf=malloc(PP_BUFSZ);
	MPI_Request* req = malloc( sizeof(MPI_Request) * g_size );
	MPI_Status*  sta = malloc( sizeof(MPI_Status) * g_size );
	assert(buf);
	assert(req);
	assert(sta);
	for(r=0; r<reps; r++){
		for(s=1; s<PP_BUFSZ; s=s*2){
			if(g_rank==0){
				for(i=1; i<g_size; i++){
					MPI_Irecv( buf, s, MPI_CHAR, i, 0xFF+r, MPI_COMM_WORLD, &req[i] );
					fprintf(stderr, "r=%d s=%d i=%d\n", r, s, i);
				}
				MPI_Waitall( g_size-1, &req[1], &sta[1] );
			}else{
				usleep(100000);
				MPI_Isend( buf, s, MPI_CHAR, 0, 0xFF+r, MPI_COMM_WORLD, &req[g_rank] );
				MPI_Wait( &req[g_rank], &sta[g_rank] );
			}
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}
	return 0;
#undef PP_BUFSZ
}

static void test_hash00(){	MPI_Barrier(MPI_COMM_WORLD); };
static void test_hash01(){	MPI_Barrier(MPI_COMM_WORLD); test_hash00(); };
static void test_hash02(){	MPI_Barrier(MPI_COMM_WORLD); test_hash01(); };
static void test_hash03(){	MPI_Barrier(MPI_COMM_WORLD); test_hash02(); };
static void test_hash04(){	MPI_Barrier(MPI_COMM_WORLD); test_hash03(); };
static void test_hash05(){	MPI_Barrier(MPI_COMM_WORLD); test_hash04(); };
static void test_hash06(){	MPI_Barrier(MPI_COMM_WORLD); test_hash05(); };
static void test_hash07(){	MPI_Barrier(MPI_COMM_WORLD); test_hash06(); };
static void test_hash08(){	MPI_Barrier(MPI_COMM_WORLD); test_hash07(); };
static void test_hash09(){	MPI_Barrier(MPI_COMM_WORLD); test_hash08(); };
static void test_hash0a(){	MPI_Barrier(MPI_COMM_WORLD); test_hash09(); };
static void test_hash0b(){	MPI_Barrier(MPI_COMM_WORLD); test_hash0a(); };

static int 
test_hash(){
	test_hash00(); test_hash01(); test_hash02(); test_hash03();
	test_hash04(); test_hash05(); test_hash06(); test_hash07();
	test_hash08(); test_hash09(); test_hash0a(); test_hash0b();
	return 0;
}

int
main( int argc, char **argv ){

	struct harness_options o;
	memset( &o, 0, sizeof(struct harness_options) );

	assert( MPI_SUCCESS == MPI_Init( &argc, &argv ) );
	assert( MPI_SUCCESS == MPI_Comm_rank( MPI_COMM_WORLD, &g_rank ) );
	assert( MPI_SUCCESS == MPI_Comm_size( MPI_COMM_WORLD, &g_size ) );

	parse_options( argc, argv, &o );

	// Various tests (return zero on success).
	assert( !o.test_rank || !test_rank() );
	assert( !o.test_ping || !test_ping() );
	assert( !o.test_hash || !test_hash() );
	assert( !o.test_spin || !test_spin() );

	assert( MPI_SUCCESS == MPI_Finalize() );
	return 0;
}

