USR_TESTS_EXTRA := test_sw test_km
USR_TESTS := test-dummy_op test-mul_op test-crypto_op \
	test-class_op test-det_op test-seg_op

CPPFLAGS := -I.. -Wall
USR_CPPFLAGS := $(USR_CPPFLAGS)

tests: $(USR_TESTS)

test-%.o: test-%.c
	$(CC) $(USR_CFLAGS) ${CPPFLAGS} -o $@ -c $^
test-%_op: test-%_op.o test-common.o
	$(CC) $(USR_CFLAGS) ${CPPFLAGS} -o $@ $^

clean: test_sw
test_sw: CPPFLAGS := $(CPPFLAGS) -I. -W -Wall -Wno-unknown-pragmas \
					-fno-common -O2 -g -fopenmp $(USR_CPPFLAGS)
export CPPFLAGS
test_sw: CFLAGS := $(CPPFLAGS)
export CFLAGS
test_sw: CXXFLAGS := $(CXXFLAGS) $(CPPFLAGS) -fpermissive
export CXXFLAGS
test_sw: LDFLAGS := $(LDFLAGS) -L. -fopenmp
export LDFLAGS
test_sw:
	$(MAKE) -C smithwaterman $(MAKECMDGOALS)

clean: test_km
test_km: CPPFLAGS := $(CPPFLAGS) -I. -W -Wall -Wno-unknown-pragmas \
					-fno-common -O2 -g $(USR_CPPFLAGS)
export CPPFLAGS
test_km: CFLAGS := $(CPPFLAGS)
export CFLAGS
test_km: CXXFLAGS := $(CXXFLAGS) $(CPPFLAGS) -fpermissive
export CXXFLAGS
test_km: LDFLAGS := $(LDFLAGS) -L.
export LDFLAGS
test_km:
	$(MAKE) -C kmeans $(MAKECMDGOALS)

.PHONY: clean test_sw test_km

clean:
	rm -f $(USR_TESTS) $(USR_TESTS_EXTRA)
