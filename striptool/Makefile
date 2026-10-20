# Standalone and EPICS-extensions build for legacy Motif StripTool.
.DEFAULT_GOAL := all
QT_MODULES :=
EPICS_REQUIRED_LIBS := ca Com
MOTIF_REQUIRED := 1
include ../Makefile.rules

USE_CLUES ?= YES
USE_SDDS ?= NO
USE_XMU ?= YES
USE_XPM ?= YES
STRIP_DAQ ?= StripCA.c
STRIP_HISTORY ?= StripHistoryAR+ArR.c
ARCHIVER_CALL ?= NONE
USE_ARCHIVE_RECORD ?= NO

ifneq ($(filter clean distclean,$(MAKECMDGOALS)),)
SKIP_DEPENDENCY_CHECK := 1
endif

ifneq ($(SKIP_DEPENDENCY_CHECK),1)
ifeq ($(OS),Windows)
$(error Legacy Motif StripTool is not supported by this standalone build on Windows)
endif
ifeq ($(HAVE_EPICS),)
$(error EPICS Base not found; set EPICS_BASE=/path/to/built/base)
endif
ifeq ($(HAVE_MOTIF),)
$(error Motif development headers and libraries were not found)
endif
ifeq ($(strip $(EPICS_HOST_ARCH)),)
$(error Could not determine EPICS_HOST_ARCH; set it to the Base build architecture)
endif
endif

# Legacy sources use GNU C99 independently of shared optimization defaults.
CFLAGS += -std=gnu99
CPPFLAGS += -DUNIX -DUSE_MOTIF -I. -I$(EPICS_BASE)/include \
            -I$(EPICS_BASE)/include/compiler/$(if $(filter Darwin,$(OS)),clang,gcc) \
            -I$(EPICS_BASE)/include/os/$(OS) \
            $(addprefix -I,$(MOTIF_INC) $(X11_INC))

SOURCES = $(STRIP_HISTORY) StripConfig.c StripCurve.c Strip.c StripDialog.c \
          StripDataSource.c StripGraph.c StripMisc.c cColorManager.c \
          ColorDialog.c StripTool.c $(STRIP_DAQ) jlAxis.c jlLegend.c \
          ComboBox.c FastLabel.c Tabs.c x_rotate.c rotated.c browserHelp.c \
          Annotation.c main_c.c

ifeq ($(USE_CLUES),YES)
CPPFLAGS += -DUSE_CLUES
SOURCES += LiteClue.c
endif
ifeq ($(USE_XMU),YES)
CPPFLAGS += -DUSE_XMU
endif
ifeq ($(USE_XPM),YES)
CPPFLAGS += -DUSE_XPM
endif
ifeq ($(STRIP_DAQ),StripCDEV.cc)
CPPFLAGS += -DUSE_CDEV
endif
ifeq ($(USE_SDDS),YES)
CPPFLAGS += -DUSE_SDDS
endif

ifeq ($(STRIP_HISTORY),StripHistoryAR+ArR.c)
SOURCES += getHistory.c
ifeq ($(USE_ARCHIVE_RECORD),YES)
CPPFLAGS += -DSTRIP_HISTORY -DUSE_ARCHIVE_RECORD
SOURCES += getArchiveRecord.c
endif
ifeq ($(ARCHIVER_CALL),CAR)
CPPFLAGS += -DSTRIP_HISTORY -DUSE_CAR
SOURCES += StripHistoryLANLmodif.cc
endif
ifeq ($(ARCHIVER_CALL),AAPI)
CPPFLAGS += -DSTRIP_HISTORY -DUSE_AAPI
SOURCES += get_AAPI_data.c
endif
ifeq ($(ARCHIVER_CALL),NONE)
CPPFLAGS += -DNO_ARCHIVER_CALL
endif
endif

ifeq ($(USE_SDDS),YES)
EXTENSIONS_LIB_DIR ?= /usr/local/epics/extensions/lib/$(EPICS_HOST_ARCH)
EXTENSIONS_INCLUDE_DIR ?= /usr/local/epics/extensions/include
CPPFLAGS += -I$(EXTENSIONS_INCLUDE_DIR)
OPTIONAL_LIBS += $(EXTENSIONS_LIB_DIR)/libSDDS1.a \
                 $(EXTENSIONS_LIB_DIR)/liblzma.a \
                 $(EXTENSIONS_LIB_DIR)/libmdblib.a -lz
endif

ifeq ($(USE_CDEV),YES)
OPTIONAL_LIBS += -lcdev
endif

ifeq ($(ARCHIVER_CALL),AAPI)
OPTIONAL_LIBS += -lAAPI_client -lAAPI_common
endif

EPICS_LIBS = $(CA_LIBS)
X_LIBS := $(XM_LIB) $(MOTIF_STATIC_DEP_LIBS) $(MOTIF_STATIC_EXTRA_LIBS) \
          $(if $(filter YES,$(USE_XMU)),$(XMU_LIB)) \
          $(if $(filter YES,$(USE_XPM)),$(XPM_LIB)) $(XT_LIB) $(X11_LIB) \
          $(XEXT_LIB) $(XP_LIB)
SYSTEM_LIBS = $(EPICS_SYS_LIBS)

OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(filter %.c,$(SOURCES))) \
           $(patsubst %.cc,$(OBJ_DIR)/%.o,$(filter %.cc,$(SOURCES)))
PROGRAM := $(OBJ_DIR)/StripTool

.PHONY: all clean distclean install-program prepare-output
all: install-program

$(PROGRAM): $(OBJECTS)
	$(CXX) -o $@ $(filter %.$(OBJEXT),$^) $(LDFLAGS) -Wl,-rpath,$(EPICS_BASE_LIB_DIR) \
	  $(EPICS_LIBS) $(OPTIONAL_LIBS) $(X_LIBS) $(SYSTEM_LIBS)

$(OBJ_DIR)/%.o: %.c $(BUILD_FILES) | prepare-output
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: %.cc $(BUILD_FILES) | prepare-output
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

CUSTOM_COMPILE_RULES := 1
CUSTOM_CLEAN := 1
PROD = StripTool
StripTool_OBJS = $(OBJECTS)
StripTool_LIBS = $(EPICS_LIBS) $(OPTIONAL_LIBS) $(X_LIBS) $(SYSTEM_LIBS)
BUILD_CONFIG_VARIABLES += USE_CLUES USE_SDDS USE_XMU USE_XPM STRIP_DAQ STRIP_HISTORY ARCHIVER_CALL USE_ARCHIVE_RECORD
CUSTOM_LINK_PRODUCTS += StripTool
include ../Makefile.build
-include $(wildcard $(OBJ_DIR)/*.d)

clean:
	rm -rf O.*

distclean: clean
	rm -f $(BIN_DIR)/StripTool
