#===============================================================================
# User Options
#===============================================================================

COMPILER    = gnu
OPTIMIZE    = yes
DEBUG       = no
PROFILE     = no
MPI         = no
PAPI        = no
VEC_INFO    = no
VERIFY      = no
BENCHMARK   = no
BINARY_DUMP = no
BINARY_READ = no

include Makefile.hpe

#===============================================================================
# Program name & source code list
#===============================================================================

program = XSBench

source = \
Main.c \
io.c \
CalculateXS.c \
GridInit.c \
XSutils.c \
Materials.c

headers = \
XSbench_header.h

ifeq ($(HPE_MMAP),yes)
  source += \
  MmapWrapper.c
  headers += \
  MmapWrapper.h
  ifeq ($(ZHPE),yes)
    source += \
    ServerApp/mmapUtils.c
    headers += \
    ServerApp/mmapUtils.h
  endif
endif

obj = $(source:.c=.o)

#===============================================================================
# Sets Flags
#===============================================================================

# Standard Flags
#CFLAGS := -std=gnu99 -Wall
CFLAGS := -Wall

# Linker Flags
LDFLAGS = -lm

LDFLAGS += -lnuma

# Regular gcc Compiler
ifeq ($(COMPILER),gnu)
  CC = gcc
  CFLAGS += -fopenmp -flto
endif

# Intel Compiler
ifeq ($(COMPILER),intel)
  CC = icc
  CFLAGS += -qopenmp 
endif

# BG/Q gcc Cross-Compiler
ifeq ($(MACHINE),bluegene)
  CC = mpicc
endif

# Debug Flags
ifeq ($(DEBUG),yes)
  CFLAGS += -g
  LDFLAGS  += -g
endif

# Profiling Flags
ifeq ($(PROFILE),yes)
  CFLAGS += -pg
  LDFLAGS  += -pg
endif

# Optimization Flags
ifeq ($(OPTIMIZE),yes)
  CFLAGS += -O3
endif

# Compiler Vectorization (needs -O3 flag) information
ifeq ($(VEC_INFO),yes)
  CFLAGS += -ftree-vectorizer-verbose=6
endif

# PAPI source (you may need to provide -I and -L pointing
# to PAPI depending on your installation
ifeq ($(PAPI),yes)
  source += papi.c
  CFLAGS += -DPAPI
  #CFLAGS += -I/soft/apps/packages/papi/papi-5.1.1/include
  #LDFLAGS += -L/soft/apps/packages/papi/papi-5.1.1/lib -lpapi
  LDFLAGS += -lpapi
endif

# MPI
ifeq ($(MPI),yes)
  CC = mpicc
  CFLAGS += -DMPI
endif

# libzhpe_mmap
ifeq ($(HPE_MMAP),yes)
  CFLAGS += -DHPE_MMAP
  ifeq ($(HPE_DBG),yes)
    CFLAGS += -DHPE_DBG
  endif
  ifeq ($(ZHPE),yes)
    CFLAGS += -DUSE_ZHPE
    CFLAGS += -I$(ZHPE_PREFIX)/include
    CFLAGS += -I./ServerApp
    LDFLAGS += -L$(ZHPE_PREFIX)/lib
    LDFLAGS += -lzhpeq_util
    LDFLAGS += -lzhpe_mmap
    LDFLAGS += -lfabric
    LDFLAGS += -lzhpeq_util_fab
    LDFLAGS += -lzhpeq
    LDFLAGS += -lzhpe_stats
  endif
endif



# Verification of results mode
ifeq ($(VERIFY),yes)
  CFLAGS += -DVERIFICATION
endif

# Adds outer 'benchmarking' loop to do multiple trials for
# 1 < threads <= max_threads
ifeq ($(BENCHMARK),yes)
  CFLAGS += -DBENCHMARK
endif

# Binary dump for file I/O based initialization
ifeq ($(BINARY_DUMP),yes)
  CFLAGS += -DBINARY_DUMP
endif

# Binary read for file I/O based initialization
ifeq ($(BINARY_READ),yes)
  CFLAGS += -DBINARY_READ
endif


#===============================================================================
# Targets to Build
#===============================================================================

$(program): $(obj) $(headers) Makefile Makefile.hpe
	$(CC) $(CFLAGS) $(obj) -o $@ $(LDFLAGS)

%.o: %.c $(headers) Makefile Makefile.hpe
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(program) $(obj)

edit:
	vim -p $(source) papi.c $(headers)

run:
	./$(program)

bgqrun:
	qsub -t 10 -n 1 -O test XSBench
