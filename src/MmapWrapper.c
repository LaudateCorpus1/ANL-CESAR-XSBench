#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "numa.h"
#include "numaif.h"

#ifdef USE_ZHPE
#include "mmapUtils.h"
struct args  args = {
};
struct stuff conn = {
};

void * zhpeHack_mmap_alloc(size_t length) 
{
  int   ret;

  args.once_mode = true;
  args.threads = 1;
  args.mmap_len = page_up(length + page_size);

  if (((args.node=getenv("ZHPE_SERVER")) == NULL ) ||
      ((args.service=getenv("ZHPE_PORTID")) == NULL )) {
      printf("ERROR both of the environment variables \n");
      printf("ZHPE_SERVER and ZHPE_PORTID should be set\n");
      printf("to use Gen-Z memory\n");
      exit(5);
  }

  if ((ret = start_client(&args,&conn)) < 0) {
    printf("ERROR start_client returned %d\n", ret);
    ret = stop_client(&conn);
    exit(15-ret);
  }

  /* Invalidate any prefetched cache lines. */
  conn.ext_ops->commit(conn.mdesc, (char *)conn.mdesc->addr, args.mmap_len, true, true, false);

  return (char *)conn.mdesc->addr;
}

int    zhpeHack_munmap_free(void *buffer) 
{
  int   ret;

  /* Final handshake. */
  if ((ret = sock_send_blob(conn.sock_fd, NULL, 0)) < 0) {
    printf("WARNING final handshake returned %d\n", ret);
  }
  return stop_client(&conn);
}

#else
void * zhpeHack_mmap_alloc(size_t length) {return malloc(length);}
int    zhpeHack_munmap_free(void *buffer) {free(buffer); return 0;}
#endif

typedef unsigned long uint64_t;

/*
 * In the mmap phase, keep track of the buffer address and
 * the buffer length so that we can munmap properly.
 */
typedef struct{
  uint64_t  address;
  size_t    length;
  int       isFile;
  int       isGenZ;
} myMmappedItem;
#ifndef MAX_MAPPED_ITEMS
#define MAX_MAPPED_ITEMS 20
#endif
int           myNumberMmappedItems=0;
myMmappedItem myListMmappedItems[MAX_MAPPED_ITEMS];
int           myListMmappedFDs[MAX_MAPPED_ITEMS];

/*
 * For Gen-Z, we will map one large chunk and carve that
 * up for the required data structures.
 *   baseGenZBufferAddress: We need to keep track of the
 *                          buffer base address
 *   sizeGenZBuffer:        We need to keep track of the
 *                          buffer size
 *   nextGenZBufferAddress: We need to keep track of the
 *                          next free block in the buffer,
 *                          so new, non-overlapping, allocations
 *                          can be carved out of the buffer
 *   usedGenZBuffer:        We also track the amount of the
 *                          buffer being used so we can
 *                          deallocate the Gen-Z buffer when
 *                          the amount used reaches zero
 *                          during an munmap call
 * The implementation does not do any buffer recycling,
 * if a chunk of the buffer is requested to be unmapped,
 * then it is not reused.
 */
void   *baseGenZBufferAddress=NULL;
void   *nextGenZBufferAddress=NULL;
size_t sizeGenZBuffer=0;
size_t usedGenZBuffer;

/* 
 * The original version of XSBench is sloppy about memory 
 * cleanup. This is important for genz mmap'd memory, so
 * this function was added to clean up mmap'd memory.
 */
