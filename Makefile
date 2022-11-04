SHELL=/bin/bash
.SHELLFLAGS = -eu -o pipefail -c

UTH_PATH     := ${KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM}
UTH_CXXFLAGS := -I$(UTH_PATH)/include -fno-stack-protector -Wno-register -Wl,-export-dynamic
UTH_LDFLAGS  := -L$(UTH_PATH)/lib -luth -lmcomm

PCAS_PATH     := ${KOCHI_INSTALL_PREFIX_PCAS}
PCAS_CXXFLAGS := -I$(PCAS_PATH)/include -lrt

LIBUNWIND_PATH     := ${KOCHI_INSTALL_PREFIX_LIBUNWIND}
LIBUNWIND_CXXFLAGS := -I$(LIBUNWIND_PATH)/include
LIBUNWIND_LDFLAGS  := -L$(LIBUNWIND_PATH)/lib -lunwind

BACKWARD_PATH     := ${KOCHI_INSTALL_PREFIX_BACKWARD_CPP}
BACKWARD_CXXFLAGS := -I$(BACKWARD_PATH)/include
BACKWARD_LDFLAGS  := -lbfd

PCG_PATH     := ${KOCHI_INSTALL_PREFIX_PCG}
PCG_CXXFLAGS := -I$(PCG_PATH)/include
PCG_LDFLAGS  :=

# TODO: remove it when boost dependency is removed
BOOST_PATH     := ${KOCHI_INSTALL_PREFIX_BOOST}
BOOST_CXXFLAGS := -I$(BOOST_PATH)/include
BOOST_LDFLAGS  := -L$(BOOST_PATH)/lib -Wl,-R$(BOOST_PATH)/lib -lboost_container

CXXFLAGS := $(UTH_CXXFLAGS) $(PCAS_CXXFLAGS) $(LIBUNWIND_CXXFLAGS) $(BACKWARD_CXXFLAGS) $(PCG_CXXFLAGS) $(BOOST_CXXFLAGS) -I. -std=c++17 -O3 -g -Wall $(CXXFLAGS) $(CFLAGS)
LDFLAGS  := $(UTH_LDFLAGS) $(LIBUNWIND_LDFLAGS) $(BACKWARD_LDFLAGS) $(PCG_LDFLAGS) $(BOOST_LDFLAGS) -lpthread -lm -ldl

MPICXX := $(or ${MPICXX},mpicxx)

SRCS := $(wildcard ./*.cpp)
HEADERS := $(wildcard ./ityr/**/*.hpp)

MAIN_TARGETS := $(patsubst %.cpp,%.out,$(SRCS)) uts.out

all: $(MAIN_TARGETS)

%.out: %.cpp $(HEADERS)
	$(MPICXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

uts.out: uts/uts.c uts/rng/brg_sha1.c uts/main.cc $(HEADERS)
	$(MPICXX) $(CXXFLAGS) -DBRG_RNG=1 -o $@ uts/uts.c uts/rng/brg_sha1.c uts/main.cc $(LDFLAGS)

uts++.out: uts/uts.c uts/rng/brg_sha1.c uts/main++.cc $(HEADERS)
	$(MPICXX) $(CXXFLAGS) -DBRG_RNG=1 -o $@ uts/uts.c uts/rng/brg_sha1.c uts/main++.cc $(LDFLAGS)

clean:
	rm -rf $(MAIN_TARGETS)
