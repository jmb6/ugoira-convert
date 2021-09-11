PROGNAME	:= ugoira-convert
INCDIR		:= include
SHORTNAME	:= ugconv
BLDDIR		:= build
SRCDIR		:= src
INSTALLDIR	:= /usr/local/bin/

LIBS := -lcurl

# Note: Build type is release by default

CXX      := g++
CXXFLAGS := --std=c++20 -Wall -Werror=implicit-fallthrough=5 -Wsuggest-final-types -Wsuggest-final-methods -Wnoexcept -pipe -I$(INCDIR)/
LDFLAGS  := $(LIBS) -o $(BLDDIR)/$(PROGNAME)

CXXFLAGS_REL_GEN    := -O2 -flto -flto-partition=none -finline-functions -fweb -frename-registers -fno-plt
LDFLAGS_REL_GEN     := -s

CXXFLAGS_RELEASE := $(CXXFLAGS_REL_GEN) -march=native
LDFLAGS_RELEASE  := $(LDFLAGS_REL_GEN)

CXXFLAGS_DEV        := -O1
LDFLAGS_DEV         :=

CXXFLAGS_DEBUG      := -O0 -g -fsanitize=address,undefined
LDFLAGS_DEBUG       :=

MKDIR := mkdir -p
RM    := rm -f
RMDIR := rm -rf
CP    := cp

HDRS := $(wildcard $(INCDIR)/$(SHORTNAME)/*.hpp)
SRCS := $(wildcard $(SRCDIR)/*.cxx)
OBJS := $(addprefix $(BLDDIR)/, $(notdir $(SRCS:.cxx=.o)))

ifndef BUILD
	BUILD := release
endif

all: $(BUILD)

release: CXXFLAGS += $(CXXFLAGS_RELEASE)
release: LDFLAGS += $(LDFLAGS_RELEASE)
release: $(BLDDIR)/$(PROGNAME)

release-generic: CXXFLAGS += $(CXXFLAGS_REL_GEN)
release-generic: LDFLAGS += $(LDFLAGS_REL_GEN)
release-generic: $(BLDDIR)/$(PROGNAME)

dev: CXXFLAGS += $(CXXFLAGS_DEV)
dev: LDFLAGS += $(LDFLAGS_DEV)
dev: $(BLDDIR)/$(PROGNAME)

debug: CXXFLAGS += $(CXXFLAGS_DEBUG)
debug: LDFLAGS += $(LDFLAGS_DEBUG)
debug: $(BLDDIR)/$(PROGNAME)

$(BLDDIR)/$(PROGNAME): $(BLDDIR) $(OBJS)
	+$(CXX) $(OBJS) $(CXXFLAGS) $(LDFLAGS)

$(BLDDIR)/%.o: $(SRCDIR)/%.cxx $(HDRS) makefile
	$(CXX) $< $(CXXFLAGS) -c -o $@

$(BLDDIR):
	$(MKDIR) $(BLDDIR)

install:
	$(MKDIR) $(INSTALLDIR)
	$(CP) $(BLDDIR)/$(PROGNAME) $(INSTALLDIR)/

clean:
	$(RMDIR) $(BLDDIR)

.PHONY: all release debug install clean
