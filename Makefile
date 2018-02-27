USR_CFLAGS := $(USR_CFLAGS) -Wall
USR_TESTS := test-crypto test-crypto-verify test-dummy_op test-mul_op \
			 test-mul_op-verify test-crypto_op test-crypto_op-verify

KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
KVERBOSE = 'V=1'
DEBUG = n

#EXTRA_CFLAGS += -Wno-unused-variable
ifeq ($(DEBUG),y)
  EXTRA_CFLAGS += -g -DDEBUG
endif

KMAKE_OPTS := -C $(KERNEL_DIR) M=$(CURDIR)
ifneq ($(ARCH),)
KMAKE_OPTS += ARCH=$(ARCH)
endif
ifneq ($(CROSS_COMPILE),)
KMAKE_OPTS += CROSS_COMPILE=$(CROSS_COMPILE)
endif

obj-m := virtio_accel.o
virtio_accel-objs := \
	virtio_accel-core.o \
	virtio_accel-mgr.o \
	virtio_accel-reqs.o \
	virtio_accel-zc.o \
	accel.o

all: modules

modules:
	$(MAKE) $(KMAKE_OPTS) $(KVERBOSE) modules

tests: $(USR_TESTS) test_sw

test-%: test-%.c
	$(CC) $(USR_CFLAGS) -o $@ $^

clean: test_sw
test_sw: CPPFLAGS := $(CPPFLAGS) -I. -W -Wall -Wno-unknown-pragmas \
					-fno-common -O2 -g
export CPPFLAGS
test_sw: CFLAGS := $(CPPFLAGS)
export CFLAGS
test_sw: CXXFLAGS := $(CXXFLAGS) $(CPPFLAGS) -fpermissive
export CXXFLAGS
test_sw: LDFLAGS := $(LDFLAGS) -L.
export LDFLAGS
test_sw:
	$(MAKE) -C smithwaterman $(MAKECMDGOALS)

.PHONY: all clean test_sw

clean:
	$(MAKE) $(KMAKE_OPTS) clean
	rm -f $(USR_TESTS)
