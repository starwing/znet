walk = $1 $(foreach x,$(filter %/,$(wildcard $1*/)),$(call walk,$x))
tests := $(patsubst %.c,%,$(foreach x,$(call walk,tests/),$(wildcard $x*.c)))

LDLIBS := -pthread
CFLAGS := -O2 -g -D_XOPEN_SOURCE=600 \
  -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter

.PHONY : all
all : ${tests}

define example_template
$(filter-out \,$(shell gcc -MQ '$1' -MM '$1.c'))
	${CC} ${CFLAGS} -o $$@ $$< ${LDLIBS}
endef
$(foreach x,${tests},$(eval $(call example_template,$x)))

.PHONY : clean
clean :
	-rm ${tests}
