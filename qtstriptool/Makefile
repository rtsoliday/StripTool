.DEFAULT_GOAL := all
QT_REQUIRED := 1
QT_MODULES := Core Gui Widgets Network PrintSupport
EPICS_REQUIRED_LIBS := ca Com
include ../Makefile.rules

OBJ_DIR := O.$(OS)-$(ARCH)-qt$(QT_VERSION)
PROGRAM := $(OBJ_DIR)/qtstriptool$(if $(filter Windows,$(OS)),.exe)
TEST_PROGRAM := $(OBJ_DIR)/test_smoke$(if $(filter Windows,$(OS)),.exe)
IOC_TEST_PROGRAM := $(OBJ_DIR)/test_channel_access$(if $(filter Windows,$(OS)),.exe)
OBJEXT := $(if $(filter Windows,$(OS)),obj,o)

CORE = core/application.cc core/command_line.cc core/model.cc core/config.cc core/sample_buffer.cc \
       core/plot_data.cc
SERVICES = services/runtime_capabilities.cc services/acquisition_manager.cc \
           services/channel_access.cc services/cpu_usage_provider.cc \
           services/file_workflow.cc services/export_service.cc services/sdds_export.cc \
           services/history_provider.cc
UI = ui/main_window.cc ui/plot_widget.cc ui/controls_window.cc \
     ui/history_dialog.cc ui/help.cc ui/about_banner.cc ui/pv_drop.cc ui/motif_pv_drop.cc
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


ifeq ($(OS),Windows)
CXX = cl
CXXFLAGS = /nologo /O2 /W3 /external:anglebrackets /external:W0 /MD /EHsc \
           /permissive- /Zc:lambda /Zc:__cplusplus /utf-8 $(CXX_STD_FLAGS)
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
	link /nologo $(LDFLAGS) /OUT:$@ $(filter %.$(OBJEXT),$^) $(QT_LIBS) $(1) $(EPICS_LIBS) $(SDDS_LINK_LIBS)
endef
else
CPPFLAGS += -I. -I$(OBJ_DIR) -I$(EPICS_BASE)/include
ifeq ($(OS),Linux)
CXXFLAGS += -fPIE -fstack-protector-strong
LDFLAGS += -pie -Wl,-z,relro,-z,now
endif
MOC_CPPFLAGS = $(CPPFLAGS)
EPICS_LIBS = $(CA_LIBS)
EPICS_SYSTEM_LIBS = $(EPICS_SYS_LIBS)
CPPFLAGS += -I$(EPICS_BASE)/include/compiler/$(if $(filter Darwin,$(OS)),clang,gcc) \
            -I$(EPICS_BASE)/include/os/$(OS)
define compile_cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(1) $(CXXFLAGS) -MMD -MP -c $< -o $@
endef
define link_exe
	$(CXX) $(LDFLAGS) -o $@ $(filter %.$(OBJEXT),$^) -Wl,-rpath,$(EPICS_BASE_LIB_DIR) \
	  $(EPICS_LIBS) $(EPICS_SYSTEM_LIBS) $(QT_LIBS) $(1) $(SDDS_LINK_LIBS)
endef
endif

# The legacy MEDM drop protocol is available on X11 without linking Motif.
ifeq ($(OS),Linux)
ifneq ($(shell $(PKG_CONFIG) --exists xcb && echo yes),)
CPPFLAGS += -DSTRIPTOOL_HAVE_XCB $(shell $(PKG_CONFIG) --cflags xcb)
QT_LIBS += $(shell $(PKG_CONFIG) --libs xcb)
endif
endif

