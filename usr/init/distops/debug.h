#ifndef DISTOPS_DEBUG_H
#define DISTOPS_DEBUG_H

// #define DEBUG_INIT_DISTOPS

#ifdef DEBUG_INIT_DISTOPS
#define DEBUG_CAPOPS(x...) debug_printf(x)
#define DEBUG_PRINTCAP(x...) debug_print_capability(x)
#else
#define DEBUG_CAPOPS(x...)
#define DEBUG_PRINTCAP(x...)
#endif


#endif // DISTOPS_DEBUG_H
