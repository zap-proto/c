ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign subdir-objects

lib_LTLIBRARIES =
bin_PROGRAMS =
check_PROGRAMS =
EXTRA_DIST =
noinst_HEADERS =
include_HEADERS =


EXTRA_DIST += README.md

EXTRA_DIST += c-zap.pc.in
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = c-zap.pc

EXTRA_DIST += compiler/c.zap
zap_DATA = compiler/c.zap
AM_CPPFLAGS = \
	-I${srcdir}/compiler \
	-I${srcdir}/lib

lib_LTLIBRARIES += libzap_c.la
libzap_c_la_LDFLAGS = -version-info 0:0:0
libzap_c_la_SOURCES = \
	lib/zap-malloc.c \
	lib/zap-stream.c \
	lib/zap.c
EXTRA_DIST += \
	lib/zap-list.inc

bin_PROGRAMS += zapc-c
zapc_c_SOURCES = \
	compiler/zapc-c.c \
	compiler/schema.zap.c \
	compiler/str.c
zapc_c_LDADD = libzap_c.la
include_HEADERS += \
	lib/zap_c.h

noinst_HEADERS += \
	lib/zap_priv.h \
	compiler/str.h \
	compiler/schema.zap.h \
	compiler/c.zap.h \
	compiler/c++.zap.h

# Don't try to generate any *.zap files. Otherwise make wants to compile them
# from *.zap.c and fails.
%.zap: ;

# googletest
GTEST_LDADD = gtest/googletest/lib/libgtest.la
gtest/googletest/lib/libgtest.la:
	make -C gtest/googletest lib/libgtest.la
GTEST_CPPFLAGS = -I${srcdir}/gtest/googletest/include
DIST_SUBDIRS = gtest/googletest

# Tests
check_PROGRAMS += \
	zap-test
zap_test_SOURCES = \
	tests/zap-test.cpp \
	tests/zap-stream-test.cpp \
	tests/example-test.cpp \
	tests/addressbook.zap.c \
	compiler/test.zap.c \
	compiler/schema-test.cpp \
	compiler/schema.zap.c
noinst_HEADERS += \
	compiler/test.zap.h \
	tests/addressbook.zap.h
EXTRA_DIST += \
	compiler/c.zap \
	compiler/c++.zap \
	compiler/schema.zap \
	compiler/test.zap \
	tests/addressbook.zap
zap_test_CPPFLAGS = $(AM_CPPFLAGS) $(GTEST_CPPFLAGS)
zap_test_CXXFLAGS = -std=gnu++17 -pthread
zap_test_LDADD = libzap_c.la $(GTEST_LDADD)
zap_test_LDFLAGS = -pthread
TESTS = zap-test

ZAP_SCHEMA_FILES := $(shell find . -type f -name \*.zap)

ZAP ?= zap
.PHONY: zap-compile
zap-compile:
	$(ZAP) compile \
		--output=./zapc-c \
		-Icompiler \
		$(ZAP_SCHEMA_FILES)
