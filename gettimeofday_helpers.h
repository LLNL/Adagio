#include <sys/time.h>	// struct timeval
#include <time.h>	// struct timeval

double delta_seconds(struct timeval *s, struct timeval *e);
void dump_timeval( FILE *f, char *str, struct timeval *t );
