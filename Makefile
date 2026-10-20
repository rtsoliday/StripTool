# Motif StripTool and Qt StripTool build dispatcher.
.DEFAULT_GOAL := all
QT_MODULES := Core Gui Widgets Network PrintSupport
# Discover Base here; individual components enforce their own requirements.
EPICS_OPTIONAL_LIBS := ca Com
include Makefile.rules

.PHONY: all striptool qtstriptool test-qtstriptool test-qtstriptool-core \
        test-qtstriptool-config test-qtstriptool-ui test-qtstriptool-ioc \
        test-qtstriptool-visual test-qtstriptool-performance test-qt-versions \
        test-ioc test-visual install install-qt-package package-qtstriptool \
        docs docs-clean docs-distclean \
        clean distclean \
        check-dependencies

PREFIX ?= /usr/local
DESTDIR ?=
PACKAGE_VERSION ?= 1.0.0
PACKAGE_ROOT := O.package/qtstriptool-$(PACKAGE_VERSION)-$(OS)-$(ARCH)
DOCS_BASE ?= /
export QT_VERSION HAVE_QT EPICS_BASE
QT_SUBMAKE = $(MAKE) -C qtstriptool

all: check-dependencies $(if $(HAVE_MOTIF),striptool) qtstriptool

check-dependencies:
ifeq ($(OS),Windows)
	@echo "Legacy Motif StripTool is not built on Windows."
else ifeq ($(HAVE_MOTIF),)
	@echo ""
	@echo "=========================================="
	@echo "NOTE: Motif development files were not found."
	@echo "Skipping the Motif-based StripTool build."
	@echo "Install the Motif development package to enable it."
	@echo "=========================================="
	@echo ""
endif
	@if [ -z "$(HAVE_QT)" ]; then \
	  echo "ERROR: Qt 5.15 or Qt 6 Widgets development files are required."; \
	  exit 1; \
	fi

striptool:
ifeq ($(OS),Windows)
	@echo "Legacy Motif StripTool is not supported by this dispatcher on Windows."
	@exit 1
else ifeq ($(HAVE_MOTIF),)
	@echo "Motif development files are required for legacy StripTool."
	@exit 1
else
	+$(MAKE) -C striptool EPICS_BASE="$(EPICS_BASE)"
endif

qtstriptool:
	+$(QT_SUBMAKE)

test-qtstriptool:
	+$(QT_SUBMAKE) test

test-qtstriptool-core:
	+$(QT_SUBMAKE) test-core

test-qtstriptool-config:
	+$(QT_SUBMAKE) test-config

test-qtstriptool-ui:
	+$(QT_SUBMAKE) test-ui

test-qtstriptool-performance:
	+$(QT_SUBMAKE) test-performance

test-qtstriptool-ioc:
	+$(QT_SUBMAKE) test-ioc TEST_PV="$(TEST_PV)"

test-qtstriptool-visual:
	+$(QT_SUBMAKE) test-visual

test-qt-versions:
	+$(MAKE) -C qtstriptool EPICS_BASE="$(EPICS_BASE)" test-qt-versions

test-ioc: test-qtstriptool-ioc

test-visual: test-qtstriptool-visual

install: check-dependencies $(if $(HAVE_MOTIF),striptool) qtstriptool

# Build the standalone HTML documentation without Qt or EPICS.
# Cleanup uses Python 3 on PATH, independent of the toolkit interpreter.
# An explicit command-line PYTHON setting still takes precedence.
docs-clean docs-distclean: PYTHON = $(if $(filter Windows,$(OS)),python,python3)
docs:
	$(PYTHON) scripts/build-docs.py --base "$(DOCS_BASE)"

docs-clean:
	$(PYTHON) scripts/clean-docs.py

docs-distclean:
	$(PYTHON) scripts/clean-docs.py --dependencies

install-qt-package: qtstriptool
ifeq ($(OS),Windows)
	@echo "Desktop integration installation is for Unix-like systems; use windeployqt on Windows."
	@exit 1
else
	install -d "$(DESTDIR)$(PREFIX)/bin" \
	             "$(DESTDIR)$(PREFIX)/share/applications" \
	             "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps" \
	             "$(DESTDIR)$(PREFIX)/share/metainfo" \
	             "$(DESTDIR)$(PREFIX)/share/doc/qtstriptool"
	install -m 755 "$(BIN_DIR)/qtstriptool" "$(DESTDIR)$(PREFIX)/bin/qtstriptool"
	install -m 644 qtstriptool/resources/org.epics.qtstriptool.desktop \
	               "$(DESTDIR)$(PREFIX)/share/applications/org.epics.qtstriptool.desktop"
	install -m 644 qtstriptool/resources/qtstriptool.svg \
	               "$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/qtstriptool.svg"
	install -m 644 qtstriptool/resources/org.epics.qtstriptool.metainfo.xml \
	               "$(DESTDIR)$(PREFIX)/share/metainfo/org.epics.qtstriptool.metainfo.xml"
	install -m 644 README.md LICENSE docs/site/project/authors.md \
	               docs/qtstriptool-*.md docs/QtParityMatrix.md \
	               "$(DESTDIR)$(PREFIX)/share/doc/qtstriptool/"
endif

package-qtstriptool: qtstriptool
ifeq ($(OS),Windows)
	@echo "Use windeployqt as documented in README.md (Install and package Qt StripTool)."
	@exit 1
else
	rm -rf "$(PACKAGE_ROOT)"
	$(MAKE) install-qt-package DESTDIR="$(abspath $(PACKAGE_ROOT))" PREFIX=/usr
	tar -C O.package -czf "O.package/qtstriptool-$(PACKAGE_VERSION)-$(OS)-$(ARCH).tar.gz" \
	  "qtstriptool-$(PACKAGE_VERSION)-$(OS)-$(ARCH)"
	@echo "Package: O.package/qtstriptool-$(PACKAGE_VERSION)-$(OS)-$(ARCH).tar.gz"
endif

clean: docs-clean
	+$(MAKE) -C striptool clean
	+$(MAKE) -C qtstriptool clean

distclean:
	+$(MAKE) -C striptool distclean
	+$(MAKE) -C qtstriptool distclean
	rmdir bin/$(OS)-$(ARCH) bin 2>/dev/null || true
	rm -rf O.package