int munmap_wrapper_cleanup()
{
  void *memPtr;
  int i;
  int ret = -1;

  /*
   * Loop through the mmap'd items
   */
  for (i=0; i<myNumberMmappedItems; i++) {

    /*
     * Do something if the address is set. Any previous explicit
     * munmap call would have set the address and all other fields 
     * to zero.
     */
    if ( myListMmappedItems[i].address ) {

      memPtr = (void *) myListMmappedItems[i].address;

      if ( myListMmappedItems[i].isGenZ ) {
	/* Only decrement amount of GenZ buffer in use */
	usedGenZBuffer -= myListMmappedItems[i].length;
	printf( "munmapped %d item: cleanup genz-mmap at %lu with %lu bytes\n",
		i+1,
		myListMmappedItems[i].address,
		myListMmappedItems[i].length);
      } else if ( myListMmappedItems[i].isFile ) {
	/* Simple munmap and close the corresponding file */
	munmap(memPtr, myListMmappedItems[i].length);
	close(myListMmappedFDs[i]);
	printf( "munmapped %d item: cleanup file-mmap at %lu with %lu bytes\n",
		i+1,
		myListMmappedItems[i].address,
		myListMmappedItems[i].length);
      } else {
	/* Simple munmap only */
	munmap(memPtr, myListMmappedItems[i].length);
	printf( "munmapped %d item: cleanup node-mmap at %lu with %lu bytes\n",
		i+1,
		myListMmappedItems[i].address,
		myListMmappedItems[i].length);
      }

      /*
       * Discard this mapped item
       */
      myListMmappedItems[i].address = 0;
      myListMmappedItems[i].length = 0;
      myListMmappedItems[i].isFile = 0;
      myListMmappedItems[i].isGenZ = 0;

    }
  }

  /*
   * Special case for GenZ - munmap the full buffer
   */
  if ( sizeGenZBuffer > 0 ) {
#ifdef HPE_DBG
    printf("kludge zhpeHack_munmap called for %lu\n", (uint64_t) baseGenZBufferAddress);
#endif
    if ( usedGenZBuffer != 0 ) {
      /* This should be zero! */
      printf("kludge zhpeHack_munmap cleanup incomplete: usedGenZBuffer = %lu\n",
	     usedGenZBuffer );
    }
    ret = zhpeHack_munmap_free(baseGenZBufferAddress);
    return ret;
  } else {
    return 0;
  }
}

/*
 * Straightforward deallocation of memory
 */
int munmap_wrapper(
		   char *grid_type,
		   char *data_name,
		   void *memPtr )
{
  char *myGridIdEnvStr, *myDataNameEnvStr;
  uint64_t tmpAddress;

#ifdef HPE_DBG
  printf("munmap_wrapper called for %lu\n", (uint64_t) memPtr);
#endif

  tmpAddress = (uint64_t) memPtr;
  if (((myGridIdEnvStr=getenv(grid_type)) == NULL ) ||
      ((myDataNameEnvStr=getenv(data_name)) == NULL )) {
    /*
     * Data not mmap'd so call free
     */
    free( memPtr );
#ifdef HPE_DBG
    printf( "freed: %s %s at %lu\n",
	    grid_type, data_name, tmpAddress);
#endif
  } else {
    char *myMmapOperation, myTmpStr[256];
    int i;

    strcpy(myTmpStr, myDataNameEnvStr);
    myMmapOperation = strtok(myTmpStr,":");

    /*
     * Search for corresponding mmap'd item
     */
    for (i=0; i<myNumberMmappedItems; i++) {

      if ( tmpAddress == myListMmappedItems[i].address ){
	/* we have a match for this address */

	if (strcmp(myMmapOperation,"genz") == 0) {
	  /*
	   * Decrement amount of Gen-Z buffer in use by
	   * the size for the corresponding allocation.
	   */
	  usedGenZBuffer -= myListMmappedItems[i].length;

	  /*
	   * If Gen-Z buffer is now totally unused, unmap it.
	   */
	  if (usedGenZBuffer == 0) {
	    zhpeHack_munmap_free(baseGenZBufferAddress);
#ifdef HPE_DBG
	    printf("zhpeHack_munmap_free: %lu\n", (uint64_t) baseGenZBufferAddress);
#endif
	    baseGenZBufferAddress = NULL;
	    nextGenZBufferAddress = NULL;
	    sizeGenZBuffer = 0;
	  }
	} else {
	  /*
	   * Nothing special for other cases - just call unmap
	   */
	  munmap(memPtr, myListMmappedItems[i].length);
#ifdef HPE_DBG
	  printf("conventional munmap called for %lu\n", (uint64_t) memPtr);
#endif
	}

	/*
	 * If this was a file, close the file
	 */
	if ((strcmp(myMmapOperation,"open") == 0) ||
	    (strcmp(myMmapOperation,"create") == 0)) close(myListMmappedFDs[i]);

	/*
	 * Record what we did.
	 */
	printf( "munmapped %d item: %s %s at %lu with %lu bytes\n",
		i+1,
		grid_type, data_name,
		myListMmappedItems[i].address,
		myListMmappedItems[i].length);

	/*
	 * Discard this mapped item
	 */
	myListMmappedItems[i].address = 0;
	myListMmappedItems[i].length = 0;
	myListMmappedItems[i].isFile = 0;
	myListMmappedItems[i].isGenZ = 0;

      }
    }
  }
  return 0;
}

