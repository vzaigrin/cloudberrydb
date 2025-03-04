/*-------------------------------------------------------------------------
 *
 * memutils.h
 *	  This file contains declarations for memory allocation utility
 *	  functions.  These are functions that are not quite widely used
 *	  enough to justify going in utils/palloc.h, but are still part
 *	  of the API of the memory management subsystem.
 *
 *
 * Portions Copyright (c) 2007-2008, Greenplum inc
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/memutils.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MEMUTILS_H
#define MEMUTILS_H

#include "nodes/memnodes.h"


/*
 * MaxAllocSize, MaxAllocHugeSize
 *		Quasi-arbitrary limits on size of allocations.
 *
 * Note:
 *		There is no guarantee that smaller allocations will succeed, but
 *		larger requests will be summarily denied.
 *
 * palloc() enforces MaxAllocSize, chosen to correspond to the limiting size
 * of varlena objects under TOAST.  See VARSIZE_4B() and related macros in
 * postgres.h.  Many datatypes assume that any allocatable size can be
 * represented in a varlena header.  This limit also permits a caller to use
 * an "int" variable for an index into or length of an allocation.  Callers
 * careful to avoid these hazards can access the higher limit with
 * MemoryContextAllocHuge().  Both limits permit code to assume that it may
 * compute twice an allocation's size without overflow.
 */
#define MaxAllocSize	((Size) 0x3fffffff) /* 1 gigabyte - 1 */

#define AllocSizeIsValid(size)	((Size) (size) <= MaxAllocSize)

#define MaxAllocHugeSize	(SIZE_MAX / 2)

#define AllocHugeSizeIsValid(size)	((Size) (size) <= MaxAllocHugeSize)

/*
 * All chunks allocated by any memory context manager are required to be
 * preceded by a StandardChunkHeader at a spacing of STANDARDCHUNKHEADERSIZE.
 * A currently-allocated chunk must contain a backpointer to its owning
 * context as well as the allocated size of the chunk.  The backpointer is
 * used by pfree() and repalloc() to find the context to call.  The allocated
 * size is not absolutely essential, but it's expected to be needed by any
 * reasonable implementation.
 */
typedef struct StandardChunkHeader
{
	MemoryContext context;		/* owning context */
	Size		size;			/* size of data space allocated in chunk */
#ifdef MEMORY_CONTEXT_CHECKING
	/* when debugging memory usage, also store actual requested size */
	Size		requested_size;
#endif

#ifdef CDB_PALLOC_TAGS
	const char  *alloc_tag;
	int 		alloc_n;
	void *prev_chunk;
	void *next_chunk;
#endif
} StandardChunkHeader;

#define STANDARDCHUNKHEADERSIZE  MAXALIGN(sizeof(StandardChunkHeader))


/*
 * Standard top-level memory contexts.
 *
 * Only TopMemoryContext and ErrorContext are initialized by
 * MemoryContextInit() itself.
 */
extern PGDLLIMPORT MemoryContext TopMemoryContext;
extern PGDLLIMPORT MemoryContext ErrorContext;
extern PGDLLIMPORT MemoryContext PostmasterContext;
extern PGDLLIMPORT MemoryContext CacheMemoryContext;
extern PGDLLIMPORT MemoryContext MessageContext;
extern PGDLLIMPORT MemoryContext TopTransactionContext;
extern PGDLLIMPORT MemoryContext CurTransactionContext;
extern PGDLLIMPORT MemoryContext DispatcherContext;
extern PGDLLIMPORT MemoryContext InterconnectContext;
extern PGDLLIMPORT MemoryContext OptimizerMemoryContext;

/* This is a transient link to the active portal's memory context: */
extern PGDLLIMPORT MemoryContext PortalContext;

/* Backwards compatibility macro */
#define MemoryContextResetAndDeleteChildren(ctx) MemoryContextReset(ctx)


/*
 * Memory-context-type-independent functions in mcxt.c
 */
extern void MemoryContextInit(void);
extern void MemoryContextReset(MemoryContext context);
extern void MemoryContextResetOnly(MemoryContext context);
extern void MemoryContextResetChildren(MemoryContext context);
extern void MemoryContextDeleteChildren(MemoryContext context);
extern void MemoryContextSetIdentifier(MemoryContext context, const char *id);
extern void MemoryContextSetParent(MemoryContext context,
								   MemoryContext new_parent);
extern Size GetMemoryChunkSpace(void *pointer);
extern MemoryContext MemoryContextGetParent(MemoryContext context);
extern bool MemoryContextIsEmpty(MemoryContext context);

/* Statistics */
extern void MemoryContextDeclareAccountingRoot(MemoryContext context);
extern Size MemoryContextGetCurrentSpace(MemoryContext context);
extern Size MemoryContextGetPeakSpace(MemoryContext context);
extern Size MemoryContextSetPeakSpace(MemoryContext context, Size nbytes);

#define MemoryContextDelete(context)    (MemoryContextDeleteImpl(context, __FILE__, PG_FUNCNAME_MACRO, __LINE__))
extern void MemoryContextDeleteImpl(MemoryContext context, const char* sfile, const char *func, int sline);

extern Size MemoryContextMemAllocated(MemoryContext context, bool recurse);
extern void MemoryContextStats(MemoryContext context);
extern void MemoryContextStatsDetail(MemoryContext context, int max_children,
									 bool print_to_stderr);
