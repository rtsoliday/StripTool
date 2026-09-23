.DEFAULT_GOAL := all
include ../Makefile.rules

OBJ_DIR := O.$(OS)-$(ARCH)-qt$(QT_VERSION)
PROGRAM := $(OBJ_DIR)/qtstriptool$(if $(filter Windows,$(OS)),.exe)
TEST_PROGRAM := $(OBJ_DIR)/test_smoke$(if $(filter Windows,$(OS)),.exe)
IOC_TEST_PROGRAM := $(OBJ_DIR)/test_channel_access$(if $(filter Windows,$(OS)),.exe)
OBJEXT := $(if $(filter Windows,$(OS)),obj,o)

CORE = core/application.cc core/model.cc core/config.cc core/sample_buffer.cc \
       core/plot_data.cc
SERVICES = services/runtime_capabilities.cc services/acquisition_manager.cc \
           services/channel_access.cc services/cpu_usage_provider.cc \
           services/file_workflow.cc services/export_service.cc \
           services/history_provider.cc
UI = ui/main_window.cc ui/plot_widget.cc ui/controls_window.cc \
     ui/history_dialog.cc
APP_SOURCES = $(CORE) $(SERVICES) $(UI)
APP_OBJECTS = $(addprefix $(OBJ_DIR)/,$(APP_SOURCES:.cc=.$(OBJEXT)))
MOC_HEADERS = services/channel_provider.h services/acquisition_manager.h \
              services/history_provider.h ui/plot_widget.h \
              ui/controls_window.h ui/history_dialog.h
