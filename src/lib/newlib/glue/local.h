#ifndef ERA_NEWLIB_STRING_LOCAL_H
#define ERA_NEWLIB_STRING_LOCAL_H

/* The selected upstream files need only this internal newlib macro, not
 * local.h's unrelated wide-character/locale declarations. Keep the sources
 * unchanged and inhibit recursive library-call recognition for these loops. */
#define __inhibit_loop_to_libcall \
  __attribute__((__optimize__("-fno-tree-loop-distribute-patterns")))

#endif