/*
 * Straightforward allocation of memory
 */
void * mmap_wrapper(
		    char *grid_type,
		    char *data_name,
		    size_t length )
{
  char *myGridIdEnvStr, *myDataNameEnvStr;
  void *tmpPtr;

  if (((myGridIdEnvStr=getenv(grid_type)) == NULL ) ||
      ((myDataNameEnvStr=getenv(data_name)) == NULL )) {
    /*
     * Data not mmap'd so call malloc
     */
    tmpPtr = malloc(length);
    printf( "malloced:");
  } else {
    char *myMmapOperation, myTmpStr[256];

    /*
     * We will need to keep a record of the mapping, make
     * sure we have enough space reserved.
     */
    if (myNumberMmappedItems == MAX_MAPPED_ITEMS) {
      printf("ERROR too many mapped items to proceed\n");
      printf("Increase MAX_MAPPED_ITEMS and recompile\n");
      exit(4);
    }

    strcpy(myTmpStr, myDataNameEnvStr);
    myMmapOperation = strtok(myTmpStr,":");

    /*
     * The aggregate cleanup function munmap_wrapper_cleanup()
     * will have no information about environment variables, so
     * we need to record whether the mmap'd item was a numa
     * node type [default], file type or GenZ type.
     */
    myListMmappedItems[myNumberMmappedItems].isFile = 0;
    myListMmappedItems[myNumberMmappedItems].isGenZ = 0;

    if ((strcmp(myMmapOperation,"node") == 0) || 
	(strcmp(myMmapOperation,"node_nothp") == 0)) {
      /*
       * String is of the form "node:1,2,3,5-7" listing
       * a group of nodes for binding the memory to.
       */
      char *myNodeStr;
      struct bitmask *nodes;

      /*
       * Do the mmap call and check it worked
       */
      tmpPtr = mmap(NULL, length, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      if ( tmpPtr == NULL ) {
	printf("mmap error for: %s %s\n", grid_type, data_name);
	exit(4);
      }

      /*
       * Extract nodes for binding and call mbind
       */
      myNodeStr = strtok(NULL,":");
      nodes = numa_allocate_nodemask();
      nodes = numa_parse_nodestring(myNodeStr);
      if (mbind(tmpPtr, length, MPOL_BIND,
		nodes->maskp, nodes->size, 0) < 0) {
	printf("mbind error for: %s %s\n", grid_type, data_name);
	exit(4);
      }
      if (strcmp(myMmapOperation,"node_nothp") == 0) {
	madvise(tmpPtr, length, MADV_NOHUGEPAGE );
	printf("....advising no transparent huge pages...\n");
      }
    } else if ((strcmp(myMmapOperation,"interleave") == 0) ||
	       (strcmp(myMmapOperation,"interleave_nothp") == 0)) {
      /*
       * String is of the form "interleave:1,2,3,5-7" listing
       * a group of nodes for interleaving the memory across.
       */
      char *myNodeStr;
      struct bitmask *nodes;

      /*
       * Do the mmap call and check it worked
       */
      tmpPtr = mmap(NULL, length, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
      if ( tmpPtr == NULL ) {
	printf("mmap error for: %s %s\n", grid_type, data_name);
	exit(4);
      }

      /*
       * Extract nodes for binding and call mbind
       */
      myNodeStr = strtok(NULL,":");
      nodes = numa_allocate_nodemask();
      nodes = numa_parse_nodestring(myNodeStr);
      if (mbind(tmpPtr, length, MPOL_INTERLEAVE,
		nodes->maskp, nodes->size, 0) < 0) {
	printf("mbind interleave error for: %s %s\n", grid_type, data_name);
	exit(4);
      }
      if (strcmp(myMmapOperation,"interleave_nothp") == 0) {
	madvise(tmpPtr, length, MADV_NOHUGEPAGE );
	printf("....advising no transparent huge pages...\n");
      }
    } else if ((strcmp(myMmapOperation,"open") == 0) ||
	       (strcmp(myMmapOperation,"create") == 0)) {
      /*
       * String is of the form "create:/path/to/file" or
       * "open:/path/to/file" to indicate that we should mmap
       * the corresponding file. The open option assumses that
       * it exists already.
       */
      char *myFileStr;
      int fd;

      /*
       * Get the file name and open or create the file and check
       * that it worked
       */
      myFileStr = strtok(NULL,":");
      if (strcmp(myMmapOperation,"open") == 0) {
	fd  = open(myFileStr, O_RDWR, 0666);
      } else {
	fd  = open(myFileStr, O_CREAT|O_RDWR, 0666);
      }
      if ( fd == -1 ) {
	printf("file %s open error for: %s %s\n",
	       myFileStr, grid_type, data_name);
	exit(4);
      }

      /*
       * If created, make it have the requested size
       */
      if (strcmp(myMmapOperation,"create") == 0) {
	int status;
	status = ftruncate(fd, length);
	if (status) {
	  printf("ftruncate error for: %s %s\n", grid_type, data_name);
	  exit(4);
	}
      }

      /*
       * Do the mmap call and check it worked
       */
      tmpPtr = mmap(NULL, length, PROT_READ|PROT_WRITE,
		    MAP_SHARED, fd, 0);
      if ( tmpPtr == NULL ) {
	printf("mmap error for: %s %s\n", grid_type, data_name);
	exit(4);
      }

      /*
       * Keep a record of the file descriptor so the
       * file can be closed during the unmap call and 
       * record this mmap'd item as a file type
       */
      myListMmappedFDs[myNumberMmappedItems] = fd;
      myListMmappedItems[myNumberMmappedItems].isFile = 1;

    } else if ((strcmp(myMmapOperation,"genz") == 0) ||
	       (strcmp(myMmapOperation,"genz_nothp") == 0)) {
      /*
       * String is of the form "genz:1234567" which specifies
       * the size of the genz block of memory that will be used
       */
      char *myMaxSizeStr;
      size_t myMaxSizeVal;

      /*
       * Get and parse the size string
       */
      myMaxSizeStr = strtok(NULL,":");
      if (!(sscanf(myMaxSizeStr, "%ld", &myMaxSizeVal))) {
	printf("Error reading Gen-Z size value\n");
	exit(4);
      }

      /*
       * If the first call, allocate the buffer and initialize
       * the navigation variables used to carve it up
       */
      if (baseGenZBufferAddress == NULL) {
	sizeGenZBuffer = myMaxSizeVal;
	baseGenZBufferAddress = zhpeHack_mmap_alloc(sizeGenZBuffer);
        printf("Allocated baseGenZBufferAddress %lu\n", (uint64_t) baseGenZBufferAddress);
	if (baseGenZBufferAddress == NULL) {
	  printf("Error allocating Gen-Z memory\n");
	  exit(4);
	}
	nextGenZBufferAddress = baseGenZBufferAddress;
	usedGenZBuffer = 0;
	if (strcmp(myMmapOperation,"genz_nothp") == 0) {
	  madvise(baseGenZBufferAddress, sizeGenZBuffer, MADV_NOHUGEPAGE );
	  printf("....advising no transparent huge pages...\n");
	}
      }

      /*
       * Chech that the max buffer size is consistent
       */
      if (sizeGenZBuffer != myMaxSizeVal) {
	printf("mmap error for: %s %s\n", grid_type, data_name);
	printf("max Gen-Z buffer size mismatch: requested %ld but have %ld\n",
	       myMaxSizeVal, sizeGenZBuffer);
	exit(4);
      }

      /*
       * Return the next free chunk of the buffer and update
       * the navigation variables
       */
      tmpPtr = nextGenZBufferAddress;
      nextGenZBufferAddress += length;
      usedGenZBuffer += length;

      /*
       * Record this as a GenZ mmap'd item
       */
      myListMmappedItems[myNumberMmappedItems].isGenZ = 1;
    } else {
      printf("ERROR %s method for: %s %s\n",
	     myMmapOperation, grid_type, data_name);
      printf("Method not recognized\n");
      exit(4);
    }

    /*
     * Record that we mmap'd an item and save a record of
     * the mmap'ing used
     */
    printf( "mmapped %d item:", myNumberMmappedItems+1);
    myListMmappedItems[myNumberMmappedItems].address = (uint64_t) tmpPtr;
    myListMmappedItems[myNumberMmappedItems].length = length;
    myNumberMmappedItems++;
  }

  printf( " %s %s at %lu with %lu bytes\n",
	  grid_type, data_name, (uint64_t) tmpPtr, length);
  return tmpPtr;
}
