walk = $1 $(foreach x,$(filter %/,$(wildcard $1*/)),$(call walk,$x))
examples := $(patsubst %.c,%,$(foreach x,$(call walk,examples/),$(wildcard $x*.c)))

LDLIBS := -lrt
CFLAGS := -O2 -Wall -pthread

.PHONY : all
all : ${examples}

define example_template
$(filter-out \,$(shell gcc -MQ '$1' -MM '$1.c'))
	${CC} ${CFLAGS} -o $$@ $$< ${LDLIBS}
endef
$(foreach x,${examples},$(eval $(call example_template,$x)))

.PHONY : clean
clean :
	-rm ${examples}
