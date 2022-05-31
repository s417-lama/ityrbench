UTH_PATH     := ${KOCHI_INSTALL_PREFIX_MASSIVETHREADS_DM}
UTH_CXXFLAGS := -I$(UTH_PATH)/include -fno-stack-protector
UTH_LDFLAGS  := -L$(UTH_PATH)/lib -luth -lmcomm

CXXFLAGS := $(UTH_CXXFLAGS) -I. -std=c++14 -O3 -g -Wall $(CXXFLAGS) $(CFLAGS)
LDFLAGS  := $(UTH_LDFLAGS) -lpthread -lm -ldl

SRCS := $(filter-out $(LIBS),$(wildcard ./*.cpp))

LIB_TARGETS  := $(patsubst %.cpp,%.so,$(LIBS))
MAIN_TARGETS := $(patsubst %.cpp,%.out,$(SRCS))

all: $(MAIN_TARGETS) $(LIB_TARGETS)

%.out: %.cpp
	$(MPICXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(MAIN_TARGETS) $(LIB_TARGETS)
