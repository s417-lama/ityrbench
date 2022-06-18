UTH_PATH     := ${KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM}
UTH_CXXFLAGS := -I$(UTH_PATH)/include -fno-stack-protector -Wno-register
UTH_LDFLAGS  := -L$(UTH_PATH)/lib -luth -lmcomm

PCAS_PATH     := ${KOCHI_INSTALL_PREFIX_PCAS}
PCAS_CXXFLAGS := -I$(PCAS_PATH)/include

CXXFLAGS := $(UTH_CXXFLAGS) $(PCAS_CXXFLAGS) -I. -std=c++17 -O3 -g -Wall $(CXXFLAGS) $(CFLAGS)
LDFLAGS  := $(UTH_LDFLAGS) -lpthread -lm -ldl -lrt

MPICXX := $(or ${MPICXX},mpicxx)

SRCS := $(wildcard ./*.cpp)
HEADERS := $(wildcard **/*.hpp)

MAIN_TARGETS := $(patsubst %.cpp,%.out,$(SRCS))

all: $(MAIN_TARGETS) $(LIB_TARGETS)

%.out: %.cpp $(HEADERS)
	$(MPICXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(MAIN_TARGETS) $(LIB_TARGETS)
