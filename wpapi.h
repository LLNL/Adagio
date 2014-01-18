#include <papi.h>

// High-level interface.  This does auto-initialize.
void start_papi(void);
double stop_papi(void);

// Low-level stuff.  Don't use.
static void initialize_papi();
static int wpapi_library_init(int version);
static int wpapi_create_eventset (int *eventset);
static int wpapi_add_events(int eventset, int *eventcodes, int number);
static int wpapi_add_event(int eventset, int eventcode);
static int wpapi_read(int EventSet, long_long *values);
static int wpapi_accum(int EventSet, long_long *values);
static int wpapi_start(int EventSet);
static int wpapi_stop(int EventSet, long_long *values);
static int wpapi_reset(int EventSet);