MOC_SOURCES = $(addprefix $(OBJ_DIR)/moc_,$(notdir $(MOC_HEADERS:.h=.cc)))
MOC_OBJECTS = $(MOC_SOURCES:.cc=.$(OBJEXT))
HEADERS = $(wildcard core/*.h services/*.h ui/*.h)
RESOURCE_SOURCE = $(OBJ_DIR)/resources.cc
RESOURCE_OBJECT = $(OBJ_DIR)/resources.$(OBJEXT)

ifneq ($(filter clean distclean,$(MAKECMDGOALS)),)
SKIP_DEPENDENCY_CHECK := 1
endif
ifneq ($(SKIP_DEPENDENCY_CHECK),1)
ifeq ($(HAVE_QT),)
$(error Qt 5.15 or Qt 6 Widgets development files are required)
endif
ifeq ($(HAVE_EPICS),)
$(error EPICS Base not found; set EPICS_BASE=/path/to/built/base)
endif
endif

CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17 -fPIC

ifeq ($(OS),Windows)
CXX = cl
CXXFLAGS = /nologo /O2 /W3 /external:anglebrackets /external:W0 /MD /EHsc \
           /permissive- /Zc:lambda /Zc:__cplusplus /utf-8 /std:c++17
CPPFLAGS = /I. /I$(OBJ_DIR) /I"$(EPICS_BASE)/include" \
           /I"$(EPICS_BASE)/include/compiler/msvc" \
           /I"$(EPICS_BASE)/include/os/WIN32" \
           /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS
MOC_CPPFLAGS = -I. -I$(OBJ_DIR) -I"$(EPICS_BASE)/include" \
               -I"$(EPICS_BASE)/include/compiler/msvc" \
               -I"$(EPICS_BASE)/include/os/WIN32"
EPICS_LIBS = $(EPICS_BASE)/lib/$(EPICS_HOST_ARCH)/ca.lib \
             $(EPICS_BASE)/lib/$(EPICS_HOST_ARCH)/Com.lib ws2_32.lib
define compile_cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(1) $(CXXFLAGS) /c $< /Fo$@
endef
define link_exe
	link /nologo /OUT:$@ $^ $(QT_LIBS) $(1) $(EPICS_LIBS)
endef
else
CPPFLAGS += -I. -I$(OBJ_DIR) -I$(EPICS_BASE)/include
MOC_CPPFLAGS = $(CPPFLAGS)
EPICS_LIB_DIR := $(EPICS_BASE)/lib/$(EPICS_HOST_ARCH)
EPICS_LIBS = $(EPICS_LIB_DIR)/libca.a $(EPICS_LIB_DIR)/libCom.a
EPICS_SYSTEM_LIBS = -lpthread -ldl -lrt -lm
ifeq ($(OS),Darwin)
EPICS_SYSTEM_LIBS = -lpthread -lm
endif
CPPFLAGS += -I$(EPICS_BASE)/include/compiler/$(if $(filter Darwin,$(OS)),clang,gcc) \
            -I$(EPICS_BASE)/include/os/$(OS)
define compile_cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(1) $(CXXFLAGS) -MMD -MP -c $< -o $@
endef
define link_exe
	$(CXX) $(LDFLAGS) -o $@ $^ -Wl,-rpath,$(EPICS_LIB_DIR) \
	  $(EPICS_LIBS) $(EPICS_SYSTEM_LIBS) $(QT_LIBS) $(1)
endef
endif

include ../Makefile.build

.PHONY: all test test-core test-config test-ui test-performance test-report \
        test-ioc test-visual test-qt-versions clean distclean install-program \
        prepare-output
all: install-program

$(PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(OBJ_DIR)/main.$(OBJEXT) $(RESOURCE_OBJECT)
	$(call link_exe,)

$(OBJ_DIR)/%.$(OBJEXT): %.cc $(HEADERS) $(BUILD_FILES) | prepare-output
	$(call compile_cxx,)

$(RESOURCE_SOURCE): resources/qtstriptool.qrc resources/qtstriptool.svg | prepare-output
	$(RCC) $< -o $@

$(RESOURCE_OBJECT): $(RESOURCE_SOURCE) $(BUILD_FILES)
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(if $(filter Windows,$(OS)),/c $< /Fo$@,-MMD -MP -c $< -o $@)

$(OBJ_DIR)/moc_channel_provider.cc: services/channel_provider.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_acquisition_manager.cc: services/acquisition_manager.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_plot_widget.cc: ui/plot_widget.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_controls_window.cc: ui/controls_window.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_history_provider.cc: services/history_provider.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_history_dialog.cc: ui/history_dialog.h | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/moc_%.$(OBJEXT): $(OBJ_DIR)/moc_%.cc
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(if $(filter Windows,$(OS)),/c $< /Fo$@,-MMD -MP -c $< -o $@)

$(OBJ_DIR)/test_smoke.moc: tests/test_smoke.cc $(HEADERS) | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/tests/test_smoke.$(OBJEXT): tests/test_smoke.cc $(OBJ_DIR)/test_smoke.moc $(HEADERS)
	$(call compile_cxx,$(QT_TEST_CFLAGS))

$(OBJ_DIR)/test_channel_access.moc: tests/test_channel_access.cc $(HEADERS) | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/tests/test_channel_access.$(OBJEXT): tests/test_channel_access.cc $(OBJ_DIR)/test_channel_access.moc $(HEADERS)
	$(call compile_cxx,$(QT_TEST_CFLAGS))

$(TEST_PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(RESOURCE_OBJECT) $(OBJ_DIR)/tests/test_smoke.$(OBJEXT)
	$(call link_exe,$(QT_TEST_LIBS))

$(IOC_TEST_PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(RESOURCE_OBJECT) $(OBJ_DIR)/tests/test_channel_access.$(OBJEXT)
	$(call link_exe,$(QT_TEST_LIBS))

CORE_TESTS = metadataIsConfigured defaultsAreToolkitNeutral \
             sampleBufferWrapsAndHonorsMemoryLimit \
             decimationPreservesChronologyAndExtrema logDecimationKeepsNonPositiveGap \
             acquisitionTracksReconnectMetadataAndTimestamps \
             acquisitionShutdownDisconnectsActiveChannels \
             samplingAndRefreshCadencesRemainIndependent staleDataIsExplicit \
             disconnectedChannelDoesNotRepeatOldSamples \
             disconnectedIntervalsSurviveDecimation \
             denseGapsRetainDataWithoutJoiningDisconnectedSegments \
             cpuUsageIsASeparateLocalProvider \
             plotDataSelectionAndHistoryJoinAreToolkitNeutral \
             noHistoryProviderReportsFailureAndCancellation \
             testHistoryProviderReturnsSelectedOwnedSamples \
             activeHistoryRequestCanBeDestroyedSafely exportsTextAndCsvData
CONFIG_TESTS = readsCanonicalFixture derivesSamplesWhenOmitted readsLegacyFixture \
               preservesUnknownFieldsAcrossRoundTrip \
               rejectsMalformedInputTransactionally clampsLegacyNumericRanges \
               appliesLayersInOrder writerRejectsInvalidModels \
               configurationRoundTripsFullDoublePrecision \
               followsLegacySearchOrder fileWorkflowIsTransactionalAndTracksRecentFiles
UI_TESTS = mainWindowHasStableScaffold controlsWindowEditsTheSharedModel \
           mainAndControlWindowsShareOneModel historyDialogRoundTripsRange \
           mainWindowFileExportAndSnapshotEntryPointsWork iconResourceIsEmbedded \
           plotWidgetSupportsViewAndAnnotationOperations \
           localChannelMetadataReachesControlsAndGraph \
           connectButtonKeepsUntouchedLimitsAutomatic \
           addingCurveKeepsExistingAcquisitionHistory \
           openingConfigurationClearsPreviousAcquisitionData \
           mainWindowExportsOnlyVisibleSamples \
           plotWidgetRetainsHistoryAcrossLiveRefreshAndModelEdits \
           autoScaleTracksOnlyVisibleSamples automaticMetadataDoesNotCancelRequestedAutoScale \
           automaticRangeUsesVisibleSamplesAndManualBound \
           curveLimitEditorsPreserveScientificValues liveScrollAdvancesWhenSamplesStop \
           metadataRefreshPreservesPendingCurveEdits reopeningControlsPreservesPendingEdits \
           removingCurvePreservesOtherPendingEdits \
           legendSelectionChangesCursorCurve plotMouseButtonsSelectMovePanAndShowCommands \
           plotKeepsCrossingSegmentsAndDoesNotRewindOnLateSamples \
           toolbarRightClickUsesFinePanAndZoomSteps
PERFORMANCE_TESTS = performanceTenCurveBufferAndLargeRender \
                    performanceWindowStartupAndShutdown

test: test-core test-config test-ui test-performance

test-core: all $(TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) $(CORE_TESTS)

test-config: all $(TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) $(CONFIG_TESTS)

test-ui: all $(TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) $(UI_TESTS)

test-performance: all $(TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) $(PERFORMANCE_TESTS)

test-report: all $(TEST_PROGRAM)
	@mkdir -p $(OBJ_DIR)/test-artifacts
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) \
	  -o $(OBJ_DIR)/test-artifacts/smoke.txt,txt
	@cat $(OBJ_DIR)/test-artifacts/smoke.txt

test-ioc: $(IOC_TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen QTSTRIPTOOL_TEST_PV="$(TEST_PV)" \
	  QTSTRIPTOOL_TEST_DESC="$(TEST_DESC)" $(IOC_TEST_PROGRAM)

test-visual: $(TEST_PROGRAM)
	@mkdir -p $(OBJ_DIR)/test-artifacts
	QT_QPA_PLATFORM=offscreen \
	  QTSTRIPTOOL_VISUAL_OUTPUT=$(OBJ_DIR)/test-artifacts/plot-baseline.png \
	  $(TEST_PROGRAM) plotWidgetRendersFixtureOffscreen mainAndControlsRenderOffscreen
	@echo "Visual review image: $(OBJ_DIR)/test-artifacts/plot-baseline.png"

test-qt-versions:
	@set -e; found=0; \
	for version in 5 6; do \
	  if $(MAKE) -s -f ../Makefile.rules QT_VERSION=$$version -pRrq : 2>/dev/null | \
	       grep -q '^HAVE_QT := 1'; then \
	    found=1; $(MAKE) QT_VERSION=$$version test; \
	  else \
	    echo "Qt $$version development files unavailable; skipping."; \
	  fi; \
	done; test $$found -eq 1

clean:
	rm -rf O.*

distclean: clean
	rm -f $(BIN_DIR)/$(notdir $(PROGRAM))

-include $(wildcard $(OBJ_DIR)/*.d $(OBJ_DIR)/*/*.d)