# Optional SDDS: try complete installations in priority order. Explicit include
# or library overrides are authoritative, including nonexistent paths (useful
# for building/testing without SDDS). Never borrow a core library from another
# installation. The link probe is compiled, never executed, and stays quiet.
SDDS_REPO ?= $(abspath $(RULES_DIR)../SDDS)
ifneq ($(strip $(SDDS_INCLUDE_DIR) $(SDDS_LIB_DIR)),)
SDDS_CANDIDATES := explicit
else
SDDS_CANDIDATES := sibling extensions aps
endif
sdds_include_explicit = $(SDDS_INCLUDE_DIR)
sdds_library_explicit = $(SDDS_LIB_DIR)
sdds_include_sibling = $(SDDS_REPO)/include
sdds_library_sibling = $(SDDS_REPO)/lib/$(OS)-$(ARCH)
sdds_include_extensions = $(or $(EXTENSIONS_INCLUDE_DIR),$(EPICS_INC_DIR))
sdds_library_extensions = $(or $(EXTENSIONS_LIB_DIR),$(EPICS_LIB_DIR))
sdds_include_aps = /usr/local/epics/extensions/include
sdds_library_aps = /usr/local/epics/extensions/lib/$(EPICS_HOST_ARCH)
sdds_lib = $(if $(strip $(2)),$(if $(filter Windows,$(OS)),$(wildcard $(2)/$(1).lib),$(call find_lib,$(1),a dylib so,$(2))))
sdds_files = $(foreach lib,SDDS1 rpnlib mdbmth mdblib,$(call sdds_lib,$(lib),$(sdds_library_$(1)))) \
             $(or $(call sdds_lib,lzma,$(sdds_library_$(1))),$(LZMA_LIB)) \
             $(or $(call sdds_lib,z,$(sdds_library_$(1))),$(Z_LIB))
sdds_includes = $(if $(filter Windows,$(OS)),/I"$(sdds_include_$(1))" /I"$(sdds_include_$(1))/../lzma",-I"$(sdds_include_$(1))")
sdds_windows_gsl = $(foreach lib,gsl gslcblas,$(or $(call sdds_lib,$(lib),$(sdds_library_$(1))),$(call sdds_lib,$(lib),$(WINDOWS_GSL_LIB_DIR))))
sdds_links = $(foreach lib,$(call sdds_files,$(1)),"$(lib)") \
             $(if $(filter Windows,$(OS)),$(foreach lib,$(call sdds_windows_gsl,$(1)),"$(lib)") /LIBPATH:"$(sdds_library_$(1))" $(LIB_LINK_DIRS),$(GSL_LINK_LIBS) -lm $(call epics_rpath,$(call sdds_files,$(1)),$(sdds_library_$(1))))
ifeq ($(OS),Windows)
sdds_probe_command = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(call sdds_includes,$(1)) build/sdds_link_probe.cc /Fo"$(OBJ_DIR)/sdds-probe.obj" /Fe"$(OBJ_DIR)/sdds-probe.exe" /link $(LDFLAGS) $(call sdds_links,$(1))
else
sdds_probe_command = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(call sdds_includes,$(1)) build/sdds_link_probe.cc $(LDFLAGS) -o "$(OBJ_DIR)/sdds-probe" $(call sdds_links,$(1))
endif
sdds_probe = $(if $(and $(wildcard $(sdds_include_$(1))/SDDS.h),$(call sdds_lib,SDDS1,$(sdds_library_$(1)))),$(shell mkdir -p "$(OBJ_DIR)"; $(call sdds_probe_command,$(1)) > "$(OBJ_DIR)/sdds-probe-$(1).log" 2>&1 && echo yes))
sdds_first_usable = $(if $(strip $(1)),$(if $(call sdds_probe,$(firstword $(1))),$(firstword $(1)),$(call sdds_first_usable,$(wordlist 2,$(words $(1)),$(1)))))
ifneq ($(SKIP_DEPENDENCY_CHECK),1)
# Probe on each invocation so replaced headers/libraries and toolchain changes
# are recognized without a clean build, including recovery from a failed probe.
SDDS_SELECTED := $(call sdds_first_usable,$(SDDS_CANDIDATES))
endif
ifneq ($(SDDS_SELECTED),)
SDDS_LINK_LIBS := $(call sdds_links,$(SDDS_SELECTED))
SDDS_DEPENDENCIES := $(call sdds_files,$(SDDS_SELECTED)) $(if $(filter Windows,$(OS)),$(call sdds_windows_gsl,$(SDDS_SELECTED)),$(wildcard $(filter-out -%,$(GSL_LINK_LIBS))))
SDDS_HEADERS := $(wildcard $(sdds_include_$(SDDS_SELECTED))/SDDS*.h)
CPPFLAGS += $(call sdds_includes,$(SDDS_SELECTED)) $(if $(filter Windows,$(OS)),/DSTRIPTOOL_HAVE_SDDS,-DSTRIPTOOL_HAVE_SDDS)
ifeq ($(OS),Windows)
MOC_CPPFLAGS += -DSTRIPTOOL_HAVE_SDDS
endif
endif