extern void MemoryContextAllowInCriticalSection(MemoryContext context,
												bool allow);

#ifdef MEMORY_CONTEXT_CHECKING
extern void MemoryContextCheck(MemoryContext context);
#endif
extern bool MemoryContextContains(MemoryContext context, void *pointer);
extern bool MemoryContextContainsGenericAllocation(MemoryContext context, void *pointer);

extern void MemoryContextError(int errorcode, MemoryContext context,
                               const char *sfile, int sline,
                               const char *fmt, ...)
                              pg_attribute_noreturn()
                              pg_attribute_printf(5, 6);

/* Handy macro for copying and assigning context ID ... but note double eval */
#define MemoryContextCopyAndSetIdentifier(cxt, id) \
	MemoryContextSetIdentifier(cxt, MemoryContextStrdup(cxt, id))

/*
 * GetMemoryChunkContext
 *		Given a currently-allocated chunk, determine the context
 *		it belongs to.
 *
 * All chunks allocated by any memory context manager are required to be
 * preceded by the corresponding MemoryContext stored, without padding, in the
 * preceding sizeof(void*) bytes.  A currently-allocated chunk must contain a
 * backpointer to its owning context.  The backpointer is used by pfree() and
 * repalloc() to find the context to call.
 */
#ifndef FRONTEND
static inline MemoryContext
GetMemoryChunkContext(void *pointer)
{
	MemoryContext context;

	/*
	 * Try to detect bogus pointers handed to us, poorly though we can.
	 * Presumably, a pointer that isn't MAXALIGNED isn't pointing at an
	 * allocated chunk.
	 */
	Assert(pointer != NULL);
	Assert(pointer == (void *) MAXALIGN(pointer));

	/*
	 * OK, it's probably safe to look at the context.
	 */
	context = *(MemoryContext *) (((char *) pointer) - sizeof(void *));

	AssertArg(MemoryContextIsValid(context));

	return context;
}
#endif

/*
 * This routine handles the context-type-independent part of memory
 * context creation.  It's intended to be called from context-type-
 * specific creation routines, and noplace else.
 */
extern void MemoryContextCreate(MemoryContext node,
								NodeTag tag,
								const MemoryContextMethods *methods,
								MemoryContext parent,
								const char *name);

extern void HandleLogMemoryContextInterrupt(void);
extern void ProcessLogMemoryContextInterrupt(void);

/*
 * Memory-context-type-specific functions
 */

/* aset.c */
extern MemoryContext AllocSetContextCreateInternal(MemoryContext parent,
												   const char *name,
												   Size minContextSize,
												   Size initBlockSize,
												   Size maxBlockSize);

/*
 * This wrapper macro exists to check for non-constant strings used as context
 * names; that's no longer supported.  (Use MemoryContextSetIdentifier if you
 * want to provide a variable identifier.)
 */
#ifdef HAVE__BUILTIN_CONSTANT_P
#define AllocSetContextCreate(parent, name, ...) \
	(StaticAssertExpr(__builtin_constant_p(name), \
					  "memory context names must be constant strings"), \
	 AllocSetContextCreateInternal(parent, name, __VA_ARGS__))
#else
#define AllocSetContextCreate \
	AllocSetContextCreateInternal
#endif

extern bool AllocSetContains(MemoryContext context, void *pointer);

/* slab.c */
extern MemoryContext SlabContextCreate(MemoryContext parent,
									   const char *name,
									   Size blockSize,
									   Size chunkSize);

/* generation.c */
extern MemoryContext GenerationContextCreate(MemoryContext parent,
											 const char *name,
											 Size blockSize);

/* this function should be only called by MemoryContextSetParent() */
extern void AllocSetTransferAccounting(MemoryContext context,
									   MemoryContext new_parent);

/*
 * Recommended default alloc parameters, suitable for "ordinary" contexts
 * that might hold quite a lot of data.
 */
#define ALLOCSET_DEFAULT_MINSIZE   0
#define ALLOCSET_DEFAULT_INITSIZE  (8 * 1024)
#define ALLOCSET_DEFAULT_MAXSIZE   (8 * 1024 * 1024)
#define ALLOCSET_DEFAULT_SIZES \
	ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

/*
 * Recommended alloc parameters for "small" contexts that are never expected
 * to contain much data (for example, a context to contain a query plan).
 */
#define ALLOCSET_SMALL_MINSIZE	 0
#define ALLOCSET_SMALL_INITSIZE  (1 * 1024)
#define ALLOCSET_SMALL_MAXSIZE	 (8 * 1024)
#define ALLOCSET_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_SMALL_MAXSIZE

/*
 * Recommended alloc parameters for contexts that should start out small,
 * but might sometimes grow big.
 */
#define ALLOCSET_START_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE


/*
 * Threshold above which a request in an AllocSet context is certain to be
 * allocated separately (and thereby have constant allocation overhead).
 * Few callers should be interested in this, but tuplesort/tuplestore need
 * to know it.
 */
#define ALLOCSET_SEPARATE_THRESHOLD  8192

#define SLAB_DEFAULT_BLOCK_SIZE		(8 * 1024)
#define SLAB_LARGE_BLOCK_SIZE		(8 * 1024 * 1024)

#endif							/* MEMUTILS_H */
