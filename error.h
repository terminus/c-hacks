#ifndef __ERROR_H
#define __ERROR_H

/* Error/warning macros for the lazy.  If (like me) you
 * don't like to obscure your code with cases that handle
 * possible errors, then just wrap your code in these
 * macros.
 * 
 * The are decidedly uglier than not doing the error
 * handling at all, but cleaner in that you just specify
 * the error scenarios and this would handle the diagnostic
 * printing/exit etc internally.
 *
 * A nicer more verbose assert, if you will. No cleanup
 * unfotunately.
 */

/* cleanup function? inline? a jump? and exit of course. */
#define __common(cond, error, message, ...)				\
do {										\
	if ((cond)) {								\
		fprintf(stderr, "[%s:%ld] [%s:%s.%d] %s: " message "\n",	\
				 program_invocation_short_name,			\
				 syscall(SYS_gettid),				\
				 __FILE__, __func__,  __LINE__,			\
				 #cond, ## __VA_ARGS__);			\
		if (error)							\
			exit(error);						\
	}									\
} while(0)

/* Keep the names short. */
#define __w(cond, message, ...)						\
	__common((cond), 0, message, ## __VA_ARGS__);

#define __e_m(cond, message, ...)					\
	__common((cond), 1, message, ## __VA_ARGS__);

#define __e(cond, ...)							\
	__common((cond), 1, "", ## __VA_ARGS__);

#endif