CUSTOM_COMPILE_RULES := 1
CUSTOM_CLEAN := 1
PROD = qtstriptool
qtstriptool_OBJS = $(APP_OBJECTS) $(MOC_OBJECTS) $(OBJ_DIR)/main.$(OBJEXT) $(RESOURCE_OBJECT)
qtstriptool_LIBS = $(EPICS_LIBS) $(EPICS_SYSTEM_LIBS) $(QT_LIBS) $(SDDS_LINK_LIBS)
qtstriptool_DEPS = $(SDDS_DEPENDENCIES)
BUILD_CONFIG_VARIABLES += SDDS_SELECTED SDDS_LINK_LIBS SDDS_REPO SDDS_INCLUDE_DIR SDDS_LIB_DIR
BUILD_OBJECTS += $(OBJ_DIR)/tests/test_smoke.$(OBJEXT) $(OBJ_DIR)/tests/test_channel_access.$(OBJEXT)
BUILD_GENERATED += $(MOC_SOURCES) $(RESOURCE_SOURCE) $(OBJ_DIR)/test_smoke.moc $(OBJ_DIR)/test_channel_access.moc
CUSTOM_LINK_PRODUCTS += qtstriptool
include ../Makefile.build

.PHONY: all test test-core test-config test-ui test-performance test-report test-memory \
        test-ioc test-visual test-qt-versions clean distclean install-program \
        prepare-output
all: install-program

