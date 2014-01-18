#include "stacktrace.h"
#include "md5.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>
int
hash_backtrace(int fid) {
	unw_cursor_t cursor; unw_context_t uc;
	unw_word_t ip, sp;

	md5_state_t pms;
	md5_byte_t digest[16];
	md5_init(   &pms );

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);
	while (unw_step(&cursor) > 0) {
		unw_get_reg(&cursor, UNW_REG_IP, &ip);
		unw_get_reg(&cursor, UNW_REG_SP, &sp);
		md5_append( &pms, (md5_byte_t *)(&ip), sizeof(unw_word_t) );
		md5_append( &pms, (md5_byte_t *)(&sp), sizeof(unw_word_t) );
	}
	md5_append( &pms, (md5_byte_t*)(&fid), sizeof(int) );
	md5_finish( &pms, digest );
	return *((int*)digest) & 0x1fff; //8192 entries.
}
