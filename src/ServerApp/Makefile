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
ZHPE        = yes
ZHPE_PREFIX = /shared/${ME}/${TESTDIR}/bld

#===============================================================================
# Program name & source code list
#===============================================================================

program = mmapApp

source = \
mmapApp.c \
mmapUtils.c

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

# libzhpe_mmap
ifeq ($(ZHPE),yes)
  CFLAGS += -DUSE_ZHPE
  CFLAGS += -I$(ZHPE_PREFIX)/include
  LDFLAGS += -L$(ZHPE_PREFIX)/lib
  LDFLAGS += -lzhpeq_util
  LDFLAGS += -lzhpe_mmap
  LDFLAGS += -lfabric
  LDFLAGS += -lzhpeq_util_fab
  LDFLAGS += -lzhpeq
  LDFLAGS += -lzhpe_stats
endif

#===============================================================================
# Targets to Build
#===============================================================================

$(program): $(obj) mmapUtils.h Makefile runmmapApp.sh
	$(CC) $(CFLAGS) $(obj) -o $@ $(LDFLAGS)

runmmapApp.sh: runmmapApp.sh.template
	sed 's#ZHPE_PREFIX#'$(ZHPE_PREFIX)'#' runmmapApp.sh.template > $@
	chmod +x $@

%.o: %.c mmapUtils.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(program) $(obj)