$(PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(OBJ_DIR)/main.$(OBJEXT) $(RESOURCE_OBJECT)
	$(call link_exe,)

$(OBJ_DIR)/%.$(OBJEXT): %.cc $(HEADERS) $(BUILD_FILES) | prepare-output
	$(call compile_cxx,)

$(OBJ_DIR)/services/sdds_export.$(OBJEXT) $(OBJ_DIR)/tests/test_smoke.$(OBJEXT): $(SDDS_HEADERS)

$(RESOURCE_SOURCE): resources/qtstriptool.qrc resources/qtstriptool.svg \
                    ../docs/qtstriptool-user-guide.md | prepare-output
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

$(OBJ_DIR)/moc_%.$(OBJEXT): $(OBJ_DIR)/moc_%.cc $(BUILD_FILES)
	$(CXX) $(CPPFLAGS) $(QT_CFLAGS) $(CXXFLAGS) $(if $(filter Windows,$(OS)),/c $< /Fo$@,-MMD -MP -c $< -o $@)

$(OBJ_DIR)/test_smoke.moc: tests/test_smoke.cc $(HEADERS) | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/tests/test_smoke.$(OBJEXT): tests/test_smoke.cc $(OBJ_DIR)/test_smoke.moc $(HEADERS) $(BUILD_FILES)
	$(call compile_cxx,$(QT_TEST_CFLAGS))

$(OBJ_DIR)/test_channel_access.moc: tests/test_channel_access.cc $(HEADERS) | prepare-output
	$(MOC) $(MOC_CPPFLAGS) $(MOC_QT_CFLAGS) $< -o $@

$(OBJ_DIR)/tests/test_channel_access.$(OBJEXT): tests/test_channel_access.cc $(OBJ_DIR)/test_channel_access.moc $(HEADERS) $(BUILD_FILES)
	$(call compile_cxx,$(QT_TEST_CFLAGS))

$(TEST_PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(RESOURCE_OBJECT) $(OBJ_DIR)/tests/test_smoke.$(OBJEXT) $(SDDS_DEPENDENCIES)
	$(call link_exe,$(QT_TEST_LIBS))

$(IOC_TEST_PROGRAM): $(APP_OBJECTS) $(MOC_OBJECTS) $(RESOURCE_OBJECT) $(OBJ_DIR)/tests/test_channel_access.$(OBJEXT) $(SDDS_DEPENDENCIES)
	$(call link_exe,$(QT_TEST_LIBS))

CORE_TESTS = metadataIsConfigured defaultsAreToolkitNeutral \
             commandLineTimingOverridesConfiguration commandLineRejectsInvalidTiming \
             sampleBufferWrapsAndHonorsMemoryLimit \
             decimationPreservesChronologyAndExtrema \
             denseDecimationRetainsStepTransitionsAndSpikes \
             traceSelectionIsStableAsViewScrolls \
             traceSelectionHandlesClockLimits \
             logDecimationKeepsNonPositiveGap \
             acquisitionTracksReconnectMetadataAndTimestamps \
             nonFiniteDisplayLimitsFallBackToFiniteRange \
             acquisitionShutdownDisconnectsActiveChannels \
             samplingAndRefreshCadencesRemainIndependent staleStatusKeepsUnchangedValuePlotable \
             disconnectedChannelDoesNotRepeatOldSamples \
             disconnectedIntervalsSurviveDecimation \
             denseGapsRetainDataWithoutJoiningDisconnectedSegments \
             cpuUsageIsASeparateLocalProvider cpuUsageExcludesIdleWallTime \
             plotDataSelectionAndHistoryJoinAreToolkitNeutral \
             visibleRangeIncludesCrossingSegments \
             extremeFiniteSamplesKeepRangesAndLabelsFinite \
             noHistoryProviderReportsFailureAndCancellation \
             testHistoryProviderReturnsSelectedOwnedSamples \
             archiverHistoryProviderRetrievesNativeJson \
             archiverRejectsOutOfRangeTimestamps \
             archiverRequestsIdentifyApplicationVersion \
             activeHistoryRequestCanBeDestroyedSafely exportReportsWriteFailures \
             exportsTextAndCsvData exportsPreEpochTimestamps exportsClockMinimumWithoutOverflow \
             sddsExportAvailabilityMatchesMenu exportsSddsDataAndMetadata \
             exportsEmptyAndExtremeSddsData sddsExportRejectsInvalidNamesAndDestinations \
             mainWindowExportsVisibleSddsSamples \
             applicationUsesCNumericLocale \
             exportsGapSamplesAsInvalidNan mainWindowExportEndsWithTrailingGap \
             liveSamplesClearErrorAndLaterOutageEndsTrace \
             archiverRequestsEncodePlusInPvNames \
             exportsCsvNeutralizesFormulaNames \
             loadDiagnosticsNameFileLineAndActualFallback \
             testProcessKeepsChannelAccessAndArchiveLocal \
             unterminatedChannelAccessStringsStayInBounds
CONFIG_TESTS = readsCanonicalFixture derivesSamplesWhenOmitted readsLegacyFixture \
               preservesUnknownFieldsAcrossRoundTrip \
               rejectsMalformedInputTransactionally rejectsOversizedConfigurationsTransactionally \
               writerRejectsConfigurationsBeyondReadLimits \
               clampsLegacyNumericRanges \
               appliesLayersInOrder writerRejectsInvalidModels \
               configurationRoundTripsFullDoublePrecision \
               configurationReportsCloseFailures \
               configurationWritesDiscoveredChannelMetadata \
               configurationOmitsDiscoveredUnitsThatAreNotOneWord \
               followsLegacySearchOrder fileWorkflowIsTransactionalAndTracksRecentFiles \
               configurationNumbersIgnoreCommaLocale configurationAcceptsLegacyCommaDecimals \
               configurationRoundTripsSubnormalLimits \
               readsLegacyWrittenUnitsFixture \
               configurationSavesLineBreaksAsSpaces \
               configurationPreservesLongUnknownKeys \
               clampsTimespanToControlsRange \
               readsConfigurationWithUtf8Bom
UI_TESTS = mainWindowHasStableScaffold controlsWindowEditsTheSharedModel \
           typedTimingValuesApplyOnceWhenEntered \
           helpTopicsSearchAndWindowReuse aboutDialogCreditsAndHelpHandoff \
           aboutBannerClickScrollsMessage helpManualUsesSiteOverride \
           pvDropsAddCurvesOnBothScreens pvDropCapacityIsTransactionalAndReplotsExistingCurves \
           plotRendersModelTitle appearanceTitleEditsAndPersists \
           curveLimitEditorsFitLongValues \
           returnInCurveFieldsAppliesModify \
           mouseWheelZoomKeepsPointerTimestampFixed \
           narrowYAxisTicksRemainDistinct \
           yAxisUsesConciseSignificantDigits gridLinesFollowLabelledTicks \
           timeAxisUnitsFollowVisibleDuration \
           autoScrollAndLiveZoomPreserveDuration plotAreaIgnoresLatestValueWidth \
           legendShowsFullValueAndLimits \
           largeFontsExpandPlotMarginsAndLegendRows \
           mainAndControlWindowsShareOneModel historyDialogRoundTripsRange \
           extremeZoomPanAndDatesKeepTimeRangesValid \
           mainWindowFileExportAndSnapshotEntryPointsWork unicodeFilePathsRoundTrip iconResourceIsEmbedded \
           printPreviewHasUsefulInitialGeometry \
           plotWidgetSupportsViewAndAnnotationOperations \
           liveAndHistoricalTracesRenderAsStepsWithoutCrossingGaps \
           stepTraceStopsAtPresentTime \
           localChannelMetadataReachesControlsAndGraph \
           connectButtonKeepsUntouchedLimitsAutomatic \
           addingPvSilentlyBackfillsFiveMinutesFromArchiver \
           automaticHistoryUsesConfiguredWindow \
           zoomingOutExtendsArchivedHistory \
           resetViewUsesProviderLimitsWithoutManualEdits \
           newCurveInheritsGlobalConfiguredLimitMode \
           addingCurveKeepsExistingAcquisitionHistory \
           openingConfigurationClearsPreviousAcquisitionData \
           mainWindowExportsOnlyVisibleSamples \
           plotWidgetRetainsHistoryAcrossLiveRefreshAndModelEdits \
           autoScaleTracksVisibleTrace automaticMetadataDoesNotCancelRequestedAutoScale \
           globalAutoScaleIgnoresIndividualConfiguredBounds \
           curveLimitEditorsPreserveScientificValues liveScrollAdvancesWhenSamplesStop \
           liveRefreshDoesNotRewindTheTimeWindow \
           appendedSampleUpdatesLiveAutomaticRangeImmediately \
           panDraggedBackToStartRestoresOriginalRange stationaryPanKeepsAutoScroll \
           livePanDragUsesStableRangeAndStationaryReleaseCatchesUp \
           dragReleaseAppliesItsFinalPosition \
           lostMouseReleaseCannotContinueDragging \
           lightAnnotationTextRemainsReadable \
           metadataRefreshPreservesPendingCurveEdits reopeningControlsPreservesPendingEdits \
           removingCurvePreservesOtherPendingEdits \
           legendSelectionChangesCursorCurve plotMouseButtonsSelectMovePanAndShowCommands \
           middleDragKeepsLiveViewStableUntilRelease \
           removingCurveRemapsAnnotationSelection middleDragStopsAtPlotEdge \
           plotKeepsCrossingSegmentsAndDoesNotRewindOnLateSamples \
           toolbarRightClickUsesFinePanAndZoomSteps \
           evictedLiveSamplesBreakArchivedHold unevictedLiveSamplesJoinArchiveContinuously \
           sampleBufferRecordsFirstEvictedTimestamp displayRefreshBreaksArchivedHoldAfterEviction \
           legendValueKeepsSixSignificantDigits \
           subnormalConstantCurvesKeepValidRangesAndLabels \
           farOffScaleSpikesKeepBothStepEdges \
           hugeFiniteSamplesDrawInsidePlot \
           extremeRangeYZoomAndPanMoveTheView \
           stationaryClicksKeepLiveZoomDuration doubleClickAnnotatesClickedTimeInLiveZoom \
           fineWheelDeltasZoomInProportionToTheNotch \
           invalidPvEntryIsRejectedAndKept \
           overlongCurveRowNameIsRejectedBeforeSave \
           controlsDismissAsksBeforeHidingTheLastWindow \
           emptyHistoricalRangeIsRejectedBeforeCancellingBackfill \
           savingUntitledConfigurationKeepsApplicationWindowTitle \
           graphToolbarTooltipsOmitMenuMnemonics
PERFORMANCE_TESTS = performanceTenCurveBufferAndLargeRender \
                    performanceWindowStartupAndShutdown

MEMORY_TESTS = caInboxPreservesOrderedSamples caInboxOverflow caInboxByteLimitAndRetirement \
               sampleStorageShrinksAndReleases boundedArchiveHistoryAndStorageRelease \
               archiveParserRejectsOversizedResponses archiveDownloadByteLimit \
               archiveAggregateBudgetCancellationAndTimeout archiveWorkerParsingAndSampleLimit

test-memory: all $(TEST_PROGRAM)
	QT_QPA_PLATFORM=offscreen $(TEST_PROGRAM) $(MEMORY_TESTS)

test: test-core test-config test-ui test-performance test-memory

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
	QT_QPA_PLATFORM=offscreen QTSTRIPTOOL_REQUIRE_IOC=1 \
	  QTSTRIPTOOL_SOFTIOC="$(EPICS_BASE)/bin/$(EPICS_HOST_ARCH)/softIoc$(if $(filter Windows,$(OS)),.exe)" \
	  QTSTRIPTOOL_TEST_IOC_DB="$(CURDIR)/tests/reconnect.db" \
	  $(IOC_TEST_PROGRAM) automaticallyReconnectsAfterIocRestart \
	  refreshesMetadataAfterIocRestart \
	  iocRestartTestsRestoreEpicsEnvironment
	QT_QPA_PLATFORM=offscreen QTSTRIPTOOL_TEST_PV="$(TEST_PV)" \
	  QTSTRIPTOOL_TEST_DESC="$(TEST_DESC)" $(IOC_TEST_PROGRAM) \
	  receivesMetadataAndSampleFromIoc shutsDownWithAnActiveSubscription

test-visual: $(TEST_PROGRAM)
	@mkdir -p $(OBJ_DIR)/test-artifacts
	QT_QPA_PLATFORM=offscreen \
	  QTSTRIPTOOL_VISUAL_OUTPUT=$(OBJ_DIR)/test-artifacts/plot-baseline.png \
  $(TEST_PROGRAM) plotWidgetRendersFixtureOffscreen mainAndControlsRenderOffscreen \
    largeFontsExpandPlotMarginsAndLegendRows \
    helpTopicsSearchAndWindowReuse aboutDialogCreditsAndHelpHandoff \
    aboutBannerClickScrollsMessage \
    printPreviewHasUsefulInitialGeometry
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

# Real Motif interoperability check; run in an X11 session or under xvfb-run.
.PHONY: test-motif
$(OBJ_DIR)/motif_drag_source: tests/motif_drag_source.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(addprefix -I,$(MOTIF_INC) $(X11_INC)) $< -o $@ -lXm -lXt -lX11 -lXtst

test-motif: $(TEST_PROGRAM) $(OBJ_DIR)/motif_drag_source
	QT_QPA_PLATFORM=xcb QTSTRIPTOOL_TEST_MOTIF_SOURCE="$(abspath $(OBJ_DIR)/motif_drag_source)" \
	  $(TEST_PROGRAM) motifPvDropsUseRealMotifSource

# End-to-end QtEDM gesture: plain middle button, with no Ctrl modifier.
QTEDM_TEST_PROGRAM ?= $(abspath ../../medm/bin/$(OS)-$(ARCH)/qtedm)
.PHONY: test-qtedm-drop
$(OBJ_DIR)/x11_drag_driver: tests/x11_drag_driver.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(addprefix -I,$(X11_INC)) $< -o $@ -lX11 -lXtst

test-qtedm-drop: $(TEST_PROGRAM) $(OBJ_DIR)/x11_drag_driver
	QT_QPA_PLATFORM=xcb QTSTRIPTOOL_TEST_QTEDM="$(QTEDM_TEST_PROGRAM)" \
	  QTSTRIPTOOL_TEST_DRAG_DRIVER="$(abspath $(OBJ_DIR)/x11_drag_driver)" \
	  $(TEST_PROGRAM) qtedmMiddleButtonDropsReachBothScreens
