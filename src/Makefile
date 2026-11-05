all: all-deps realtime user
.PHONY: all all-deps install install-user install-realtime user realtime all-tests test

-include ../config.mk
-include $(MODINC)
include Makefile.clean

#CC=g++

RTLDFLAGS += -Wl,-rpath,$(LIBDIR)
RTEXTRA_LDFLAGS += -Wl,--whole-archive liblcecdevices.a -Wl,--no-whole-archive -L$(LIBDIR) -llinuxcnchal -lethercat -lrt

#EXTRA_CFLAGS += --std=c2x
EXTRA_CFLAGS += -Wall # Increase debugging level
#EXTRA_CFLAGS += -fanalyzer # Use GCC's static analyzer tool, doubles compile time

## targets
lcec-common-objs := lcec_devicelist.o lcec_ethercat.o lcec_pins.o lcec_lookup.o lcec_modparam.o lcec_malloc.o
lcec-objs := lcec_main.o $(lcec-common-objs)
lcec-conf-srcs := $(wildcard lcec_conf*.c)
lcec-conf-objs = $(subst .c,.o,$(lcec-conf-srcs))
device-srcs := $(wildcard devices/*.c)
device-objs := $(subst .c,.o,$(device-srcs))
all-srcs := $(wildcard *.c devices/*.c tests/*.c)
all-deps := $(all-srcs:.c=.d)
all-tests-srcs := $(wildcard tests/test_*.c)
all-tests := $(all-tests-srcs:.c=.bin)

## target-specific variables

# override EXTRA_CFLAGS for lcec_conf's .c files
$(lcec-conf-objs): EXTRA_CFLAGS := $(filter-out -Wframe-larger-than=%,$(EXTRA_CFLAGS))


## build rules

# Automatically generate .d dependency files.  Mostly from GNU Make's docs.
#
# GCC Flags used:
#
#   -MT foo: explicitly set the dependency target name to 'foo'.
#        Without this, the target for 'devices/lcec_generic.o' would just
#        be 'lcec_generic.o', which breaks the makefile.
#   -MM: generate dependencies, but ignore system .h files
#   -MP: add a dummy rule for each .h that will keep make from throwing
#        errors if headers are deleted without 'make clean'.
%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MT $(subst .d,.o,$@) -MM -MP $(EXTRA_CFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(all-srcs:.c=.d)
all-deps: $(all-deps)


liblcecdevices.a: $(device-objs)
	@$(ECHO) Creating library $@
	@$(Q)ar rcs liblcecdevices.a $(device-objs)

## lcec_uspace.so: lcec built against the userspace EtherCAT master
#
# The userspace master is a build of the IgH master as a library, without a
# kernel module (https://github.com/stratuMAK/ethercat, configured with
# --enable-uspace-master). Install it in a prefix of its own, so that its ecrt.h
# does not shadow the kernel master's /usr/include/ecrt.h:
#   ./configure --enable-uspace-master --prefix=/opt/ethercat-uspace
# lcec_uspace.so is built and installed next to lcec.so when that prefix has
# ecrt.h. The same sources are compiled a second time, as *.uspace.o, against
# that ecrt.h; the userspace code paths are under #ifdef EC_USPACE_MASTER.
USPACE_ETHERCAT ?= /opt/ethercat-uspace
ifneq ($(wildcard $(USPACE_ETHERCAT)/include/ecrt.h),)
  BUILD_USPACE := yes
endif

uspace-objs := $(lcec-objs:.o=.uspace.o)
uspace-device-objs := $(device-objs:.o=.uspace.o)

# -I comes after EXTRA_CFLAGS, but before the system /usr/include/ecrt.h.
# EXTRA_CFLAGS cannot carry it: Makefile.modinc assigns it with :=.
%.uspace.o: %.c
	@$(ECHO) Compiling realtime $< for lcec_uspace
	@$(Q)$(CC) -o $@ $(EXTRA_CFLAGS) -I$(USPACE_ETHERCAT)/include -MMD -MP -c $<

-include $(uspace-objs:.o=.d) $(uspace-device-objs:.o=.d)

liblcecdevices_uspace.a: $(uspace-device-objs)
	@$(ECHO) Creating library $@
	@$(Q)ar rcs $@ $(uspace-device-objs)

lcec_uspace.so: $(uspace-objs) liblcecdevices_uspace.a
	$(ECHO) Linking $@
	ld -d -r -o $@.tmp $(uspace-objs)
	objcopy -j .rtapi_export -O binary $@.tmp $@.sym
	(echo '{ global : '; tr -s '\0' < $@.sym | xargs -r0 printf '%s;\n' | grep .; echo 'local : * ; };') > $@.ver
	$(CC) -shared -Bsymbolic $(RTLDFLAGS) -Wl,--version-script,$@.ver -o $@ $(uspace-objs) -lm \
	  -Wl,--whole-archive liblcecdevices_uspace.a -Wl,--no-whole-archive -L$(LIBDIR) -llinuxcnchal \
	  -L$(USPACE_ETHERCAT)/lib -Wl,-rpath,$(USPACE_ETHERCAT)/lib -l:libethercat.so.2 -lpthread -lrt
	chmod -x $@


# Rules for building RTAI.  Currently disabled, and needs updated to
# work.  Ping @scottlaird if you need this and can't get it to work.
#
#ifeq ($(BUILDSYS),kbuild)
#
# dirty workaround to get the RTAI directory
#RTAIINCDIR = $(subst /rtai.h,,$(firstword $(wildcard $(foreach i,$(subst -I,,$(filter -I%,$(RTFLAGS))), $(i)/rtai.h))))
#ifneq ($(RTAIINCDIR),)
#  RTAIDIR = $(realpath $(RTAIINCDIR)/..)
#endif
#
#realtime:
#	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(RTLIBDIR)/Module.symvers $(RTAIDIR)/modules/ethercat/Module.symvers" -C $(KERNELDIR) SUBDIRS=`pwd` CC=$(CC) V=0 modules
#
#else

# This throws a warning about ignoring the old recipe for the target.
install: install-user install-realtime
	true  # override 'install' from $(MODINC)

realtime: lcec.so $(if $(BUILD_USPACE),lcec_uspace.so)
user: lcec_conf lcec_devices lcec_configgen

# Run all tests (auto-generated above from tests/test_*.c).
test: $(all-tests)
	$(foreach var, $(all-tests), $(var);)

install-user: user
	mkdir -p $(DESTDIR)$(EMC2_HOME)/bin
	cp lcec_conf $(DESTDIR)$(EMC2_HOME)/bin/
	cp lcec_configgen $(DESTDIR)/usr/bin/

install-realtime: realtime
	mkdir -p $(DESTDIR)$(RTLIBDIR)/
	cp lcec.so $(DESTDIR)$(RTLIBDIR)/
ifdef BUILD_USPACE
	cp lcec_uspace.so $(DESTDIR)$(RTLIBDIR)/
endif

lcec.so: lcec_main.o $(lcec-common-objs) liblcecdevices.a
	$(ECHO) Linking $@
	ld -d -r -o $@.tmp lcec_main.o $(lcec-common-objs)
	objcopy -j .rtapi_export -O binary $@.tmp $@.sym
	(echo '{ global : '; tr -s '\0' < $@.sym | xargs -r0 printf '%s;\n' | grep .; echo 'local : * ; };') > $@.ver
#$(CC) -shared -Bsymbolic $(RTLDFLAGS) -Wl,--version-script,$@.ver -o $@ lcec_main.o $(lcec-comon-objs) -lm
	$(CC) -shared -Bsymbolic $(RTLDFLAGS) -Wl,--version-script,$@.ver -o $@ lcec_main.o $(lcec-common-objs) -lm $(RTEXTRA_LDFLAGS)
	chmod -x $@

lcec_conf: $(lcec-conf-objs) $(lcec-common-objs) liblcecdevices.a
	$(CC) -o $@ $(lcec-conf-objs) $(lcec-common-objs) -Wl,-rpath,$(LIBDIR) -L$(LIBDIR) -llinuxcnchal -lexpat -Wl,--whole-archive liblcecdevices.a -Wl,--no-whole-archive -lethercat -lm

lcec_devices: lcec_devices.o $(lcec-common-objs) liblcecdevices.a
	$(CC) -o $@ lcec_devices.o $(lcec-common-objs) -Wl,-rpath,$(LIBDIR) -L$(LIBDIR) -llinuxcnchal -lexpat -Wl,--whole-archive liblcecdevices.a -Wl,--no-whole-archive -lethercat -lm

lcec_configgen: configgen/*.go configgen/*/*.go
	(cd configgen ; go build lcec_configgen.go)
	cp configgen/lcec_configgen .

configgen/devicelist: configgen/devicelist.go
	(cd configgen ; go build devicelist.go)

configgen/drivers/drivers.go: configgen/devicelist lcec_devices
	(cd configgen ; go generate)

# Rule for compiling tests/*.bin files.  We're naming test excutables *.bin so we can use wildcards in .gitignore and `make clean` to match them.
tests/%.bin: tests/%.o $(lcec-common-objs) liblcecdevices.a
	$(CC) -o $@ $(subst .bin,.o,$@) $(lcec-common-objs) -Wl,-rpath,$(LIBDIR) -L$(LIBDIR) -llinuxcnchal -lexpat -Wl,--whole-archive liblcecdevices.a -Wl,--no-whole-archive -lethercat -lm

