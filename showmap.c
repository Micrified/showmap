#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <inttypes.h>
#include "pmap.h"

#define DIV					"*****************************************************"\
							"***************************"

#define PERM2PROT(p)    	(((p)[0] == 'r') * PROT_READ | ((p)[1] == 'w') * PROT_WRITE | ((p)[2] == 'x') * PROT_EXEC)

#define CONTAINS(lb,ub,v)	(((v) >= (lb) && (v) < (ub)))

#define FAULT_FORMAT		"--- Segmentation Fault ---\n"\
							" inst: %p\n"\
							" addr: %p\n"\
							"--------------------------\n"

/*
 *******************************************************************************
 *                              Global Variables                               *
 *******************************************************************************
*/

// Heap Variable.
int v;

// The page size.
int pagesize;

// Pointer to allocated page.
void *page;

/*
 *******************************************************************************
 *                                 Prototypes                                  *
 *******************************************************************************
*/

// Return program counter. Note: Doesn't work in signal handlers how you want.
void *get_pc ();

// Wrapper for mprotect. Exists with error on failure.
void safeProtect (void *addr, size_t size, int perms);

// Finds permissions for page containing addr. Returns as a string [rwxt].
const char *permissionsAt (void *addr);

// Protects all pages in the address space of: addr + size.
void protect (void *addr, size_t size, int perms);

/*
 *******************************************************************************
 *                           Utility Routines                                  *
 *******************************************************************************
*/

// Return the current instruction
void *get_pc () { 
	return __builtin_return_address(0); 
}

// Wrapper for mprotect. Exists with error on failure.
void safeProtect (void *addr, size_t size, int perms) {
	if (mprotect(addr, size, perms) == -1) {
		fprintf(stderr, "Error: mprotect(%p, %zu, %d) failed. Reason: %s\n", 
			addr, size, perms, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

// Configures a signal handler, then returns it's pointer. Use for ONE signal.
void setHandler (int signal, void (*f)(int, siginfo_t *, void *)) {
	static struct sigaction handler;		
	handler.sa_flags = SA_SIGINFO;			// Set to use signal-handler.
	sigemptyset(&handler.sa_mask);			// Block no signals during handler.
	handler.sa_sigaction = f;				// Assign handler function.

	// Specify signal action: Verify success.
	if (sigaction(signal, &handler, NULL) == -1) {
		fprintf(stderr, "Error: sigaction(%d, %p, NULL) failed: %s\n", 
			signal, &handler, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/*
 *******************************************************************************
 *                               Signal Handlers                               *
 *******************************************************************************
*/

// Handler: Segmentation Fault.
void handler (int signal, siginfo_t *info, void *ucontext) {
	printf(FAULT_FORMAT, info->si_ptr, info->si_addr);

	ucontext_t *context = (ucontext_t *)ucontext;
	printf("Instruction: %p\n", (void *)context->uc_mcontext.gregs[REG_RIP]);

	printf("Okay, giving write permissions back...\n");
	protect(info->si_addr, sizeof(int), PROT_WRITE);
}

/*
 *******************************************************************************
 *                                  Routines                                   *
 *******************************************************************************
*/

// Finds permissions for page containing addr. Returns as a string [rwxt].
const char *permissionsAt (void *addr) {
	int v;
	ProcMap map;

	// Open process memory map.
	if (openProcMaps(getpid()) == 0) {
		exit(EXIT_FAILURE);
	}

	// Locate region containing addr.
	while ((v = parseNext(&map)) == 1) {
		if (CONTAINS(map.startAddress, map.endAddress, addr)) {
			break;
		}
	}
	printf("%s\n", DIV);
	dumpProcMaps();
	if (v != 0) {
		printf("%s\n", DIV);
		printf("Found: %p\n", addr);
		printf(FMT_PRNT, map.startAddress, map.endAddress, map.perms, map.filePath);
		printf("%s\n", DIV);
	}

	// If not found, return NULL;
	closeProcMaps();
	return (v == 0) ? NULL : map.perms;
}

// Protects all pages in the address space of: addr + size.
void protect (void *addr, size_t size, int perms) {
	const char *oldPerms;

	// Compute the offset of the address.
	size_t offset = (uintptr_t)addr % (uintptr_t)pagesize;

	// Compute starting address of the page.
	void *start = addr - offset;

	// Obtain current permissions.
	if ((oldPerms = permissionsAt(addr)) == NULL) {
		fprintf(stderr, "protect: Can't find old permissions. Aborting...\n");
		return;
	}

	// Apply new protections: Start -> offset + size - 1.
	safeProtect(start, offset + size, perms);
	printf("Applied \"perms\" to: %p → %p\n", start, start + offset + size - 1);

	// Check permissions.
	printf("Checking: %p\n", addr);
	permissionsAt(addr);
}

int main (void) {

	// Set page size.
	pagesize = sysconf(_SC_PAGE_SIZE);

	// Configure a handler for SIGSEGV.
	setHandler(SIGSEGV, handler);

	// Allocate aligned page.
	if (posix_memalign(&page, pagesize, pagesize) != 0) {
		fprintf(stderr, "Error: Couldn't allocated aligned page: %s\n", 
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Wipe memory page.
	memset(page, 0, pagesize);

	// Protect memory page.
	protect(page, pagesize, PROT_READ);

	// Violation Test:
	int *p = (int *)page;
	goto routine;
	routine: ;
		*p = 5;

	printf("p = %d\n", *p);

	// Free allocated memory.
	free(page);
	return 0;
}